// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Recording/IVRSimulatedFrameSource.h"
#include "Recording/IVRRenderFrameSource.h"
#include "Recording/IVRFolderFrameSource.h"
#include "Recording/IVRVideoFrameSource.h"
#include "Recording/IVRWebcamFrameSource.h"
#include "Engine/Texture2D.h" 
#include "Engine/TextureRenderTarget2D.h" 
#include "CineCameraComponent.h" // Se ainda estiver usando

#include "IVRTypes.h"
#include "IVRFramePool.h"

#include "IVRCaptureComponent.generated.h"

class UIVRRecordingSession;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIVRRecordingStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIVRRecordingPaused);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIVRRecordingResumed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIVRRecordingStopped);

// Delegate para notificar que um frame em tempo real (agora com features) está pronto para coleta
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
     * @brief Prepara um arquivo de vídeo para gravação, transcodificando-o para um formato compatível
     *        com o OpenCV, se necessário. O processo pode levar tempo e bloquear a thread.
     * @param InSourceVideoPath O caminho completo para o vídeo original.
     * @param OutPreparedVideoPath O caminho completo onde o vídeo preparado será salvo.
     * @param bOverwrite Se true, sobrescreve o arquivo de saída se ele já existir.
     * @return O caminho completo do vídeo preparado se a transcodificação for bem-sucedida,
     *         uma string vazia caso contrário.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Video Preparation",
              meta = (DisplayName = "Prepare Video for Recording",
                      ToolTip = "Transcodes a video file to a compatible format for OpenCV capture. Can be slow.",
                      DeterminesOutputType = "OutPreparedVideoPath"))
    FString PrepareVideoForRecording(const FString& InSourceVideoPath, const FString& OutPreparedVideoPath, bool bOverwrite = true);
    /**
     * @brief Transcodifica um vídeo existente para um formato mais amplamente compatível.
     *        Útil para gerar uma versão para distribuição ao usuário final a partir de um master otimizado.
     *        Pode levar tempo e bloquear a thread.
     * @param InSourceVideoPath O caminho completo para o vídeo de entrada (ex: o Master gerado).
     * @param OutCompatibleVideoPath O caminho completo onde o vídeo compatível será salvo.
     * @param bOverwrite Se true, sobrescreve o arquivo de saída se ele já existir.
     * @param EncodingSettings As configurações de vídeo para a transcodificação (codec, bitrate, etc.).
     * @return O caminho completo do vídeo compatível se a transcodificação for bem-sucedida,
     *         uma string vazia caso contrário.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Video Export",
              meta = (DisplayName = "Export Video to Compatible Format",
                      ToolTip = "Transcodes an existing video to a more widely compatible format for end-user distribution.",
                      DeterminesOutputType = "OutCompatibleVideoPath"))
    FString ExportVideoToCompatibleFormat(const FString& InSourceVideoPath, const FString& OutCompatibleVideoPath, bool bOverwrite, const FIVR_VideoSettings& EncodingSettings);
    // Delegates para notificação de estados da gravação (Proposta 02)
    UPROPERTY(BlueprintAssignable, Category = "IVR|Recording Events")
    FOnIVRRecordingStarted OnRecordingStarted;

    UPROPERTY(BlueprintAssignable, Category = "IVR|Recording Events")
    FOnIVRRecordingPaused OnRecordingPaused;

    UPROPERTY(BlueprintAssignable, Category = "IVR|Recording Events")
    FOnIVRRecordingResumed OnRecordingResumed;

    UPROPERTY(BlueprintAssignable, Category = "IVR|Recording Events")
    FOnIVRRecordingStopped OnRecordingStopped;

    // Delegate para notificar que um frame em tempo real está pronto para coleta
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
     * @brief Processa a FIVR_JustRTFrame em uma thread de segundo plano para extrair features e deprojetá-las para 3D.
     * O resultado é adicionado de volta à FIVR_JustRTFrame antes de ser transmitido via delegate.
     * @param InOutFrame A estrutura FIVR_JustRTFrame a ser preenchida com as features. Passada por valor (cópia).
     * @param CameraTransform A transformação da câmera de captura usada para a deprojeção 3D.
     * @param CameraFOV O Campo de Visão da câmera de captura.
     * @param FramePoolInstance O pool de frames para liberação de buffers se necessário.
     * @param MaxCorners O número máximo de cantos para a goodFeaturesToTrack.
     * @param QualityLevel O nível de qualidade para a goodFeaturesToTrack.
     * @param MinDistance A distância mínima entre cantos para a goodFeaturesToTrack.
     * @param bDebugDrawFeatures Se verdadeiro, desenha as detecções na imagem.
     */
    void ProcessFrameAndFeaturesAsync(FIVR_JustRTFrame InOutFrame, FTransform CameraTransform, float CameraFOV, UIVRFramePool* FramePoolInstance, int32 MaxCorners, float QualityLevel, float MinDistance, bool bDebugDrawFeatures);
    
    /**
     * @brief Função auxiliar para realizar a deprojeção de um ponto 2D do frame para o mundo 3D.
     *        Esta é uma implementação manual para uso em threads de segundo plano, sem acesso direto
     *        a UGameplayStatics ou objetos da Render Thread.
     * @param PixelPos O ponto 2D na coordenada da imagem (relativa à ImageResolution).
     * @param CameraTransform A transformação (posição e rotação) da câmera de captura.
     * @param FOVDegrees O Campo de Visão da câmera em graus.
     * @param ImageResolution As dimensões da imagem de origem dos pixels (largura e altura da gravação).
     * @param OutWorldLocation A localização 3D no mundo onde o ponto 2D se projeta.
     * @param OutWorldDirection A direção 3D do raio de projeção a partir da câmera.
     */
    static void DeprojectPixelToWorld(
        const FVector2D& PixelPos,
        const FTransform& CameraTransform,
        float FOVDegrees,
        const FIntPoint& ImageResolution, 
        FVector& OutWorldLocation,
        FVector& OutWorldDirection);
};