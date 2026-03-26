// D:\william\UnrealProjects\IVRExample\Plugins\IVR\Source\IVROpenCVBridge\Private\IVROpenCVGlobals.cpp
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "IVROpenCVGlobals.h" // NOSSO NOVO ARQUIVO DE CABEÇALHO GLOBAL
#include "IVROpenCVBridge.h" // Para LogCategory do módulo IVROpenCVBridge

// Includes do OpenCV
#if WITH_OPENCV 
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h" 

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp> 

#include "PostOpenCVHeaders.h"
#endif



namespace IVROpenCVBridge
{

#if WITH_OPENCV 

    void ProcessFrameAndExtractFeatures(
        uint8* PixelData,
        int32 Width,
        int32 Height,
        FTransform CameraTransform,
        float CameraFOV,
        int32 MaxCorners,
        float QualityLevel,
        float MinDistance,
        bool bDebugDrawFeatures,
        FOCV_NativeJustRTFeatures& OutFeatures // Saída das features
    )
    {
        // Limpa as features antigas para garantir um estado limpo
        OutFeatures.JustRTInterestPoints.Empty();
        OutFeatures.HistogramRed.Empty();
        OutFeatures.HistogramGreen.Empty();
        OutFeatures.HistogramBlue.Empty();
        OutFeatures.BiggestPointIndex = INDEX_NONE;
        OutFeatures.SmallerPointIndex = INDEX_NONE;
        OutFeatures.NumOfQuads = 0;
        OutFeatures.NumOfRectangles = 0;

        if (PixelData != nullptr && Width > 0 && Height > 0)
        {
            cv::Mat InputMat(Height, Width, CV_8UC4, PixelData); // Assume BGRA como entrada

            // --- Lógica de detecção de cantos ---
            cv::Mat GrayMat;
            cv::cvtColor(InputMat, GrayMat, cv::COLOR_BGRA2GRAY);
            std::vector<cv::Point2f> OpenCVInterestPoints;
            cv::goodFeaturesToTrack(GrayMat, OpenCVInterestPoints, MaxCorners, QualityLevel, MinDistance);

            // --- Lógica de detecção de contornos (quads/rects) ---
            cv::Mat BinaryMat;
            cv::adaptiveThreshold(GrayMat, BinaryMat, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV, 11, 2);
            std::vector<std::vector<cv::Point>> Contours;
            cv::findContours(BinaryMat, Contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

            float MaxShapeArea = FLT_MIN;
            int32 MaxShapeIndex = INDEX_NONE;
            float MinShapeArea = FLT_MAX; 
            int32 MinShapeIndex = INDEX_NONE;
            
            std::vector<cv::RotatedRect> AllDetectedShapesForDrawing; // Para o debug draw

            for (const auto& Contour : Contours)
            {
                double contourArea = cv::contourArea(Contour);
                // Filtra contornos muito pequenos ou muito grandes
                if (contourArea < 100.0 || contourArea > (Width * Height * 0.9))
                {
                    continue;
                }

                std::vector<cv::Point> Approx;
                cv::approxPolyDP(Contour, Approx, cv::arcLength(Contour, true) * 0.02, true);
                
                // Verifica se é um quadrilátero convexo
                if (Approx.size() == 4 && cv::isContourConvex(Approx))
                {
                    cv::Rect BoundingRect = cv::boundingRect(Approx);
                    
                    FOCV_NativeJustRTPoint CurrentInterestPoint; // Usar nossa nova struct local
                    CurrentInterestPoint.Point2D = FVector2D(BoundingRect.x + BoundingRect.width / 2.0f, BoundingRect.y + BoundingRect.height / 2.0f); 
                    CurrentInterestPoint.Size2D = FVector2D(BoundingRect.width, BoundingRect.height);
                    
                    cv::RotatedRect RotatedRect = cv::minAreaRect(Contour);
                    CurrentInterestPoint.Angle = RotatedRect.angle; // Ângulo do retângulo rotacionado

                    // Verifica se é um quadrado ou um retângulo
                    const float SquareTolerance = 0.15f;
                    if (BoundingRect.height > 0)
                    {
                        float AspectRatio = (float)BoundingRect.width / BoundingRect.height;
                        if (FMath::Abs(AspectRatio - 1.0f) < SquareTolerance)
                        {
                            CurrentInterestPoint.IsQuad = true;
                            OutFeatures.NumOfQuads++;
                        }
                        else
                        {
                            CurrentInterestPoint.IsQuad = false;
                            OutFeatures.NumOfRectangles++;
                        }
                    }
                    else
                    {
                        CurrentInterestPoint.IsQuad = false; // Se altura for zero, não é um quad/rect válido
                    }

                    // Deprojeção para o mundo 3D (ainda preenchida com zeros aqui)
                    // NOTA: A lógica de deprojeção manual precisaria ser implementada aqui se fosse necessário.
                    // Atualmente, IVRCaptureComponent::DeprojectPixelToWorld é um UFUNCTION.
                    // Para funcionar aqui, uma versão C++ pura teria que ser criada dentro deste namespace.
                    CurrentInterestPoint.Point3D = FVector::ZeroVector; 
                    CurrentInterestPoint.Direction = FVector::ZeroVector; 
                    
                    OutFeatures.JustRTInterestPoints.Add(CurrentInterestPoint);
                    AllDetectedShapesForDrawing.push_back(RotatedRect);

                    // Atualiza índices do maior e menor
                    if (contourArea > MaxShapeArea)
                    {
                        MaxShapeArea = contourArea;
                        MaxShapeIndex = OutFeatures.JustRTInterestPoints.Num() - 1;
                    }
                    if (contourArea < MinShapeArea)
                    {
                        MinShapeArea = contourArea;
                        MinShapeIndex = OutFeatures.JustRTInterestPoints.Num() - 1;
                    }
                }
            }
            OutFeatures.BiggestPointIndex = MaxShapeArea == FLT_MIN ? INDEX_NONE : MaxShapeIndex; 
            OutFeatures.SmallerPointIndex = MinShapeArea == FLT_MAX ? INDEX_NONE : MinShapeIndex; 

            // --- Cálculo de Histograma ---
            std::vector<cv::Mat> BGRChannels(3); 
            cv::split(InputMat, BGRChannels); 
            
            cv::Mat B_hist, G_hist, R_hist;
            int HistSize = 256;
            float Range[] = {0, 256};
            const float* HistRanges[] = { Range }; 
            int channels_idx[] = {0};
            int hist_dims[] = {HistSize};
            bool Uniform = true;
            bool Accumulate = false;

            cv::calcHist(&BGRChannels[0], 1, channels_idx, cv::Mat(), B_hist, 1, hist_dims, HistRanges, Uniform, Accumulate);
            cv::calcHist(&BGRChannels[1], 1, channels_idx, cv::Mat(), G_hist, 1, hist_dims, HistRanges, Uniform, Accumulate);
            cv::calcHist(&BGRChannels[2], 1, channels_idx, cv::Mat(), R_hist, 1, hist_dims, HistRanges, Uniform, Accumulate);
            
            cv::normalize(B_hist, B_hist, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());
            cv::normalize(G_hist, G_hist, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());
            cv::normalize(R_hist, R_hist, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());
            
            OutFeatures.HistogramBlue.SetNumUninitialized(HistSize);
            OutFeatures.HistogramGreen.SetNumUninitialized(HistSize);
            OutFeatures.HistogramRed.SetNumUninitialized(HistSize);
            for (int j = 0; j < HistSize; ++j)
            {
                OutFeatures.HistogramBlue[j] = B_hist.at<float>(j);
                OutFeatures.HistogramGreen[j] = G_hist.at<float>(j);
                OutFeatures.HistogramRed[j] = R_hist.at<float>(j);
            }
            
            // --- Debug Draw Features (se habilitado) ---
            if (bDebugDrawFeatures)
            {
                for (const auto& RotatedRect : AllDetectedShapesForDrawing)
                {
                    cv::Point2f Points[4];
                    RotatedRect.points(Points);
                    std::vector<std::vector<cv::Point>> Polygon(1);
                    for (int i = 0; i < 4; ++i)
                    {
                        Polygon[0].push_back(Points[i]);
                    }
                    cv::Scalar Color = cv::Scalar(0, 255, 0, 255); // Verde
                    
                    cv::polylines(InputMat, Polygon, true, Color, 2); // Desenha polígono
                }
                for (const cv::Point2f& Corner : OpenCVInterestPoints) { 
                    cv::circle(InputMat, Corner, 5, cv::Scalar(0, 0, 255, 255), -1); // Desenha círculos vermelhos
                }
            }
        }
        else 
        {
            UE_LOG(LogIVROpenCVBridge, Error, TEXT("IVROpenCVBridge: PixelData é inválido ou vazio. Não é possível processar features."));
        }
    }


    TArray<FString> ListWebcamDevicesNative()
    {
        TArray<FString> Devices;
        for (int32 i = 0; i < 10; ++i)
        {
            cv::VideoCapture TestCapture(i, cv::CAP_DSHOW); 
            if (TestCapture.isOpened())
            {
                FString DeviceInfo = FString::Printf(TEXT("Dispositivo %d (Res: %dx%d @ %.1fFPS)"), i,
                                                    (int32)TestCapture.get(cv::CAP_PROP_FRAME_WIDTH),
                                                    (int32)TestCapture.get(cv::CAP_PROP_FRAME_HEIGHT),
                                                    (float)TestCapture.get(cv::CAP_PROP_FPS));
                Devices.Add(DeviceInfo);
                TestCapture.release();
            }
        }
        if (Devices.Num() == 0)
        {
            Devices.Add(TEXT("Nenhuma webcam encontrada ou OpenCV não habilitado."));
        }
        return Devices;
    }
#else // WITH_OPENCV == 0
    // Forneça implementações de fallback para plataformas sem OpenCV
    void ProcessFrameAndExtractFeatures(
        uint8* PixelData,
        int32 Width,
        int32 Height,
        FTransform CameraTransform,
        float CameraFOV,
        int32 MaxCorners,
        float QualityLevel,
        float MinDistance,
        bool bDebugDrawFeatures,
        FOCV_NativeJustRTFeatures& OutFeatures // Saída das features
    )
    {
        UE_LOG(LogIVROpenCVBridge, Warning, TEXT("OpenCV não habilitado para a plataforma. ProcessFrameAndExtractFeatures é uma operação vazia (no-op)."));
        // --- INÍCIO DA CORREÇÃO: Inicialização explícita dos membros ---
        OutFeatures.JustRTInterestPoints.Empty();
        OutFeatures.BiggestPointIndex = INDEX_NONE;
        OutFeatures.SmallerPointIndex = INDEX_NONE;
        OutFeatures.NumOfQuads = 0;
        OutFeatures.NumOfRectangles = 0;
        OutFeatures.HistogramRed.Empty();
        OutFeatures.HistogramGreen.Empty();
        OutFeatures.HistogramBlue.Empty();
        // --- FIM DA CORREÇÃO ---
    }
    
    bool LoadAndResizeImage(const FString& FilePath, int32 TargetWidth, int32 TargetHeight, TArray<uint8>& OutRawData)
    {
        UE_LOG(LogIVROpenCVBridge, Warning, TEXT("OpenCV não habilitado para a plataforma. LoadAndResizeImage será implementado no fallback do ImageLoadingHelpers.cpp."));
        // Não precisamos de um fallback aqui, pois o fallback é tratado diretamente em ImageLoadingHelpers.cpp
        return false; // Retorne false para indicar que o redimensionamento OpenCV não ocorreu
    }

    TArray<FString> ListWebcamDevicesNative()
    {
        UE_LOG(LogIVROpenCVBridge, Warning, TEXT("OpenCV não habilitado para a plataforma. ListWebcamDevicesNative retorna vazio."));
        TArray<FString> Devices;
        Devices.Add(TEXT("OpenCV não habilitado."));
        return Devices;
    }
#endif // WITH_OPENCV

} // namespace IVROpenCVBridge