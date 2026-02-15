// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "Recording/IVRVideoFrameSource.h"
#include "IVRGlobalStatics.h"
#include "IVR.h"
#include "Engine/World.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include <atomic> // Necessário para std::atomic
#include <string> // Inclusão explícita para std::string
#include "HAL/Runnable.h" // Inclusão explícita e em ordem para FRunnable

// [MANUAL_REF_POINT] Includes do OpenCV foram movidos para IVROpenCVBridge.
// Incluindo o cabeçalho do worker que agora está em IVROpenCVBridge
#include "IVROpenCVBridge/Public/FVideoFileCaptureWorker.h"

UIVRVideoFrameSource::UIVRVideoFrameSource()
    : UIVRFrameSource()
    , WorkerRunnable(nullptr)
    , WorkerThread(nullptr)
{
    NewFrameEvent = FPlatformProcess::GetSynchEventFromPool(false); // false para auto-reset
}

void UIVRVideoFrameSource::BeginDestroy()
{
    Shutdown(); 

    FIVR_VideoFrame DummyFrame;
    while (CapturedFrameQueue.Dequeue(DummyFrame))
    {
        if (FramePool && DummyFrame.RawDataPtr.IsValid())
        {
            FramePool->ReleaseFrame(DummyFrame.RawDataPtr);
        }
    }

    if (WorkerThread)
    {
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
    }
    if (WorkerRunnable)
    {
        delete WorkerRunnable;
        WorkerRunnable = nullptr;
    }

    if (NewFrameEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(NewFrameEvent);
        NewFrameEvent = nullptr;
    }

    Super::BeginDestroy();
}
void UIVRVideoFrameSource::Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool)
{
    if (!World || !InFramePool)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRVideoFrameSource::Initialize: World ou FramePool é nulo."));
        return;
    }
    CurrentWorld = World;
    FrameSourceSettings = Settings;
    FramePool = InFramePool;
// Cria a instância do worker runnable
    WorkerRunnable = new FVideoFileCaptureWorker(
        FramePool,
        CapturedFrameQueue,
        bShouldStopWorker,
        NewFrameEvent,
        Settings.IVR_VideoFilePath,
        Settings.FPS, // Usamos Settings.FPS como o FPS desejado para o OpenCV
        Settings.IVR_LoopVideoPlayback
    );
    if (WorkerRunnable)
    {
        WorkerThread = FRunnableThread::Create(WorkerRunnable, TEXT("IVRVideoFileCaptureThread"), 0, TPri_Normal);
        if (!WorkerThread)
        {
            UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRVideoFrameSource: Falha ao criar worker thread."));
            delete WorkerRunnable;
            WorkerRunnable = nullptr;
        }
    }
    
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRVideoFrameSource inicializado para arquivo de vídeo: %s."), *Settings.IVR_VideoFilePath);
}

void UIVRVideoFrameSource::Shutdown()
{
    StopCapture();

    FIVR_VideoFrame DummyFrame;
    while (CapturedFrameQueue.Dequeue(DummyFrame))
    {
        if (FramePool && DummyFrame.RawDataPtr.IsValid())
        {
            FramePool->ReleaseFrame(DummyFrame.RawDataPtr);
        }
    }
    if (WorkerThread)
    {
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
    }
    if (WorkerRunnable)
    {
        delete WorkerRunnable;
        WorkerRunnable = nullptr;
    }

    CurrentWorld = nullptr;
    FramePool = nullptr;
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRVideoFrameSource Encerrado."));
}
void UIVRVideoFrameSource::StartCapture()
{
    if (!CurrentWorld || !WorkerRunnable || !WorkerThread)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRVideoFrameSource::StartCapture: Não inicializado ou worker não pronto."));
        return;
    }
    
    bShouldStopWorker.AtomicSet(false);
    // Ajusta o FPS de polling baseado no FPS do vídeo e na velocidade de reprodução.
    // O DesiredFPS na worker é o FPS de leitura, o FPS do GameThread é a cadência de broadcast.
    float VideoNativeFPS = GetActualVideoFileFPS(); // Obtém o FPS nativo do vídeo
    if (VideoNativeFPS <= 0.0f) VideoNativeFPS = 30.0f; // Fallback para evitar divisão por zero

    // Calcula o FPS efetivo de broadcast, considerando o VideoPlaybackSpeed
    float EffectiveFPS = VideoNativeFPS * FrameSourceSettings.IVR_VideoPlaybackSpeed;
    float PollDelay = (EffectiveFPS > 0.0f) ? (1.0f / EffectiveFPS) : (1.0f / 30.0f);
    CurrentWorld->GetTimerManager().SetTimer(FramePollTimerHandle, this, &UIVRVideoFrameSource::PollForNewFrames, PollDelay, true);
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRVideoFrameSource: Iniciando captura de frames de arquivo de vídeo em %.2f FPS (efetivo)."), EffectiveFPS);
}

void UIVRVideoFrameSource::StopCapture()
{
    if (CurrentWorld && CurrentWorld->GetTimerManager().IsTimerActive(FramePollTimerHandle))
    {
        CurrentWorld->GetTimerManager().ClearTimer(FramePollTimerHandle);
    }
    FramePollTimerHandle.Invalidate();

    bShouldStopWorker.AtomicSet(true);
    if (NewFrameEvent) NewFrameEvent->Trigger();

    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRVideoFrameSource: Captura de frames parada."));
}

void UIVRVideoFrameSource::PollForNewFrames()
{
    FIVR_VideoFrame QueuedFrame;
    while (CapturedFrameQueue.Dequeue(QueuedFrame))
    {
        OnFrameAcquired.Broadcast(MoveTemp(QueuedFrame));
    }
}
int32 UIVRVideoFrameSource::GetActualFrameWidth() const
{
    if (WorkerRunnable)
    {
        return WorkerRunnable->ActualFrameWidth.GetValue();
    }
    return 0; 
}

int32 UIVRVideoFrameSource::GetActualFrameHeight() const
{
    if (WorkerRunnable)
    {
        return WorkerRunnable->ActualFrameHeight.GetValue();
    }
    return 0; 
}

float UIVRVideoFrameSource::GetActualVideoFileFPS() const
{
    if (WorkerRunnable)
    {
        return WorkerRunnable->ActualVideoFileFPS.load(); 
    }
    return 0.0f; 
}
float UIVRVideoFrameSource::GetEffectivePlaybackFPS() const
{
    if (WorkerRunnable)
    {
        float nativeFPS = GetActualVideoFileFPS();
        if (nativeFPS > 0.0f)
        {
            return nativeFPS * FrameSourceSettings.IVR_VideoPlaybackSpeed;
        }
    }
    return 0.0f; 
}