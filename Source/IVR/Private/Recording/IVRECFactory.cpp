// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Recording/IVRECFactory.h" // Inclua o cabe�alho da sua pr�pria classe
#include "Internationalization/Text.h" // Para FText e FText::Format
#include "HAL/PlatformFileManager.h" // Necess�rio para FPlatformFileManager

// Defini��o do LogCategory (se j� definido em outro lugar, remova esta linha)
DEFINE_LOG_CATEGORY(LogIVRECFactory);

UIVRECFactory::UIVRECFactory()
{
    // Removido: Adi��o de formatos de comando padr�o para �udio.
}

void UIVRECFactory::IVR_AddCommandFormat(const FString& Name, const FString& Format)
{
    EncoderCommandFormats.Add(Name, Format);
}

void UIVRECFactory::IVR_BuildRawRgbCommand()
{
    // Constroi argumentos de forma individual para evitar problemas de sintaxe do Printf
    TArray<FString> ArgsArray;
    ArgsArray.Add(TEXT("-y")); // Sobrescreve o arquivo de sa�da sem perguntar

    // Entrada de V�deo RGBA    
    ArgsArray.Add(FString::Printf(TEXT("-f rawvideo -pix_fmt %s -s %dx%d -r %f"),*VideoSettings.PixelFormat, VideoSettings.Width, VideoSettings.Height, VideoSettings.FPS));
    
    //Informa Pipe de Entrada.
    ArgsArray.Add(FString::Printf(TEXT("-i %s"), *InVideoPipePath)); 
    
    // --- Mapeamento de Streams: Mudar para apenas mapear o v�deo (assumindo que � o primeiro e �nico input) ---
    ArgsArray.Add(TEXT("-map 0:v")); // Ou "-map 0:0" se preferir o �ndice da stream

    //=========================================TESTE 01===========================================
    ArgsArray.Add(TEXT("-vframes 300")); // Grava 300 frames (aprox. 10 segundos a 30 FPS)
    
    // Definir o formato de sa�da como rawvideo e o pixel format
    ArgsArray.Add(TEXT("-f rawvideo")); 
    ArgsArray.Add(FString::Printf(TEXT("-pix_fmt %s"),*VideoSettings.PixelFormat));
    //=========================================TESTE===========================================
    
    // Arquivo de saida
    ArgsArray.Add(*InOutputFilePath); 

    // Une todos os argumentos com um espa�o
    FString Arguments = FString::Join(ArgsArray, TEXT(" "));

    IVR_AddCommandFormat("RawRgbFrames", Arguments);
}

void UIVRECFactory::IVR_BuildLibx264Command()
{
    // Constroi argumentos de forma individual para evitar problemas de sintaxe do Printf
    TArray<FString> ArgsArray;
    ArgsArray.Add(TEXT("-y")); // Sobrescreve o arquivo de sa�da sem perguntar

    // Entrada de V�deo RGBA    
    // Entrada de V�deo RGBA - AGORA USANDO AS DIMENS�ES REAIS
    ArgsArray.Add(FString::Printf(TEXT("-f rawvideo -pix_fmt %s -s %dx%d -r %f"), *VideoSettings.PixelFormat, ActualVideoWidth, ActualVideoHeight, VideoSettings.FPS));
    
    //Informa Caminho do Pipe de Entrada.
    ArgsArray.Add(FString::Printf(TEXT("-i %s"), *InVideoPipePath)); 
    
    // --- Mapeamento de Streams: Mudar para apenas mapear o v�deo (assumindo que � o primeiro e �nico input) ---
    ArgsArray.Add(TEXT("-map 0:v")); // Apenas mapeia o v�deo, sem stream de �udio

    //=========================================TESTE 02===========================================
    ArgsArray.Add(TEXT("-c:v libx264"));      // Usar o encoder libx264
    ArgsArray.Add(TEXT("-preset ultrafast")); // Preset para velocidade (menos exigente)
    ArgsArray.Add(TEXT("-crf 23 "));           // Constant Rate Factor (controle de qualidade)
    //=========================================TESTE===========================================
    
    // Arquivo Saida
    ArgsArray.Add(*InOutputFilePath); 

    // Une todos os argumentos com um espa�o
    FString Arguments = FString::Join(ArgsArray, TEXT(" "));

    IVR_AddCommandFormat("libx264", Arguments);
}

void UIVRECFactory::IVR_BuildSettingsCommand()
{
    // Constroi argumentos de forma individual para evitar problemas de sintaxe do Printf
    TArray<FString> ArgsArray;
    ArgsArray.Add(TEXT("-y")); // Sobrescreve o arquivo de sa�da sem perguntar

    // Entrada de V�deo RGBA    
    ArgsArray.Add(FString::Printf(TEXT("-f rawvideo -pix_fmt %s -s %dx%d -r %f"), *VideoSettings.PixelFormat, ActualVideoWidth, ActualVideoHeight, VideoSettings.FPS));
    //Informa Caminho.
    ArgsArray.Add(FString::Printf(TEXT("-i %s"), *InVideoPipePath)); 
    
    // --- Mapeamento de Streams: Mudar para apenas mapear o v�deo (assumindo que � o primeiro e �nico input) ---
    ArgsArray.Add(TEXT("-map 0:v")); // Apenas mapeia o v�deo, sem stream de �udio

    // Sa�da de V�deo
    ArgsArray.Add(FString::Printf(TEXT("-c:v %s -b:v %d"), *VideoSettings.Codec, VideoSettings.Bitrate));
    
    // Arquivo de Sa�da
    ArgsArray.Add(*InOutputFilePath); 

    // Une todos os argumentos com um espa�o
    FString Arguments = FString::Join(ArgsArray, TEXT(" "));

    IVR_AddCommandFormat("UserSetRecSettings", Arguments);

}

void UIVRECFactory::IVR_BuildReadFrameCommand()
{
    //Comando Base: ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30 -i \.\pipe\SeuNomeDoPipe -vframes 1 saida_frame_unico.png
    TArray<FString> ArgsArray;
    ArgsArray.Add(FString::FromInt(VideoSettings.Width));
    ArgsArray.Add(FString::FromInt(VideoSettings.Height));
    ArgsArray.Add(FString::SanitizeFloat(VideoSettings.FPS));
    ArgsArray.Add(ConsumerPipePath); // O caminho do pipe para o FFmpeg
    // Une todos os argumentos com um espa�o
    FString Arguments = FString::Join(ArgsArray, TEXT(" "));
}

void UIVRECFactory::IVR_BuildConcatenationCommand(const FString& InFilelistPath, const FString& InMasterOutputPath)
{
    TArray<FString> ArgsArray;
    ArgsArray.Add(TEXT("-y")); // Sobrescreve o arquivo de sa�da sem perguntar
    ArgsArray.Add(TEXT("-f concat")); // Usa o demuxer concat
    ArgsArray.Add(TEXT("-safe 0")); // Necess�rio para permitir caminhos absolutos no filelist.txt
    ArgsArray.Add(FString::Printf(TEXT("-i %s"), *InFilelistPath)); // Arquivo de lista de takes
    ArgsArray.Add(TEXT("-c copy")); // Copia os streams sem re-encode para maior velocidade e qualidade
    ArgsArray.Add(TEXT("-map 0:v")); // Explicitamente mapeia apenas o stream de v�deo para evitar problemas com �udio
    ArgsArray.Add(FString::Printf(TEXT(" %s"), *InMasterOutputPath)); // Arquivo de sa�da master

    FString Arguments = FString::Join(ArgsArray, TEXT(" "));
    IVR_AddCommandFormat("ConcatenateTakes", Arguments);
}


FString UIVRECFactory::IVR_GetEncoderCommand(const FString& CommandName)
{
    // Verifique se o formato de comando existe
    if (!EncoderCommandFormats.Contains(CommandName))
    {
        UE_LOG(LogIVRECFactory, Error, TEXT("Command format '%s' not found."), *CommandName);
        return FString();
    }

    FString FoundFormat = EncoderCommandFormats[CommandName];
    
    return FoundFormat;
}

void UIVRECFactory::IVR_SetPipeSettings()
{
    VideoPipeConfig.BasePipeName = TEXT("URVPipe");
    VideoPipeConfig.bBlockingMode = true;
    VideoPipeConfig.bDuplexAccess = false; 
    VideoPipeConfig.bMessageMode = false; 
}

FIVR_PipeSettings UIVRECFactory::IVR_GetPipeSettings()
{
    return VideoPipeConfig;
}

FIVR_VideoSettings UIVRECFactory::IVR_GetVideoSettings()
{
    return VideoSettings;
}

bool UIVRECFactory::IVR_GetExecFPath(FString& pIVR_ExecutablePath)
{
    // O caminho j� foi atribu�do corretamente no Initialize. Apenas verifique sua exist�ncia.
    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*FFmpegExecutablePath))
    {
        UE_LOG(LogIVRECFactory, Error, TEXT("FFmpeg executable not found at: %s. Please ensure the path is correct."), *FFmpegExecutablePath);
        return false;
    }

    pIVR_ExecutablePath = FFmpegExecutablePath;
    return true;
}

FString UIVRECFactory::IVR_GetProducerPath()
{
    return ProducerPipePath;
}

FString UIVRECFactory::IVR_GetConsumerPath()
{
    return ConsumerPipePath;
}

