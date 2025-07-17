// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/IVRTypes.h"
#include "IVRECFactory.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogIVRECFactory, Log, All);

UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="IVR Encoder Command Factory"))
class IVR_API UIVRECFactory : public UObject
{
    GENERATED_BODY()

public:
    // Construtor padr�o da classe
    UIVRECFactory();

    // Map para armazenar os formatos dos comandos do FFmpeg
    // Chave: Nome do comando (e.g., "RecordRawFrame")
    // Valor: String de formato com placeholders para FString::Format (e.g., "-f s16le -ar {0} -ac {1} -i "{2}"")
    // 'VisibleAnywhere' permite visualizar e 'EditDefaultsOnly' permite editar no Blueprint Defaults
    UPROPERTY(EditDefaultsOnly, Category = "FFmpeg Encoder")
    TMap<FString, FString> EncoderCommandFormats;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Recording Settings")
    FIVR_VideoSettings VideoSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Recording Settings")
    FIVR_PipeSettings VideoPipeConfig;

    /**
     * Retorna uma string de comando FFmpeg formatada com base no nome do comando e uma lista de argumentos.
     * Os argumentos devem ser passados como um array de strings.
     * O formato esperado na string de comando usa placeholders como {0}, {1}, etc.
     *
     * @param CommandName O nome do comando a ser formatado (e.g., "RecordRawFrame").
     * @param FormatterArgs Um array de strings com os valores a serem inseridos nos placeholders.
     *        Por exemplo, para "RecordRawFrame" (-f s16le -ar {0} -ac {1} -i "{2}"),
     *        FormatterArgs deve ser: ["<SampleRate>", "<NumChannels>", "<AudioPipePath>"].
     * @return A string de comando FFmpeg formatada, ou uma string vazia se o comando n�o for encontrado.
     */
    UFUNCTION(BlueprintCallable, Category = "FFmpeg Encoder|Commands")
    FString IVR_GetEncoderCommand(const FString& CommandName);

    void IVR_SetInPipePath    (FString InVideoPipe) { InVideoPipePath = InVideoPipe; }
    void IVR_SetOutputFilePath(FString OutVideoFile) { InOutputFilePath = OutVideoFile; }
    void IVR_SetExecutablePath(const FString& InFFmpegExecutablePath) { FFmpegExecutablePath = InFFmpegExecutablePath; }
    void IVR_SetVideoSettings (const FIVR_VideoSettings& InVideoSettings) { VideoSettings = InVideoSettings; }
    void IVR_SetActualVideoDimensions(int32 InWidth, int32 InHeight) { ActualVideoWidth = InWidth; ActualVideoHeight = InHeight; }
    void IVR_SetPipeSettings  ();
    
    void IVR_SetProducerPipePath    (FString InPipePath) { ProducerPipePath = InPipePath; }
    void IVR_SetConsumerPipePath    (FString InPipePath) { ConsumerPipePath = InPipePath; }
   
    FIVR_PipeSettings  IVR_GetPipeSettings ();
    FIVR_VideoSettings IVR_GetVideoSettings();
    bool               IVR_GetExecFPath(FString &pIVR_ExecutablePath);
    FString            IVR_GetProducerPath();
    FString            IVR_GetConsumerPath();

    /**
    * Build FFmpeg Command using Raw definitions. Usefull to just test file generation.
    */
    void IVR_BuildRawRgbCommand();

    /**
    * Build FFmpeg Command using Libx264 with ultrafast preset.
    * This command now expects InVideoPipePath and InOutputFilePath to be set.
    */
    void IVR_BuildLibx264Command();

    /**
    * Build FFmpeg Command using The IVR Settings Codec and BitRate Info.
    */
    void IVR_BuildSettingsCommand();

    void IVR_BuildReadFrameCommand();

    /**
     * @brief Builds an FFmpeg command for concatenating video files using the concat demuxer.
     * @param InFilelistPath Path to the temporary file containing the list of videos to concatenate.
     * @param InMasterOutputPath Path for the final concatenated "Master" video file.
     */
    void IVR_BuildConcatenationCommand(const FString& InFilelistPath, const FString& InMasterOutputPath);


protected:

    // Fun��o auxiliar para adicionar formatos de comando (usada no construtor)
    void IVR_AddCommandFormat(const FString& Name, const FString& Format);

    FString InVideoPipePath;
    FString InOutputFilePath;

    FString FFmpegExecutablePath;           // Caminho para o execut�vel do FFmpeg

    FString ProducerPipePath;
    FString ConsumerPipePath;

    // Armazena a largura e altura reais que a FFmpeg deve usar para o stream de entrada
    int32 ActualVideoWidth;
    int32 ActualVideoHeight;
};

