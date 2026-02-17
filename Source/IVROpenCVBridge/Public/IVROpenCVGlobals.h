// D:\william\UnrealProjects\IVRExample\Plugins\IVR\Source\IVROpenCVBridge\Public\IVROpenCVGlobals.h
#pragma once

#include "CoreMinimal.h" // Para FVector2D, FVector, FTransform, TArray, etc.

// Adicione aqui DECLARE_LOG_CATEGORY_EXTERN(LogIVROpenCVBridge, Log, All); se precisar de uma categoria de log específica para este módulo
#include "IVROpenCVBridge.h" // Para o LogCategory do módulo

// Corresponde a FIVR_JustRTPoint
struct IVROPENCVBRIDGE_API FOCV_NativeJustRTPoint
{
    FVector2D Point2D;
    FVector Point3D;
    FVector Direction;
    FVector2D Size2D;
    float Angle;
    bool IsQuad;

    FOCV_NativeJustRTPoint()
        : Point2D(FVector2D::ZeroVector), Point3D(FVector::ZeroVector), Direction(FVector::ZeroVector),
        Size2D(FVector2D::ZeroVector), Angle(0.0f), IsQuad(false) {
    }
};


// Corresponde a FIVR_JustRTFeatures
struct IVROPENCVBRIDGE_API FOCV_NativeJustRTFeatures
{
    TArray<FOCV_NativeJustRTPoint> JustRTInterestPoints;
    int32 BiggestPointIndex = INDEX_NONE;
    int32 SmallerPointIndex = INDEX_NONE;
    int32 NumOfQuads = 0;
    int32 NumOfRectangles = 0;
    TArray<float> HistogramRed;
    TArray<float> HistogramGreen;
    TArray<float> HistogramBlue;

    FOCV_NativeJustRTFeatures() {}
};

// Corresponde a FIVR_JustRTFrame
struct IVROPENCVBRIDGE_API FOCV_NativeJustRTFrame
{
    TArray<uint8> RawDataBuffer;
    int32 Width;
    int32 Height;
    float Timestamp;
    FLinearColor DisplayTint;
    FLinearColor SourceFrameTint;
    FOCV_NativeJustRTFeatures Features; // Usa a struct nativa de features

    FOCV_NativeJustRTFrame()
        : Width(0), Height(0), Timestamp(0.0f), DisplayTint(FLinearColor::White), SourceFrameTint(FLinearColor::White) {
    }
};

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
        FOCV_NativeJustRTFeatures& OutFeatures
    );

    // Funções para migrar a lógica de LoadImageFromFile (redimensionamento OpenCV)
    IVROPENCVBRIDGE_API bool LoadAndResizeImage(const FString& FilePath, int32 TargetWidth, int32 TargetHeight, TArray<uint8>& OutRawData);

    // Funções para listar webcams
    IVROPENCVBRIDGE_API TArray<FString> ListWebcamDevicesNative();


} // namespace IVROpenCVBridge