// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#pragma once
#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IVRTypes.h"
#include "Containers/Queue.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "../IVRGlobalStatics.h"
// [MANUAL_REF_POINT] FIVR_PipeWrapper é agora de IVROpenCVBridge
#include "IVR_PipeWrapper.h"
#include "IVRVideoEncoder.h" // Inclui o novo encoder centralizado
#include "IVRFramePool.h" // Adicionar este include!

#include "IVRRecordingSession.generated.h"

// Definir um LogCategory para mensagens específicas da gravação IVR
DECLARE_LOG_CATEGORY_EXTERN(LogIVRRecSession, Log, All);

/**
 * Classe responsável por gerenciar uma única sessão de gravação de um take de vídeo.
 * Ela orquestra a captura de frames e delega a codificação ao UIVRVideoEncoder.
 */
UCLASS(BlueprintType)
class IVR_API UIVRRecordingSession : public UObject, public FRunnable
{
    GENERATED_BODY()

public:

    UIVRRecordingSession();
    virtual ~UIVRRecordingSession();

     /**
    * Inicializa a sessão de gravação.
    * @param InVideoSettings Configurações de vídeo.
    * @param InFFmpegExecutablePath Caminho completo para o executável ffmpeg.exe.
    * @param InActualFrameWidth Largura real dos frames.
    * @param InActualFrameHeight Altura real dos frames.
    * @param InFramePool Referência ao pool de frames para gerenciamento de buffers. 
    */
    void Initialize(const FIVR_VideoSettings& InVideoSettings, const FString& InFFmpegExecutablePath, int32 InActualFrameWidth, int32 InActualFrameHeight, UIVRFramePool* InFramePool); 

    /**
     * @brief Inicia a gravação do take de vídeo.
     * @return true se a gravação foi iniciada com sucesso, false caso contrário.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Recording")
    bool StartRecording();
    /**
     * @brief Para a gravação do take de vídeo e finaliza o arquivo.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Recording")
    void StopRecording();

    /**
     * @brief Pausa a gravação do take de vídeo.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Recording")
    void PauseRecording();

    /**
     * @brief Retoma a gravação do take de vídeo.
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
     * @brief Retorna o caminho do arquivo do take gravado por esta sessão.
     * Será válido após o StopRecording bem-sucedido.
     */
    UFUNCTION(BlueprintPure, Category = "IVR")
    FString GetOutputPath() const { return CurrentTakeFilePath; } 

    /**
     * @brief Retorna o o Session ID da gravação.
     * Será válido após o StopRecording bem-sucedido.
     */
    UFUNCTION(BlueprintPure, Category = "IVR")
    FString GetSessionID() const { return SessionID; } 

    /**
     * @brief Adiciona um frame de vídeo à fila para ser processado pelo encoder.
     * @param Frame O frame FIVR_VideoFrame (com TSharedPtr) a ser adicionado.
     */
    void AddVideoFrame(FIVR_VideoFrame Frame); // Assinatura mudada para receber por valor
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Recording Settings")
    FIVR_VideoSettings UserRecordingSettings;
    int32 GetConsumerQCounter() { return VideoConsumerQCounter; }
    int32 GetProducerQCounter() { return VideoProducerQCounter; }

private:
    // Referência ao codificador de vídeo, que agora gerencia o FFmpeg.
    UPROPERTY()
    UIVRVideoEncoder* VideoEncoder;

    FThreadSafeBool bIsRecording = false; 
    FThreadSafeBool bIsPaused = false;    
    
    FDateTime StartTime;
    float RecordingDuration = 0.0f;
    
    FString SessionID; // ID único para este take.

    // Caminho do arquivo do take individual que está sendo gravado por esta sessão.
    FString CurrentTakeFilePath;      
    
    // Fila para frames do thread principal (produtor) para o worker thread (consumidor).
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc> VideoFrameProducerQueue; 
    
    // Contadores para monitoramento da fila de frames de vídeo.
    int32 VideoProducerQCounter = 0;
    int32 VideoConsumerQCounter = 0;
    /**
    * @brief Gera o caminho completo para o arquivo de take desta sessão.
    * Será chamado uma vez durante a inicialização/start.
    */
    FString GenerateTakeFilePath(); 

    /**
    * Gera o caminho completo para o arquivo de vídeo Master.
    * Utiliza o Timestamp e o SessionID desta sessão.
    */
    FString GenerateMasterFilePath() const;

    // --- Membros para o FRunnable (thread de gravação de frames) ---
    FRunnableThread* RecordingThread;       // Thread de gravação dedicado
    FThreadSafeBool bStopThread = false;    // Flag para sinalizar o encerramento do thread
    FEvent* HasNewFrameEvent;               // Evento para sinalizar novos frames para o thread de gravação

    // --- Implementação da Interface FRunnable ---
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;
    UPROPERTY()
    UIVRFramePool* FramePool; // Referência ao pool de frames para liberar buffers 
};