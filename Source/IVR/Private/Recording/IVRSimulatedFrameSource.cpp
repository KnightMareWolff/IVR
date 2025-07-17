// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Recording/IVRSimulatedFrameSource.h"
#include "HAL/PlatformTime.h" // Para FPlatformTime::Seconds()
#include "Engine/World.h"     // Para GetWorldTimerManager()

UIVRSimulatedFrameSource::UIVRSimulatedFrameSource()
    : UIVRFrameSource() // Chama o construtor da base
{
}

void UIVRSimulatedFrameSource::BeginDestroy()
{
    UIVRFrameSource::BeginDestroy(); // Chama o BeginDestroy da base
}

// Implementa��o base
void UIVRSimulatedFrameSource::Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool)
{
    // Chama o overload com a tintura padr�o
    Initialize(World, Settings, InFramePool, FLinearColor::White);
}

// NOVO: Overload de Initialize com tintura
void UIVRSimulatedFrameSource::Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool, FLinearColor InFrameTint)
{
    if (!World || !InFramePool)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRSimulatedFrameSource::Initialize: World or FramePool is null. Cannot initialize."));
        return;
    }
    CurrentWorld         = World;
    FrameSourceSettings  = Settings; // Armazena as configura��es
    FramePool            = InFramePool;        // Armazena a refer�ncia ao pool

    FrameRate            = Settings.FPS;
    FrameWidth           = Settings.Width;
    FrameHeight          = Settings.Height;
    ElapsedTime          = 0.0f;
    FrameCount           = 0;
    FrameTint            = InFrameTint; // NOVO: Armazena a tintura
    IVR_UseRandomPattern = Settings.IVR_UseRandomPattern;

    UE_LOG(LogIVRFrameSource, Log, TEXT("Simulated Frame Source Initialized: %dx%d @ %.2f FPS, Tint: %s"), FrameWidth, FrameHeight, FrameRate, *FrameTint.ToString());
}

void UIVRSimulatedFrameSource::StartCapture()
{
    if (!CurrentWorld || !FramePool)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("UIVRSimulatedFrameSource::StartCapture: Not initialized. Call Initialize() first."));
        return;
    }

    // Garante que qualquer timer anterior seja parado
    StopCapture();

    float Delay = (FrameRate > 0.0f) ? (1.0f / FrameRate) : 0.0333f; // Default para ~30 FPS se FPS for 0
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRSimulatedFrameSource: Attempting to set timer for frame generation. Delay: %f"), Delay);
    
    // Inicia o timer que chamar GenerateSimulatedFrame repetidamente
    CurrentWorld->GetTimerManager().SetTimer(FrameGenerationTimerHandle, this, &UIVRSimulatedFrameSource::GenerateSimulatedFrame, Delay, true);
    
    UE_LOG(LogIVRFrameSource, Log, TEXT("Simulated Frame Source Started. Generating frames every %.4f seconds."), Delay);
}

void UIVRSimulatedFrameSource::StopCapture()
{
    if (CurrentWorld && CurrentWorld->GetTimerManager().IsTimerActive(FrameGenerationTimerHandle))
    {
        CurrentWorld->GetTimerManager().ClearTimer(FrameGenerationTimerHandle);
        UE_LOG(LogIVRFrameSource, Log, TEXT("Simulated Frame Source Stopped."));
    }
    FrameGenerationTimerHandle.Invalidate(); // Marca o handle como invlido
}

void UIVRSimulatedFrameSource::Shutdown()
{
    StopCapture(); // Garante que o timer seja parado
    CurrentWorld = nullptr;
    FramePool = nullptr;
    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRSimulatedFrameSource Shutdown."));
}


void UIVRSimulatedFrameSource::GenerateSimulatedFrame()
{
    if (!CurrentWorld || !FramePool) return; // World ou FramePool pode ter sido invalidado ou no inicializado

    ElapsedTime += CurrentWorld->GetDeltaSeconds();
    FrameCount++;

    // Adquire um buffer do pool
    TSharedPtr<TArray<uint8>> FrameBuffer = AcquireFrameBufferFromPool();
    if (!FrameBuffer.IsValid())
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("Failed to acquire frame buffer from pool. Dropping simulated frame."));
        return;
    }

    // Cria um novo FIVR_VideoFrame e preenche-o com o buffer adquirido
    FIVR_VideoFrame NewFrame(FrameWidth, FrameHeight, FPlatformTime::Seconds());
    NewFrame.RawDataPtr = FrameBuffer; // Atribui o buffer adquirido

    // Preenche o frame com dados simulados
    FillSimulatedFrame(NewFrame);

    UE_LOG(LogIVRFrameSource, Log, TEXT("UIVRSimulatedFrameSource: Broadcasting frame %lld. RawDataPtr size: %d"), 
        FrameCount, NewFrame.RawDataPtr.IsValid() ? NewFrame.RawDataPtr->Num() : 0);

    // Notifica os ouvintes com o novo frame
    OnFrameAcquired.Broadcast(MoveTemp(NewFrame)); // Usar MoveTemp para eficincia
}

void UIVRSimulatedFrameSource::FillSimulatedFrame(FIVR_VideoFrame& InFrame)
{
    const int32 NumPixels = InFrame.Width * InFrame.Height;
    const int32 NumBytes = NumPixels * 4; // BGRA
    uint8 R_base = (uint8)(0.0f);
    uint8 G_base = (uint8)(0.0f);
    uint8 B_base = (uint8)(0.0f);

    // Garante que RawDataPtr � v�lido e aloca o TArray<uint8> se necess�rio
    if (!InFrame.RawDataPtr.IsValid())
    {
        InFrame.RawDataPtr = MakeShared<TArray<uint8>>();
    }
    InFrame.RawDataPtr->SetNumUninitialized(NumBytes);

    // Gera um padro de cor que muda gradualmente com o tempo
    if (IVR_UseRandomPattern)
    {
        R_base = (uint8)(FMath::Sin(ElapsedTime * 0.5f) * 127.0f + 128.0f);
        G_base = (uint8)(FMath::Sin(ElapsedTime * 0.7f + PI / 2.0f) * 127.0f + 128.0f);
        B_base = (uint8)(FMath::Sin(ElapsedTime * 0.9f + PI) * 127.0f + 128.0f);
    }
    else
    {
        R_base = (uint8)(255.0f);
        G_base = (uint8)(255.0f);
        B_base = (uint8)(255.0f);
    }
    // Preenche o buffer com a cor gerada (formato BGRA), aplicando a tintura
    for (int32 i = 0; i < NumPixels; ++i)
    {
        // Aplica a tintura base da configura��o � cor gerada
        // Convertendo para float, aplicando a multiplica��o e convertendo de volta para uint8
        float B_float = (float)B_base / 255.0f * FrameTint.B;
        float G_float = (float)G_base / 255.0f * FrameTint.G;
        float R_float = (float)R_base / 255.0f * FrameTint.R;
        float A_float = 1.0f * FrameTint.A; // Assume alpha total (255) para a base

        (*InFrame.RawDataPtr)[i * 4 + 0] = FMath::Clamp((uint8)(B_float * 255.0f), (uint8)0, (uint8)255); // Blue
        (*InFrame.RawDataPtr)[i * 4 + 1] = FMath::Clamp((uint8)(G_float * 255.0f), (uint8)0, (uint8)255); // Green
        (*InFrame.RawDataPtr)[i * 4 + 2] = FMath::Clamp((uint8)(R_float * 255.0f), (uint8)0, (uint8)255); // Red
        (*InFrame.RawDataPtr)[i * 4 + 3] = FMath::Clamp((uint8)(A_float * 255.0f), (uint8)0, (uint8)255); // Alpha (opaco)
    }
}

