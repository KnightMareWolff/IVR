// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "FVideoEncoderWorker.h" // Inclua o cabeçalho do worker
#include "IVROpenCVBridge.h" // Inclua o log principal do IVR se quiser usar LogIVR
#include "IVR_PipeWrapper.h" // Necessário para usar FIVR_PipeWrapper

// Definição do LogCategory para o VideoEncoderWorker (pode usar o do IVRVideoEncoder)
DEFINE_LOG_CATEGORY(LogIVRVideoEncoderWorker); // Se quiser um LogCategory separado

// =====================================================================================
// FVideoEncoderWorker Implementation
// =====================================================================================
FVideoEncoderWorker::FVideoEncoderWorker(UIVRVideoEncoder* InEncoder, TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& InFrameQueue, FIVR_PipeWrapper& InVideoInputPipe, FThreadSafeBool& InStopFlag, FThreadSafeBool& InNoMoreFramesFlag, FEvent* InNewFrameEvent, UIVRFramePool* InFramePool)
    : Encoder(InEncoder)
    , FrameQueue(InFrameQueue)
    , VideoInputPipe(InVideoInputPipe)
    , bShouldStop(InStopFlag) 
    , bNoMoreFramesToEncode(InNoMoreFramesFlag)
    , NewFrameEvent(InNewFrameEvent)
    , FramePool(InFramePool) 
{
}
FVideoEncoderWorker::~FVideoEncoderWorker()
{
    // Limpeza aqui é mínima, a maioria dos recursos é gerenciada pelo UIVRVideoEncoder
}
bool FVideoEncoderWorker::Init()
{
    // Usando o LogCategory do módulo IVR para consistência, se preferir criar um específico, defina-o no .h e aqui.
    UE_LOG(LogIVRVideoEncoderWorker, Log, TEXT("Video Encoder Worker thread initialized."));
    return true;
}

uint32 FVideoEncoderWorker::Run()
{
    UE_LOG(LogIVRVideoEncoderWorker, Log, TEXT("Video Encoder Worker thread started."));
    // Este método bloqueia até que o FFmpeg se conecte ao pipe.
    // Executá-lo aqui (na thread worker) evita o congelamento da Game Thread.
    UE_LOG(LogIVRVideoEncoderWorker, Log, TEXT("FVideoEncoderWorker: Awaiting FFmpeg connection to video input pipe..."));
    if (!VideoInputPipe.Connect())
    {
        UE_LOG(LogIVRVideoEncoderWorker, Error, TEXT("FVideoEncoderWorker: Failed to connect Video Named Pipe to FFmpeg. Signaling worker to stop."));
        bShouldStop.AtomicSet(true); // Sinaliza para parar a worker thread
        return 1; // Retorna com erro
    }
    UE_LOG(LogIVRVideoEncoderWorker, Log, TEXT("FVideoEncoderWorker: Video input pipe successfully connected."));
    
    FIVR_VideoFrame CurrentFrame; // Vai receber o frame do tipo FIVR_VideoFrame (com TSharedPtr)
    while (!bShouldStop) 
    {
        while (FrameQueue.Dequeue(CurrentFrame)) // Dequeue de FIVR_VideoFrame
        {
            if (bShouldStop) 
            {
                // Se o thread foi sinalizado para parar, libera o frame atual antes de sair.
                if (FramePool && CurrentFrame.RawDataPtr.IsValid())
                {
                    FramePool->ReleaseFrame(CurrentFrame.RawDataPtr);
                }
                break; 
            }
            
            // Acessa os dados do buffer via RawDataPtr
            if (!CurrentFrame.RawDataPtr.IsValid() || CurrentFrame.RawDataPtr->Num() == 0)
            {
                UE_LOG(LogIVRVideoEncoderWorker, Error, TEXT("Video Encoder Worker: Received invalid or empty frame buffer. Dropping frame."));
                if (FramePool && CurrentFrame.RawDataPtr.IsValid())
                {
                    FramePool->ReleaseFrame(CurrentFrame.RawDataPtr);
                }
                continue; 
            }
            // UE_LOG(LogIVRVideoEncoder, Warning, TEXT("Video Encoder Worker: Attempting to write %d bytes to pipe (Frame %dx%d)."), 
            //    CurrentFrame.RawDataPtr->Num(), CurrentFrame.Width, CurrentFrame.Height); // Descomente para debug intenso.
            // Escreve o frame no pipe
            if (VideoInputPipe.Write(CurrentFrame.RawDataPtr->GetData(), CurrentFrame.RawDataPtr->Num()))
            {
                // UE_LOG(LogIVRVideoEncoder, Warning, TEXT("Video Encoder Worker: Successfully wrote %d bytes to video pipe."),CurrentFrame.RawDataPtr->Num()); // Descomente para debug intenso.
            }
            else
            {
                UE_LOG(LogIVRVideoEncoderWorker, Error, TEXT("Failed to write video frame to pipe. Pipe may be closed or in error state. Signalling worker stop."));
                bShouldStop.AtomicSet(true); 
            }
            // SEMPRE RETORNA O FRAME PARA O POOL APÓS USÁ-LO
            if (FramePool && CurrentFrame.RawDataPtr.IsValid())
            {
                FramePool->ReleaseFrame(CurrentFrame.RawDataPtr);
            }
        }
        // Se a fila estiver vazia e não houver mais frames ou não for para parar, espera por um novo evento.
        if (FrameQueue.IsEmpty() && !bShouldStop)
        {
            NewFrameEvent->Wait(100); // Espera por até 100ms por um novo frame ou sinal de parada
        }
    }
    UE_LOG(LogIVRVideoEncoderWorker, Log, TEXT("Video Encoder Worker thread stopped."));
    return 0;
}

void FVideoEncoderWorker::Stop()
{
    bShouldStop.AtomicSet(true); 
    if (NewFrameEvent) NewFrameEvent->Trigger();
}

void FVideoEncoderWorker::Exit()
{
    UE_LOG(LogIVRVideoEncoderWorker, Log, TEXT("Video Encoder Worker thread exiting."));
}