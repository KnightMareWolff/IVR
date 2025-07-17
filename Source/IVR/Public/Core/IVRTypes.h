// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h" // Necess�rio para UTextureRenderTarget2D
#include "Engine/Texture2D.h"             // Necess�rio para UTexture2D
#include "IVRTypes.generated.h"

// NOVO: Enum para definir o tipo de fonte de frames
UENUM(BlueprintType)
enum class EIVRFrameSourceType : uint8
{
    Simulated       UMETA(DisplayName = "Simulated Frames"),
    RenderTarget    UMETA(DisplayName = "Render Target Capture"),
    Folder          UMETA(DisplayName = "Image Folder"),
    VideoFile       UMETA(DisplayName = "Video File"),
    Webcam          UMETA(DisplayName = "Webcam")
};

USTRUCT(BlueprintType)
struct IVR_API FIVR_VideoSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings")
    int32 Width = 1920;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings")
    int32 Height = 1080;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings")
    float FPS = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings")
    FString Codec = TEXT("H264");// Ex: h264, vp9, etc.

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings")
    int32 Bitrate = 5000000; // Em bps (bits por segundo), ex: 5 Mbps

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings")
    FString PixelFormat = TEXT("bgra"); // FFmpeg pixel format (e.g., bgra for raw frame input)

    // NOVO: Seleo do tipo de fonte de frames
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings")
    EIVRFrameSourceType FrameSourceType = EIVRFrameSourceType::RenderTarget; // Default para captura real

    // NOVO: Categoria e flag para JustRTCapture
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|JustRTCapture",
        meta = (DisplayName = "Enable Real-Time Frame Output", ToolTip = "Quando verdadeiro, os frames capturados so disponibilizados em tempo real via delegate, em vez de serem enviados para gravao FFmpeg. Ideal para integrao com UI ou manipulao de pixels."))
    bool bEnableRTFrames = false;

    // NOVO: Ator a ser seguido pela cmera
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Camera Follow")
    class AActor* IVR_FollowActor = nullptr;

    // --- Parmetros para UIVRFolderFrameSource ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Folder Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Folder", EditConditionHides))
    FString IVR_FramesFolder;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Folder Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Folder", EditConditionHides))
    float IVR_FolderPlaybackFPS = 30.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Folder Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Folder", EditConditionHides))
    bool IVR_LoopFolderPlayback = true;

    // --- Parmetros para UIVRRenderFrameSource (Configuraes de Cmera Cinemtica) ---
    // NOTA: Estas configuraes sero aplicadas ao USceneCaptureComponent2D ou a um UCineCameraComponent
    // se o UIVRCaptureComponent estiver anexado a um ator com um CineCameraComponent.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Render Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::RenderTarget", EditConditionHides))
    float IVR_CineCameraFOV = 90.0f; // Campo de Viso
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Render Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::RenderTarget", EditConditionHides))
    float IVR_CineCameraFocalLength = 50.0f; // Distncia Focal (apenas para UCineCameraComponent)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Render Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::RenderTarget", EditConditionHides))
    float IVR_CineCameraAperture = 2.8f; // Abertura (apenas para UCineCameraComponent)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Render Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::RenderTarget", EditConditionHides))
    float IVR_CineCameraFocusDistance = 1000.0f; // Distncia de Foco (apenas para UCineCameraComponent)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Render Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::RenderTarget", EditConditionHides))
    bool IVR_EnableCinematicPostProcessing = true; // Flag para habilitar ou desabilitar ps-processamento da cmera

    // --- Parmetros para UIVRVideoFrameSource (Arquivo de Vdeo Generalizado) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Video File Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::VideoFile", EditConditionHides))
    FString IVR_VideoFilePath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Video File Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::VideoFile", EditConditionHides))
    float IVR_VideoPlaybackSpeed = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Video File Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::VideoFile", EditConditionHides))
    bool IVR_LoopVideoPlayback = true;

    // --- Parmetros para UIVRWebcamFrameSource ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Webcam Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Webcam", EditConditionHides))
    int32 IVR_WebcamIndex = 0; // 0 para padro, 1 para segunda, etc.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Webcam Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Webcam", EditConditionHides))
    FVector2D IVR_WebcamResolution = FVector2D(1280.0f, 720.0f); // Resoluo desejada (largura x altura)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Webcam Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Webcam", EditConditionHides))
    float IVR_WebcamFPS = 30.0f;

    // Cor de tintura para frames gerados pela fonte (ex: Simulated)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings")
    FLinearColor IVR_FrameTint = FLinearColor::White; 
    // Padrao randomico de tempo para alterao da cor
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Simulated Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Simulated", EditConditionHides))
    bool IVR_UseRandomPattern = true;

    // --- NOVOS PAR�METROS PARA FEATURE EXTRACTION ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Feature Extraction",
        meta = (DisplayName = "Enable Debug Draw Features", ToolTip = "Se verdadeiro, desenha caixas de detec��o na sa�da de v�deo em tempo real."))
    bool IVR_DebugDrawFeatures = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Feature Extraction",
        meta = (DisplayName = "Max Corners (goodFeaturesToTrack)", ClampMin = "1", UIMin = "1", ToolTip = "N�mero m�ximo de cantos a serem detectados pela goodFeaturesToTrack."))
    int32 IVR_GFTT_MaxCorners = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Feature Extraction",
        meta = (DisplayName = "Quality Level (goodFeaturesToTrack)", ClampMin = "0.001", ClampMax = "1.0", UIMin = "0.001", UIMax = "1.0", ToolTip = "N�vel de qualidade para cantos, como uma fra��o da maior resposta de canto."))
    float IVR_GFTT_QualityLevel = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Feature Extraction",
        meta = (DisplayName = "Min Distance (goodFeaturesToTrack)", ClampMin = "1", UIMin = "1", ToolTip = "Dist�ncia euclidiana m�nima entre os cantos detectados."))
    float IVR_GFTT_MinDistance = 10.0f;
};

// ... (Restante do arquivo IVRTypes.h permanece inalterado) ...
// NOVO: Estrutura para um �nico ponto de interesse com informa��es detalhadas
USTRUCT(BlueprintType)
struct IVR_API FIVR_JustRTPoint
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    FVector2D Point2D; // Coordenada 2D na resolu��o da grava��o

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    FVector Point3D; // Localiza��o no mundo 3D ap�s a deproje��o da c�mera de captura

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    FVector Direction; // Dire��o no mundo 3D (vetor do raio de proje��o)

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    FVector2D Size2D; // Largura, Altura do bounding box detectado (para quads/ret�ngulos)

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    float Angle; // Orienta��o em graus (para quads/ret�ngulos)

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    bool IsQuad; // True se for quadrado (ou pr�ximo disso), false se for ret�ngulo
};

// NOVO: Estrutura para encapsular todas as caracter�sticas extra�das
USTRUCT(BlueprintType)
struct IVR_API FIVR_JustRTFeatures
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    TArray<FIVR_JustRTPoint> JustRTInterestPoints; // Array de todos os pontos de interesse

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    int32 BiggestPointIndex = INDEX_NONE; // �ndice do maior ponto de interesse (baseado na �rea 2D)

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    int32 SmallerPointIndex = INDEX_NONE; // �ndice do menor ponto de interesse (baseado na �rea 2D)

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    int32 NumOfQuads = 0; // Contagem de quadril�teros detectados

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    int32 NumOfRectangles = 0; // Contagem de ret�ngulos (n�o-quadrados) detectados

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    TArray<float> HistogramRed; // Histograma normalizado (0-1) do canal Vermelho

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    TArray<float> HistogramGreen; // Histograma normalizado (0-1) do canal Verde

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    TArray<float> HistogramBlue; // Histograma normalizado (0-1) do canal Azul
};


// Estrutura para os dados de sada de frames em tempo real
USTRUCT(BlueprintType)
struct IVR_API FIVR_JustRTFrame
{
    GENERATED_BODY()

    // Referncia direta ao RenderTarget interno usado pelo UIVRRenderFrameSource.
    UPROPERTY(BlueprintReadOnly, Category = "JustRTFrame Output")
    UTextureRenderTarget2D* RenderTarget; 

    // UTexture2D dinamicamente atualizada com os pixels do frame.
    UPROPERTY(BlueprintReadOnly, Category = "JustRTFrame Output")
    UTexture2D* LiveTexture; 

    // Dados brutos de pixel do frame (BGRA).
    UPROPERTY(BlueprintReadOnly, Category = "JustRTFrame Output")
    TArray<uint8> RawDataBuffer; 

    UPROPERTY(BlueprintReadOnly, Category = "JustRTFrame Output")
    int32 Width;

    UPROPERTY(BlueprintReadOnly, Category = "JustRTFrame Output")
    int32 Height;

    UPROPERTY(BlueprintReadOnly, Category = "JustRTFrame Output")
    float Timestamp; // Tempo de captura do frame

    UPROPERTY(BlueprintReadOnly, Category = "JustRTFrame Output") // Tintura aplicada pelo IVRCaptureComponent
    FLinearColor DisplayTint;

    UPROPERTY(BlueprintReadOnly, Category = "JustRTFrame Output") // Tintura original da fonte (FIVR_VideoSettings)
    FLinearColor SourceFrameTint; 

    // NOVO: A estrutura de caracter�sticas aninhada!
    UPROPERTY(BlueprintReadOnly, Category = "JustRTFrame Output")
    FIVR_JustRTFeatures Features;
};


USTRUCT(BlueprintType)
struct IVR_API FIVR_TakeInfo
{
    GENERATED_BODY()

    int32 TakeNumber = 0;

    float Duration = 0.0f;

    FDateTime StartTime;

    FDateTime EndTime;

    FString FilePath;

    FString SessionID; 
};

USTRUCT(BlueprintType)
struct IVR_API FIVR_VideoFrame
{
    GENERATED_BODY()

    TSharedPtr<TArray<uint8>> RawDataPtr; 

    int32 Width; // Width of the frame

    int32 Height; // Height of the frame

    float Timestamp; // Time when the frame was generated/captured (in seconds)

    // Construtor padro
    FIVR_VideoFrame()
        : Width(0)
        , Height(0)
        , Timestamp(0.0f)
    {}

    // Construtor para facilitar a criao
    FIVR_VideoFrame(int32 InWidth, int32 InHeight, float InTimestamp)
        : Width(InWidth)
        , Height(InHeight)
        , Timestamp(InTimestamp)
    {}
};

/**
 * Estrutura para configuraes globais de Named Pipes.
 * Esta estrutura pode ser usada para configurar Named Pipes criados ou acessados pelo plugin IVR.
 */
USTRUCT(BlueprintType)
struct IVR_API FIVR_PipeSettings
{
    GENERATED_BODY()

    /** O nome base do pipe. O path completo ser construdo usando \.\pipe\<BasePipeName> */
    FString BasePipeName = TEXT("UnrealRecordingPipe");

    /**
     * Se true, esta instncia ser o "servidor" do pipe (o criador que aguarda conexes).
     * Se false, esta instncia tentar se conectar a um pipe existente (como cliente).
     */
    bool bIsServerPipe = true;

    /**
     * Se true, o pipe permitir comunicao bidirecional (leitura e escrita).
     * Se false, o pipe permitir apenas acesso de sada (escrita) para o servidor.
     * (Equivale a PIPE_ACCESS_DUPLEX se true, ou PIPE_ACCESS_OUTBOUND se false, para o servidor).
     */
    bool bDuplexAccess = false;

    /**
     * Se true, as operaes de leitura/escrita no pipe sero bloqueantes (sincronizadas).
     * A thread que chama a operao aguardar at que ela seja concluda.
     * (Equivale a PIPE_WAIT se true, ou PIPE_NOWAIT se false).
     */
    bool bBlockingMode = true;

    /**
     * Se true, o pipe operar em modo de mensagem, onde dados so lidos/escritos como blocos discretos.
     * Se false, o pipe operar em modo de byte, onde dados so lidos/escritos como um fluxo contnuo.
     * (Equivale a PIPE_TYPE_MESSAGE se true, ou PIPE_TYPE_BYTE se false).
     */
    bool bMessageMode = false;

    /**
     * O nmero mximo de instncias que podem ser criadas para este pipe.
     * Defina como -1 para PIPE_UNLIMITED_INSTANCES (limitado a 255 pelo Windows).
     */
    int32 MaxInstances = -1;

    /**
     * O tamanho sugerido do buffer de sada do pipe em bytes.
     * O Windows pode ajustar este valor.
     */
    int32 OutBufferSize = 65536; // 64 KB

    /**
     * O tamanho sugerido do buffer de entrada do pipe em bytes.
     * O Windows pode ajustar este valor.
     */
    int32 InBufferSize = 65536; // 64 KB
};

