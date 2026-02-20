// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once
#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h" // Necessário para UTextureRenderTarget2D
#include "Engine/Texture2D.h"             // Necessário para UTexture2D
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
struct IVRCORE_API FIVR_VideoSettings
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

    // NOVO: Seleção do tipo de fonte de frames
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings")
    EIVRFrameSourceType FrameSourceType = EIVRFrameSourceType::RenderTarget; // Default para captura real

    // NOVO: Categoria e flag para JustRTCapture
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|JustRTCapture",
        meta = (DisplayName = "Enable Real-Time Frame Output", ToolTip = "Quando verdadeiro, os frames capturados são disponibilizados em tempo real via delegate, em vez de serem enviados para gravação FFmpeg. Ideal para integração com UI ou manipulação de pixels."))
    bool bEnableRTFrames = false;

    // NOVO: Ator a ser seguido pela câmera
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Camera Follow")
    class AActor* IVR_FollowActor = nullptr;

    // --- Parâmetros para UIVRFolderFrameSource ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Folder Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Folder", EditConditionHides))
    FString IVR_FramesFolder;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Folder Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Folder", EditConditionHides))
    float IVR_FolderPlaybackFPS = 30.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Folder Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Folder", EditConditionHides))
    bool IVR_LoopFolderPlayback = true;

    // --- Parâmetros para UIVRRenderFrameSource (Configurações de Câmera Cinemática) ---
    // NOTA: Estas configurações serão aplicadas ao USceneCaptureComponent2D ou a um UCineCameraComponent
    // se o UIVRCaptureComponent estiver anexado a um ator com um CineCameraComponent.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Render Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::RenderTarget", EditConditionHides))
    float IVR_CineCameraFOV = 90.0f; // Campo de Visão
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Render Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::RenderTarget", EditConditionHides))
    float IVR_CineCameraFocalLength = 50.0f; // Distância Focal (apenas para UCineCameraComponent)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Render Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::RenderTarget", EditConditionHides))
    float IVR_CineCameraAperture = 2.8f; // Abertura (apenas para UCineCameraComponent)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Render Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::RenderTarget", EditConditionHides))
    float IVR_CineCameraFocusDistance = 1000.0f; // Distância de Foco (apenas para UCineCameraComponent)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Render Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::RenderTarget", EditConditionHides))
    bool IVR_EnableCinematicPostProcessing = true; // Flag para habilitar ou desabilitar pós-processamento da câmera

    // --- Parâmetros para UIVRVideoFrameSource (Arquivo de Vídeo Generalizado) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Video File Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::VideoFile", EditConditionHides))
    FString IVR_VideoFilePath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Video File Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::VideoFile", EditConditionHides))
    float IVR_VideoPlaybackSpeed = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Video File Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::VideoFile", EditConditionHides))
    bool IVR_LoopVideoPlayback = true;

    // --- Parâmetros para UIVRWebcamFrameSource ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Webcam Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Webcam", EditConditionHides))
    int32 IVR_WebcamIndex = 0; // 0 para padrão, 1 para segunda, etc.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Webcam Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Webcam", EditConditionHides))
    FVector2D IVR_WebcamResolution = FVector2D(1280.0f, 720.0f); // Resolução desejada (largura x altura)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Webcam Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Webcam", EditConditionHides))
    float IVR_WebcamFPS = 30.0f;

    // Cor de tintura para frames gerados pela fonte (ex: Simulated)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings")
    FLinearColor IVR_FrameTint = FLinearColor::White; 
    // Padrão randômico de tempo para alteração da cor
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Simulated Frame Source",
        meta = (EditCondition = "FrameSourceType == EIVRFrameSourceType::Simulated", EditConditionHides))
    bool IVR_UseRandomPattern = true;

    // --- NOVOS PARÂMETROS PARA FEATURE EXTRACTION ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Feature Extraction",
        meta = (DisplayName = "Enable Debug Draw Features", ToolTip = "Se verdadeiro, desenha caixas de detecção na saída de vídeo em tempo real."))
    bool IVR_DebugDrawFeatures = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Feature Extraction",
        meta = (DisplayName = "Max Corners (goodFeaturesToTrack)", ClampMin = "1", UIMin = "1", ToolTip = "Número máximo de cantos a serem detectados pela goodFeaturesToTrack."))
    int32 IVR_GFTT_MaxCorners = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Feature Extraction",
        meta = (DisplayName = "Quality Level (goodFeaturesToTrack)", ClampMin = "0.001", ClampMax = "1.0", UIMin = "0.001", UIMax = "1.0", ToolTip = "Nível de qualidade para cantos, como uma fração da maior resposta de canto."))
    float IVR_GFTT_QualityLevel = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Feature Extraction",
        meta = (DisplayName = "Min Distance (goodFeaturesToTrack)", ClampMin = "1", UIMin = "1", ToolTip = "Distância euclidiana mínima entre os cantos detectados."))
    float IVR_GFTT_MinDistance = 10.0f;

    // --- INÍCIO DA ALTERAÇÃO: NOVAS PROPRIEDADES PARA CUSTOMIZAÇÃO DE NOME DE ARQUIVO ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Output",
              meta = (DisplayName = "Custom Output Folder Name", ToolTip = "Nome de uma subpasta opcional dentro de Project/Saved/Recordings. Deixe vazio para usar a pasta padrão 'Recordings'."))
    FString IVR_CustomOutputFolderName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video Settings|Output",
              meta = (DisplayName = "Custom Output Base Filename", ToolTip = "Nome base opcional para o arquivo de saída (sem extensão). Será combinado com timestamp e ID de sessão. Deixe vazio para o padrão com timestamp."))
    FString IVR_CustomOutputBaseFilename;
    // --- FIM DA ALTERAÇÃO ---
};

// ... (Restante do arquivo IVRTypes.h permanece inalterado) ...
// NOVO: Estrutura para um único ponto de interesse com informações detalhadas
USTRUCT(BlueprintType)
struct IVRCORE_API FIVR_JustRTPoint
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    FVector2D Point2D; // Coordenada 2D na resolução da gravação

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    FVector Point3D; // Localização no mundo 3D após a deprojeção da câmera de captura

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    FVector Direction; // Direção no mundo 3D (vetor do raio de projeção)

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    FVector2D Size2D; // Largura, Altura do bounding box detectado (para quads/retângulos)

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    float Angle; // Orientação em graus (para quads/retângulos)

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features|Point")
    bool IsQuad; // True se for quadrado (ou próximo disso), false se for retângulo
};

// NOVO: Estrutura para encapsular todas as características extraídas
USTRUCT(BlueprintType)
struct IVRCORE_API FIVR_JustRTFeatures
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    TArray<FIVR_JustRTPoint> JustRTInterestPoints; // Array de todos os pontos de interesse

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    int32 BiggestPointIndex = INDEX_NONE; // Índice do maior ponto de interesse (baseado na área 2D)

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    int32 SmallerPointIndex = INDEX_NONE; // Índice do menor ponto de interesse (baseado na área 2D)

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    int32 NumOfQuads = 0; // Contagem de quadriláteros detectados

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    int32 NumOfRectangles = 0; // Contagem de retângulos (não-quadrados) detectados

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    TArray<float> HistogramRed; // Histograma normalizado (0-1) do canal Vermelho

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    TArray<float> HistogramGreen; // Histograma normalizado (0-1) do canal Verde

    UPROPERTY(BlueprintReadOnly, Category = "IVR|Features")
    TArray<float> HistogramBlue; // Histograma normalizado (0-1) do canal Azul
};


// Estrutura para os dados de saída de frames em tempo real
USTRUCT(BlueprintType)
struct IVRCORE_API FIVR_JustRTFrame
{
    GENERATED_BODY()

    // Referência direta ao RenderTarget interno usado pelo UIVRRenderFrameSource.
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

    // NOVO: A estrutura de características aninhada!
    UPROPERTY(BlueprintReadOnly, Category = "JustRTFrame Output")
    FIVR_JustRTFeatures Features;
};


USTRUCT(BlueprintType)
struct IVRCORE_API FIVR_TakeInfo
{
    GENERATED_BODY()

    int32 TakeNumber = 0;

    float Duration = 0.0f;

    FDateTime StartTime;

    FDateTime EndTime;

    FString FilePath;

    FString SessionID; 

    // --- INÍCIO DA ALTERAÇÃO: NOVAS PROPRIEDADES PARA CUSTOMIZAÇÃO DE NOME DE ARQUIVO ---
    FString CustomOutputFolderName;
    FString CustomOutputBaseFilename;
    // --- FIM DA ALTERAÇÃO ---
};

USTRUCT(BlueprintType)
struct IVRCORE_API FIVR_VideoFrame
{
    GENERATED_BODY()

    TSharedPtr<TArray<uint8>> RawDataPtr; 

    int32 Width; // Width of the frame

    int32 Height; // Height of the frame

    float Timestamp; // Time when the frame was generated/captured (in seconds)

    // Construtor padrão
    FIVR_VideoFrame()
        : Width(0)
        , Height(0)
        , Timestamp(0.0f)
    {}

    // Construtor para facilitar a criação
    FIVR_VideoFrame(int32 InWidth, int32 InHeight, float InTimestamp)
        : Width(InWidth)
        , Height(InHeight)
        , Timestamp(InTimestamp)
    {}
};

/**
 * Estrutura para configurações globais de Named Pipes.
 * Esta estrutura pode ser usada para configurar Named Pipes criados ou acessados pelo plugin IVR.
 */
USTRUCT(BlueprintType)
struct IVRCORE_API FIVR_PipeSettings
{
    GENERATED_BODY()

    /** O nome base do pipe. O path completo será construído usando \.\pipe\<BasePipeName> */
    FString BasePipeName = TEXT("UnrealRecordingPipe");

    /**
     * Se true, esta instância será o "servidor" do pipe (o criador que aguarda conexões).
     * Se false, esta instância tentará se conectar a um pipe existente (como cliente).
     */
    bool bIsServerPipe = true;

    /**
     * Se true, o pipe permitirá comunicação bidirecional (leitura e escrita).
     * Se false, o pipe permitirá apenas acesso de saída (escrita) para o servidor.
     * (Equivale a PIPE_ACCESS_DUPLEX se true, ou PIPE_ACCESS_OUTBOUND se false, para o servidor).
     */
    bool bDuplexAccess = false;

    /**
     * Se true, as operações de leitura/escrita no pipe serão bloqueantes (sincronizadas).
     * A thread que chama a operação aguardará até que ela seja concluída.
     * (Equivale a PIPE_WAIT se true, ou PIPE_NOWAIT se false).
     */
    bool bBlockingMode = true;

    /**
     * Se true, o pipe operará em modo de mensagem, onde dados são lidos/escritos como blocos discretos.
     * Se false, o pipe operará em modo de byte, onde dados são lidos/escritos como um fluxo contínuo.
     * (Equivale a PIPE_TYPE_MESSAGE se true, ou PIPE_TYPE_BYTE se false).
     */
    bool bMessageMode = false;

    /**
     * O número máximo de instâncias que podem ser criadas para este pipe.
     * Defina como -1 para PIPE_UNLIMITED_INSTANCES (limitado a 255 pelo Windows).
     */
    int32 MaxInstances = -1;

    /**
     * O tamanho sugerido do buffer de saída do pipe em bytes.
     * O Windows pode ajustar este valor.
     */
    int32 OutBufferSize = 65536; // 64 KB

    /**
     * O tamanho sugerido do buffer de entrada do pipe em bytes.
     * O Windows pode ajustar este valor.
     */
    int32 InBufferSize = 65536; // 64 KB
};
