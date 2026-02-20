// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "IVROpenCVBridge/Public/FWebcamCaptureWorker.h"
#include "IVROpenCVBridge.h" // Inclua o log principal do IVR se quiser usar LogIVROpenCVBridge

// --- INÍCIO DA ALTERAÇÃO: Includes de OpenCV APENAS no arquivo .cpp ---
#if WITH_OPENCV 
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h" // Abre namespace/desativa avisos

#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

#include "PostOpenCVHeaders.h" // Fecha namespace/reativa avisos
#endif
// --- FIM DA ALTERAÇÃO ---

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
    // --- INÍCIO DA ALTERAÇÃO: Inicialização de OpenCVWebcamCapture e ApiPreference ---
#if WITH_OPENCV
    , OpenCVWebcamCapture(new cv::VideoCapture(InDeviceIndex, static_cast<cv::VideoCaptureAPIs>(InApiPreference))) // Aloca e inicializa com parâmetros
#else
    , OpenCVWebcamCapture(nullptr) // Fallback se OpenCV não estiver habilitado
#endif
    , ApiPreference(InApiPreference)
    // --- FIM DA ALTERAÇÃO ---
{
    // --- INÍCIO DA ALTERAÇÃO: Remove o bloco de erro/fallback de OpenCV não habilitado ---
    // A lógica de fallback e erro para WITH_OPENCV é agora tratada nos métodos
    // Init(), Run(), etc. através do ponteiro.
    // --- FIM DA ALTERAÇÃO ---
}

FWebcamCaptureWorker::~FWebcamCaptureWorker()
{
    // --- INÍCIO DA ALTERAÇÃO: Liberação do OpenCVWebcamCapture ---
#if WITH_OPENCV
    if (OpenCVWebcamCapture)
    {
        if (OpenCVWebcamCapture->isOpened())
        {
            OpenCVWebcamCapture->release();
        }
        delete OpenCVWebcamCapture;
        OpenCVWebcamCapture = nullptr;
    }
#endif
    // --- FIM DA ALTERAÇÃO ---
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
    if (!OpenCVWebcamCapture)
    {
        UE_LOG(LogIVROpenCVBridge, Error, TEXT("WebcamCaptureWorker: OpenCVWebcamCapture é nulo no Run(). Parando thread."));
        bShouldStop.AtomicSet(true);
        return 1;
    }
    cv::Mat Frame; // Matriz OpenCV para armazenar o frame
    // Tenta abrir a webcam no início do Run(), no worker thread.
    // --- INÍCIO DA ALTERAÇÃO: Uso do ponteiro para abrir a webcam ---
    // OpenCVWebcamCapture->open(DeviceIndex, static_cast<cv::VideoCaptureAPIs>(ApiPreference)); // Já foi feito no construtor
    // --- FIM DA ALTERAÇÃO ---
    if (!OpenCVWebcamCapture->isOpened()) // Usa o ponteiro
    {
        UE_LOG(LogIVROpenCVBridge, Error, TEXT("WebcamCaptureWorker: Falha ao abrir dispositivo de webcam %d com API %d. Parando thread de captura."), DeviceIndex, ApiPreference);
        bShouldStop.AtomicSet(true); // Sinaliza para parar
        return 1; // Retorna código de erro
    }
    // Tenta definir a resolução, FPS e codec *após* a abertura bem-sucedida
    OpenCVWebcamCapture->set(cv::CAP_PROP_FRAME_WIDTH , DesiredWidth); // Usa o ponteiro
    OpenCVWebcamCapture->set(cv::CAP_PROP_FRAME_HEIGHT, DesiredHeight); // Usa o ponteiro
    OpenCVWebcamCapture->set(cv::CAP_PROP_FPS, DesiredFPS); // Usa o ponteiro
    OpenCVWebcamCapture->set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G')); // Tenta MJPG para melhor FPS/qualidade
    // Verifica as configurações aplicadas (podem não ser exatamente as desejadas pela webcam)
    ActualFrameWidth.Set((int32)OpenCVWebcamCapture->get(cv::CAP_PROP_FRAME_WIDTH)); // Usa o ponteiro
    ActualFrameHeight.Set((int32)OpenCVWebcamCapture->get(cv::CAP_PROP_FRAME_HEIGHT)); // Usa o ponteiro

    UE_LOG(LogIVROpenCVBridge, Log, TEXT("WebcamCaptureWorker: Abriu dispositivo %d com API %d. Resolução: %dx%d, FPS Real: %.1f"),
           DeviceIndex, ApiPreference, ActualFrameWidth.GetValue(), ActualFrameHeight.GetValue(), (float)OpenCVWebcamCapture->get(cv::CAP_PROP_FPS)); // Usa o ponteiro

    while (!bShouldStop)
    {
        if (!OpenCVWebcamCapture->isOpened()) // Usa o ponteiro
        {
            UE_LOG(LogIVROpenCVBridge, Warning, TEXT("WebcamCaptureWorker: Webcam foi fechada inesperadamente. Parando thread de captura."));
            bShouldStop.AtomicSet(true);
            break;
        }

        // Lê um frame da webcam (geralmente é uma chamada bloqueante até um frame estar disponível)
        OpenCVWebcamCapture->read(Frame); // Usa o ponteiro
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
    // --- INÍCIO DA ALTERAÇÃO: Lógica de erro para WITH_OPENCV = false ---
    UE_LOG(LogIVROpenCVBridge, Warning, TEXT("WebcamCaptureWorker::Run: OpenCV não habilitado. Não é possível capturar frames."));
    // --- FIM DA ALTERAÇÃO ---
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
    // --- INÍCIO DA ALTERAÇÃO: Liberação do OpenCVWebcamCapture ---
#if WITH_OPENCV
    if (OpenCVWebcamCapture) // A desalocação (delete) é feita no destrutor
    {
        if (OpenCVWebcamCapture->isOpened())
        {
            OpenCVWebcamCapture->release();
        }
    }
#endif
    // --- FIM DA ALTERAÇÃO ---
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("WebcamCaptureWorker: Encerrado."));
}