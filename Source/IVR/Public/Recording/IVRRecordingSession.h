// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Core/IVRTypes.h"
#include "Containers/Queue.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "../IVRGlobalStatics.h"
#include "../IVR_PipeWrapper.h" 
#include "IVRVideoEncoder.h" // Inclui o novo encoder centralizado
#include "Core/IVRFramePool.h" // Adicionar este include!

#include "IVRRecordingSession.generated.h"

// Definir um LogCategory para mensagens espec�ficas da grava��o IVR
DECLARE_LOG_CATEGORY_EXTERN(LogIVRRecSession, Log, All);

/**
 * Classe respons�vel por gerenciar uma �nica sess�o de grava��o de um take de v�deo.
 * Ela orquestra a captura de frames e delega a codifica��o ao UIVRVideoEncoder.
 */
UCLASS(BlueprintType)
class IVR_API UIVRRecordingSession : public UObject, public FRunnable
{
    GENERATED_BODY()

public:

    UIVRRecordingSession();
    virtual ~UIVRRecordingSession();

     /**
    * Inicializa a sess�o de grava��o.
    * @param InVideoSettings Configura��es de v�deo.
    * @param InFFmpegExecutablePath Caminho completo para o execut�vel ffmpeg.exe.
    * @param InActualFrameWidth Largura real dos frames.
    * @param InActualFrameHeight Altura real dos frames.
    * @param InFramePool Refer�ncia ao pool de frames para gerenciamento de buffers. 
    */
    void Initialize(const FIVR_VideoSettings& InVideoSettings, const FString& InFFmpegExecutablePath, int32 InActualFrameWidth, int32 InActualFrameHeight, UIVRFramePool* InFramePool); 

    /**
     * @brief Inicia a grava��o do take de v�deo.
     * @return true se a grava��o foi iniciada com sucesso, false caso contr�rio.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Recording")
    bool StartRecording();

    /**
     * @brief Para a grava��o do take de v�deo e finaliza o arquivo.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Recording")
    void StopRecording();

    /**
     * @brief Pausa a grava��o do take de v�deo.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Recording")
    void PauseRecording();

    /**
     * @brief Retoma a grava��o do take de v�deo.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Recording")
    void ResumeRecording();

    UFUNCTION(BlueprintPure, Category = "IVR")
    bool IsRecording() const { return bIsRecording; }

    UFUNCTION(BlueprintPure, Category = "IVR")
    bool IsPaused() const { return bIsPaused; }

    UFUNCTION(BlueprintPure, Category = "IVR")
    float GetDuration() const;

    UFUNCTION(BlueprintCallable, Category = "IVR")
    void ClearQueues();

    UFUNCTION(BlueprintPure, Category = "IVR")
    FDateTime GetStartTime() const { return StartTime; }

    /**
     * @brief Retorna o caminho do arquivo do take gravado por esta sess�o.
     * Ser� v�lido ap�s o StopRecording bem-sucedido.
     */
    UFUNCTION(BlueprintPure, Category = "IVR")
    FString GetOutputPath() const { return CurrentTakeFilePath; } 

    /**
     * @brief Retorna o o Session ID da grava��o.
     * Ser� v�lido ap�s o StopRecording bem-sucedido.
     */
    UFUNCTION(BlueprintPure, Category = "IVR")
    FString GetSessionID() const { return SessionID; } 

    /**
     * @brief Adiciona um frame de v�deo � fila para ser processado pelo encoder.
     * @param Frame O frame FIVR_VideoFrame (com TSharedPtr) a ser adicionado.
     */
    void AddVideoFrame(FIVR_VideoFrame Frame); // Assinatura mudada para receber por valor
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Recording Settings")
    FIVR_VideoSettings UserRecordingSettings;

    int32 GetConsumerQCounter() { return VideoConsumerQCounter; }
    int32 GetProducerQCounter() { return VideoProducerQCounter; }

private:
    // Refer�ncia ao codificador de v�deo, que agora gerencia o FFmpeg.
    UPROPERTY()
    UIVRVideoEncoder* VideoEncoder;

    FThreadSafeBool bIsRecording = false; 
    FThreadSafeBool bIsPaused = false;    
    
    FDateTime StartTime;
    float RecordingDuration = 0.0f;
    
    FString SessionID; // ID �nico para este take.

    // Caminho do arquivo do take individual que est� sendo gravado por esta sess�o.
    FString CurrentTakeFilePath;      
    
    // Fila para frames do thread principal (produtor) para o worker thread (consumidor).
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc> VideoFrameProducerQueue; 
    
    // Contadores para monitoramento da fila de frames de v�deo.
    int32 VideoProducerQCounter = 0;
    int32 VideoConsumerQCounter = 0;

    /**
    * @brief Gera o caminho completo para o arquivo de take desta sess�o.
    * Ser� chamado uma vez durante a inicializa��o/start.
    */
    FString GenerateTakeFilePath(); 

    /**
    * Gera o caminho completo para o arquivo de v�deo Master.
    * Utiliza o Timestamp e o SessionID desta sess�o.
    */
    FString GenerateMasterFilePath() const;

    // --- Membros para o FRunnable (thread de grava��o de frames) ---
    FRunnableThread* RecordingThread;       // Thread de grava��o dedicado
    FThreadSafeBool bStopThread = false;    // Flag para sinalizar o encerramento do thread
    FEvent* HasNewFrameEvent;               // Evento para sinalizar novos frames para o thread de grava��o

    // --- Implementa��o da Interface FRunnable ---
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;

    UPROPERTY()
    UIVRFramePool* FramePool; // Refer�ncia ao pool de frames para liberar buffers 
};

