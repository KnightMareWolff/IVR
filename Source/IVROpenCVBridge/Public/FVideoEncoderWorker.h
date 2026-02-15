// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h" // Para o mecanismo de "locked rendering"
#include "HAL/PlatformProcess.h"
#include "Containers/Queue.h"
#include "IVRTypes.h" // Para FIVR_VideoFrame
#include "IVRFramePool.h" // Para UIVRFramePool
#include "IVR_PipeWrapper.h" // Para FIVR_PipeWrapper


DECLARE_LOG_CATEGORY_EXTERN(LogIVRVideoEncoderWorker, Log, All);

struct FIVR_VideoFrame;

// Forward declaration para UIVRVideoEncoder
class UIVRVideoEncoder;

// FVideoEncoderWorker
// Implementa FRunnable para processar a fila de frames e escrevê-los no Named Pipe em um thread separado.
class IVROPENCVBRIDGE_API FVideoEncoderWorker : public FRunnable
{
public:
    FVideoEncoderWorker(UIVRVideoEncoder* InEncoder, TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& InFrameQueue, FIVR_PipeWrapper& InVideoInputPipe, FThreadSafeBool& InStopFlag, FThreadSafeBool& InNoMoreFramesFlag, FEvent* InNewFrameEvent, UIVRFramePool* InFramePool);
    virtual ~FVideoEncoderWorker();

    // Implementação da interface FRunnable
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;
private:
    UIVRVideoEncoder* Encoder; // Ponteiro raw para o UObject pai (para acesso a logs e configurações)
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& FrameQueue; // Referência à fila de frames
    FIVR_PipeWrapper& VideoInputPipe; // Referência ao wrapper do pipe de vídeo
    FThreadSafeBool& bShouldStop; // Referência para a flag de parada da thread
    FThreadSafeBool& bNoMoreFramesToEncode; // Referência para a flag de "sem mais frames"
    FEvent* NewFrameEvent; // Referência ao evento de sinalização
    UIVRFramePool* FramePool; // Referência ao pool de frames para liberar buffers 
};