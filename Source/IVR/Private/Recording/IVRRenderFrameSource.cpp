// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Recording/IVRRenderFrameSource.h"
#include "IVR.h"
#include "HAL/PlatformMisc.h" 
#include "IVRGlobalStatics.h" 
#include "Async/Async.h" 
#include "Engine/World.h" // For UWorld and GetTimeSeconds()
#include "TextureResource.h" // For FTextureResource

DEFINE_LOG_CATEGORY(LogIVRRenderFrameSource);

UIVRRenderFrameSource::UIVRRenderFrameSource()
    : UIVRFrameSource()
{
    bCanCaptureNextFrame.Set(1); // Inicializa como 1 (Unlocked), pronto para capturar
}

void UIVRRenderFrameSource::BeginDestroy()
{
    Shutdown(); // Garante que a fonte seja desligada ao ser destru�da
    UIVRFrameSource::BeginDestroy(); // Chama o BeginDestroy da classe base
}

// Implementa��o b�sica de Initialize (para casos onde o CaptureComponent n�o � passado)
void UIVRRenderFrameSource::Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool)
{
    // Este overload agora sempre chamar� o overload principal com um nullptr para VideoCaptureComponent.
    // A responsabilidade de fornecer um componente � unicamente do chamador (UIVRCaptureComponent).
    Initialize(World, Settings, InFramePool, nullptr);
}

// Overload de Initialize que aceita um USceneCaptureComponent2D existente
void UIVRRenderFrameSource::Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool, USceneCaptureComponent2D* InVideoCaptureComponent)
{
    if (!World || !InFramePool)
    {
        UE_LOG(LogIVRRenderFrameSource, Error, TEXT("UIVRRenderFrameSource::Initialize: World or FramePool is null. Cannot initialize."));
        return;
    }
    CurrentWorld = World;
    FrameSourceSettings = Settings;
    FramePool = InFramePool;

    // Inicializa��o do RenderTarget
    if (!VideoRenderTarget)
    {
        VideoRenderTarget = NewObject<UTextureRenderTarget2D>(this);
        if (VideoRenderTarget)
        {
            VideoRenderTarget->ClearColor = FLinearColor::Black;
            VideoRenderTarget->TargetGamma = 1.5f;
            VideoRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8; // RGBA8 � RGBA
            VideoRenderTarget->InitAutoFormat(FrameSourceSettings.Width, FrameSourceSettings.Height);
            VideoRenderTarget->bGPUSharedFlag = true;
            VideoRenderTarget->UpdateResourceImmediate(true);
        }
    }
    else
    {
        VideoRenderTarget->ResizeTarget(FrameSourceSettings.Width, FrameSourceSettings.Height);
        VideoRenderTarget->UpdateResourceImmediate(true);
    }

    // Gerencia o VideoCaptureComponent: AGORA APENAS USA O QUE FOI PASSADO
    // A responsabilidade de criar e gerenciar o ciclo de vida do USceneCaptureComponent2D
    // � do UIVRCaptureComponent ou do usu�rio que o anexa.
    if (InVideoCaptureComponent)
    {
        VideoCaptureComponent = InVideoCaptureComponent;
        VideoCaptureComponent->TextureTarget = VideoRenderTarget; // Atribui o RenderTarget
        
        // Liga o delegate para o OnBackBufferReady
        if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer() && !OnBackBufferReadyToPresentHandle.IsValid())
        {
            OnBackBufferReadyToPresentHandle = FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddUObject(this, &UIVRRenderFrameSource::OnBackBufferReady);
            UE_LOG(LogIVRRenderFrameSource, Log, TEXT("OnBackBufferReadyToPresent delegate BOUND."));
        }
        else if (OnBackBufferReadyToPresentHandle.IsValid())
        {
             // J� bound, talvez devido � re-inicializa��o sem shutdown completo
             UE_LOG(LogIVRRenderFrameSource, Warning, TEXT("OnBackBufferReadyToPresent delegate already bound. Skipping re-binding."));
        }
    }
    else
    {
        // Se nenhum componente de captura foi fornecido, loga um erro e n�o tenta capturar.
        VideoCaptureComponent = nullptr;
        UE_LOG(LogIVRRenderFrameSource, Error, TEXT("UIVRRenderFrameSource: No valid USceneCaptureComponent2D provided. Frame capture will not work."));
    }
    UE_LOG(LogIVRRenderFrameSource, Log, TEXT("UIVRRenderFrameSource initialized."));
}

void UIVRRenderFrameSource::Shutdown()
{
    StopCapture(); // Para a captura antes de desligar

    // N�o destru�mos VideoCaptureComponent aqui, pois ele � de propriedade externa (UIVRCaptureComponent)
    VideoCaptureComponent = nullptr; // Apenas limpa nossa refer�ncia

    if (VideoRenderTarget)
    {
        VideoRenderTarget->ReleaseResource();
        VideoRenderTarget = nullptr;
    }
    if (OnBackBufferReadyToPresentHandle.IsValid())
    {
        // Verifique se o FSlateApplication e o Renderer ainda s�o v�lidos
        if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
        {
            FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(OnBackBufferReadyToPresentHandle);
            UE_LOG(LogIVRRenderFrameSource, Log, TEXT("OnBackBufferReadyToPresent delegate UNBOUND."));
        }
        else
        {
            // Se n�o forem mais v�lidos, apenas logue um aviso e resete o handle
            UE_LOG(LogIVRRenderFrameSource, Warning, TEXT("Could not unbind OnBackBufferReadyToPresent delegate: FSlateApplication or renderer already shut down."));
        }
        OnBackBufferReadyToPresentHandle.Reset(); // Sempre resete o handle para evitar uso futuro
        UE_LOG(LogIVRRenderFrameSource, Log, TEXT("OnBackBufferReadyToPresent delegate UNBOUND."));
    }
    
    // Limpa a fila de requests pendentes
    TSharedPtr<FRenderRequestInternal> DummyRequest;
    while(RenderRequestQueue.Dequeue(DummyRequest))
    {
        RReqQueueCounter--;
        // Buffers ser�o liberados quando o TSharedPtr sair de escopo
    }

    CurrentWorld = nullptr;
    FramePool = nullptr;
    UE_LOG(LogIVRRenderFrameSource, Log, TEXT("UIVRRenderFrameSource Shutdown."));
}

void UIVRRenderFrameSource::StartCapture()
{
    UE_LOG(LogIVRRenderFrameSource, Log, TEXT("UIVRRenderFrameSource: Starting capture."));
    bCanCaptureNextFrame.Set(1); // Libera para come�ar a capturar frames
}

void UIVRRenderFrameSource::StopCapture()
{
    UE_LOG(LogIVRRenderFrameSource, Log, TEXT("UIVRRenderFrameSource: Stopping capture."));
    bCanCaptureNextFrame.Set(0); // Bloqueia a captura de frames
}

// Esta fun��o � chamada na Render Thread
void UIVRRenderFrameSource::OnBackBufferReady(SWindow& SlateWindow, const FTextureRHIRef& BackBuffer)
{
    if (!IsInRenderingThread()) return; 

    AsyncTask(ENamedThreads::GameThread, [this]()
        {
            if (bCanCaptureNextFrame.GetValue() == 0 || !VideoRenderTarget || !VideoRenderTarget->GetResource())
            {
                return;
            }

            bCanCaptureNextFrame.Set(0);

            TSharedPtr<TArray<FColor>> AcquiredColorBuffer = MakeShared<TArray<FColor>>();
            AcquiredColorBuffer->SetNumUninitialized(FrameSourceSettings.Width * FrameSourceSettings.Height);
            TSharedPtr<FRenderRequestInternal> NewRenderRequest = MakeShared<FRenderRequestInternal>();
            NewRenderRequest->ImageBuffer = AcquiredColorBuffer; 

            struct FReadSurfaceContext
            {
                FRHITexture* Texture;
                TArray<FColor>* OutData;
                FIntRect Rect;
                FReadSurfaceDataFlags Flags;
            };

            FReadSurfaceContext ReadSurfaceContext = {
                VideoRenderTarget->GetResource()->GetTexture2DRHI(),
                NewRenderRequest->ImageBuffer.Get(), 
                FIntRect(0, 0, FrameSourceSettings.Width, FrameSourceSettings.Height),
                FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX)
            };

            ENQUEUE_RENDER_COMMAND(ReadRenderTargetCommand)(
                [ReadSurfaceContext, NewRenderRequest](FRHICommandListImmediate& RHICmdList)
                {
                    RHICmdList.ReadSurfaceData(
                        ReadSurfaceContext.Texture,
                        ReadSurfaceContext.Rect,
                        *ReadSurfaceContext.OutData,
                        ReadSurfaceContext.Flags
                    );
                }
                );

            RenderRequestQueue.Enqueue(NewRenderRequest);
            RReqQueueCounter++;

            NewRenderRequest->RenderFence.BeginFence();

            UE_LOG(LogIVRRenderFrameSource, Warning, TEXT("Render request enqueued. Queue size: %d"), RReqQueueCounter);
        });
}

// Esta fun��o deve ser chamada na Game Thread (ex: do TickComponent do IVRCaptureComponent)
void UIVRRenderFrameSource::ProcessRenderQueue()
{
    if (!CurrentWorld || !FramePool) return;

    TSharedPtr<FRenderRequestInternal> CurrentRequest;
    if (RenderRequestQueue.Peek(CurrentRequest)) 
    {
        if (CurrentRequest->RenderFence.IsFenceComplete()) 
        {
            RenderRequestQueue.Dequeue(CurrentRequest); 
            RReqQueueCounter--;

            // Adquire o buffer do pool - Este � o TSharedPtr que gerenciaremos.
            TSharedPtr<TArray<uint8>> AcquiredByteBuffer = AcquireFrameBufferFromPool();
            if (!AcquiredByteBuffer.IsValid())
            {
                UE_LOG(LogIVRRenderFrameSource, Error , TEXT("Failed to acquire byte buffer from pool. Dropping processed render frame."));
                bCanCaptureNextFrame.Set(1); 
                return;
            }

            ConvertRgbaToBgraAndCopyToBuffer(*CurrentRequest->ImageBuffer, *AcquiredByteBuffer);

            // Cria o FIVR_VideoFrame. Ele agora cont�m uma c�pia do TSharedPtr AcquiredByteBuffer.
            FIVR_VideoFrame NewFrame(FrameSourceSettings.Width, FrameSourceSettings.Height, CurrentWorld->GetTimeSeconds());
            NewFrame.RawDataPtr = AcquiredByteBuffer; 

            // Faz o broadcast. Isso cria uma C�PIA do TSharedPtr RawDataPtr para o delegate.
            // A TSharedPtr original (AcquiredByteBuffer) e a TSharedPtr dentro de NewFrame.RawDataPtr
            // mant�m suas refer�ncias fortes.
            OnFrameAcquired.Broadcast(NewFrame); 

            // IMPORTANTE: Agora, NewFrame.RawDataPtr precisa liberar sua refer�ncia forte.
            // Isso garante que quando AcquiredByteBuffer for devolvido ao pool,
            // ele ser� a �nica refer�ncia forte restante (assumindo que o delegate n�o est� retendo-o de forma inesperada).
            NewFrame.RawDataPtr.Reset(); // Isso decrementa o ref count da TArray<uint8>

            // Agora, devolva o TSharedPtr original (AcquiredByteBuffer) ao pool.
            // Se NewFrame.RawDataPtr.Reset() fez o trabalho, o ref count de AcquiredByteBuffer deve ser 1.
            FramePool->ReleaseFrame(AcquiredByteBuffer); 

            UE_LOG(LogIVRRenderFrameSource, Warning , TEXT("Render frame processed and broadcasted. Remaining queue size: %d"), RReqQueueCounter);

            bCanCaptureNextFrame.Set(1); 
        }
    }
}


void UIVRRenderFrameSource::ConvertRgbaToBgraAndCopyToBuffer(const TArray<FColor>& InColors, TArray<uint8>& OutBuffer)
{
    const int32 NumPixels = InColors.Num();
    OutBuffer.SetNumUninitialized(NumPixels * 4); 

    for (int32 i = 0; i < NumPixels; ++i)
    {
        const FColor& Color = InColors[i];
        OutBuffer[i * 4 + 0] = Color.B; 
        OutBuffer[i * 4 + 1] = Color.G; 
        OutBuffer[i * 4 + 2] = Color.R; 
        OutBuffer[i * 4 + 3] = Color.A; 
    }
}

