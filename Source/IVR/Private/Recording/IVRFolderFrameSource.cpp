// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Recording/IVRFolderFrameSource.h"
#include "IVR.h"
#include "IVRGlobalStatics.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h" // For FFileHelper

// C2: Mover Includes do OpenCV para o escopo global do arquivo .cpp.
// Este bloco foi movido do interior de LoadImageFromFile para aqui.
#if WITH_OPENCV 
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h" // Abre namespace/desativa avisos

#include <opencv2/opencv.hpp>
// #include <opencv2/videoio.hpp> // Nao diretamente necessario aqui para este cpp
#include <opencv2/imgproc.hpp> 

#include "PostOpenCVHeaders.h" // Fecha namespace/reativa avisos
#endif

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

bool UIVRFolderFrameSource::LoadImageFromFile(const FString& FilePath, TArray<uint8>& OutRawData)
{
    TArray<uint8> CompressedData;
    if (!FFileHelper::LoadFileToArray(CompressedData, *FilePath))
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("Falha ao carregar arquivo de imagem para array: %s"), *FilePath);
        return false;
    }
    TSharedPtr<IImageWrapper> ImageWrapper = GetImageWrapperByExtention(FilePath);
    if (!ImageWrapper.IsValid())
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("Falha ao criar wrapper de imagem para o formato: (%s)"), *FilePath);
        return false;
    }

    if (!ImageWrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("Falha ao definir dados compactados ou descomprimir imagem para: %s"), *FilePath);
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
            UE_LOG(LogIVRFrameSource, Error, TEXT("Falha ao obter dados brutos da imagem (BGRA ou RGBA) para: %s"), *FilePath);
            return false;
        }
    }

    // Se a descompressão foi para RGBA, converte para BGRA
    if (ImageFormatToUse == ERGBFormat::RGBA)
    {
        const int32 NumPixels = ImageWrapper->GetWidth() * ImageWrapper->GetHeight();
        TArray<uint8> BGRATempData;
        BGRATempData.SetNumUninitialized(NumPixels * 4); // Alocar para a saída BGRA
        // Conversão manual de RGBA para BGRA (trocar canais R e B)
        for (int32 i = 0; i < NumPixels; ++i)
        {
            BGRATempData[i * 4 + 0] = DecompressedData[i * 4 + 2]; // Blue (do R do RGBA)
            BGRATempData[i * 4 + 1] = DecompressedData[i * 4 + 1]; // Green
            BGRATempData[i * 4 + 2] = DecompressedData[i * 4 + 0]; // Red (do B do RGBA)
            BGRATempData[i * 4 + 3] = DecompressedData[i * 4 + 3]; // Alpha
        }
        DecompressedData = MoveTemp(BGRATempData); // Substitui os dados RGBA pelos BGRA
    }

#if WITH_OPENCV // AGORA O CÓDIGO OpenCV SÓ EXISTE AQUI (includes estão no global)
    // Converter o TArray<uint8> para cv::Mat (tipo esperado pelo OpenCV)
    cv::Mat OriginalImageMat(ImageWrapper->GetHeight(), ImageWrapper->GetWidth(), CV_8UC4, DecompressedData.GetData());

    // Verificar se as dimensões da imagem original correspondem às dimensões alvo
    if (OriginalImageMat.cols != FrameSourceSettings.Width || OriginalImageMat.rows != FrameSourceSettings.Height)
    {
        UE_LOG(LogIVRFrameSource, Warning, TEXT("Dimensões da imagem (%dx%d) não correspondem às do alvo (%dx%d) para %s. Redimensionando..."),
               OriginalImageMat.cols, OriginalImageMat.rows, FrameSourceSettings.Width, FrameSourceSettings.Height, *FilePath);

        cv::Mat ResizedImageMat;
        cv::resize(OriginalImageMat, ResizedImageMat, cv::Size(FrameSourceSettings.Width, FrameSourceSettings.Height), 0, 0, cv::INTER_AREA);
        // Copiar os dados da cv::Mat redimensionada para OutRawData
        OutRawData.SetNumUninitialized(ResizedImageMat.total() * ResizedImageMat.elemSize());
        FMemory::Memcpy(OutRawData.GetData(), ResizedImageMat.data, ResizedImageMat.total() * ResizedImageMat.elemSize());
    }
    else
    {
        // Se as dimensões já correspondem, apenas move os dados descompactados para OutRawData
        OutRawData = MoveTemp(DecompressedData);
    }
#else
    // Se OpenCV não estiver habilitado, apenas move os dados descompactados (sem redimensionar)
    UE_LOG(LogIVRFrameSource, Warning, TEXT("OpenCV não habilitado. O redimensionamento de imagem para %s será pulado."), *FilePath);
    OutRawData = MoveTemp(DecompressedData);
#endif

    return true; // Imagem carregada, convertida e redimensionada (se OpenCV ativo) com sucesso.
}
TSharedPtr<IImageWrapper> UIVRFolderFrameSource::GetImageWrapperByExtention(FString InImagePath)
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