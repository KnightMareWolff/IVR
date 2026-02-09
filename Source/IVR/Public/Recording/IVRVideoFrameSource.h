// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "Recording/IVRFrameSource.h"

// Includes para Threading
#include "HAL/Runnable.h"       // Para FRunnable
#include "HAL/RunnableThread.h" // Para FRunnableThread
#include "HAL/ThreadSafeBool.h" // Para FThreadSafeBool
#include "HAL/ThreadSafeCounter.h" // Para FThreadSafeCounter (resolução)
#include "Containers/Queue.h"   // Para TQueue
#include "TimerManager.h" // For FTimerHandle and FTimerManager
#include <atomic> // Necessário para std::atomic (mesmo sem cv::)

// Forward declare do worker thread
class FVideoFileCaptureWorker;

// **CORREÇÃO CRÍTICA**: O include do arquivo .generated.h DEVE SER O ÚLTIMO include
// em qualquer arquivo de cabeçalho UObject.
#include "IVRVideoFrameSource.generated.h"

/**
 * @brief Fonte de frames que lê de um arquivo de vídeo usando OpenCV em um thread separado.
 */
UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="IVR Video File Frame Source"))
class IVR_API UIVRVideoFrameSource : public UIVRFrameSource
{
    GENERATED_BODY() // <-- ESTA MACRO REQUER O .generated.h ACIMA

public:
    UIVRVideoFrameSource();
    virtual void BeginDestroy() override;

    /**
     * @brief Inicializa a fonte de frames de arquivo de vídeo.
     * @param World O UWorld atual.
     * @param Settings As configurações de vídeo, incluindo o caminho do arquivo e opções de reprodução.
     * @param InFramePool O pool de frames para adquirir e liberar buffers.
     */
    virtual void Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool) override;

    /**
     * @brief Desliga a fonte de frames e libera seus recursos.
     */
    virtual void Shutdown() override;

    /**
     * @brief Inicia a captura de frames do arquivo de vídeo.
     */
    virtual void StartCapture() override;

    /**
     * @brief Para a captura de frames do arquivo de vídeo.
     */
    virtual void StopCapture() override;

    /**
     * @brief Retorna a largura real do frame lido do arquivo de vídeo.
     * Válido após Initialize.
     */
    UFUNCTION(BlueprintPure, Category = "IVR|VideoFile")
    int32 GetActualFrameWidth() const;

    /**
     * @brief Retorna a altura real do frame lido do arquivo de vídeo.
     * Válido após Initialize.
     */
    UFUNCTION(BlueprintPure, Category = "IVR|VideoFile")
    int32 GetActualFrameHeight() const;

    /**
     * @brief Retorna o FPS nativo do arquivo de vídeo.
     * Válido após Initialize.
     */
    UFUNCTION(BlueprintPure, Category = "IVR|VideoFile")
    float GetActualVideoFileFPS() const;
    
    /**
     * @brief Retorna o FPS efetivo de reprodução do arquivo de vídeo (FPS nativo * Playback Speed).
     * Válido após Initialize.
     */
    UFUNCTION(BlueprintPure, Category = "IVR|VideoFile")
    float GetEffectivePlaybackFPS() const;

protected:
    /** Timer handle para polling de frames do worker thread no Game Thread. */
    FTimerHandle FramePollTimerHandle;

    /** Instância do worker runnable que lida com a leitura do arquivo de vídeo no thread. */
    FVideoFileCaptureWorker* WorkerRunnable;

    /** Thread em que o WorkerRunnable será executado. */
    FRunnableThread* WorkerThread;

    /** Fila thread-safe para passar frames do worker thread (produtor) para o Game Thread (consumidor). */
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc> CapturedFrameQueue;

    /** Flag atômica para sinalizar à thread worker para parar. */
    FThreadSafeBool bShouldStopWorker;

    /** Evento para sinalizar novos frames disponíveis do worker thread para o Game Thread. */
    FEvent* NewFrameEvent;

    /**
     * @brief Função de callback do timer para pegar frames da fila e broadcastá-los.
     * Executada no Game Thread.
     */
    void PollForNewFrames();
};