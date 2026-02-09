// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
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

// C2: Mover Includes do OpenCV para o escopo global do arquivo .cpp.
#if WITH_OPENCV 
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h" // Abre namespace/desativa avisos

#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

#include "PostOpenCVHeaders.h" // Fecha namespace/reativa avisos
#endif

// ==============================================================================
// FWebcamCaptureWorker Implementation (FRunnable para Thread de Captura)
// ESTA CLASSE FRunnable É DEFINIDA AQUI (AGORA COM ACESSO DIRETO AOS TIPOS DO OpenCV)
// ==============================================================================

// **NOVO**: Definição de uma classe Dummy para cv::VideoCapture quando WITH_OPENCV não está ativo.
// Isso garante que o membro 'WebcamCapture' sempre tenha um tipo declarado e métodos stubs,
// evitando erros C2065 e C2039 se WITH_OPENCV for 0, mesmo que o código seja guardado.
#if !WITH_OPENCV
struct FDummyVideoCapture {
    bool isOpened() const { return false; }
    void release() {}
    void open(int index, int apiPreference) {}
    void set(int propId, double value) {}
    double get(int propId) { return 0.0; }
    bool read(void* frame) { return false; } // Dummy read, cv::Mat& é a assinatura real
};
// Define cv::VideoCaptureAPIs como int para compatibilidade quando OpenCV não está presente
using VideoCaptureAPIs = int;
#else
// Quando WITH_OPENCV está ativo, usa os tipos reais do OpenCV
using VideoCaptureAPIs = cv::VideoCaptureAPIs;
#endif

/**
 * @brief Worker thread para capturar frames de uma webcam usando OpenCV.
 * Esta classe opera em um thread separado para evitar bloqueios do Game Thread.
 */
class FWebcamCaptureWorker : public FRunnable
{
public:
    /**
     * @brief Construtor do worker.
     * @param InFramePool Referência ao pool de frames para adquirir buffers.
     * @param InQueue Fila para enfileirar frames capturados.
     * @param InStopFlag Flag para sinalizar a parada do thread.
     * @param InNewFrameEvent Evento para sinalizar novos frames.
     * @param InDeviceIndex Índice da webcam a ser utilizada.
     * @param InWidth Largura desejada para a captura.
     * @param InHeight Altura desejada para a captura.
     * @param InFPS FPS desejado para a captura.
     * @param InApiPreference Preferência de API para OpenCV (e.g., cv::CAP_DSHOW).
     */
    FWebcamCaptureWorker(UIVRFramePool* InFramePool, TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& InQueue, FThreadSafeBool& InStopFlag, FEvent* InNewFrameEvent, int32 InDeviceIndex, int32 InWidth, int32 InHeight, float InFPS,
        VideoCaptureAPIs InApiPreference
    );
    
    /**
     * @brief Destrutor do worker, libera o VideoCapture.
     */
    virtual ~FWebcamCaptureWorker();
    /**
     * @brief Inicializa o worker. Não abre a webcam aqui para evitar bloqueios no início da thread.
     * @return true.
     */
    virtual bool Init() override;

    /**
     * @brief Loop principal do worker thread para capturar frames.
     * Abertura da webcam ocorre aqui para garantir que bloqueios aconteçam no worker thread.
     * @return 0 ao sair, ou 1 se a webcam não puder ser aberta.
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
    // Membros para armazenar a resolução real da captura, atualizados no Run()
    FThreadSafeCounter ActualFrameWidth;
    FThreadSafeCounter ActualFrameHeight;

private:
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& CapturedFrameQueue; // Referência à fila de frames
    UIVRFramePool* FramePool; // Referência ao pool para adquirir buffers
    FThreadSafeBool& bShouldStop; // Referência para a flag de parada do thread
    FEvent* NewFrameEvent; // Referência ao evento de sinalização

    int32 DeviceIndex; // Índice da webcam a ser utilizada
    int32 DesiredWidth; // Largura desejada para a captura
    int32 DesiredHeight; // Altura desejada para a captura
    float DesiredFPS; // FPS desejado para a captura

#if WITH_OPENCV // Membros OpenCV devem ser condicionalmente compilados
    cv::VideoCapture WebcamCapture; // Objeto de captura de vídeo do OpenCV
    cv::VideoCaptureAPIs ApiPreference; // Preferência de API para OpenCV
#else
    FDummyVideoCapture WebcamCapture; // Agora usa o tipo dummy
    VideoCaptureAPIs ApiPreference; // Usa o 'using' para o tipo
#endif
};


// ==============================================================================
// IMPLEMENTAÇÃO DOS MÉTODOS DA CLASSE FWebcamCaptureWorker
// ==============================================================================
FWebcamCaptureWorker::FWebcamCaptureWorker(UIVRFramePool* InFramePool, TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& InQueue, FThreadSafeBool& InStopFlag, FEvent* InNewFrameEvent, int32 InDeviceIndex, int32 InWidth, int32 InHeight, float InFPS,
    VideoCaptureAPIs InApiPreference
)
    : CapturedFrameQueue(InQueue)
    , FramePool(InFramePool)
    , bShouldStop(InStopFlag)
    , NewFrameEvent(InNewFrameEvent)
    , DeviceIndex(InDeviceIndex)
    , DesiredWidth(InWidth)
    , DesiredHeight(InHeight)
    , DesiredFPS(InFPS)
#if WITH_OPENCV // Inicialização condicional para membros cv::
    , WebcamCapture(InDeviceIndex, InApiPreference) // Inicializa com os parâmetros reais do OpenCV
    , ApiPreference(InApiPreference)
#else
    // Para o tipo dummy, o construtor padrão já é suficiente.
    // A chamada open() será um stub.
    // É importante inicializar ApiPreference também.
    , ApiPreference(InApiPreference) 
#endif
{
#if !WITH_OPENCV
    // O log de erro agora é mais um aviso, já que o código pode compilar,
    // mas a funcionalidade de webcam estará inativa.
    UE_LOG(LogIVRFrameSource, Warning, TEXT("FWebcamCaptureWorker: OpenCV não habilitado. A captura de webcam não funcionará."));
#endif
}
FWebcamCaptureWorker::~FWebcamCaptureWorker()
{
    if (WebcamCapture.isOpened()) // Chamada sempre válida devido ao tipo dummy
    {
        WebcamCapture.release(); // Chamada sempre válida devido ao tipo dummy
    }
}

bool FWebcamCaptureWorker::Init()
{
    bShouldStop.AtomicSet(false);
    // Abertura da webcam agora é feita no Run(), no worker thread, para evitar bloqueios no início.
    UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Inicializado com sucesso. Abertura da webcam adiada para Run()."));
    return true;
}

uint32 FWebcamCaptureWorker::Run()
{
    UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Executando."));
#if WITH_OPENCV
    cv::Mat Frame; // Matriz OpenCV para armazenar o frame
    // Tenta abrir a webcam no início do Run(), no worker thread.
    WebcamCapture.open(DeviceIndex, ApiPreference);
    if (!WebcamCapture.isOpened())
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("WebcamCaptureWorker: Falha ao abrir dispositivo de webcam %d com API %d. Parando thread de captura."), DeviceIndex, (int32)ApiPreference);
        bShouldStop.AtomicSet(true); // Sinaliza para parar
        return 1; // Retorna código de erro
    }

    // Tenta definir a resolução, FPS e codec *após* a abertura bem-sucedida
    WebcamCapture.set(cv::CAP_PROP_FRAME_WIDTH , DesiredWidth);
    WebcamCapture.set(cv::CAP_PROP_FRAME_HEIGHT, DesiredHeight);
    WebcamCapture.set(cv::CAP_PROP_FPS, DesiredFPS);
    WebcamCapture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G')); // Tenta MJPG para melhor FPS/qualidade
    // Verifica as configurações aplicadas (podem não ser exatamente as desejadas pela webcam)
    ActualFrameWidth.Set((int32)WebcamCapture.get(cv::CAP_PROP_FRAME_WIDTH));
    ActualFrameHeight.Set((int32)WebcamCapture.get(cv::CAP_PROP_FRAME_HEIGHT));

    UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Abriu dispositivo %d com API %d. Resolução: %dx%d, FPS Real: %.1f"),
           DeviceIndex, (int32)ApiPreference, ActualFrameWidth.GetValue(), ActualFrameHeight.GetValue(), (float)WebcamCapture.get(cv::CAP_PROP_FPS));

    while (!bShouldStop)
    {
        if (!WebcamCapture.isOpened())
        {
            UE_LOG(LogIVRFrameSource, Warning, TEXT("WebcamCaptureWorker: Webcam foi fechada inesperadamente. Parando thread de captura."));
            bShouldStop.AtomicSet(true);
            break;
        }

        // Lê um frame da webcam (geralmente é uma chamada bloqueante até um frame estar disponível)
        WebcamCapture.read(Frame);
        if (Frame.empty())
        {
            // Se o frame estiver vazio, pode ser que a webcam esteja sendo desconectada ou em um estado ruim
            UE_LOG(LogIVRFrameSource, Warning, TEXT("WebcamCaptureWorker: Frame vazio recebido. Tentando novamente..."));
            FPlatformProcess::Sleep(0.1f); // Pequena pausa antes de tentar novamente
            continue; // Tenta novamente na próxima iteração
        }

        // Adquire um buffer do pool (otimiza reuso de memória)
        TSharedPtr<TArray<uint8>> FrameBuffer = FramePool->AcquireFrame();
        if (!FrameBuffer.IsValid())
        {
            UE_LOG(LogIVRFrameSource, Error, TEXT("WebcamCaptureWorker: Falha ao adquirir buffer de frame do pool. Descartando frame."));
            continue; // Pula este frame
        }

        // Converte o frame OpenCV (geralmente BGR) para BGRA (formato esperado pelo FFmpeg)
        cv::Mat BGRAFrame;
        cv::cvtColor(Frame, BGRAFrame, cv::COLOR_BGR2BGRA);
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
    UE_LOG(LogIVRFrameSource, Warning, TEXT("WebcamCaptureWorker::Run: OpenCV não habilitado. Não é possível capturar frames."));
#endif // WITH_OPENCV
    UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Saindo do loop de execução."));
    return 0; // Retorna sucesso
}
void FWebcamCaptureWorker::Stop()
{
    bShouldStop.AtomicSet(true);
    if (NewFrameEvent) NewFrameEvent->Trigger(); // Acorda o Run para que ele possa sair do Wait
    UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Sinal de parada recebido."));
}

void FWebcamCaptureWorker::Exit()
{
    if (WebcamCapture.isOpened()) // Chamada sempre válida devido ao tipo dummy
    {
        WebcamCapture.release(); // Chamada sempre válida devido ao tipo dummy
    }
    UE_LOG(LogIVRFrameSource, Log, TEXT("WebcamCaptureWorker: Encerrado."));
}


// ==============================================================================
// UIVRWebcamFrameSource Implementation
// ==============================================================================

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
#if WITH_OPENCV
        cv::CAP_DSHOW // Passa a preferência de API para o worker
#else
        0 // Fallback para int se OpenCV não estiver habilitado
#endif
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
    
    // Reseta a flag para permitir que a thread worker execute
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
    TArray<FString> Devices;
#if WITH_OPENCV // Código de listagem de webcams protegido por #if WITH_OPENCV
    // Tenta abrir e fechar dispositivos para verificar se existem e obter algumas características.
    // O OpenCV não oferece um método direto para listar nomes de dispositivos de forma multiplataforma.
    // Esta abordagem tenta os primeiros 10 índices de dispositivos.
    
    // NOTA: cv::VideoCapture cap(index) pode levar alguns milissegundos para abrir e fechar,
    // então esta função pode ser um pouco lenta dependendo do número de dispositivos testados.
    
    for (int32 i = 0; i < 10; ++i) // Tenta os primeiros 10 dispositivos
    {
        // Utiliza cv::CAP_DSHOW para tentar uma abertura mais robusta durante a listagem
        cv::VideoCapture TestCapture(i, cv::CAP_DSHOW); 
        if (TestCapture.isOpened())
        {
            FString DeviceInfo = FString::Printf(TEXT("Dispositivo %d (Res: %dx%d @ %.1fFPS)"), i,
                                                (int32)TestCapture.get(cv::CAP_PROP_FRAME_WIDTH),
                                                (int32)TestCapture.get(cv::CAP_PROP_FRAME_HEIGHT),
                                                (float)TestCapture.get(cv::CAP_PROP_FPS));
            Devices.Add(DeviceInfo);
            TestCapture.release(); // Libera o recurso da webcam
        }
    }
#else
    UE_LOG(LogIVRFrameSource, Warning, TEXT("ListWebcamDevices: OpenCV não habilitado. Não é possível listar dispositivos."));
#endif
    if (Devices.Num() == 0)
    {
        Devices.Add(TEXT("Nenhuma webcam encontrada ou OpenCV não habilitado."));
    }
    UE_LOG(LogIVRFrameSource, Log, TEXT("Webcams Listadas: %s"), *FString::Join(Devices, TEXT(", ")));
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