// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TimerManager.h" // Para FTimerHandle e FTimerManager
#include "Core/IVRTypes.h" // Para FIVR_VideoFrame
#include "Recording/IVRFrameSource.h" // NOVO: Deriva da classe base


#include "IVRSimulatedFrameSource.generated.h"


UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="IVR Simulated Frame Source"))
class IVR_API UIVRSimulatedFrameSource  : public UIVRFrameSource // NOVO: Deriva de UIVRFrameSource
{
    GENERATED_BODY()

public:
    UIVRSimulatedFrameSource();
     virtual void BeginDestroy() override;

    /**
     * @brief Inicializa o gerador de frames simulados.
     * @param World O ponteiro para o UWorld, necessrio para acessar o TimerManager.
     * @param Settings As configuraes de vdeo, incluindo dimenses e FPS.
     * @param InFramePool O pool de frames para adquirir e liberar buffers.
     */
    //UFUNCTION(BlueprintCallable, Category = "IVR|SimulatedFrames")
    virtual void Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool) override; // Implementa base
    
    // NOVO: Overload para incluir a tintura
    void Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool, FLinearColor InFrameTint); // NOVO


    /**
     * @brief Inicia a gerao de frames simulados.
     */
    //UFUNCTION(BlueprintCallable, Category = "IVR|SimulatedFrames")
    virtual void StartCapture() override; // Implementa base

    /**
     * @brief Para a gerar de frames simulados.
     */
    //UFUNCTION(BlueprintCallable, Category = "IVR|SimulatedFrames")
    virtual void Shutdown() override; // Implementa base
    
    //UFUNCTION(BlueprintCallable, Category = "IVR|SimulatedFrames")
    virtual void StopCapture() override; // Implementa base

protected:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|SimulatedFrames")
    float FrameRate = 30.0f; // Frequncia de gerao de frames em FPS

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|SimulatedFrames")
    int32 FrameWidth = 1920; // Largura padro do frame simulado

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVR|SimulatedFrames")
    int32 FrameHeight = 1080; // Altura padro do frame simulado

private:
    FTimerHandle FrameGenerationTimerHandle; // Handle para o timer de gerao de frames

    float ElapsedTime = 0.0f; // Tempo total desde o incio da simulao
    int64 FrameCount = 0;     // Contador de frames gerados

    FLinearColor FrameTint; // NOVO: Cor de tintura para o frame simulado

    bool IVR_UseRandomPattern = true;
    /**
     * @brief Funcao de callback do timer para gerar e notificar um novo frame simulado.
     */
    UFUNCTION()
    void GenerateSimulatedFrame();

    /**
     * @brief Gera um padro de cor simples para o frame simulado.
     * @param InFrame Um ponteiro para o FIVR_VideoFrame para preencher.
     */
    void FillSimulatedFrame(FIVR_VideoFrame& InFrame);
};

