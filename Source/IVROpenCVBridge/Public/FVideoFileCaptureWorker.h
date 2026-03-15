// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h" // Para FThreadSafeBool
#include "HAL/ThreadSafeCounter.h" // Para FThreadSafeCounter (resolução)
#include "Containers/Queue.h"   // Para TQueue
#include "IVRTypes.h" // Para FIVR_VideoFrame
#include "IVRFramePool.h" // Para UIVRFramePool
#include <atomic> // Necessário para std::atomic
#include <string> // Inclusão explícita para std::string


#include "IVROpenCVBridge.h"


// --- Ocultar tipos OpenCV dos headers públicos ---
// Não inclui headers de OpenCV aqui.
namespace cv { class VideoCapture; } // Forward declaration para cv::VideoCapture
// --- Crie um alias para o tipo de API, se precisar expor no header ---
using VideoCaptureAPIs = int; // Fallback para int se OpenCV não estiver habilitado no Unreal

/**
 * @brief Worker thread para ler frames de um arquivo de vídeo usando OpenCV.
 * Esta classe opera em um thread separado para evitar bloqueios do Game Thread.
 */
class IVROPENCVBRIDGE_API FVideoFileCaptureWorker : public FRunnable
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
    bool bLoopPlayback; // <-- ESTE MEMBRO AGORA ESTÁ DECLARADO ANTES
    
#if WITH_OPENCV // <--- Adicione este ifdef
    cv::VideoCapture* OpenCVVideoCapture; // Objeto de captura de vídeo do OpenCV, agora ponteiro
#else
    // Mock ou nullptr para plataformas sem OpenCV
    void* OpenCVVideoCapture = nullptr; // Apenas para que o código compile, será sempre nullptr
#endif // WITH_OPENCV
};
