// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Containers/Queue.h"
#include "Templates/SharedPointer.h"
#include "Core/IVRTypes.h" // Para FIVR_VideoFrame e TArray<uint8>

#include "IVRFramePool.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogIVRFramePool, Log, All);

/**
 * @brief Gerencia um pool de buffers de frames (TArray<uint8>) para reuso eficiente.
 * Acesso thread-safe garantido pela TQueue.
 */
UCLASS()
class IVR_API UIVRFramePool : public UObject
{
    GENERATED_BODY()

public:

    UIVRFramePool();
    virtual void BeginDestroy() override;

    /**
     * @brief Inicializa o pool de frames com um tamanho e dimens�es espec�ficos.
     * @param InPoolSize O n�mero inicial de buffers pr�-alocados no pool.
     * @param InFrameWidth A largura esperada dos frames.
     * @param InFrameHeight A altura esperada dos frames.
     * @param bForceReinitialize Se true, o pool ser� resetado e re-inicializado, mesmo que j� esteja inicializado.
     */
    void Initialize(int32 InPoolSize, int32 InFrameWidth, int32 InFrameHeight, bool bForceReinitialize = false);

    /**
     * @brief Libera um buffer de frame, retornando-o ao pool para reuso.
     * @param FrameBuffer O TSharedPtr para o buffer do frame a ser liberado.
     */
    void ReleaseFrame(TSharedPtr<TArray<uint8>> FrameBuffer);

    /**
     * @brief Adquire um buffer de frame do pool. Se o pool estiver vazio, um novo buffer � criado.
     * @return Um TSharedPtr para um TArray<uint8> que representa o buffer do frame.
     */
    TSharedPtr<TArray<uint8>> AcquireFrame();

    /**
     * @brief Retorna se o pool est� inicializado.
     */
    bool IsInitialized() const { return bIsInitialized; }

    /**
     * @brief Retorna a largura dos frames que o pool est� configurado para gerenciar.
     */
    int32 GetFrameWidth() const { return FrameWidth; }

    /**
     * @brief Retorna a altura dos frames que o pool est� configurado para gerenciar.
     */
    int32 GetFrameHeight() const { return FrameHeight; }

private:

    TQueue<TSharedPtr<TArray<uint8>>> FrameBufferPool;
    int32 PoolSize;
    int32 FrameWidth;
    int32 FrameHeight;
    int32 FrameBufferSize; // Tamanho total em bytes de um frame (Width * Height * 4 para BGRA)

    bool bIsInitialized = false;
};

