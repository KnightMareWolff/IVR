// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/IVRTypes.h"
#include "Core/IVRFramePool.h" // Inclua o pool de frames

#include "IVRFrameSource.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogIVRFrameSource, Log, All);

// NOVO: Delegate multicast para notificar sobre novos frames adquiridos
DECLARE_MULTICAST_DELEGATE_OneParam(FOnFrameAcquiredDelegate, FIVR_VideoFrame /*Frame*/);

/**
 * @brief Classe base abstrata para fontes de frames. Define a interface polim�rfica.
 */
UCLASS(Blueprintable, Abstract)
class IVR_API UIVRFrameSource : public UObject
{
    GENERATED_BODY()

public:
    UIVRFrameSource();
    virtual void BeginDestroy() override;

    /**
     * @brief Inicializa a fonte de frames. Deve ser implementada por classes derivadas.
     * @param World O UWorld atual, necess�rio para gerenciar timers ou acessar componentes.
     * @param Settings As configura��es de v�deo, incluindo dimens�es e FPS.
     * @param InFramePool O pool de frames para adquirir e liberar buffers.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|FrameSource")
    virtual void Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool) PURE_VIRTUAL(Initialize);

    /**
     * @brief Desliga a fonte de frames e libera seus recursos. Deve ser implementada por classes derivadas.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|FrameSource")
    virtual void Shutdown() PURE_VIRTUAL(Shutdown);

    /**
     * @brief Inicia a captura/gera��o de frames. Deve ser implementada por classes derivadas.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|FrameSource")
    virtual void StartCapture() PURE_VIRTUAL(StartCapture);

    /**
     * @brief Para a captura/gera��o de frames. Deve ser implementada por classes derivadas.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|FrameSource")
    virtual void StopCapture() PURE_VIRTUAL(StopCapture);

    /** Delegate para o qual as classes consumidoras se ligar�o para receber frames. */
    FOnFrameAcquiredDelegate OnFrameAcquired;

protected:
    UPROPERTY(Transient)
    UWorld* CurrentWorld;

    UPROPERTY(Transient)
    FIVR_VideoSettings FrameSourceSettings;

    UPROPERTY(Transient)
    UIVRFramePool* FramePool;

    // Helper para adquirir um frame do pool e configurar as dimens�es b�sicas
    TSharedPtr<TArray<uint8>> AcquireFrameBufferFromPool();
};

