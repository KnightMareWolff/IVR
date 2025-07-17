// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Core/IVRTypes.h"

#include "Core/IVRFramePool.h"
#include "Recording/IVRFrameSource.h"
#include "Recording/IVRSimulatedFrameSource.h"
#include "Recording/IVRRenderFrameSource.h"
#include "Recording/IVRFolderFrameSource.h"
#include "Recording/IVRVideoFrameSource.h"
#include "Recording/IVRWebcamFrameSource.h"

#include "Engine/Texture2D.h" 
#include "Engine/TextureRenderTarget2D.h" 

#include "IVRCaptureComponent.generated.h"

class UIVRRecordingSession;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIVRRecordingStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIVRRecordingPaused);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIVRRecordingResumed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIVRRecordingStopped);

// Delegate para notificar que um frame em tempo real (agora com features) est pronto para coleta
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRealTimeFrameReady, const FIVR_JustRTFrame&, FrameOutput);


UCLASS(ClassGroup=(IVR), meta=(BlueprintSpawnableComponent))
class IVR_API UIVRCaptureComponent : public USceneComponent 
{
    GENERATED_BODY()

public:
    UIVRCaptureComponent();
    virtual void BeginDestroy() override;

    // Recording Control
    UFUNCTION(BlueprintCallable, Category = "IVR")
    void StartRecording();

    UFUNCTION(BlueprintCallable, Category = "IVR")
    void StopRecording();

    UFUNCTION(BlueprintCallable, Category = "IVR")
    void PauseTake();

    UFUNCTION(BlueprintCallable, Category = "IVR")
    void ResumeTake();

    UFUNCTION(BlueprintPure, Category = "IVR")
    bool IsRecording() const { return bIsRecording; }

    // Settings
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video")
    FIVR_VideoSettings VideoSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Takes")
    float TakeDuration = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Takes")
    bool bAutoStartNewTake = true;

    /**
     * @brief Prepara um arquivo de vdeo para gravao, transcodificando-o para um formato compatvel
     *        com o OpenCV, se necessrio. O processo pode levar tempo e bloquear a thread.
     * @param InSourceVideoPath O caminho completo para o vdeo original.
     * @param OutPreparedVideoPath O caminho completo onde o vdeo preparado ser salvo.
     * @param bOverwrite Se true, sobrescreve o arquivo de sada se ele j existir.
     * @return O caminho completo do vdeo preparado se a transcodificao for bem-sucedida,
     *         uma string vazia caso contrrio.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Video Preparation",
              meta = (DisplayName = "Prepare Video for Recording",
                      ToolTip = "Transcodes a video file to a compatible format for OpenCV capture. Can be slow.",
                      DeterminesOutputType = "OutPreparedVideoPath"))
    FString PrepareVideoForRecording(const FString& InSourceVideoPath, const FString& OutPreparedVideoPath, bool bOverwrite = true);


    /**
     * @brief Transcodifica um vdeo existente para um formato mais amplamente compatvel.
     *        til para gerar uma verso para distribuio ao usurio final a partir de um master otimizado.
     *        Pode levar tempo e bloquear a thread.
     * @param InSourceVideoPath O caminho completo para o vdeo de entrada (ex: o Master gerado).
     * @param OutCompatibleVideoPath O caminho completo onde o vdeo compatvel ser salvo.
     * @param bOverwrite Se true, sobrescreve o arquivo de sada se ele j existir.
     * @param EncodingSettings As configuraes de vdeo para a transcodificao (codec, bitrate, etc.).
     * @return O caminho completo do vdeo compatvel se a transcodificao for bem-sucedida,
     *         uma string vazia caso contrrio.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Video Export",
              meta = (DisplayName = "Export Video to Compatible Format",
                      ToolTip = "Transcodes an existing video to a more widely compatible format for end-user distribution.",
                      DeterminesOutputType = "OutCompatibleVideoPath"))
    FString ExportVideoToCompatibleFormat(const FString& InSourceVideoPath, const FString& OutCompatibleVideoPath, bool bOverwrite, const FIVR_VideoSettings& EncodingSettings);

    // Delegates para notificao de estados da gravao (Proposta 02)
    UPROPERTY(BlueprintAssignable, Category = "IVR|Recording Events")
    FOnIVRRecordingStarted OnRecordingStarted;

    UPROPERTY(BlueprintAssignable, Category = "IVR|Recording Events")
    FOnIVRRecordingPaused OnRecordingPaused;

    UPROPERTY(BlueprintAssignable, Category = "IVR|Recording Events")
    FOnIVRRecordingResumed OnRecordingResumed;

    UPROPERTY(BlueprintAssignable, Category = "IVR|Recording Events")
    FOnIVRRecordingStopped OnRecordingStopped;

    // Delegate para notificar que um frame em tempo real est pronto para coleta
    UPROPERTY(BlueprintAssignable, Category = "IVR|JustRTCapture Events")
    FOnRealTimeFrameReady OnRealTimeFrameReady;

    // Cor de tintura para o modo de captura em tempo real (JustRTCapture)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|Video|JustRTCapture",
        meta = (EditCondition = "bEnableRTFrames", EditConditionHides, DisplayName = "Real-Time Display Tint"))
    FLinearColor RTDisplayTint = FLinearColor::White; 
    
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

    bool bIsRecording = false;
    
    UPROPERTY(Transient)
    UIVRRecordingSession* CurrentSession = nullptr;
    
    float CurrentTakeTime = 0.0f;
    int32 CurrentTakeNumber = 0;
    
    float RecordingStartTimeSeconds = 0.0f;

    UPROPERTY(Transient) 
    int32 ActualFrameWidth = 0;
    UPROPERTY(Transient)
    int32 ActualFrameHeight = 0;

    void StartNewTake();
    void EndCurrentTake();

    UPROPERTY(Transient)
    USceneCaptureComponent2D* OwnedVideoCaptureComponent;

    UPROPERTY(Transient)
    UIVRFramePool* FramePool;
    const int32 FramePoolSize = 60;

    UPROPERTY(Transient)
    UIVRFrameSource* CurrentFrameSource;

    UFUNCTION()
    void OnFrameAcquiredFromSource(FIVR_VideoFrame Frame);
    
    UPROPERTY(Transient)
    UTexture2D* RealTimeOutputTexture2D; 

    void UpdateTextureFromRawData(UTexture2D* Texture, const TArray<uint8>& RawData, int32 InWidth, int32 InHeight);

    /**
     * @brief Processa a FIVR_JustRTFrame em uma thread de segundo plano para extrair features e deprojet�-las para 3D.
     * O resultado � adicionado de volta � FIVR_JustRTFrame antes de ser transmitido via delegate.
     * @param InOutFrame A estrutura FIVR_JustRTFrame a ser preenchida com as features. Passada por valor (c�pia).
     * @param CameraTransform A transforma��o da c�mera de captura usada para a deproje��o 3D.
     * @param CameraFOV O Campo de Vis�o da c�mera de captura.
     * @param FramePoolInstance O pool de frames para libera��o de buffers se necess�rio.
     * @param MaxCorners O n�mero m�ximo de cantos para a goodFeaturesToTrack.
     * @param QualityLevel O n�vel de qualidade para a goodFeaturesToTrack.
     * @param MinDistance A dist�ncia m�nima entre cantos para a goodFeaturesToTrack.
     * @param bDebugDrawFeatures Se verdadeiro, desenha as detec��es na imagem.
     */
    void ProcessFrameAndFeaturesAsync(FIVR_JustRTFrame InOutFrame, FTransform CameraTransform, float CameraFOV, UIVRFramePool* FramePoolInstance, int32 MaxCorners, float QualityLevel, float MinDistance, bool bDebugDrawFeatures);
    
    /**
     * @brief Fun��o auxiliar para realizar a deproje��o de um ponto 2D do frame para o mundo 3D.
     *        Esta � uma implementa��o manual para uso em threads de segundo plano, sem acesso direto
     *        a UGameplayStatics ou objetos da Render Thread.
     * @param PixelPos O ponto 2D na coordenada da imagem (relativa � ImageResolution).
     * @param CameraTransform A transforma��o (posi��o e rota��o) da c�mera de captura.
     * @param FOVDegrees O Campo de Vis�o da c�mera em graus.
     * @param ImageResolution As dimens�es da imagem de origem dos pixels (largura e altura da grava��o).
     * @param OutWorldLocation A localiza��o 3D no mundo onde o ponto 2D se projeta.
     * @param OutWorldDirection A dire��o 3D do raio de proje��o a partir da c�mera.
     */
    static void DeprojectPixelToWorld(
        const FVector2D& PixelPos,
        const FTransform& CameraTransform,
        float FOVDegrees,
        const FIntPoint& ImageResolution, 
        FVector& OutWorldLocation,
        FVector& OutWorldDirection);
};


