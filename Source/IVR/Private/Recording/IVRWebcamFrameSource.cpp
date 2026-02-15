// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "Recording/IVRWebcamFrameSource.h"
#include "IVR.h"
#include "IVRGlobalStatics.h"
#include "Engine/World.h" // Para GetWorldTimerManager
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include <atomic> // Necessário para std::atomic
#include <string> // Inclusão explícita para std::string
#include "HAL/Runnable.h" // Inclusão explícita e em ordem para FRunnable

// [MANUAL_REF_POINT] Includes do OpenCV foram movidos para IVROpenCVBridge.
// Incluindo o cabeçalho do worker e do bridge que agora estão em IVROpenCVBridge
#include "FWebcamCaptureWorker.h"
#include "IVROpenCVGlobals.h"

UIVRWebcamFrameSource::UIVRWebcamFrameSource()
    : UIVRFrameSource()
    , WorkerRunnable(nullptr)
    , WorkerThread(nullptr)
{
    // Cria um evento de sincronização que reseta automaticamente após ser triggered
    NewFrameEvent = FPlatformProcess::GetSynchEventFromPool(false); 
}
void UIVRWebcamFrameSource::BeginDestroy()
{
    // Garante que a thread seja desligada e os recursos liberados durante a destruição do UObject
    Shutdown();
// Libera o evento de sincronização de volta para o pool
    if (NewFrameEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(NewFrameEvent);
        NewFrameEvent = nullptr;
    }

    Super::BeginDestroy();
}
void UIVRWebcamFrameSource::Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool)
{
    if (!World || !InFramePool)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRWebcamFrameSource::Initialize: World ou FramePool é nulo."));
        return;
    }
    CurrentWorld = World;
    FrameSourceSettings = Settings;
    FramePool = InFramePool;
    // Cria a instância do worker runnable
    WorkerRunnable = new FWebcamCaptureWorker(
        FramePool,
        CapturedFrameQueue,
        bShouldStopWorker,
        NewFrameEvent,
        Settings.IVR_WebcamIndex,
        (int32)Settings.IVR_WebcamResolution.X,
        (int32)Settings.IVR_WebcamResolution.Y,
        Settings.IVR_WebcamFPS,
        // OpenCV API Preference agora é passado do bridge.
        // Use cv::CAP_DSHOW ou outro, mas agora vindo do módulo de bridge.
        (VideoCaptureAPIs)0 // Placeholder, ou passe um valor real se o enum for exposto via bridge
    );
    if (WorkerRunnable)
    {
        // Cria e inicia o thread.
        WorkerThread = FRunnableThread::Create(WorkerRunnable, TEXT("IVRWebcamCaptureThread"), 0, TPri_Normal);
        if (!WorkerThread)
        {
            UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRWebcamFrameSource: Falha ao criar worker thread."));
            delete WorkerRunnable; // Limpa o runnable se o thread não puder ser criado
            WorkerRunnable = nullptr;
        }
    }
    
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRWebcamFrameSource inicializado para webcam index: %d."), Settings.IVR_WebcamIndex);
}
void UIVRWebcamFrameSource::Shutdown()
{
    // Garante que a captura e o polling sejam interrompidos
    StopCapture();

    // Sinaliza à thread worker para parar e espera que ela termine
    bShouldStopWorker.AtomicSet(true);
    if (NewFrameEvent) NewFrameEvent->Trigger(); // Acorda a thread caso esteja esperando
    // Espera a thread worker terminar completamente
    if (WorkerThread)
    {
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
    }
    // Libera a memória do worker runnable
    if (WorkerRunnable)
    {
        delete WorkerRunnable;
        WorkerRunnable = nullptr;
    }
// Limpa a fila de quaisquer frames remanescentes
    FIVR_VideoFrame DummyFrame;
    while (CapturedFrameQueue.Dequeue(DummyFrame))
    {
        // Libera o buffer do frame de volta para o pool, se for válido
        if (FramePool && DummyFrame.RawDataPtr.IsValid())
        {
            FramePool->ReleaseFrame(DummyFrame.RawDataPtr);
        }
    }

    // Limpa referências do UObject
    CurrentWorld = nullptr;
    FramePool = nullptr;
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRWebcamFrameSource Encerrado."));
}
void UIVRWebcamFrameSource::StartCapture()
{
    if (!CurrentWorld || !WorkerRunnable || !WorkerThread)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRWebcamFrameSource::StartCapture: Não inicializado ou worker não pronto."));
        return;
    }
    
    bShouldStopWorker.AtomicSet(false);
// Determina a cadência de polling no Game Thread.
    // A leitura real dos frames no worker thread é tão rápida quanto o OpenCV permite.
    // O polling aqui controla com que frequência o Game Thread "pega" os frames da fila.
    float PollDelay = (FrameSourceSettings.IVR_WebcamFPS > 0.0f) ? (1.0f / FrameSourceSettings.IVR_WebcamFPS) : (1.0f / 30.0f);
    // Inicia o timer para polling de frames da fila
    CurrentWorld->GetTimerManager().SetTimer(FramePollTimerHandle, this, &UIVRWebcamFrameSource::PollForNewFrames, PollDelay, true);
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRWebcamFrameSource: Iniciando captura de frames de webcam. Polling a cada %.4f segundos."), PollDelay);
}
void UIVRWebcamFrameSource::StopCapture()
{
    // Para o timer de polling no Game Thread
    if (CurrentWorld && CurrentWorld->GetTimerManager().IsTimerActive(FramePollTimerHandle))
    {
        CurrentWorld->GetTimerManager().ClearTimer(FramePollTimerHandle);
    }
    FramePollTimerHandle.Invalidate();

    // Sinaliza à thread worker para parar
    bShouldStopWorker.AtomicSet(true);
    if (NewFrameEvent) NewFrameEvent->Trigger(); // Acorda a thread caso esteja esperando (para que ela possa sair do loop Run())

    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRWebcamFrameSource: Captura de frames parada."));
}
void UIVRWebcamFrameSource::PollForNewFrames()
{
    FIVR_VideoFrame QueuedFrame;
    // Enquanto houver frames na fila, os retira e broadcasta
    while (CapturedFrameQueue.Dequeue(QueuedFrame))
    {
        // Broadcast do frame processado (o TSharedPtr dentro de QueuedFrame é movido)
        OnFrameAcquired.Broadcast(MoveTemp(QueuedFrame));
    }
}
TArray<FString> UIVRWebcamFrameSource::ListWebcamDevices()
{
    // A lógica foi movida para IVROpenCVBridge::ListWebcamDevicesNative
    return IVROpenCVBridge::ListWebcamDevicesNative();
}
int32 UIVRWebcamFrameSource::GetActualFrameWidth() const
{
    if (WorkerRunnable)
    {
        return WorkerRunnable->ActualFrameWidth.GetValue();
    }
    return 0; // Ou um valor de erro adequado
}
int32 UIVRWebcamFrameSource::GetActualFrameHeight() const
{
    if (WorkerRunnable)
    {
        return WorkerRunnable->ActualFrameHeight.GetValue();
    }
    return 0; // Ou um valor de erro adequado
}