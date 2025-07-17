// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Core/IVRFramePool.h"
#include "IVR.h" // Inclua a LogCategory (se n�o estiver em CoreMinimal)

DEFINE_LOG_CATEGORY(LogIVRFramePool);

UIVRFramePool::UIVRFramePool()
    : PoolSize(0)
    , FrameWidth(0)
    , FrameHeight(0)
    , FrameBufferSize(0)
{
    // O pool n�o � inicializado no construtor. Isso ser� feito em Initialize().
}

void UIVRFramePool::BeginDestroy()
{
    // Limpa o pool para liberar a mem�ria
    TSharedPtr<TArray<uint8>> DummyBuffer;
    while (FrameBufferPool.Dequeue(DummyBuffer))
    {
        // Buffers s�o liberados quando o TSharedPtr sai de escopo
    }
    Super::BeginDestroy();
}

void UIVRFramePool::Initialize(int32 InPoolSize, int32 InFrameWidth, int32 InFrameHeight, bool bForceReinitialize)
{
    if (bIsInitialized && !bForceReinitialize)
    {
        // Se j� inicializado e n�o for for�ado, verifica se os par�metros s�o os mesmos.
        if (PoolSize == InPoolSize && FrameWidth == InFrameWidth && FrameHeight == InFrameHeight)
        {
             UE_LOG(LogIVRFramePool, Log, TEXT("UIVRFramePool already initialized with same parameters (%dx%d). No-op."), FrameWidth, FrameHeight);
             return;
        }
        else
        {
             // Loga um aviso se tentar re-inicializar com par�metros diferentes sem for�ar.
             UE_LOG(LogIVRFramePool, Warning, TEXT("UIVRFramePool already initialized with different parameters (%dx%d vs %dx%d). Not re-initializing. Call Shutdown() or pass bForceReinitialize=true to override."),
                    FrameWidth, FrameHeight, InFrameWidth, InFrameHeight);
             return;
        }
    }

    // Se for for�ado ou n�o estiver inicializado, limpa o estado existente primeiro.
    if (bIsInitialized && bForceReinitialize)
    {
        TSharedPtr<TArray<uint8>> DummyBuffer;
        while (FrameBufferPool.Dequeue(DummyBuffer)) {} // Limpa buffers existentes
        UE_LOG(LogIVRFramePool, Log, TEXT("UIVRFramePool forced re-initialization: Cleared previous buffers."));
    }
    
    PoolSize = InPoolSize;
    FrameWidth = InFrameWidth;
    FrameHeight = InFrameHeight;
    FrameBufferSize = FrameWidth * FrameHeight * 4; // Assumindo 4 bytes por pixel (BGRA)

    if (FrameBufferSize <= 0)
    {
        UE_LOG(LogIVRFramePool, Error, TEXT("Invalid frame dimensions provided to UIVRFramePool::Initialize. Width: %d, Height: %d"), FrameWidth, FrameHeight);
        bIsInitialized = false; // Marca como n�o inicializado devido a erro
        return;
    }

    // Pr�-aloca os buffers no pool
    for (int32 i = 0; i < PoolSize; ++i)
    {
        TSharedPtr<TArray<uint8>> NewBuffer = MakeShared<TArray<uint8>>();
        NewBuffer->SetNumUninitialized(FrameBufferSize);
        FrameBufferPool.Enqueue(NewBuffer);
    }

    bIsInitialized = true;
    UE_LOG(LogIVRFramePool, Log, TEXT("UIVRFramePool initialized with %d buffers, each %d bytes (%dx%d)."), PoolSize, FrameBufferSize, FrameWidth, FrameHeight);
}

TSharedPtr<TArray<uint8>> UIVRFramePool::AcquireFrame()
{
    TSharedPtr<TArray<uint8>> PooledBuffer;

    if (!bIsInitialized)
    {
        UE_LOG(LogIVRFramePool, Error, TEXT("Attempted to acquire frame from uninitialized pool. Returning nullptr."));
        return nullptr;
    }

    // Tenta obter um buffer do pool.
    if (!FrameBufferPool.Dequeue(PooledBuffer))
    {
        // Se o pool estiver vazio, cria um novo buffer e loga um aviso.
        // Este buffer ter� o tamanho configurado no pool.
        UE_LOG(LogIVRFramePool, Warning, TEXT("FrameBufferPool exhausted! Creating new buffer (%d bytes). Consider increasing PoolSize."), FrameBufferSize);
        PooledBuffer = MakeShared<TArray<uint8>>();
        PooledBuffer->SetNumUninitialized(FrameBufferSize); // Garante o tamanho correto
    }
    // N�o precisa esvaziar/reservar novamente, SetNumUninitialized j� garante o tamanho e n�o precisa de dados anteriores.

    return PooledBuffer;
}

void UIVRFramePool::ReleaseFrame(TSharedPtr<TArray<uint8>> FrameBuffer)
{
    if (!FrameBuffer.IsValid())
    {
        UE_LOG(LogIVRFramePool, Warning, TEXT("Attempted to release an invalid (nullptr) frame buffer."));
        return;
    }
    if (!bIsInitialized)
    {
        UE_LOG(LogIVRFramePool, Error, TEXT("Attempted to release frame to uninitialized pool. Frame will not be reused."));
        return;
    }

    // N�o precisamos verificar o tamanho aqui, o pool aceita de volta.
    // O TArray<uint8> ser� automaticamente redimensionado se AcquireFrame criar um de tamanho diferente.
    // No entanto, � crucial que o AcquireFrame sempre retorne um buffer do tamanho correto.
    // Se um buffer de tamanho diferente for retornado ao pool, ele ser� armazenado e poder� ser reutilizado
    // incorretamente mais tarde se o AcquireFrame n�o garantir que ele tenha o FrameBufferSize correto.
    // A implementa��o atual de AcquireFrame (acima) j� garante que novos buffers t�m o tamanho certo.
    // Buffers retornados devem ser do tamanho esperado para evitar confus�o no pool.
    if (FrameBuffer->Num() != FrameBufferSize)
    {
        UE_LOG(LogIVRFramePool, Warning, TEXT("Released frame buffer has unexpected size (%d bytes) for a pool configured for %d bytes. Discarding frame to prevent issues."), FrameBuffer->Num(), FrameBufferSize);
        // N�o adiciona ao pool, pois � de tamanho incorreto. Ser� destru�do ao sair do escopo.
        return;
    }

    FrameBufferPool.Enqueue(FrameBuffer);
}

