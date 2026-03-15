// IVROpenCVBridge/Private/ImageLoadingHelpers.cpp
#include "IVROpenCVGlobals.h" // Para as declarações das funções públicas e o namespace IVROpenCVBridge
#include "Misc/FileHelper.h" // Para FFileHelper::LoadFileToArray

// AQUI é onde você inclui os headers da Unreal para ImageWrapper
#include "Modules/ModuleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

// Outros includes do OpenCV para a lógica de LoadAndResizeImage
#if WITH_OPENCV 
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h"

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

#include "PostOpenCVHeaders.h" 
#endif


namespace IVROpenCVBridge
{
    // Funções internas (helpers) a este arquivo .cpp
    // Não é IVROPENCVBRIDGE_API porque não é exportada.
    // TSharedPtr<IImageWrapper> GetImageWrapperByExtention(FString InImagePath) está agora definida aqui.
    static TSharedPtr<IImageWrapper> GetImageWrapperByExtentionInternal(FString InImagePath)
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

    // Implementação de LoadAndResizeImage (agora chama o helper interno)
    bool LoadAndResizeImage(const FString& FilePath, int32 TargetWidth, int32 TargetHeight, TArray<uint8>& OutRawData)
    {
        TArray<uint8> CompressedData;
        if (!FFileHelper::LoadFileToArray(CompressedData, *FilePath))
        {
            UE_LOG(LogIVROpenCVBridge, Error, TEXT("IVROpenCVBridge: Failed to load image file to array: %s"), *FilePath);
            return false;
        }
        
        // Chame a função helper interna
        TSharedPtr<IImageWrapper> ImageWrapper = GetImageWrapperByExtentionInternal(FilePath);
        if (!ImageWrapper.IsValid())
        {
            UE_LOG(LogIVROpenCVBridge, Error, TEXT("IVROpenCVBridge: Failed to create image wrapper for format: (%s)"), *FilePath);
            return false;
        }

        if (!ImageWrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
        {
            UE_LOG(LogIVROpenCVBridge, Error, TEXT("IVROpenCVBridge: Failed to set compressed data or decompress image for: %s"), *FilePath);
            return false;
        }
        TArray<uint8> DecompressedData;
        ERGBFormat ImageFormatToUse = ERGBFormat::BGRA;
        if (!ImageWrapper->GetRaw(ImageFormatToUse, 8, DecompressedData))
        {
            ImageFormatToUse = ERGBFormat::RGBA;
            if (!ImageWrapper->GetRaw(ImageFormatToUse, 8, DecompressedData))
            {
                UE_LOG(LogIVROpenCVBridge, Error, TEXT("IVROpenCVBridge: Failed to get raw image data (BGRA or RGBA) for: %s"), *FilePath);
                return false;
            }
        }
        if (ImageFormatToUse == ERGBFormat::RGBA)
        {
            const int32 NumPixels = ImageWrapper->GetWidth() * ImageWrapper->GetHeight();
            TArray<uint8> BGRATempData;
            BGRATempData.SetNumUninitialized(NumPixels * 4);
            for (int32 i = 0; i < NumPixels; ++i)
            {
                BGRATempData[i * 4 + 0] = DecompressedData[i * 4 + 2];
                BGRATempData[i * 4 + 1] = DecompressedData[i * 4 + 1];
                BGRATempData[i * 4 + 2] = DecompressedData[i * 4 + 0];
                BGRATempData[i * 4 + 3] = DecompressedData[i * 4 + 3];
            }
            DecompressedData = MoveTemp(BGRATempData);
        }

        // ... (restante da lógica de redimensionamento OpenCV) ...
        cv::Mat OriginalImageMat(ImageWrapper->GetHeight(), ImageWrapper->GetWidth(), CV_8UC4, DecompressedData.GetData());
        if (OriginalImageMat.cols != TargetWidth || OriginalImageMat.rows != TargetHeight)
        {
               UE_LOG(LogIVROpenCVBridge, Warning, TEXT("IVROpenCVBridge: Image dimensions (%dx%d) do not match target (%dx%d) for %s. Resizing..."),
                      OriginalImageMat.cols, OriginalImageMat.rows, TargetWidth, TargetHeight, *FilePath);
            cv::Mat ResizedImageMat;
            cv::resize(OriginalImageMat, ResizedImageMat, cv::Size(TargetWidth, TargetHeight), 0, 0, cv::INTER_AREA);
            OutRawData.SetNumUninitialized(ResizedImageMat.total() * ResizedImageMat.elemSize());
            FMemory::Memcpy(OutRawData.GetData(), ResizedImageMat.data, ResizedImageMat.total() * ResizedImageMat.elemSize());
        }
        else
        {
            OutRawData = MoveTemp(DecompressedData);
        }
        return true;
    }
}