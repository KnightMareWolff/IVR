// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "IVROpenCVBridge/Public/FWebcamCaptureWorker.h"
#include "IVROpenCVBridge.h" // Inclua o log principal do IVR se quiser usar LogIVROpenCVBridge
// #include "IVROpenCVBridge/Public/IVROpenCVBridge.h" // Para LogCategory do módulo (se definido)

// [MANUAL_REF_POINT] Includes do OpenCV - REMOVA OS #if/#endif AQUI
#if WITH_OPENCV 
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h" // Abre namespace/desativa avisos

#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

#include "PostOpenCVHeaders.h" // Fecha namespace/reativa avisos
#endif

// Definição do LogCategory (se precisar, descomente e ajuste)
// DEFINE_LOG_CATEGORY(LogIVROpenCVBridgeOpenCVBridge); // Exemplo de LogCategory para este módulo

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
    UE_LOG(LogIVROpenCVBridge, Warning, TEXT("FWebcamCaptureWorker: OpenCV não habilitado. A captura de webcam não funcionará."));
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
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("WebcamCaptureWorker: Inicializado com sucesso. Abertura da webcam adiada para Run()."));
    return true;
}

uint32 FWebcamCaptureWorker::Run()
{
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("WebcamCaptureWorker: Executando."));
#if WITH_OPENCV
    cv::Mat Frame; // Matriz OpenCV para armazenar o frame
    // Tenta abrir a webcam no início do Run(), no worker thread.
    WebcamCapture.open(DeviceIndex, ApiPreference);
    if (!WebcamCapture.isOpened())
    {
        UE_LOG(LogIVROpenCVBridge, Error, TEXT("WebcamCaptureWorker: Falha ao abrir dispositivo de webcam %d com API %d. Parando thread de captura."), DeviceIndex, (int32)ApiPreference);
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

    UE_LOG(LogIVROpenCVBridge, Log, TEXT("WebcamCaptureWorker: Abriu dispositivo %d com API %d. Resolução: %dx%d, FPS Real: %.1f"),
           DeviceIndex, (int32)ApiPreference, ActualFrameWidth.GetValue(), ActualFrameHeight.GetValue(), (float)WebcamCapture.get(cv::CAP_PROP_FPS));
    while (!bShouldStop)
    {
        if (!WebcamCapture.isOpened())
        {
            UE_LOG(LogIVROpenCVBridge, Warning, TEXT("WebcamCaptureWorker: Webcam foi fechada inesperadamente. Parando thread de captura."));
            bShouldStop.AtomicSet(true);
            break;
        }

        // Lê um frame da webcam (geralmente é uma chamada bloqueante até um frame estar disponível)
        WebcamCapture.read(Frame);
        if (Frame.empty())
        {
            // Se o frame estiver vazio, pode ser que a webcam esteja sendo desconectada ou em um estado ruim
            UE_LOG(LogIVROpenCVBridge, Warning, TEXT("WebcamCaptureWorker: Frame vazio recebido. Tentando novamente..."));
            FPlatformProcess::Sleep(0.1f); // Pequena pausa antes de tentar novamente
            continue; // Tenta novamente na próxima iteração
        }
        // Adquire um buffer do pool (otimiza reuso de memória)
        TSharedPtr<TArray<uint8>> FrameBuffer = FramePool->AcquireFrame();
        if (!FrameBuffer.IsValid())
        {
            UE_LOG(LogIVROpenCVBridge, Error, TEXT("WebcamCaptureWorker: Falha ao adquirir buffer de frame do pool. Descartando frame."));
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
    UE_LOG(LogIVROpenCVBridge, Warning, TEXT("WebcamCaptureWorker::Run: OpenCV não habilitado. Não é possível capturar frames."));
#endif // WITH_OPENCV
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("WebcamCaptureWorker: Saindo do loop de execução."));
    return 0; // Retorna sucesso
}
void FWebcamCaptureWorker::Stop()
{
    bShouldStop.AtomicSet(true);
    if (NewFrameEvent) NewFrameEvent->Trigger(); // Acorda o Run para que ele possa sair do Wait
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("WebcamCaptureWorker: Sinal de parada recebido."));
}
void FWebcamCaptureWorker::Exit()
{
    if (WebcamCapture.isOpened()) // Chamada sempre válida devido ao tipo dummy
    {
        WebcamCapture.release(); // Chamada sempre válida devido ao tipo dummy
    }
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("WebcamCaptureWorker: Encerrado."));
}