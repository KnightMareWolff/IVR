// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Recording/IVRFolderFrameSource.h"
#include "IVR.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h" // For FFileHelper

#if WITH_OPENCV
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h"

#undef check // the check macro causes problems with opencv headers
#pragma warning(disable: 4668) // 'symbol' not defined as a preprocessor macro, replacing with '0' for 'directives'
#pragma warning(disable: 4828) // The character set in the source file does not support the character used in the literal
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp> // Para cv::VideoCapture
#include <opencv2/imgproc.hpp> // Para cv::cvtColor

#include "PostOpenCVHeaders.h"
#endif

// Adicione "ImageWrapper" ao Build.cs do seu m�dulo:
// PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "ImageWrapper" });

UIVRFolderFrameSource::UIVRFolderFrameSource()
    : UIVRFrameSource()
    , CurrentImageIndex(0)
{
}

void UIVRFolderFrameSource::Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool)
{
    if (!World || !InFramePool)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRFolderFrameSource::Initialize: World or FramePool is null."));
        return;
    }
    CurrentWorld = World;
    FrameSourceSettings = Settings;
    FramePool = InFramePool;

    ImageFiles.Empty();
    CurrentImageIndex = 0;

    FString AbsoluteFolderPath = FPaths::Combine(FPaths::ProjectDir(), Settings.IVR_FramesFolder); // Assume caminho relativo ao projeto

    // Garante que o caminho � absoluto e normalize
    FPaths::NormalizeDirectoryName(AbsoluteFolderPath);
    
    if (!IFileManager::Get().DirectoryExists(*AbsoluteFolderPath))
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRFolderFrameSource: Folder '%s' does not exist!"), *AbsoluteFolderPath);
        return;
    }

    // Busca arquivos de imagem na pasta
    TArray<FString> FoundFiles;
    IFileManager::Get().FindFiles(FoundFiles, *AbsoluteFolderPath, TEXT("*.*"));

    // Filtra por extens�es de imagem suportadas
    TArray<FString> SupportedExtensions = { TEXT(".png"), TEXT(".jpg"), TEXT(".jpeg"), TEXT(".bmp"), TEXT(".tga"), TEXT(".exr") };
    for (const FString& File : FoundFiles)
    {
        FString Extension = FPaths::GetExtension(File, true).ToLower(); // GetExtension with dot
        if (SupportedExtensions.Contains(Extension))
        {
            ImageFiles.Add(FPaths::Combine(AbsoluteFolderPath, File));
        }
    }
    
    // Opcional: Ordenar os arquivos para garantir uma sequ�ncia correta
    ImageFiles.Sort();

    if (ImageFiles.Num() == 0)
    {
        UE_LOG(LogIVRFrameSource, Warning, TEXT("UIVRFolderFrameSource: No supported image files found in folder '%s'."), *AbsoluteFolderPath);
    }
    else
    {
        UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource initialized with %d image files from '%s'."), ImageFiles.Num(), *AbsoluteFolderPath);
    }
}

void UIVRFolderFrameSource::Shutdown()
{
    StopCapture();
    ImageFiles.Empty();
    CurrentWorld = nullptr;
    FramePool = nullptr;
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource Shutdown."));
}

void UIVRFolderFrameSource::StartCapture()
{
    if (!CurrentWorld) return;
    if (ImageFiles.Num() == 0)
    {
        UE_LOG(LogIVRFrameSource, Warning, TEXT("UIVRFolderFrameSource: Cannot start capture, no image files found."));
        return;
    }

    float Delay = (FrameSourceSettings.IVR_FolderPlaybackFPS > 0.0f) ? (1.0f / FrameSourceSettings.IVR_FolderPlaybackFPS) : (1.0f / 30.0f);
    CurrentWorld->GetTimerManager().SetTimer(FrameReadTimerHandle, this, &UIVRFolderFrameSource::ReadNextFrameFromFile, Delay, true);
    // Log atualizado para mostrar a dura��o por imagem
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource: Starting frame reading from folder. Images will change every %.2f seconds (based on IVR_FolderPlaybackFPS of %.2f)."), Delay, FrameSourceSettings.IVR_FolderPlaybackFPS);
}

void UIVRFolderFrameSource::StopCapture()
{
    if (CurrentWorld && CurrentWorld->GetTimerManager().IsTimerActive(FrameReadTimerHandle))
    {
        CurrentWorld->GetTimerManager().ClearTimer(FrameReadTimerHandle);
    }
    FrameReadTimerHandle.Invalidate();
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource: Stopped frame reading."));
}

void UIVRFolderFrameSource::ReadNextFrameFromFile()
{
    if (ImageFiles.Num() == 0)
    {
        StopCapture();
        UE_LOG(LogIVRFrameSource, Warning, TEXT("UIVRFolderFrameSource: No more image files to read. Stopping capture."));
        return;
    }

    // Gerencia o looping
    if (CurrentImageIndex >= ImageFiles.Num())
    {
        if (FrameSourceSettings.IVR_LoopFolderPlayback)
        {
            CurrentImageIndex = 0; // Reinicia o loop
            UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource: Looping folder playback."));
        }
        else
        {
            StopCapture();
            UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRFolderFrameSource: Folder playback finished (no looping)."));
            return;
        }
    }

    TSharedPtr<TArray<uint8>> FrameBuffer = AcquireFrameBufferFromPool();
    if (!FrameBuffer.IsValid())
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("Failed to acquire frame buffer from pool. Dropping folder frame."));
        CurrentImageIndex++; // Move para o pr�ximo mesmo se falhar a aquisi��o do buffer
        return;
    }

    // Carrega a imagem e decodifica para BGRA
    if (LoadImageFromFile(ImageFiles[CurrentImageIndex], *FrameBuffer))
    {
        FIVR_VideoFrame NewFrame(FrameSourceSettings.Width, FrameSourceSettings.Height, CurrentWorld->GetTimeSeconds());
        NewFrame.RawDataPtr = FrameBuffer;

        UE_LOG(LogIVRFrameSource, Warning, TEXT("UIVRFolderFrameSource: Read frame %d from '%s'."), CurrentImageIndex, *ImageFiles[CurrentImageIndex]);
        OnFrameAcquired.Broadcast(MoveTemp(NewFrame));
    }
    else
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRFolderFrameSource: Failed to load image from '%s'. Skipping frame."), *ImageFiles[CurrentImageIndex]);
        // Libera o buffer de volta para o pool se a carga falhar
        FramePool->ReleaseFrame(FrameBuffer);
    }

    CurrentImageIndex++;
}

bool UIVRFolderFrameSource::LoadImageFromFile(const FString& FilePath, TArray<uint8>& OutRawData)
{
    TArray<uint8> CompressedData;
    if (!FFileHelper::LoadFileToArray(CompressedData, *FilePath))
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("Failed to load image file to array: %s"), *FilePath);
        return false;
    }

    TSharedPtr<IImageWrapper> ImageWrapper = GetImageWrapperByExtention(FilePath);
    if (!ImageWrapper.IsValid())
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("Failed to create image wrapper for format: (%s)"), *FilePath);
        return false;
    }

    if (!ImageWrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("Failed to set compressed data or decompress image for: %s"), *FilePath);
        return false;
    }

    TArray<uint8> DecompressedData;
    ERGBFormat ImageFormatToUse = ERGBFormat::BGRA; // Prioriza BGRA

    // Tenta obter BGRA diretamente
    if (!ImageWrapper->GetRaw(ImageFormatToUse, 8, DecompressedData))
    {
        // Se BGRA falhou, tenta RGBA
        ImageFormatToUse = ERGBFormat::RGBA;
        if (!ImageWrapper->GetRaw(ImageFormatToUse, 8, DecompressedData))
        {
            // Se ambos falharam
            UE_LOG(LogIVRFrameSource, Error, TEXT("Failed to get raw image data (BGRA or RGBA) for: %s"), *FilePath);
            return false;
        }
    }

    // Se a descompress�o foi para RGBA, converte para BGRA
    if (ImageFormatToUse == ERGBFormat::RGBA)
    {
        const int32 NumPixels = ImageWrapper->GetWidth() * ImageWrapper->GetHeight();
        TArray<uint8> BGRATempData;
        BGRATempData.SetNumUninitialized(NumPixels * 4); // Alocar para a sa�da BGRA

        // Convers�o manual de RGBA para BGRA (trocar canais R e B)
        for (int32 i = 0; i < NumPixels; ++i)
        {
            BGRATempData[i * 4 + 0] = DecompressedData[i * 4 + 2]; // Blue (do R do RGBA)
            BGRATempData[i * 4 + 1] = DecompressedData[i * 4 + 1]; // Green
            BGRATempData[i * 4 + 2] = DecompressedData[i * 4 + 0]; // Red (do B do RGBA)
            BGRATempData[i * 4 + 3] = DecompressedData[i * 4 + 3]; // Alpha
        }
        DecompressedData = MoveTemp(BGRATempData); // Substitui os dados RGBA pelos BGRA
    }

#if WITH_OPENCV
    // Converter o TArray<uint8> para cv::Mat (tipo esperado pelo OpenCV)
    cv::Mat OriginalImageMat(ImageWrapper->GetHeight(), ImageWrapper->GetWidth(), CV_8UC4, DecompressedData.GetData());

    // Verificar se as dimens�es da imagem original correspondem �s dimens�es alvo
    if (OriginalImageMat.cols != FrameSourceSettings.Width || OriginalImageMat.rows != FrameSourceSettings.Height)
    {
        UE_LOG(LogIVRFrameSource, Warning, TEXT("Image dimensions (%dx%d) do not match target (%dx%d) for %s. Resizing..."),
            OriginalImageMat.cols, OriginalImageMat.rows, FrameSourceSettings.Width, FrameSourceSettings.Height, *FilePath);

        cv::Mat ResizedImageMat;
        cv::resize(OriginalImageMat, ResizedImageMat, cv::Size(FrameSourceSettings.Width, FrameSourceSettings.Height), 0, 0, cv::INTER_AREA);

        // Copiar os dados da cv::Mat redimensionada para OutRawData
        OutRawData.SetNumUninitialized(ResizedImageMat.total() * ResizedImageMat.elemSize());
        FMemory::Memcpy(OutRawData.GetData(), ResizedImageMat.data, ResizedImageMat.total() * ResizedImageMat.elemSize());
    }
    else
    {
        // Se as dimens�es j� correspondem, apenas move os dados descompactados para OutRawData
        OutRawData = MoveTemp(DecompressedData);
    }
#else
    // Se OpenCV n�o estiver habilitado, apenas move os dados descompactados (sem redimensionar)
    UE_LOG(LogIVRFrameSource, Warning, TEXT("OpenCV is not enabled. Image resizing for %s will be skipped."), *FilePath);
    OutRawData = MoveTemp(DecompressedData);
#endif

    return true; // Imagem carregada, convertida e redimensionada (se OpenCV ativo) com sucesso.
}

TSharedPtr<IImageWrapper> UIVRFolderFrameSource::GetImageWrapperByExtention(const FString InImagePath)
{
        IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		if (InImagePath.EndsWith(".png"))
		{
			return ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		}
		else if (InImagePath.EndsWith(".jpg") || InImagePath.EndsWith(".jpeg"))
		{
			return ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
		}
		else if (InImagePath.EndsWith(".bmp"))
		{
			return ImageWrapperModule.CreateImageWrapper(EImageFormat::BMP);
		}
		else if (InImagePath.EndsWith(".ico"))
		{
			return ImageWrapperModule.CreateImageWrapper(EImageFormat::ICO);
		}
		else if (InImagePath.EndsWith(".exr"))
		{
			return ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);
		}
		else if (InImagePath.EndsWith(".icns"))
		{
			return ImageWrapperModule.CreateImageWrapper(EImageFormat::ICNS);
		}

		return nullptr;
}


