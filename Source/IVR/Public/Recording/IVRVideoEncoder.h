// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/IVRTypes.h"
#include "IVR_PipeWrapper.h"     // Para FIVR_PipeWrapper (Assumindo que está em um local acessível)
#include "IVRECFactory.h" // Para UIVRECFactory (Assumindo que está em um local acessível)
#include "HAL/Runnable.h"       // Para FRunnable (worker thread)
#include "Containers/Queue.h"   // Para TQueue (thread-safe queue)
#include "FFmpegLogReader.h"
#include "HAL/ThreadSafeBool.h" // Incluir ThreadSafeBool
#include "Core/IVRFramePool.h" // Adicionar este include!
#include "IVRVideoEncoder.generated.h"

// Definição do LogCategory para esta classe
DECLARE_LOG_CATEGORY_EXTERN(LogIVRVideoEncoder, Log, All);

// Forward declaration da classe worker thread
class FVideoEncoderWorker;

UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="IVR Video Encoder"))
class IVR_API UIVRVideoEncoder : public UObject
{
    GENERATED_BODY()

public:

    UIVRVideoEncoder();
    virtual ~UIVRVideoEncoder();

    // Sobrescreve o BeginDestroy para garantir limpeza de recursos quando o UObject é destruído
    virtual void BeginDestroy() override;

    // Caminho completo para o executável do FFmpeg
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Encoder Settings")
    FString FFmpegExecutablePath; // Agora gerado automaticamente, mas pode ser sobrescrito pelo BP

     /**
     * @brief Inicializa o codificador de vídeo, configura o pipe de entrada e o worker thread.
     * @param Settings Configurações de vídeo para a codificação.
     * @param InFFmpegExecutablePath Caminho para o executável FFmpeg.
     * @param InActualFrameWidth Largura real dos frames que serão processados.
     * @param InActualFrameHeight Altura real dos frames que serão processados.
     * @param InFramePool Referência ao pool de frames para gerenciamento de buffers. 
     * @return true se a inicialização foi bem-sucedida, false caso contrário.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Encoder")
    bool Initialize(const FIVR_VideoSettings& Settings, const FString& InFFmpegExecutablePath, int32 InActualFrameWidth, int32 InActualFrameHeight, UIVRFramePool* InFramePool); 

    /**
     * @brief Lança o processo principal do FFmpeg para iniciar a gravação ao vivo.
     * @param LiveOutputFilePath Caminho completo para o arquivo de vídeo de saída ao vivo.
     * @return true se o processo FFmpeg foi lançado com sucesso, false caso contrário.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Encoder")
    bool LaunchEncoder(const FString& LiveOutputFilePath);

    /**
     * @brief Encerra o codificador e limpa todos os recursos (pipes, processo FFmpeg, threads).
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Encoder")
    void ShutdownEncoder();

    /**
    * @brief Adiciona um frame de vídeo à fila de codificação.
    * @param Frame O frame de vídeo a ser codificado.
    * @return true se o frame foi adicionado com sucesso, false caso contrário.
    */
    bool EncodeFrame(FIVR_VideoFrame Frame); // Assinatura mudada para receber por valor

    /**
     * @brief Sinaliza que não haverá mais frames para codificar e aguarda a conclusão da escrita no pipe.
     * Isso fecha o pipe de entrada e sinaliza EOF ao FFmpeg.
     * @return true se a finalização foi bem-sucedida, false caso contrário.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Encoder")
    bool FinishEncoding();

    /**
     * @brief Concatena uma lista de arquivos de vídeo em um único arquivo mestre usando FFmpeg.
     * @param InTakePaths Array de caminhos completos para os takes individuais.
     * @param InMasterOutputPath Caminho completo para o arquivo de vídeo mestre de saída.
     * @return true se a concatenação foi bem-sucedida, false caso contrário.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Encoder")
    bool ConcatenateVideos(const TArray<FString>& InTakePaths, const FString& InMasterOutputPath);

    UFUNCTION(BlueprintPure, Category = "IVR")
    bool IsInitialized() const { return bIsInitialized; }

protected:
    // Configurações de vídeo atuais
    FIVR_VideoSettings CurrentSettings;
    
    // Instância da fábrica de comandos FFmpeg, agora gerenciada por esta classe.
    UPROPERTY()
    UIVRECFactory* EncoderCommandFactory;

    // Handle do processo principal do FFmpeg para a gravação ao vivo
    FProcHandle FFmpegProcHandle;
    FFMpegLogReader* FFmpegStdoutLogReader; // Leitor de log do FFmpeg stdout
    FFMpegLogReader* FFmpegStderrLogReader; // Leitor de log do FFmpeg stderr
    void* FFmpegReadPipeStdout;       // Handle de leitura para o pipe do FFmpeg stdout
    void* FFmpegWritePipeStdout;      // Handle de escrita para o pipe do FFmpeg stdout
    void* FFmpegReadPipeStderr;       // Handle de leitura para o pipe do FFmpeg stderr
    void* FFmpegWritePipeStderr;      // Handle de escrita para o pipe do FFmpeg stderr

    // Wrapper para o Named Pipe de entrada de vídeo (UE -> FFmpeg)
    FIVR_PipeWrapper VideoInputPipe;
    
    // Nome único do pipe de vídeo para esta sessão
    FString VideoPipeBaseName; // Nome base para o pipe

    // Fila thread-safe para frames de vídeo (Mpsc: Multiple Producer, Single Consumer)
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc> FrameQueue;
    
    // Flag atômica para sinalizar ao worker thread para parar
    FThreadSafeBool bStopWorkerThread;
    
    // Flag atômica para sinalizar que todos os frames foram submetidos (não haverá mais Enqueue)
    FThreadSafeBool bNoMoreFramesToEncode;
    
    // Estado interno para controle de inicialização
    FThreadSafeBool bIsInitialized;

    // Worker thread para escrita no pipe
    FVideoEncoderWorker* WorkerRunnable;
    FRunnableThread* WorkerThread; 
    
    // Evento para sinalizar que novos frames estão disponíveis ou que a thread deve verificar o estado (shutdown/finish)
    FEvent* NewFrameEvent;

     // Largura e altura reais dos frames a serem processados
    int32 ActualProcessingWidth;
    int32 ActualProcessingHeight;

    UPROPERTY() // Adicionar para garantir que o FramePool não seja coletado pelo GC
    UIVRFramePool* FramePool; // Referência ao pool de frames

    /**
     * @brief Função auxiliar para limpar os recursos do encoder (terminar FFmpeg e fechar pipe).
     */
    void InternalCleanupEncoderResources();

    /**
     * @brief Função auxiliar para obter o caminho do executável FFmpeg.
     * @return Caminho completo do executável FFmpeg.
     */
    FString GetFFmpegExecutablePathInternal() const;
};

/**
 * FVideoEncoderWorker
 * Implementa FRunnable para processar a fila de frames e escrevê-los no Named Pipe em um thread separado.
 */
class FVideoEncoderWorker : public FRunnable
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

