// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Recording/IVRVideoFrameSource.h"
#include "IVR.h"
#include "Engine/World.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include <atomic> // Adicionado para std::atomic

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
// FVideoFileCaptureWorker Implementation (FRunnable para Thread de Captura de Arquivo)
// ==============================================================================

/**
 * @brief Worker thread para ler frames de um arquivo de vdeo usando OpenCV.
 * Esta classe opera em um thread separado para evitar bloqueios do Game Thread.
 */
class FVideoFileCaptureWorker : public FRunnable
{
public:
    /**
     * @brief Construtor do worker.
     * @param InFramePool Referncia ao pool de frames para adquirir buffers.
     * @param InQueue Fila para enfileirar frames capturados.
     * @param InStopFlag Flag para sinalizar a parada do thread.
     * @param InNewFrameEvent Evento para sinalizar novos frames.
     * @param InVideoFilePath Caminho para o arquivo de vdeo.
     * @param InDesiredFPS Taxa de quadros desejada para leitura (pode no ser respeitada pela webcam).
     * @param InLoopPlayback Se o vdeo deve ser reproduzido em loop.
     */
    FVideoFileCaptureWorker(UIVRFramePool* InFramePool, TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& InQueue, FThreadSafeBool& InStopFlag, FEvent* InNewFrameEvent, const FString& InVideoFilePath, float InDesiredFPS, bool InLoopPlayback)
        : CapturedFrameQueue(InQueue)
        , FramePool(InFramePool)
        , bShouldStop(InStopFlag)
        , NewFrameEvent(InNewFrameEvent)
        , VideoFilePath(InVideoFilePath)
        , DesiredFPS(InDesiredFPS)
        , bLoopPlayback(InLoopPlayback)
    {}

    /**
     * @brief Destrutor do worker, libera o VideoCapture.
     */
    virtual ~FVideoFileCaptureWorker()
    {
        if (VideoCapture.isOpened())
        {
            VideoCapture.release();
        }
    }

    /**
     * @brief Inicializa o worker, tenta abrir o arquivo de vdeo.
     * @return true se o arquivo foi aberto com sucesso, false caso contrrio.
     */
    virtual bool Init() override
    {
        // Converte FString para std::string, pois OpenCV opera com ela
        std::string VideoPathStd = TCHAR_TO_UTF8(*VideoFilePath);
        VideoCapture.open(VideoPathStd);

        if (!VideoCapture.isOpened())
        {
            UE_LOG(LogIVRFrameSource, Error, TEXT("VideoFileCaptureWorker: Failed to open video file: %s"), *VideoFilePath);
            return false;
        }

        // Tenta definir FPS de leitura se o DesiredFPS for diferente do nativo do vdeo
        // Nota: cv::CAP_PROP_FPS  o FPS *nativo* do vdeo. No podemos mudar a taxa de decodificao
        // de forma arbitrria facilmente. A cadncia de broadcast ser controlada pelo GameThread.
        // --- NOVO: Captura o FPS nativo do vdeo ---
        ActualVideoFileFPS.store((float)VideoCapture.get(cv::CAP_PROP_FPS)); // Usando store() para std::atomic
        // --- FIM NOVO ---
        VideoCapture.set(cv::CAP_PROP_FPS, DesiredFPS);

        // Atualiza as dimenses reais aps a abertura
        ActualFrameWidth.Set((int32)VideoCapture.get(cv::CAP_PROP_FRAME_WIDTH));
        ActualFrameHeight.Set((int32)VideoCapture.get(cv::CAP_PROP_FRAME_HEIGHT));

        UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Opened video %s. Actual resolution: %dx%d, Actual FPS: %.1f"),
               *VideoFilePath, ActualFrameWidth.GetValue(), ActualFrameHeight.GetValue(), ActualVideoFileFPS.load()); // Usando load() para std::atomic

        bShouldStop.AtomicSet(false);
        return true;
    }

    /**
     * @brief Loop principal do worker thread para ler frames.
     * @return 0 ao sair.
     */
    virtual uint32 Run() override
    {
        UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Running."));
        cv::Mat Frame; // Matriz OpenCV para armazenar o frame
        
        while (!bShouldStop)
        {
            if (!VideoCapture.isOpened())
            {
                UE_LOG(LogIVRFrameSource, Warning, TEXT("VideoFileCaptureWorker: Video capture not open, stopping thread."));
                bShouldStop.AtomicSet(true);
                break;
            }

            // L um frame do vdeo
            VideoCapture.read(Frame);

            if (Frame.empty())
            {
                // Fim do vdeo ou erro de leitura
                if (bLoopPlayback)
                {
                    UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: End of video reached, looping back."));
                    VideoCapture.set(cv::CAP_PROP_POS_FRAMES, 0); // Volta para o incio do vdeo
                    VideoCapture.read(Frame); // Tenta ler o primeiro frame
                    if (Frame.empty()) // Se ainda estiver vazio, algo est errado
                    {
                        UE_LOG(LogIVRFrameSource, Error, TEXT("VideoFileCaptureWorker: Failed to loop video or read first frame after loop. Stopping."));
                        bShouldStop.AtomicSet(true);
                        break;
                    }
                }
                else
                {
                    UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: End of video reached, stopping."));
                    bShouldStop.AtomicSet(true);
                    break;
                }
            }

            // Adquire um buffer do pool (otimiza reuso de memria)
            TSharedPtr<TArray<uint8>> FrameBuffer = FramePool->AcquireFrame();
            if (!FrameBuffer.IsValid())
            {
                UE_LOG(LogIVRFrameSource, Error, TEXT("VideoFileCaptureWorker: Failed to acquire frame buffer from pool. Dropping frame."));
                continue; // Pula este frame
            }

            // Converte o frame OpenCV (geralmente BGR) para BGRA (formato esperado pelo FFmpeg)
            cv::Mat BGRAFrame;
            cv::cvtColor(Frame, BGRAFrame, cv::COLOR_BGR2BGRA);

            // Garante que o buffer tem o tamanho correto e copia os dados do frame BGRA
            // Calcula o tamanho de uma linha de pixels sem preenchimento
            const int32 RowSizeInBytes = BGRAFrame.cols * BGRAFrame.elemSize();
            // Garante que o buffer tem o tamanho correto para a imagem compactada (sem padding)
            FrameBuffer->SetNumUninitialized(BGRAFrame.rows * RowSizeInBytes);

            // Copia a imagem linha por linha para evitar problemas de stride/padding
            for (int32 i = 0; i < BGRAFrame.rows; ++i)
            {
                // Origem: Aponta para o incio da linha 'i' na Mat do OpenCV, usando seu stride (passo)
                const uint8* SourceRowPtr = BGRAFrame.data + (i * BGRAFrame.step[0]);
                // Destino: Aponta para o incio da linha 'i' no nosso buffer, que  compactado
                uint8* DestRowPtr = FrameBuffer->GetData() + (i * RowSizeInBytes);
                // Copia apenas os dados da linha (sem padding)
                FMemory::Memcpy(DestRowPtr, SourceRowPtr, RowSizeInBytes);
            }
            
            // Cria o FIVR_VideoFrame (contm o TSharedPtr para o buffer)
            FIVR_VideoFrame NewFrame(BGRAFrame.cols, BGRAFrame.rows, FPlatformTime::Seconds());
            NewFrame.RawDataPtr = FrameBuffer; // Atribui o buffer adquirido
            
            // Enfileira o frame para ser consumido pelo Game Thread
            CapturedFrameQueue.Enqueue(MoveTemp(NewFrame));
            
            // Sinaliza o evento para o Game Thread, indicando que h um novo frame disponvel
            NewFrameEvent->Trigger(); 
        }

        UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Exiting run loop."));
        return 0;
    }

    /**
     * @brief Sinaliza ao worker thread para parar.
     */
    virtual void Stop() override
    {
        bShouldStop.AtomicSet(true);
        if (NewFrameEvent) NewFrameEvent->Trigger();
        UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Stop signal received."));
    }

    /**
     * @brief Limpeza final ao sair do thread.
     */
    virtual void Exit() override
    {
        if (VideoCapture.isOpened())
        {
            VideoCapture.release();
        }
        UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Exited."));
    }

public: 
    // === Membros da classe que precisam ser declarados aqui para a defini��o local ===
    cv::VideoCapture VideoCapture; 
    FThreadSafeCounter ActualFrameWidth;
    FThreadSafeCounter ActualFrameHeight;
    std::atomic<float> ActualVideoFileFPS; // NOVO: Usando std::atomic para float thread-safe

private:
    
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& CapturedFrameQueue;
    UIVRFramePool* FramePool;
    FThreadSafeBool& bShouldStop;
    FEvent* NewFrameEvent;

    FString VideoFilePath;
    float DesiredFPS;
    bool bLoopPlayback;
};


// ==============================================================================
// UIVRVideoFrameSource Implementation
// ==============================================================================

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
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRVideoFrameSource::Initialize: World or FramePool is null."));
        return;
    }
    CurrentWorld = World;
    FrameSourceSettings = Settings;
    FramePool = InFramePool;

    // Cria a instncia do worker runnable
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
            UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRVideoFrameSource: Failed to create worker thread."));
            delete WorkerRunnable;
            WorkerRunnable = nullptr;
        }
    }
    
    // O Init() do WorkerRunnable  chamado pela prpria thread. Verificamos se a webcam abriu.
    // Damos um pequeno tempo para o Init() ser executado na thread.
    // Alternativamente, WorkerRunnable->Init() pode ser chamado explicitamente antes de FRunnableThread::Create
    // se quisermos falhar a inicializao imediatamente no GameThread.
    // Para um tratamento robusto, o Init() do worker deve retornar um bool e ser verificado aqui.
    // No entanto, FRunnableThread::Create lana a thread e seu Init(). Erros devem ser capturados no Run().
    
    // Para simplificar a verificao de sucesso na inicializao da webcam/vdeo:
    // Uma soluo mais elegante seria ter um FThreadSafeBool IsInitialized no worker
    // que seria setado para true se o OpenCV VideoCapture.open() for bem-sucedido.
    // Por enquanto, confiamos que os logs no worker indicam o estado.

    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRVideoFrameSource initialized for video file: %s."), *Settings.IVR_VideoFilePath);
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
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRVideoFrameSource Shutdown."));
}

void UIVRVideoFrameSource::StartCapture()
{
    if (!CurrentWorld || !WorkerRunnable || !WorkerThread)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRVideoFrameSource::StartCapture: Not initialized or worker not ready."));
        return;
    }
    
    bShouldStopWorker.AtomicSet(false);

    // Ajusta o FPS de polling baseado no FPS do vdeo e na velocidade de reproduo.
    // O DesiredFPS na worker  o FPS de leitura, o FPS do GameThread  a cadncia de broadcast.
    float VideoNativeFPS = GetActualVideoFileFPS(); // Obtm o FPS nativo do vdeo
    if (VideoNativeFPS <= 0.0f) VideoNativeFPS = 30.0f; // Fallback para evitar diviso por zero

    // Calcula o FPS efetivo de broadcast, considerando o VideoPlaybackSpeed
    float EffectiveFPS = VideoNativeFPS * FrameSourceSettings.IVR_VideoPlaybackSpeed;
    float PollDelay = (EffectiveFPS > 0.0f) ? (1.0f / EffectiveFPS) : (1.0f / 30.0f);

    CurrentWorld->GetTimerManager().SetTimer(FramePollTimerHandle, this, &UIVRVideoFrameSource::PollForNewFrames, PollDelay, true);
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRVideoFrameSource: Starting frame capture from video file at %.2f FPS (effective)."), EffectiveFPS);
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

    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRVideoFrameSource: Stopped frame capture."));
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
        return WorkerRunnable->ActualVideoFileFPS.load(); // Usando load() para std::atomic
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

