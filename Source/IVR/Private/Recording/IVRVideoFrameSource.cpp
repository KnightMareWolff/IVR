// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
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

// C2: Mover Includes do OpenCV para o escopo global do arquivo .cpp.
// Este bloco foi movido de após a declaração da classe FVideoFileCaptureWorker para aqui.
#if WITH_OPENCV 
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h" // Abre namespace/desativa avisos

#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

#include "PostOpenCVHeaders.h" // Fecha namespace/reativa avisos
#endif

// ==============================================================================
// FVideoFileCaptureWorker Implementation (FRunnable para Thread de Captura de Arquivo)
// ESTA CLASSE FRunnable É DEFINIDA AQUI (AGORA COM ACESSO DIRETO AOS TIPOS DO OpenCV)
// ==============================================================================
/**
 * @brief Worker thread para ler frames de um arquivo de vídeo usando OpenCV.
 * Esta classe opera em um thread separado para evitar bloqueios do Game Thread.
 */
class FVideoFileCaptureWorker : public FRunnable
{
public:
    /**
     * @brief Construtor do worker.
     * @param InFramePool Referência ao pool de frames para adquirir buffers.
     * @param InQueue Fila para enfileirar frames capturados.
     * @param InStopFlag Flag para sinalizar a parada do thread.
     * @param InNewFrameEvent Evento para sinalizar novos frames.
     * @param InVideoFilePath Caminho para o arquivo de vídeo.
     * @param InDesiredFPS Taxa de quadros desejada para leitura (pode não ser respeitada pela webcam).
     * @param InLoopPlayback Se o vídeo deve ser reproduzido em loop.
     */
    FVideoFileCaptureWorker(UIVRFramePool* InFramePool, TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& InQueue, FThreadSafeBool& InStopFlag, FEvent* InNewFrameEvent, const FString& InVideoFilePath, float InDesiredFPS, bool InLoopPlayback);
    
    /**
     * @brief Destrutor do worker, libera o VideoCapture.
     */
    virtual ~FVideoFileCaptureWorker();
    /**
     * @brief Inicializa o worker, tenta abrir o arquivo de vídeo.
     * @return true se o arquivo foi aberto com sucesso, false caso contrário.
     */
    virtual bool Init() override;

    /**
     * @brief Loop principal do worker thread para ler frames.
     * @return 0 ao sair.
     */
    virtual uint32 Run() override;

    /**
     * @brief Sinaliza ao worker thread para parar.
     */
    virtual void Stop() override;

    /**
     * @brief Limpeza final ao sair do thread.
     */
    virtual void Exit() override;

public: 
    // Membros da classe que precisam ser declarados aqui para a definição local
    #if WITH_OPENCV
    cv::VideoCapture VideoCapture; // cv::VideoCapture agora é definido porque os includes estão no escopo global
    #else
    void* VideoCapture; // Fallback se OpenCV não estiver habilitado
    #endif

    FThreadSafeCounter ActualFrameWidth;
    FThreadSafeCounter ActualFrameHeight;
    std::atomic<float> ActualVideoFileFPS; // Usando std::atomic para float thread-safe
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
// IMPLEMENTAÇÃO DOS MÉTODOS DA CLASSE FVideoFileCaptureWorker
// ==============================================================================

FVideoFileCaptureWorker::FVideoFileCaptureWorker(UIVRFramePool* InFramePool, TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& InQueue, FThreadSafeBool& InStopFlag, FEvent* InNewFrameEvent, const FString& InVideoFilePath, float InDesiredFPS, bool InLoopPlayback)
    : CapturedFrameQueue(InQueue)
    , FramePool(InFramePool)
    , bShouldStop(InStopFlag)
    , NewFrameEvent(InNewFrameEvent)
    , VideoFilePath(InVideoFilePath)
    , DesiredFPS(InDesiredFPS)
    , bLoopPlayback(InLoopPlayback)
{
#if !WITH_OPENCV
    // Initialize fallback if OpenCV is not enabled
    VideoCapture = nullptr;
    UE_LOG(LogIVRFrameSource, Error, TEXT("FVideoFileCaptureWorker: OpenCV não habilitado. A captura de vídeo não funcionará."));
#endif
}
FVideoFileCaptureWorker::~FVideoFileCaptureWorker()
{
#if WITH_OPENCV
    if (VideoCapture.isOpened())
    {
        VideoCapture.release();
    }
#endif
}

bool FVideoFileCaptureWorker::Init()
{
#if WITH_OPENCV
    // Converte FString para std::string, pois OpenCV opera com ela
    std::string VideoPathStd = TCHAR_TO_UTF8(*VideoFilePath);
    VideoCapture.open(VideoPathStd);

    if (!VideoCapture.isOpened())
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("VideoFileCaptureWorker: Falha ao abrir arquivo de vídeo: %s"), *VideoFilePath);
        return false;
    }

    ActualVideoFileFPS.store((float)VideoCapture.get(cv::CAP_PROP_FPS)); // Usando store() para std::atomic
    VideoCapture.set(cv::CAP_PROP_FPS, DesiredFPS);

    // Atualiza as dimensões reais após a abertura
    ActualFrameWidth.Set((int32)VideoCapture.get(cv::CAP_PROP_FRAME_WIDTH));
    ActualFrameHeight.Set((int32)VideoCapture.get(cv::CAP_PROP_FRAME_HEIGHT));
    UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Abriu vídeo %s. Resolução: %dx%d, FPS Real: %.1f"),
           *VideoFilePath, ActualFrameWidth.GetValue(), ActualFrameHeight.GetValue(), ActualVideoFileFPS.load()); // Usando load() para std::atomic

    bShouldStop.AtomicSet(false);
    return true;
#else
    UE_LOG(LogIVRFrameSource, Error, TEXT("FVideoFileCaptureWorker::Init: OpenCV não habilitado. Não é possível inicializar."));
    return false;
#endif
}

uint32 FVideoFileCaptureWorker::Run()
{
    UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Executando."));
#if WITH_OPENCV
    cv::Mat Frame; // Matriz OpenCV para armazenar o frame
    
    while (!bShouldStop)
    {
        if (!VideoCapture.isOpened())
        {
            UE_LOG(LogIVRFrameSource, Warning, TEXT("VideoFileCaptureWorker: Captura de vídeo não aberta, parando thread."));
            bShouldStop.AtomicSet(true);
            break;
        }
        // Lê um frame do vídeo
        VideoCapture.read(Frame);
        if (Frame.empty())
        {
            // Fim do vídeo ou erro de leitura
            if (bLoopPlayback)
            {
                UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Fim do vídeo alcançado, voltando ao início."));
                VideoCapture.set(cv::CAP_PROP_POS_FRAMES, 0); // Volta para o início do vídeo
                VideoCapture.read(Frame); // Tenta ler o primeiro frame
                if (Frame.empty()) // Se ainda estiver vazio, algo está errado
                {
                    UE_LOG(LogIVRFrameSource, Error, TEXT("VideoFileCaptureWorker: Falha ao fazer loop no vídeo ou ler o primeiro frame após o loop. Parando."));
                    bShouldStop.AtomicSet(true);
                    break;
                }
            }
            else
            {
                UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Fim do vídeo alcançado, parando."));
                bShouldStop.AtomicSet(true);
                break;
            }
        }
        // Adquire um buffer do pool (otimiza reuso de memória)
        TSharedPtr<TArray<uint8>> FrameBuffer = FramePool->AcquireFrame();
        if (!FrameBuffer.IsValid())
        {
            UE_LOG(LogIVRFrameSource, Error, TEXT("VideoFileCaptureWorker: Falha ao adquirir buffer de frame do pool. Descartando frame."));
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
            // Origem: Aponta para o início da linha 'i' na Mat do OpenCV, usando seu stride (passo)
            const uint8* SourceRowPtr = BGRAFrame.data + (i * BGRAFrame.step[0]);
            // Destino: Aponta para o início da linha 'i' no nosso buffer, que é compactado
            uint8* DestRowPtr = FrameBuffer->GetData() + (i * RowSizeInBytes);
            // Copia apenas os dados da linha (sem padding)
            FMemory::Memcpy(DestRowPtr, SourceRowPtr, RowSizeInBytes);
        }
        
        // Cria o FIVR_VideoFrame (contém o TSharedPtr para o buffer)
        FIVR_VideoFrame NewFrame(BGRAFrame.cols, BGRAFrame.rows, FPlatformTime::Seconds());
        NewFrame.RawDataPtr = FrameBuffer; // Atribui o buffer adquirido
        
        // Enfileira o frame para ser consumido pelo Game Thread
        CapturedFrameQueue.Enqueue(MoveTemp(NewFrame));
        
        // Sinaliza o evento para o Game Thread, indicando que há um novo frame disponível
        NewFrameEvent->Trigger(); 
    }
#else // !WITH_OPENCV
    UE_LOG(LogIVRFrameSource, Error, TEXT("VideoFileCaptureWorker::Run: OpenCV não habilitado. Não é possível capturar frames."));
#endif // WITH_OPENCV
    UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Saindo do loop de execução."));
    return 0;
}

void FVideoFileCaptureWorker::Stop()
{
    bShouldStop.AtomicSet(true);
    if (NewFrameEvent) NewFrameEvent->Trigger();
    UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Sinal de parada recebido."));
}

void FVideoFileCaptureWorker::Exit()
{
#if WITH_OPENCV
    if (VideoCapture.isOpened())
    {
        VideoCapture.release();
    }
#endif
    UE_LOG(LogIVRFrameSource, Log, TEXT("VideoFileCaptureWorker: Encerrado."));
}


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
        #if WITH_OPENCV
        return WorkerRunnable->ActualFrameWidth.GetValue();
        #else
        return 0; // Se OpenCV não habilitado
        #endif
    }
    return 0; 
}

int32 UIVRVideoFrameSource::GetActualFrameHeight() const
{
    if (WorkerRunnable)
    {
        #if WITH_OPENCV
        return WorkerRunnable->ActualFrameHeight.GetValue();
        #else
        return 0; // Se OpenCV não habilitado
        #endif
    }
    return 0; 
}

float UIVRVideoFrameSource::GetActualVideoFileFPS() const
{
    if (WorkerRunnable)
    {
        #if WITH_OPENCV
        return WorkerRunnable->ActualVideoFileFPS.load(); // Usando load() para std::atomic
        #else
        return 0.0f; // Se OpenCV não habilitado
        #endif
    }
    return 0.0f; 
}

float UIVRVideoFrameSource::GetEffectivePlaybackFPS() const
{
    if (WorkerRunnable)
    {
        #if WITH_OPENCV
        float nativeFPS = GetActualVideoFileFPS();
        if (nativeFPS > 0.0f)
        {
            return nativeFPS * FrameSourceSettings.IVR_VideoPlaybackSpeed;
        }
        #endif
    }
    return 0.0f; 
}