// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "Recording/IVRFolderFrameSource.h"
#include "IVR.h"
#include "IVRGlobalStatics.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h" // For FFileHelper

// [MANUAL_REF_POINT] Includes do OpenCV foram movidos para IVROpenCVBridge.
// Incluindo o bridge para chamar as funções OpenCV nativas
#include "IVROpenCVGlobals.h"

UIVRFolderFrameSource::UIVRFolderFrameSource()
    : UIVRFrameSource()
    , CurrentImageIndex(0)
{
}
void UIVRFolderFrameSource::Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool)
{
    if (!World || !InFramePool)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRFolderFrameSource::Initialize: World ou FramePool é nulo."));
        return;
    }
    CurrentWorld = World;
    FrameSourceSettings = Settings;
    FramePool = InFramePool;

    ImageFiles.Empty();
    CurrentImageIndex = 0;

    FString AbsoluteFolderPath = FPaths::Combine(FPaths::ProjectDir(), Settings.IVR_FramesFolder); // Assume caminho relativo ao projeto
// Garante que o caminho é absoluto e normalize
    FPaths::NormalizeDirectoryName(AbsoluteFolderPath);
    
    if (!IFileManager::Get().DirectoryExists(*AbsoluteFolderPath))
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRFolderFrameSource: Pasta '%s' não existe!"), *AbsoluteFolderPath);
        return;
    }
    // Busca arquivos de imagem na pasta
    TArray<FString> FoundFiles;
    IFileManager::Get().FindFiles(FoundFiles, *AbsoluteFolderPath, TEXT("*.*"));
// Filtra por extensões de imagem suportadas
    TArray<FString> SupportedExtensions = { TEXT(".png"), TEXT(".jpg"), TEXT(".jpeg"), TEXT(".bmp"), TEXT(".tga"), TEXT(".exr") };
    for (const FString& File : FoundFiles)
    {
        FString Extension = FPaths::GetExtension(File, true).ToLower(); // GetExtension with dot
        if (SupportedExtensions.Contains(Extension))
        {
            ImageFiles.Add(FPaths::Combine(AbsoluteFolderPath, File));
        }
    }
    
    // Opcional: Ordenar os arquivos para garantir uma sequência correta
    ImageFiles.Sort();
    if (ImageFiles.Num() == 0)
    {
        UE_LOG(LogIVRFrameSource, Warning, TEXT("UIVRFolderFrameSource: Nenhuns arquivos de imagem suportados encontrados na pasta '%s'."), *AbsoluteFolderPath);
    }
    else
    {
        UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource inicializado com %d arquivos de imagem de '%s'."), ImageFiles.Num(), *AbsoluteFolderPath);
    }
}
void UIVRFolderFrameSource::Shutdown()
{
    StopCapture();
    ImageFiles.Empty();
    CurrentWorld = nullptr;
    FramePool = nullptr;
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource Encerrado."));
}
void UIVRFolderFrameSource::StartCapture()
{
    if (!CurrentWorld) return;
    if (ImageFiles.Num() == 0)
    {
        UE_LOG(LogIVRFrameSource, Warning, TEXT("UIVRFolderFrameSource: Não é possível iniciar a captura, nenhuns arquivos de imagem encontrados."));
        return;
    }
    float Delay = (FrameSourceSettings.IVR_FolderPlaybackFPS > 0.0f) ? (1.0f / FrameSourceSettings.IVR_FolderPlaybackFPS) : (1.0f / 30.0f);
    CurrentWorld->GetTimerManager().SetTimer(FrameReadTimerHandle, this, &UIVRFolderFrameSource::ReadNextFrameFromFile, Delay, true);
    // Log atualizado para mostrar a duração por imagem
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource: Iniciando leitura de frames da pasta. Imagens mudarão a cada %.2f segundos (baseado no IVR_FolderPlaybackFPS de %.2f)."), Delay, FrameSourceSettings.IVR_FolderPlaybackFPS);
}
void UIVRFolderFrameSource::StopCapture()
{
    if (CurrentWorld && CurrentWorld->GetTimerManager().IsTimerActive(FrameReadTimerHandle))
    {
        CurrentWorld->GetTimerManager().ClearTimer(FrameReadTimerHandle);
    }
    FrameReadTimerHandle.Invalidate();
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource: Leitura de frames parada."));
}
void UIVRFolderFrameSource::ReadNextFrameFromFile()
{
    if (ImageFiles.Num() == 0)
    {
        StopCapture();
        UE_LOG(LogIVRFrameSource, Warning, TEXT("UIVRFolderFrameSource: Nenhuns arquivos de imagem restantes para ler. Parando captura."));
        return;
    }
// Gerencia o looping
    if (CurrentImageIndex >= ImageFiles.Num())
    {
        if (FrameSourceSettings.IVR_LoopFolderPlayback)
        {
            CurrentImageIndex = 0; // Reinicia o loop
            UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource: Looping na reprodução da pasta."));
        }
        else
        {
            StopCapture();
            UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource: Reprodução da pasta finalizada (sem loop)."));
            return;
        }
    }
    TSharedPtr<TArray<uint8>> FrameBuffer = AcquireFrameBufferFromPool();
    if (!FrameBuffer.IsValid())
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("Falha ao adquirir buffer de frame do pool. Descartando frame da pasta."));
        CurrentImageIndex++; // Move para o próximo mesmo se falhar a aquisição do buffer
        return;
    }
// Carrega a imagem e decodifica para BGRA
    if (LoadImageFromFile(ImageFiles[CurrentImageIndex], *FrameBuffer))
    {
        FIVR_VideoFrame NewFrame(FrameSourceSettings.Width, FrameSourceSettings.Height, CurrentWorld->GetTimeSeconds());
        NewFrame.RawDataPtr = FrameBuffer;
        UE_LOG(LogIVRFrameSource, Warning, TEXT("UIVRFolderFrameSource: Leu frame %d de '%s'."), CurrentImageIndex, *ImageFiles[CurrentImageIndex]);
        OnFrameAcquired.Broadcast(MoveTemp(NewFrame));
    }
    else
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRFolderFrameSource: Falha ao carregar imagem de '%s'. Pulando frame."), *ImageFiles[CurrentImageIndex]);
        // Libera o buffer de volta para o pool se a carga falhar
        FramePool->ReleaseFrame(FrameBuffer);
    }

    CurrentImageIndex++;
}

// A implementação de LoadImageFromFile agora chama IVROpenCVBridge::LoadAndResizeImage
bool UIVRFolderFrameSource::LoadImageFromFile(const FString& FilePath, TArray<uint8>& OutRawData)
{
    return IVROpenCVBridge::LoadAndResizeImage(FilePath, FrameSourceSettings.Width, FrameSourceSettings.Height, OutRawData);
}

// GetImageWrapperByExtention foi movido para IVROpenCVBridgeBridge.cpp