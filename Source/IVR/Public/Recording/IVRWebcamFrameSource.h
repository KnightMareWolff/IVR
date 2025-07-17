// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "Recording/IVRFrameSource.h"
#include "Core/IVRFramePool.h"

// Includes para Threading
#include "HAL/Runnable.h"       // Para FRunnable
#include "HAL/RunnableThread.h" // Para FRunnableThread
#include "HAL/ThreadSafeBool.h" // Para FThreadSafeBool
#include "HAL/ThreadSafeCounter.h" // Para FThreadSafeBool
#include "Containers/Queue.h"   // Para TQueue
#include "TimerManager.h" // For FTimerHandle and FTimerManager
#include "IVRWebcamFrameSource.generated.h"

// Forward declare do worker thread
class FWebcamCaptureWorker;

/**
 * @brief Fonte de frames que l� de uma webcam usando OpenCV em um thread separado.
 */
UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="IVR Webcam Frame Source"))
class IVR_API UIVRWebcamFrameSource : public UIVRFrameSource
{
    GENERATED_BODY()

public:
    UIVRWebcamFrameSource();
    virtual void BeginDestroy() override;

    /**
     * @brief Inicializa a fonte de frames da webcam.
     * @param World O UWorld atual.
     * @param Settings As configura��es de v�deo, incluindo o �ndice da webcam, resolu��o e FPS.
     * @param InFramePool O pool de frames para adquirir e liberar buffers.
     */
    virtual void Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool) override;

    /**
     * @brief Desliga a fonte de frames e libera seus recursos.
     */
    virtual void Shutdown() override;

    /**
     * @brief Inicia a captura de frames da webcam.
     */
    virtual void StartCapture() override;

    /**
     * @brief Para a captura de frames da webcam.
     */
    virtual void StopCapture() override;

    /**
     * @brief Lista os dispositivos de webcam dispon�veis no sistema.
     * Tenta abrir os primeiros 10 dispositivos para verificar sua exist�ncia e caracter�sticas.
     * @return Um TArray de FString com descri��es b�sicas dos dispositivos encontrados.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Webcam")
    static TArray<FString> ListWebcamDevices();

    /**
    * @brief Retorna a largura real do frame capturado pela webcam.
    * V�lido ap�s Initialize.
    */
    UFUNCTION(BlueprintPure, Category = "IVR|Webcam")
    int32 GetActualFrameWidth() const;

    /**
    * @brief Retorna a altura real do frame capturado pela webcam.
    * V�lido ap�s Initialize.
    */
    UFUNCTION(BlueprintPure, Category = "IVR|Webcam")
    int32 GetActualFrameHeight() const;


protected:
    /** Timer handle para polling de frames do worker thread no Game Thread. */
    FTimerHandle FramePollTimerHandle;

    /** Inst�ncia do worker runnable que lida com a captura da webcam no thread. */
    FWebcamCaptureWorker* WorkerRunnable;

    /** Thread em que o WorkerRunnable ser� executado. */
    FRunnableThread* WorkerThread;

    /** Fila thread-safe para passar frames do worker thread (produtor) para o Game Thread (consumidor). */
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc> CapturedFrameQueue;

    /** Flag at�mica para sinalizar � thread worker para parar. */
    FThreadSafeBool bShouldStopWorker;

    /** Evento para sinalizar novos frames dispon�veis do worker thread para o Game Thread. */
    FEvent* NewFrameEvent;

    /**
     * @brief Fun��o de callback do timer para pegar frames da fila e broadcast�-los.
     * Executada no Game Thread.
     */
    void PollForNewFrames();
};

