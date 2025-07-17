// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Recording/IVRWebcamFrameSource.h"
#include "IVR.h"
#include "Engine/World.h" // Para GetWorldTimerManager
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"


#if WITH_OPENCV
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h"

#undef check // the check macro causes problems with opencv headers
#pragma warning(disable: 4668) // 'symbol' not defined as a preprocessor macro, replacing with '0' for 'directives'
#pragma warning(disable: 4828) // The character set in the source file does not support the character used in the literal
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp> // Para cv::VideoCapture
#include <opencv2/imgproc.hpp> // Para cv::cvtColor

#include "PostOpenCVHeaders.h"
#endif

// ==============================================================================
// FWebcamCaptureWorker Implementation (FRunnable para Thread de Captura)
// ==============================================================================

/**
 * @brief Worker thread para capturar frames de uma webcam usando OpenCV.
 * Esta classe opera em um thread separado para evitar bloqueios do Game Thread.
 */
class FWebcamCaptureWorker : public FRunnable
{
public:
    /**
     * @brief Construtor do worker.
     * @param InFramePool Refer�ncia ao pool de frames para adquirir buffers.
     * @param InQueue Fila para enfileirar frames capturados.
     * @param InStopFlag Flag para sinalizar a parada do thread.
     * @param InNewFrameEvent Evento para sinalizar novos frames.
     * @param InDeviceIndex �ndice da webcam a ser utilizada.
     * @param InWidth Largura desejada para a captura.
     * @param InHeight Altura desejada para a captura.
     * @param InFPS FPS desejado para a captura.
     * @param InApiPreference Prefer�ncia de API para OpenCV (e.g., cv::CAP_DSHOW).
     */
    FWebcamCaptureWorker(UIVRFramePool* InFramePool, TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& InQueue, FThreadSafeBool& InStopFlag, FEvent* InNewFrameEvent, int32 InDeviceIndex, int32 InWidth, int32 InHeight, float InFPS, cv::VideoCaptureAPIs InApiPreference)
        : CapturedFrameQueue(InQueue)
        , FramePool(InFramePool)
        , bShouldStop(InStopFlag)
        , NewFrameEvent(InNewFrameEvent)
        , DeviceIndex(InDeviceIndex)
        , DesiredWidth(InWidth)
        , DesiredHeight(InHeight)
        , DesiredFPS(InFPS)
        , ApiPreference(InApiPreference)
    {}

    /**
     * @brief Destrutor do worker, libera o VideoCapture.
     */
    virtual ~FWebcamCaptureWorker()
    {
        if (WebcamCapture.isOpened())
        {
            WebcamCapture.release();
        }
    }

    /**
     * @brief Inicializa o worker. N�o abre a webcam aqui para evitar bloqueios no in�cio da thread.
     * @return true.
     */
    virtual bool Init() override
    {
        bShouldStop.AtomicSet(false);
        UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Initialized successfully. Webcam opening deferred to Run()."));
        return true;
    }

    /**
     * @brief Loop principal do worker thread para capturar frames.
     * Abertura da webcam ocorre aqui para garantir que bloqueios aconte�am no worker thread.
     * @return 0 ao sair, ou 1 se a webcam n�o puder ser aberta.
     */
    virtual uint32 Run() override
    {
        UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Running."));
        cv::Mat Frame; // Matriz OpenCV para armazenar o frame

        // Tenta abrir a webcam no in�cio do Run(), no worker thread.
        WebcamCapture.open(DeviceIndex, ApiPreference);
        if (!WebcamCapture.isOpened())
        {
            UE_LOG(LogIVRFrameSource, Error, TEXT("WebcamCaptureWorker: Failed to open webcam device %d with API %d. Stopping capture thread."), DeviceIndex, (int32)ApiPreference);
            bShouldStop.AtomicSet(true); // Sinaliza para parar
            return 1; // Retorna c�digo de erro
        }

        // Tenta definir a resolu��o, FPS e codec *ap�s* a abertura bem-sucedida
        WebcamCapture.set(cv::CAP_PROP_FRAME_WIDTH , DesiredWidth);
        WebcamCapture.set(cv::CAP_PROP_FRAME_HEIGHT, DesiredHeight);
        WebcamCapture.set(cv::CAP_PROP_FPS, DesiredFPS);
        WebcamCapture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G')); // Tenta MJPG para melhor FPS/qualidade

        // Verifica as configura��es aplicadas (podem n�o ser exatamente as desejadas pela webcam)
        ActualFrameWidth.Set((int32)WebcamCapture.get(cv::CAP_PROP_FRAME_WIDTH));
        ActualFrameHeight.Set((int32)WebcamCapture.get(cv::CAP_PROP_FRAME_HEIGHT));

        // Verifica as configura��es aplicadas (podem n�o ser exatamente as desejadas pela webcam)
       UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Opened device %d with API %d. Actual resolution: %dx%d, Actual FPS: %.1f"),
               DeviceIndex, (int32)ApiPreference, ActualFrameWidth.GetValue(), ActualFrameHeight.GetValue(), (float)WebcamCapture.get(cv::CAP_PROP_FPS));
        
        while (!bShouldStop)
        {
            if (!WebcamCapture.isOpened())
            {
                UE_LOG(LogIVRFrameSource, Warning, TEXT("WebcamCaptureWorker: Webcam was closed unexpectedly. Stopping capture thread."));
                bShouldStop.AtomicSet(true);
                break;
            }

            // L� um frame da webcam (geralmente � uma chamada bloqueante at� um frame estar dispon�vel)
            WebcamCapture.read(Frame);

            if (Frame.empty())
            {
                // Se o frame estiver vazio, pode ser que a webcam esteja sendo desconectada ou em um estado ruim
                UE_LOG(LogIVRFrameSource, Warning, TEXT("WebcamCaptureWorker: Empty frame received. Retrying..."));
                FPlatformProcess::Sleep(0.1f); // Pequena pausa antes de tentar novamente
                continue; // Tenta novamente na pr�xima itera��o
            }

            // Adquire um buffer do pool (otimiza reuso de mem�ria)
            TSharedPtr<TArray<uint8>> FrameBuffer = FramePool->AcquireFrame();
            if (!FrameBuffer.IsValid())
            {
                UE_LOG(LogIVRFrameSource, Error, TEXT("WebcamCaptureWorker: Failed to acquire frame buffer from pool. Dropping frame."));
                continue; // Pula este frame
            }

            // Converte o frame OpenCV (geralmente BGR) para BGRA (formato esperado pelo FFmpeg)
            cv::Mat BGRAFrame;
            cv::cvtColor(Frame, BGRAFrame, cv::COLOR_BGR2BGRA);

            // Calcula o tamanho de uma linha de pixels sem preenchimento
            const int32 RowSizeInBytes = BGRAFrame.cols * BGRAFrame.elemSize();
            // Garante que o buffer tem o tamanho correto para a imagem compactada (sem padding)
            FrameBuffer->SetNumUninitialized(BGRAFrame.rows * RowSizeInBytes);

            // *** NOVO: Copia a imagem linha por linha para evitar problemas de stride/padding ***
            for (int32 i = 0; i < BGRAFrame.rows; ++i)
            {
                // Origem: Aponta para o in�cio da linha 'i' na Mat do OpenCV, usando seu stride (passo)
                const uint8* SourceRowPtr = BGRAFrame.data + (i * BGRAFrame.step[0]);
                // Destino: Aponta para o in�cio da linha 'i' no nosso buffer, que � compactado
                uint8* DestRowPtr = FrameBuffer->GetData() + (i * RowSizeInBytes);
                // Copia apenas os dados da linha (sem padding)
                FMemory::Memcpy(DestRowPtr, SourceRowPtr, RowSizeInBytes);
            }
            // *** FIM DA NOVIDADE ***

            // Cria o FIVR_VideoFrame (cont�m o TSharedPtr para o buffer)
            FIVR_VideoFrame NewFrame(BGRAFrame.cols, BGRAFrame.rows, FPlatformTime::Seconds());
            NewFrame.RawDataPtr = FrameBuffer; // Atribui o buffer adquirido
            
            // Enfileira o frame para ser consumido pelo Game Thread
            CapturedFrameQueue.Enqueue(MoveTemp(NewFrame));
            
            // Sinaliza o evento para o Game Thread, indicando que h� um novo frame dispon�vel
            NewFrameEvent->Trigger(); 
        }

        UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Exiting run loop."));
        return 0; // Retorna sucesso
    }

    /**
     * @brief Sinaliza ao worker thread para parar.
     */
    virtual void Stop() override
    {
        bShouldStop.AtomicSet(true);
        if (NewFrameEvent) NewFrameEvent->Trigger(); // Acorda o Run para que ele possa sair do Wait
        UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Stop signal received."));
    }

    /**
     * @brief Limpeza final ao sair do thread.
     */
    virtual void Exit() override
    {
        if (WebcamCapture.isOpened())
        {
            WebcamCapture.release();
        }
        UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Exited."));
    }

public: 

    // Membros para armazenar a resolu��o real da captura, atualizados no Run()
    FThreadSafeCounter ActualFrameWidth;
    FThreadSafeCounter ActualFrameHeight;

private:

    cv::VideoCapture WebcamCapture; // Objeto de captura de v�deo do OpenCV
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& CapturedFrameQueue; // Refer�ncia � fila de frames
    UIVRFramePool* FramePool; // Refer�ncia ao pool para adquirir buffers
    FThreadSafeBool& bShouldStop; // Refer�ncia para a flag de parada do thread
    FEvent* NewFrameEvent; // Refer�ncia ao evento de sinaliza��o

    int32 DeviceIndex; // �ndice da webcam a ser utilizada
    int32 DesiredWidth; // Largura desejada para a captura
    int32 DesiredHeight; // Altura desejada para a captura
    float DesiredFPS; // FPS desejado para a captura
    cv::VideoCaptureAPIs ApiPreference; // Prefer�ncia de API para OpenCV
};


// ==============================================================================
// UIVRWebcamFrameSource Implementation
// ==============================================================================

UIVRWebcamFrameSource::UIVRWebcamFrameSource()
    : UIVRFrameSource()
    , WorkerRunnable(nullptr)
    , WorkerThread(nullptr)
{
    // Cria um evento de sincroniza��o que reseta automaticamente ap�s ser triggered
    NewFrameEvent = FPlatformProcess::GetSynchEventFromPool(false); 
}

void UIVRWebcamFrameSource::BeginDestroy()
{
    // Garante que a thread seja desligada e os recursos liberados durante a destrui��o do UObject
    Shutdown(); 

    // Libera o evento de sincroniza��o de volta para o pool
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
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRWebcamFrameSource::Initialize: World or FramePool is null."));
        return;
    }
    CurrentWorld = World;
    FrameSourceSettings = Settings;
    FramePool = InFramePool;

    // Cria a inst�ncia do worker runnable
    WorkerRunnable = new FWebcamCaptureWorker(
        FramePool,
        CapturedFrameQueue,
        bShouldStopWorker,
        NewFrameEvent,
        Settings.IVR_WebcamIndex,
        (int32)Settings.IVR_WebcamResolution.X,
        (int32)Settings.IVR_WebcamResolution.Y,
        Settings.IVR_WebcamFPS,
        cv::CAP_DSHOW // Passa a prefer�ncia de API para o worker
    );

    if (WorkerRunnable)
    {
        // Cria e inicia o thread.
        WorkerThread = FRunnableThread::Create(WorkerRunnable, TEXT("IVRWebcamCaptureThread"), 0, TPri_Normal);
        if (!WorkerThread)
        {
            UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRWebcamFrameSource: Failed to create worker thread."));
            delete WorkerRunnable; // Limpa o runnable se o thread n�o puder ser criado
            WorkerRunnable = nullptr;
        }
    }
    
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRWebcamFrameSource initialized for webcam index: %d."), Settings.IVR_WebcamIndex);
}

void UIVRWebcamFrameSource::Shutdown()
{
    // Garante que a captura e o polling sejam interrompidos
    StopCapture();

    // Sinaliza � thread worker para parar e espera que ela termine
    bShouldStopWorker.AtomicSet(true);
    if (NewFrameEvent) NewFrameEvent->Trigger(); // Acorda a thread caso esteja esperando

    // Espera a thread worker terminar completamente
    if (WorkerThread)
    {
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
    }
    // Libera a mem�ria do worker runnable
    if (WorkerRunnable)
    {
        delete WorkerRunnable;
        WorkerRunnable = nullptr;
    }

    // Limpa a fila de quaisquer frames remanescentes
    FIVR_VideoFrame DummyFrame;
    while (CapturedFrameQueue.Dequeue(DummyFrame))
    {
        // Libera o buffer do frame de volta para o pool, se for v�lido
        if (FramePool && DummyFrame.RawDataPtr.IsValid())
        {
            FramePool->ReleaseFrame(DummyFrame.RawDataPtr);
        }
    }

    // Limpa refer�ncias do UObject
    CurrentWorld = nullptr;
    FramePool = nullptr;
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRWebcamFrameSource Shutdown."));
}

void UIVRWebcamFrameSource::StartCapture()
{
    if (!CurrentWorld || !WorkerRunnable || !WorkerThread)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRWebcamFrameSource::StartCapture: Not initialized or worker not ready."));
        return;
    }
    
    // Reseta a flag para permitir que a thread worker execute
    bShouldStopWorker.AtomicSet(false);

    // Determina a cad�ncia de polling no Game Thread.
    // A leitura real dos frames no worker thread � t�o r�pida quanto o OpenCV permite.
    // O polling aqui controla com que frequ�ncia o Game Thread "pega" os frames da fila.
    float PollDelay = (FrameSourceSettings.IVR_WebcamFPS > 0.0f) ? (1.0f / FrameSourceSettings.IVR_WebcamFPS) : (1.0f / 30.0f);

    // Inicia o timer para polling de frames da fila
    CurrentWorld->GetTimerManager().SetTimer(FramePollTimerHandle, this, &UIVRWebcamFrameSource::PollForNewFrames, PollDelay, true);
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRWebcamFrameSource: Starting frame capture from webcam. Polling every %.4f seconds."), PollDelay);
}

void UIVRWebcamFrameSource::StopCapture()
{
    // Para o timer de polling no Game Thread
    if (CurrentWorld && CurrentWorld->GetTimerManager().IsTimerActive(FramePollTimerHandle))
    {
        CurrentWorld->GetTimerManager().ClearTimer(FramePollTimerHandle);
    }
    FramePollTimerHandle.Invalidate();

    // Sinaliza � thread worker para parar
    bShouldStopWorker.AtomicSet(true);
    if (NewFrameEvent) NewFrameEvent->Trigger(); // Acorda a thread caso esteja esperando (para que ela possa sair do loop Run())

    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRWebcamFrameSource: Stopped frame capture."));
}

void UIVRWebcamFrameSource::PollForNewFrames()
{
    FIVR_VideoFrame QueuedFrame;
    // Enquanto houver frames na fila, os retira e broadcasta
    while (CapturedFrameQueue.Dequeue(QueuedFrame))
    {
        // Broadcast do frame processado (o TSharedPtr dentro de QueuedFrame � movido)
        OnFrameAcquired.Broadcast(MoveTemp(QueuedFrame));
    }
}

TArray<FString> UIVRWebcamFrameSource::ListWebcamDevices()
{
    TArray<FString> Devices;
    // Tenta abrir e fechar dispositivos para verificar se existem e obter algumas caracter�sticas.
    // O OpenCV n�o oferece um m�todo direto para listar nomes de dispositivos de forma multiplataforma.
    // Esta abordagem tenta os primeiros 10 �ndices de dispositivos.
    
    // NOTA: cv::VideoCapture cap(index) pode levar alguns milissegundos para abrir e fechar,
    // ent�o esta fun��o pode ser um pouco lenta dependendo do n�mero de dispositivos testados.
    
    for (int32 i = 0; i < 10; ++i) // Tenta os primeiros 10 dispositivos
    {
        // Utiliza cv::CAP_DSHOW para tentar uma abertura mais robusta durante a listagem
        cv::VideoCapture TestCapture(i, cv::CAP_DSHOW); 
        if (TestCapture.isOpened())
        {
            FString DeviceInfo = FString::Printf(TEXT("Device %d (Res: %dx%d @ %.1fFPS)"), i,
                                                (int32)TestCapture.get(cv::CAP_PROP_FRAME_WIDTH),
                                                (int32)TestCapture.get(cv::CAP_PROP_FRAME_HEIGHT),
                                                (float)TestCapture.get(cv::CAP_PROP_FPS));
            Devices.Add(DeviceInfo);
            TestCapture.release(); // Libera o recurso da webcam
        }
    }
    if (Devices.Num() == 0)
    {
        Devices.Add(TEXT("No webcams found."));
    }
    UE_LOG(LogIVRFrameSource, Log, TEXT("Listed Webcam Devices: %s"), *FString::Join(Devices, TEXT(", ")));
    return Devices;
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


