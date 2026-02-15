// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "FVideoFileCaptureWorker.h"
#include "IVROpenCVBridge.h" // Inclua o log principal do IVR se quiser usar LogIVR
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
// DEFINE_LOG_CATEGORY(LogIVROpenCVBridge); // Exemplo de LogCategory para este módulo

// ==============================================================================
// FVideoFileCaptureWorker Implementation (FRunnable para Thread de Captura de Arquivo)
// ESTA CLASSE FRunnable É DEFINIDA AQUI (AGORA COM ACESSO DIRETO AOS TIPOS DO OpenCV)
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
    // VideoCapture = nullptr; // Este membro é cv::VideoCapture ou FDummyVideoCapture, não um ponteiro
    UE_LOG(LogIVR, Error, TEXT("FVideoFileCaptureWorker: OpenCV não habilitado. A captura de vídeo não funcionará."));
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
        UE_LOG(LogIVROpenCVBridge, Error, TEXT("VideoFileCaptureWorker: Falha ao abrir arquivo de vídeo: %s"), *VideoFilePath);
        return false;
    }

    ActualVideoFileFPS.store((float)VideoCapture.get(cv::CAP_PROP_FPS)); // Usando store() para std::atomic
    VideoCapture.set(cv::CAP_PROP_FPS, DesiredFPS);
    // Atualiza as dimensões reais após a abertura
    ActualFrameWidth.Set((int32)VideoCapture.get(cv::CAP_PROP_FRAME_WIDTH));
    ActualFrameHeight.Set((int32)VideoCapture.get(cv::CAP_PROP_FRAME_HEIGHT));
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("VideoFileCaptureWorker: Abriu vídeo %s. Resolução: %dx%d, FPS Real: %.1f"),
           *VideoFilePath, ActualFrameWidth.GetValue(), ActualFrameHeight.GetValue(), ActualVideoFileFPS.load()); // Usando load() para std::atomic

    bShouldStop.AtomicSet(false);
    return true;
#else
    UE_LOG(LogIVR, Error, TEXT("FVideoFileCaptureWorker::Init: OpenCV não habilitado. Não é possível inicializar."));
    return false;
#endif
}
uint32 FVideoFileCaptureWorker::Run()
{
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("VideoFileCaptureWorker: Executando."));
#if WITH_OPENCV
    cv::Mat Frame; // Matriz OpenCV para armazenar o frame
    
    while (!bShouldStop)
    {
        if (!VideoCapture.isOpened())
        {
            UE_LOG(LogIVROpenCVBridge, Warning, TEXT("VideoFileCaptureWorker: Captura de vídeo não aberta, parando thread."));
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
                UE_LOG(LogIVROpenCVBridge, Log, TEXT("VideoFileCaptureWorker: Fim do vídeo alcançado, voltando ao início."));
                VideoCapture.set(cv::CAP_PROP_POS_FRAMES, 0); // Volta para o início do vídeo
                VideoCapture.read(Frame); // Tenta ler o primeiro frame
                if (Frame.empty()) // Se ainda estiver vazio, algo está errado
                {
                    UE_LOG(LogIVROpenCVBridge, Error, TEXT("VideoFileCaptureWorker: Falha ao fazer loop no vídeo ou ler o primeiro frame após o loop. Parando."));
                    bShouldStop.AtomicSet(true);
                    break;
                }
            }
            else
            {
                UE_LOG(LogIVROpenCVBridge, Log, TEXT("VideoFileCaptureWorker: Fim do vídeo alcançado, parando."));
                bShouldStop.AtomicSet(true);
                break;
            }
        }
        // Adquire um buffer do pool (otimiza reuso de memória)
        TSharedPtr<TArray<uint8>> FrameBuffer = FramePool->AcquireFrame();
        if (!FrameBuffer.IsValid())
        {
            UE_LOG(LogIVROpenCVBridge, Error, TEXT("VideoFileCaptureWorker: Falha ao adquirir buffer de frame do pool. Descartando frame."));
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
    UE_LOG(LogIVR, Error, TEXT("FVideoFileCaptureWorker::Run: OpenCV não habilitado. Não é possível capturar frames."));
#endif // WITH_OPENCV
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("VideoFileCaptureWorker: Saindo do loop de execução."));
    return 0;
}
void FVideoFileCaptureWorker::Stop()
{
    bShouldStop.AtomicSet(true);
    if (NewFrameEvent) NewFrameEvent->Trigger();
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("VideoFileCaptureWorker: Sinal de parada recebido."));
}

void FVideoFileCaptureWorker::Exit()
{
#if WITH_OPENCV
    if (VideoCapture.isOpened())
    {
        VideoCapture.release();
    }
#endif
    UE_LOG(LogIVROpenCVBridge, Log, TEXT("VideoFileCaptureWorker: Encerrado."));
}