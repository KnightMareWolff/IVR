// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "Recording/IVRFrameSource.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h" 
#include "RenderGraphBuilder.h"         
#include "RenderingThread.h"            
#include "RenderUtils.h"                
#include "HAL/ThreadSafeCounter.h"      // Para o mecanismo de "locked rendering"
#include "Containers/Queue.h"           // Para a fila de render requests
#include "CineCameraComponent.h"

#include "IVRRenderFrameSource.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogIVRRenderFrameSource, Log, All);

/**
 * @brief Estrutura interna para gerenciar requisies de renderizao.
 */
struct FRenderRequestInternal
{
    TSharedPtr<TArray<FColor>> ImageBuffer; // Buffer para ler FColor da GPU
    FRenderCommandFence RenderFence;        // Fence para sincronizao com a GPU

    FRenderRequestInternal() : ImageBuffer(MakeShared<TArray<FColor>>()) {}
};

/**
 * @brief Fonte de frames que captura a sada de renderizao do Unreal Engine.
 * Incorpora a lgica de fila e "locked rendering" do exemplo AIVR_FrameSource.
 */
UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "IVR Render Frame Source"))
class IVR_API UIVRRenderFrameSource : public UIVRFrameSource
{
    GENERATED_BODY()

public:
    UIVRRenderFrameSource();
    virtual void BeginDestroy() override;

    /**
     * @brief Inicializa a fonte de frames.
     * @param World O UWorld atual.
     * @param Settings As configuraes de vdeo.
     * @param InFramePool O pool de frames.
     * @param InVideoCaptureComponent O SceneCaptureComponent2D a ser usado para a captura.
     */
    virtual void Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool) override;
    
    // Overload para aceitar um VideoCaptureComponent existente
    void Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool, USceneCaptureComponent2D* InVideoCaptureComponent);

    virtual void Shutdown() override;
    virtual void StartCapture() override;
    virtual void StopCapture() override;

    /**
     * @brief Processa a fila de requisies de renderizao pendentes.
     * Esta funo deve ser chamada periodicamente na Game Thread (ex: do TickComponent do IVRCaptureComponent).
     */
    void ProcessRenderQueue();

    // NOVO: Getter para o RenderTarget interno.
    UTextureRenderTarget2D* GetRenderTarget() const { return VideoRenderTarget; } 

protected:

    UPROPERTY(Transient)
    USceneCaptureComponent2D* VideoCaptureComponent; 

    UPROPERTY(Transient)
    UTextureRenderTarget2D* VideoRenderTarget;

    FDelegateHandle OnBackBufferReadyToPresentHandle;

    // Fila para requisies de renderizao (render thread -> game thread)
    TQueue<TSharedPtr<FRenderRequestInternal>> RenderRequestQueue;
    int32 RReqQueueCounter = 0;

    // Flag para controlar a cadncia de captura da Render Thread (similar ao IVR_LockedRendering)
    // 0 = Locked (no captura), 1 = Unlocked (pode capturar)
    FThreadSafeCounter bCanCaptureNextFrame; 

    /**
     * @brief Callback que  chamado na Render Thread quando o back buffer est pronto.
     * Ir disparar a leitura do RenderTarget e enfileirar a requisio.
     */
    void OnBackBufferReady(SWindow& SlateWindow, const FTextureRHIRef& BackBuffer);

    /**
     * @brief Converte dados RGBA (FColor) para BGRA (uint8) e copia para o buffer do pool.
     * @param InColors O array de FColors a ser convertido.
     * @param OutBuffer O TArray<uint8> de sada onde os dados BGRA sero copiados.
     */
    void ConvertRgbaToBgraAndCopyToBuffer(const TArray<FColor>& InColors, TArray<uint8>& OutBuffer);
};

