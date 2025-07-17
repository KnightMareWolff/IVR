// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "Recording/IVRFrameSource.h"

// Includes para Threading
#include "HAL/Runnable.h"       // Para FRunnable
#include "HAL/RunnableThread.h" // Para FRunnableThread
#include "HAL/ThreadSafeBool.h" // Para FThreadSafeBool
#include "HAL/ThreadSafeCounter.h" // Para FThreadSafeCounter (resolu��o) e FThreadSafeFloat (FPS)
#include "Containers/Queue.h"   // Para TQueue
#include "TimerManager.h" // For FTimerHandle and FTimerManager

#include "IVRVideoFrameSource.generated.h"

// Forward declare do worker thread
class FVideoFileCaptureWorker;

/**
 * @brief Fonte de frames que l de um arquivo de vdeo usando OpenCV em um thread separado.
 */
UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="IVR Video File Frame Source"))
class IVR_API UIVRVideoFrameSource : public UIVRFrameSource
{
    GENERATED_BODY()

public:
    UIVRVideoFrameSource();
    virtual void BeginDestroy() override;

    /**
     * @brief Inicializa a fonte de frames de arquivo de vdeo.
     * @param World O UWorld atual.
     * @param Settings As configuraes de vdeo, incluindo o caminho do arquivo e opes de reproduo.
     * @param InFramePool O pool de frames para adquirir e liberar buffers.
     */
    virtual void Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool) override;

    /**
     * @brief Desliga a fonte de frames e libera seus recursos.
     */
    virtual void Shutdown() override;

    /**
     * @brief Inicia a captura de frames do arquivo de vdeo.
     */
    virtual void StartCapture() override;

    /**
     * @brief Para a captura de frames do arquivo de vdeo.
     */
    virtual void StopCapture() override;

    /**
     * @brief Retorna a largura real do frame lido do arquivo de vdeo.
     * Vlido aps Initialize.
     */
    UFUNCTION(BlueprintPure, Category = "IVR|VideoFile")
    int32 GetActualFrameWidth() const;

    /**
     * @brief Retorna a altura real do frame lido do arquivo de vdeo.
     * Vlido aps Initialize.
     */
    UFUNCTION(BlueprintPure, Category = "IVR|VideoFile")
    int32 GetActualFrameHeight() const;

    /**
     * @brief Retorna o FPS nativo do arquivo de v�deo.
     * V�lido ap�s Initialize.
     */
    UFUNCTION(BlueprintPure, Category = "IVR|VideoFile")
    float GetActualVideoFileFPS() const;
    
    /**
     * @brief Retorna o FPS efetivo de reprodu��o do arquivo de v�deo (FPS nativo * Playback Speed).
     * V�lido ap�s Initialize.
     */
    UFUNCTION(BlueprintPure, Category = "IVR|VideoFile")
    float GetEffectivePlaybackFPS() const;

protected:
    /** Timer handle para polling de frames do worker thread no Game Thread. */
    FTimerHandle FramePollTimerHandle;

    /** Instncia do worker runnable que lida com a leitura do arquivo de vdeo no thread. */
    FVideoFileCaptureWorker* WorkerRunnable;

    /** Thread em que o WorkerRunnable ser executado. */
    FRunnableThread* WorkerThread;

    /** Fila thread-safe para passar frames do worker thread (produtor) para o Game Thread (consumidor). */
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc> CapturedFrameQueue;

    /** Flag atmica para sinalizar  thread worker para parar. */
    FThreadSafeBool bShouldStopWorker;

    /** Evento para sinalizar novos frames disponveis do worker thread para o Game Thread. */
    FEvent* NewFrameEvent;

    /**
     * @brief Funo de callback do timer para pegar frames da fila e broadcast-los.
     * Executada no Game Thread.
     */
    void PollForNewFrames();
};


