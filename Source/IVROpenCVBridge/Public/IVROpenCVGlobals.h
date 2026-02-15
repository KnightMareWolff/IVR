// D:\william\UnrealProjects\IVRExample\Plugins\IVR\Source\IVROpenCVBridge\Public\IVROpenCVGlobals.h
#pragma once

#include "CoreMinimal.h" // Para FVector2D, FVector, FTransform, TArray, etc.
#include "UObject/NoExportTypes.h" // Para FTransform, FIntPoint, etc.
#include "Containers/Array.h" // Para TArray
#include "Misc/Guid.h" // Para FGuid
#include "Misc/Paths.h" // Para FPaths
#include "HAL/PlatformFileManager.h" // Para IPlatformFileManager
#include "HAL/FileManager.h" // Para IFileManager
#include "Modules/ModuleManager.h" // Para FModuleManager::LoadModuleChecked (para ImageWrapper)
#include "IImageWrapper.h" // Para IImageWrapper
#include "IImageWrapperModule.h" // Para IImageWrapperModule
#include "Templates/SharedPointer.h" // Para TSharedPtr<IImageWrapper>

// Adicione aqui DECLARE_LOG_CATEGORY_EXTERN(LogIVROpenCVBridge, Log, All); se precisar de uma categoria de log específica para este módulo
#include "IVROpenCVBridge.h" // Para o LogCategory do módulo

#include "IVROpenCVGlobals.generated.h" // MUITO IMPORTANTE: Este deve ser o último include de header.

// --- Novas structs específicas do módulo IVROpenCVBridge ---

// Struct para os pontos de interesse que serão retornados
USTRUCT(BlueprintType)
struct IVROPENCVBRIDGE_API FIVROCV_InterestPoint
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features|Point")
    FVector2D Point2D; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features|Point")
    FVector Point3D; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features|Point")
    FVector Direction; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features|Point")
    FVector2D Size2D; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features|Point")
    float Angle; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features|Point")
    bool IsQuad; 
};

// Struct para todas as features extraídas
USTRUCT(BlueprintType)
struct IVROPENCVBRIDGE_API FIVROCV_ExtractedFeatures
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features")
    TArray<FIVROCV_InterestPoint> InterestPoints; // Array dos pontos de interesse

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features")
    int32 BiggestPointIndex = INDEX_NONE; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features")
    int32 SmallerPointIndex = INDEX_NONE; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features")
    int32 NumOfQuads = 0; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features")
    int32 NumOfRectangles = 0; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features")
    TArray<float> HistogramRed; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features")
    TArray<float> HistogramGreen; 

    UPROPERTY(BlueprintReadOnly, Category = "OpenCV|Features")
    TArray<float> HistogramBlue; 
};

// --- Declarações de Funções Globais do Namespace ---

namespace IVROpenCVBridge
{
    // Declaração da função ProcessFrameAndExtractFeatures com os novos tipos
    IVROPENCVBRIDGE_API void ProcessFrameAndExtractFeatures(
        uint8* PixelData,
        int32 Width,
        int32 Height,
        FTransform CameraTransform,
        float CameraFOV,
        int32 MaxCorners,
        float QualityLevel,
        float MinDistance,
        bool bDebugDrawFeatures,
        FIVROCV_ExtractedFeatures& OutFeatures
    );

    // Funções para migrar a lógica de LoadImageFromFile (redimensionamento OpenCV)
    IVROPENCVBRIDGE_API bool LoadAndResizeImage(const FString& FilePath, int32 TargetWidth, int32 TargetHeight, TArray<uint8>& OutRawData);

    // Funções para listar webcams
    IVROPENCVBRIDGE_API TArray<FString> ListWebcamDevicesNative();

    // Helper para ImageWrapper
    IVROPENCVBRIDGE_API TSharedPtr<IImageWrapper> GetImageWrapperByExtention(FString InImagePath);

} // namespace IVROpenCVBridge