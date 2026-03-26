// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "Components/IVRCaptureComponent.h"
#include "IVRGlobalStatics.h"
#include "Recording/IVRRecordingManager.h" 
#include "Recording/IVRRecordingSession.h"
#include "Recording/IVRRenderFrameSource.h" 
#include "IVR.h"
#include "Engine/World.h" 
#include "Kismet/KismetMathLibrary.h"
#include "Misc/Paths.h" 
#include "Misc/FileHelper.h" 
#include "HAL/PlatformFileManager.h" 
#include "CineCameraComponent.h"
#include "Engine/Texture2D.h" 
#include "RenderingThread.h"
#include "Async/Async.h" // Para AsyncTask
// [MANUAL_REF_POINT] Includes do OpenCV foram movidos para IVROpenCVBridge.
// Incluindo o bridge para chamar as funções OpenCV nativas
#include "IVROpenCVGlobals.h"
// Forward declarations de FRunnables que agora estão em IVROpenCVBridge
// UIVRVideoEncoder usará FVideoEncoderWorker, então precisa do .h dele
#include "FVideoEncoderWorker.h" 

// Removido: UIVRFramePool; struct FIVR_JustRTFrame; Essas estão no IVRTypes.h agora

UIVRCaptureComponent::UIVRCaptureComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    RecordingStartTimeSeconds = 0.0f;
    FramePool = CreateDefaultSubobject<UIVRFramePool>(TEXT("IVRFramePool"));
    OwnedVideoCaptureComponent = nullptr; 
    ActualFrameWidth = 0;
    ActualFrameHeight = 0;
    RealTimeOutputTexture2D = nullptr; 
}

void UIVRCaptureComponent::BeginDestroy()
{
    // Limpa CurrentFrameSource de forma segura
    if (CurrentFrameSource && CurrentFrameSource->IsValidLowLevelFast())
    {
        CurrentFrameSource->Shutdown(); 
        CurrentFrameSource = nullptr; // Garante que a referência seja nula após o shutdown
    }
    else if (CurrentFrameSource)
    {
        UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: CurrentFrameSource é um UObject inválido. Pulando a chamada de Shutdown()."));
    }
    
    // Libera RealTimeOutputTexture2D
    if (RealTimeOutputTexture2D)
    {
        RealTimeOutputTexture2D->ReleaseResource(); 
        RealTimeOutputTexture2D = nullptr;
    }
    
    // Destrói OwnedVideoCaptureComponent se ele foi criado por este componente
    if (OwnedVideoCaptureComponent && OwnedVideoCaptureComponent->GetOwner() == this->GetOwner() && OwnedVideoCaptureComponent->GetOuter() == this)
    {
        OwnedVideoCaptureComponent->DestroyComponent(); 
        OwnedVideoCaptureComponent = nullptr;
    }

    // A sessão de gravação é gerenciada pelo UIVRRecordingManager e CurrentSession é Transient.
    // Não precisa de limpeza explícita aqui para CurrentSession.

    Super::BeginDestroy();
}

void UIVRCaptureComponent::BeginPlay()
{
    Super::BeginPlay();
    Internal_InitializeFrameSource(); // Chamamos a função interna de inicialização aqui
}

void UIVRCaptureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopRecording();
    if (CurrentFrameSource)
    {
        CurrentFrameSource->Shutdown();
        CurrentFrameSource = nullptr;
        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: CurrentFrameSource encerrado."));
    }
    // Não destruímos OwnedVideoCaptureComponent aqui, pois BeginDestroy já fará isso se for de nossa propriedade.
    // Apenas garantimos que a referência seja nula.
    OwnedVideoCaptureComponent = nullptr; 
    Super::EndPlay(EndPlayReason); 
}

void UIVRCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (OwnedVideoCaptureComponent && VideoSettings.FrameSourceType == EIVRFrameSourceType::RenderTarget && VideoSettings.IVR_FollowActor)
    {
        if (VideoSettings.IVR_FollowActor->IsValidLowLevel())
        {
            FRotator CameraRot = UKismetMathLibrary::FindLookAtRotation(
                OwnedVideoCaptureComponent->GetComponentLocation(),
                VideoSettings.IVR_FollowActor->GetActorLocation()
            );
            OwnedVideoCaptureComponent->SetWorldRotation(CameraRot);
        }
        else
        {
            UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: IVR_FollowActor é um ponteiro inválido. Desativando comportamento de seguir."));
            VideoSettings.IVR_FollowActor = nullptr;
        }
    }
    if (bIsRecording) 
    {
        // Se for um RenderTarget, precisamos processar a fila de renderização
        if (CurrentFrameSource && VideoSettings.FrameSourceType == EIVRFrameSourceType::RenderTarget)
        {
            Cast<UIVRRenderFrameSource>(CurrentFrameSource)->ProcessRenderQueue();
        }
        if (CurrentSession) 
        {
            CurrentTakeTime += DeltaTime;
            
            if (CurrentTakeTime >= TakeDuration)
            {
                EndCurrentTake();
                
                if (bAutoStartNewTake)
                {
                    StartNewTake(); 
                }
            }
        }
    }
}

void UIVRCaptureComponent::StartRecording()
{
    TWeakObjectPtr<UIVRCaptureComponent> WeakThis = this;
    AsyncTask(ENamedThreads::GameThread, [WeakThis]()
        {
            if (!WeakThis.IsValid())
            {
                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent::StartRecording: Componente IVRCaptureComponent foi destruído antes que a tarefa assíncrona pudesse ser executada."));
                return;
            }
            UIVRCaptureComponent* StrongThis = WeakThis.Get();
            if (!StrongThis->bIsRecording)
            {
                // Garante que a fonte de frames esteja pronta e com as configurações mais recentes
                // Isso cobre o caso em que as configurações são alteradas mas RefreshFrameSourceAndApplySettings não é chamado explicitamente antes de StartRecording.
                StrongThis->Internal_InitializeFrameSource();

                StrongThis->bIsRecording = true;
                StrongThis->CurrentTakeNumber = 0; 
                StrongThis->RecordingStartTimeSeconds = StrongThis->GetWorld()->GetTimeSeconds();
                if (StrongThis->CurrentFrameSource)
                {
                    StrongThis->CurrentFrameSource->StartCapture(); 
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Captura de fonte de frames iniciada."));
                    
                    // Ajusta FPS da sessão com base no tipo de fonte
                    if (StrongThis->VideoSettings.FrameSourceType == EIVRFrameSourceType::Folder)
                    {
                        StrongThis->VideoSettings.FPS = StrongThis->VideoSettings.IVR_FolderPlaybackFPS;
                        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Ajustando FPS da sessão para IVR_FolderPlaybackFPS: %.2f"), StrongThis->VideoSettings.FPS);
                    }
                    else if (StrongThis->VideoSettings.FrameSourceType == EIVRFrameSourceType::Webcam)
                    {
                        StrongThis->VideoSettings.FPS = StrongThis->VideoSettings.IVR_WebcamFPS;
                        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Ajustando FPS da sessão para IVR_WebcamFPS: %.2f"), StrongThis->VideoSettings.FPS);
                    }
                    else if (StrongThis->VideoSettings.FrameSourceType == EIVRFrameSourceType::VideoFile) 
                    {
                        UIVRVideoFrameSource* VideoFileSource = Cast<UIVRVideoFrameSource>(StrongThis->CurrentFrameSource);
                        if (VideoFileSource)
                        {
                            float effectiveFPS = 0.0f;
                            // Polling para obter o FPS efetivo do arquivo de vídeo
                            for (int i = 0; i < 10; ++i)
                            {
                                effectiveFPS = VideoFileSource->GetEffectivePlaybackFPS();
                                if (effectiveFPS > 0.0f)
                                {
                                    break;
                                }
                                FPlatformProcess::Sleep(0.01f);
                            }
                            if (effectiveFPS > 0.0f)
                            {
                                StrongThis->VideoSettings.FPS = effectiveFPS;
                                UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Ajustando FPS da sessão para FPS efetivo do VideoFile: %.2f"), StrongThis->VideoSettings.FPS);
                            }
                            else
                            {
                                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: Não foi possível determinar o FPS efetivo do VideoFile. Usando VideoSettings.FPS padrão (%.2f) para a entrada do FFmpeg. A velocidade de saída do vídeo pode estar incorreta."), StrongThis->VideoSettings.FPS);
                            }
                        }
                    }
                    
                    // Otimização: A resolução RealFrameWidth/Height já deve estar correta de Internal_InitializeFrameSource
                    // strongThis->FramePool->Initialize(StrongThis->FramePoolSize, StrongThis->ActualFrameWidth, StrongThis->ActualFrameHeight, true); // Não é mais necessário aqui

                    if (!StrongThis->VideoSettings.bEnableRTFrames) 
                    {
                        StrongThis->StartNewTake(); 
                        if (!StrongThis->CurrentSession) 
                        {
                            UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: StartNewTake falhou ao criar sessão de gravação inicial. Abortando."));
                            StrongThis->bIsRecording = false;
                            StrongThis->CurrentFrameSource->StopCapture();
                            return;
                        }
                    }
                    else 
                    {
                        // Lógica de criação/recriação de RealTimeOutputTexture2D
                        if (!StrongThis->RealTimeOutputTexture2D || StrongThis->RealTimeOutputTexture2D->GetSizeX() != StrongThis->ActualFrameWidth || StrongThis->RealTimeOutputTexture2D->GetSizeY() != StrongThis->ActualFrameHeight)
                        {
                            if (StrongThis->RealTimeOutputTexture2D)
                            {
                                StrongThis->RealTimeOutputTexture2D->ReleaseResource(); 
                                StrongThis->RealTimeOutputTexture2D = nullptr;
                            }
                            StrongThis->RealTimeOutputTexture2D = UTexture2D::CreateTransient(StrongThis->ActualFrameWidth, StrongThis->ActualFrameHeight, PF_B8G8R8A8);
                            if (StrongThis->RealTimeOutputTexture2D)
                            {
                                StrongThis->RealTimeOutputTexture2D->UpdateResource(); 
                                StrongThis->RealTimeOutputTexture2D->Filter = TF_Bilinear;
                                StrongThis->RealTimeOutputTexture2D->CompressionSettings = TC_EditorIcon; 
                                StrongThis->RealTimeOutputTexture2D->SRGB = true; 
                                UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Textura de Saída em Tempo Real criada/recriada: %dx%d."), StrongThis->ActualFrameWidth, StrongThis->ActualFrameHeight);
                            }
                            else
                            {
                                UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Falha ao criar RealTimeOutputTexture2D. O JustRTCapture não funcionará corretamente."));
                                StrongThis->bIsRecording = false;
                                StrongThis->CurrentFrameSource->StopCapture();
                                return;
                            }
                        }
                        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: JustRTCapture habilitado. Frames serão enviados via delegate."));
                    }
                    
                    StrongThis->OnRecordingStarted.Broadcast(); 
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Gravação/Captura iniciada. Tamanho do Frame Real: %dx%d (usado para FFmpeg e FramePool)"), StrongThis->ActualFrameWidth, StrongThis->ActualFrameHeight);
                }
                else 
                {
                    UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Não é possível iniciar a gravação, CurrentFrameSource é nulo."));
                    StrongThis->bIsRecording = false;
                    return;
                }
            }
        });
}

void UIVRCaptureComponent::StopRecording()
{
    TWeakObjectPtr<UIVRCaptureComponent> WeakThis = this;
    AsyncTask(ENamedThreads::GameThread, [WeakThis]()
        {
            if (!WeakThis.IsValid())
            {
                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent::StopRecording: Componente IVRCaptureComponent foi destruído antes que a tarefa assíncrona pudesse ser executada. O cleanup foi ignorado para evitar um crash."));
                return;
            }
            UIVRCaptureComponent* StrongThis = WeakThis.Get();
            if (StrongThis->bIsRecording)
            {
                if (!StrongThis->VideoSettings.bEnableRTFrames) 
                {
                    StrongThis->EndCurrentTake();
                    UIVRRecordingManager::Get()->GenerateMasterVideoAndCleanup();
                }
                
                if (StrongThis->CurrentFrameSource)
                {
                    StrongThis->CurrentFrameSource->StopCapture();
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Captura de fonte de frames parada."));
                }
                
                StrongThis->bIsRecording = false;
                StrongThis->OnRecordingStopped.Broadcast(); 
                UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Gravação parada."));
            }
        });
}

void UIVRCaptureComponent::PauseTake()
{
    TWeakObjectPtr<UIVRCaptureComponent> WeakThis = this;
    AsyncTask(ENamedThreads::GameThread, [WeakThis]()
        {
            if (!WeakThis.IsValid())
            {
                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent::PauseTake: Componente IVRCaptureComponent foi destruído antes que a tarefa assíncrona pudesse ser executada."));
                return;
            }
            UIVRCaptureComponent* StrongThis = WeakThis.Get();
            if (StrongThis->bIsRecording && StrongThis->CurrentSession)
            {
                StrongThis->CurrentSession->PauseRecording();
                
                if (StrongThis->CurrentFrameSource)
                {
                    StrongThis->CurrentFrameSource->StopCapture(); 
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Fonte de frames pausada."));
                }
                StrongThis->OnRecordingPaused.Broadcast(); 
            }
        });
}

void UIVRCaptureComponent::ResumeTake()
{
    TWeakObjectPtr<UIVRCaptureComponent> WeakThis = this;
    AsyncTask(ENamedThreads::GameThread, [WeakThis]()
        {
            if (!WeakThis.IsValid())
            {
                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent::ResumeTake: Componente IVRCaptureComponent foi destruído antes que a tarefa assíncrona pudesse ser executada."));
                return;
            }
            UIVRCaptureComponent* StrongThis = WeakThis.Get();
            if (StrongThis->bIsRecording && StrongThis->CurrentSession)
            {
                StrongThis->CurrentSession->ResumeRecording();
                if (StrongThis->CurrentFrameSource)
                {
                    StrongThis->CurrentFrameSource->StartCapture();
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Fonte de frames resumida."));
                }
                StrongThis->OnRecordingResumed.Broadcast(); 
            }
        });
}

void UIVRCaptureComponent::StartNewTake()
{
    if (CurrentSession)
    {
        UIVRRecordingManager::Get()->StopRecording(CurrentSession);
        CurrentSession = nullptr; 
    }
    CurrentSession = UIVRRecordingManager::Get()->StartRecording(VideoSettings, ActualFrameWidth, ActualFrameHeight, FramePool);
    if (!CurrentSession)
    {
        UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Falha ao criar nova sessão de gravação para take %d. Abortando futuros takes."), CurrentTakeNumber + 1);
        bIsRecording = false; 
        if (CurrentFrameSource)
        {
             CurrentFrameSource->StopCapture();
        }
        return;
    }
    CurrentTakeTime = 0.0f;
    CurrentTakeNumber++;
    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Take %d iniciado."), CurrentTakeNumber);
}

void UIVRCaptureComponent::EndCurrentTake()
{
    if (CurrentSession)
    {
        UIVRRecordingManager::Get()->StopRecording(CurrentSession);
        CurrentSession = nullptr;
        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Take %d finalizado."), CurrentTakeNumber);
    }
}

void UIVRCaptureComponent::OnFrameAcquiredFromSource(FIVR_VideoFrame Frame)
{
    if (bIsRecording)
    {
        if (!VideoSettings.bEnableRTFrames) 
        {
            if (CurrentSession)
            {
                CurrentSession->AddVideoFrame(Frame); 
            }
            else
            {
                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: Descartando frame - nenhuma sessão de gravação disponível."));
                if (FramePool && Frame.RawDataPtr.IsValid())
                {
                    FramePool->ReleaseFrame(Frame.RawDataPtr);
                }
            }
        }
        else // Modo JustRTCapture (saída em tempo real)
        {
            FIVR_JustRTFrame FrameOutput; 
            FrameOutput.Width = Frame.Width;
            FrameOutput.Height = Frame.Height;
            FrameOutput.Timestamp = Frame.Timestamp;
            FrameOutput.SourceFrameTint = VideoSettings.IVR_FrameTint;
            FrameOutput.RawDataBuffer = *Frame.RawDataPtr; 
            
            if (FramePool && Frame.RawDataPtr.IsValid()) 
            {
                FramePool->ReleaseFrame(Frame.RawDataPtr);
            }
            if (RTDisplayTint != FLinearColor::White)
            {
                const int32 NumPixels = FrameOutput.Width * FrameOutput.Height;
                for (int32 i = 0; i < NumPixels; ++i)
                {
                    float B_float = (float)FrameOutput.RawDataBuffer[i * 4 + 0] / 255.0f; B_float *= RTDisplayTint.B;
                    FrameOutput.RawDataBuffer[i * 4 + 0] = FMath::Clamp((uint8)(B_float * 255.0f), (uint8)0, (uint8)255);
                    float G_float = (float)FrameOutput.RawDataBuffer[i * 4 + 1] / 255.0f; G_float *= RTDisplayTint.G;
                    FrameOutput.RawDataBuffer[i * 4 + 1] = FMath::Clamp((uint8)(G_float * 255.0f), (uint8)0, (uint8)255);
                    float R_float = (float)FrameOutput.RawDataBuffer[i * 4 + 2] / 255.0f; R_float *= RTDisplayTint.R;
                    FrameOutput.RawDataBuffer[i * 4 + 2] = FMath::Clamp((uint8)(R_float * 255.0f), (uint8)0, (uint8)255);
                    float A_float = (float)FrameOutput.RawDataBuffer[i * 4 + 3] / 255.0f; A_float *= RTDisplayTint.A;
                    FrameOutput.RawDataBuffer[i * 4 + 3] = FMath::Clamp((uint8)(A_float * 255.0f), (uint8)0, (uint8)255);
                }
            }
            FrameOutput.LiveTexture = RealTimeOutputTexture2D;
            FrameOutput.DisplayTint = RTDisplayTint; 
            
            FTransform CameraTransform = FTransform::Identity;
            float CameraFOV = 90.0f;
            if (OwnedVideoCaptureComponent)
            {
                CameraTransform = OwnedVideoCaptureComponent->GetComponentTransform();
                CameraFOV = OwnedVideoCaptureComponent->FOVAngle;
            }
            
            ProcessFrameAndFeaturesAsync(
                FrameOutput,
                CameraTransform,
                CameraFOV,
                VideoSettings.IVR_GFTT_MaxCorners,
                VideoSettings.IVR_GFTT_QualityLevel,
                VideoSettings.IVR_GFTT_MinDistance,
                VideoSettings.IVR_DebugDrawFeatures
             );
        }
    }
    else 
    {
        UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: Descartando frame da fonte - não gravando ou não no modo de captura RT."));
        if (FramePool && Frame.RawDataPtr.IsValid())
        {
            FramePool->ReleaseFrame(Frame.RawDataPtr);
        }
    }
}

void UIVRCaptureComponent::RefreshFrameSourceAndApplySettings()
{
    Internal_InitializeFrameSource();
    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Frame source refresh solicitado e aplicado. Resolução: %dx%d."), ActualFrameWidth, ActualFrameHeight);
}

void UIVRCaptureComponent::Internal_InitializeFrameSource()
{
    // Primeiro, fazemos um shutdown completo da fonte de frames anterior, se houver.
    if (CurrentFrameSource)
    {
        CurrentFrameSource->OnFrameAcquired.RemoveAll(this); // Desliga delegates
        CurrentFrameSource->Shutdown(); // Garante que a fonte libere seus recursos
        CurrentFrameSource = nullptr;
    }
    
    // Libera RealTimeOutputTexture2D
    if (RealTimeOutputTexture2D)
    {
        RealTimeOutputTexture2D->ReleaseResource(); 
        RealTimeOutputTexture2D = nullptr;
    }

    // Destrói OwnedVideoCaptureComponent se ele foi criado por este componente
    if (OwnedVideoCaptureComponent && OwnedVideoCaptureComponent->GetOwner() == this->GetOwner() && OwnedVideoCaptureComponent->GetOuter() == this)
    {
        OwnedVideoCaptureComponent->DestroyComponent(); 
        OwnedVideoCaptureComponent = nullptr;
    }

    // Resetamos as dimensões reais antes de re-inicializar
    ActualFrameWidth = 0;
    ActualFrameHeight = 0;

    // Recria a fonte de frames baseada nas VideoSettings atualizadas
    switch (VideoSettings.FrameSourceType)
    {
        case EIVRFrameSourceType::Simulated:
        {
            CurrentFrameSource = NewObject<UIVRSimulatedFrameSource>(this);
            Cast<UIVRSimulatedFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool, VideoSettings.IVR_FrameTint);
        }
        break;

        case EIVRFrameSourceType::RenderTarget:
        {
            CurrentFrameSource = NewObject<UIVRRenderFrameSource>(this);
            AActor* OwnerActor = GetOwner(); 
            USceneCaptureComponent2D* ExistingCaptureComp = nullptr;
            UCineCameraComponent* ExistingCineCamComp = nullptr;
            if (OwnerActor)
            {
                ExistingCaptureComp = OwnerActor->FindComponentByClass<USceneCaptureComponent2D>();
                ExistingCineCamComp = OwnerActor->FindComponentByClass<UCineCameraComponent>();
            }
            if (ExistingCaptureComp)
            {
                OwnedVideoCaptureComponent = ExistingCaptureComp;
                UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Found existing USceneCaptureComponent2D on owner Actor. Using it for capture."));
            }
            else if (ExistingCineCamComp)
            {
                OwnedVideoCaptureComponent = NewObject<USceneCaptureComponent2D>(this, TEXT("OwnedVideoCaptureComponent_FromCineCam"));
                if (OwnedVideoCaptureComponent)
                {
                    OwnedVideoCaptureComponent->RegisterComponent();
                    if (GetAttachParent()) 
                    {
                        OwnedVideoCaptureComponent->AttachToComponent(GetAttachParent(), FAttachmentTransformRules::KeepRelativeTransform);
                    }
                    else 
                    {
                        OwnedVideoCaptureComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
                    }
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Created and attached new USceneCaptureComponent2D configured from existing UCineCameraComponent."));
                    OwnedVideoCaptureComponent->SetRelativeLocation(ExistingCineCamComp->GetRelativeLocation());
                    OwnedVideoCaptureComponent->SetRelativeRotation(ExistingCineCamComp->GetRelativeRotation());
                    OwnedVideoCaptureComponent->FOVAngle = ExistingCineCamComp->FieldOfView; 
                    OwnedVideoCaptureComponent->PostProcessSettings = ExistingCineCamComp->PostProcessSettings;
                }
                else
                {
                    UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Failed to create new USceneCaptureComponent2D from CineCameraComponent. RenderTarget capture will likely fail."));
                }
            }
            else
            {
                OwnedVideoCaptureComponent = NewObject<USceneCaptureComponent2D>(this, TEXT("OwnedVideoCaptureComponent_Default"));
                if (OwnedVideoCaptureComponent)
                {
                    OwnedVideoCaptureComponent->RegisterComponent(); 
                    if (GetAttachParent()) 
                    {
                        OwnedVideoCaptureComponent->AttachToComponent(GetAttachParent(), FAttachmentTransformRules::KeepRelativeTransform);
                    }
                    else 
                    {
                        OwnedVideoCaptureComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
                    }
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Created and attached new default USceneCaptureComponent2D."));
                }
                else
                {
                    UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Failed to create new default USceneCaptureComponent2D. RenderTarget capture will likely fail."));
                }
            }
            if (OwnedVideoCaptureComponent)
            {
                OwnedVideoCaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
                OwnedVideoCaptureComponent->FOVAngle = VideoSettings.IVR_CineCameraFOV; 
                OwnedVideoCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
                OwnedVideoCaptureComponent->bCaptureEveryFrame = true;
                OwnedVideoCaptureComponent->SetRelativeLocation(FVector::ZeroVector);
                OwnedVideoCaptureComponent->SetRelativeRotation(FRotator::ZeroRotator);
                if (VideoSettings.IVR_EnableCinematicPostProcessing)
                {
                    OwnedVideoCaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
                    OwnedVideoCaptureComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
                }
                else
                {
                    OwnedVideoCaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = false;
                }
            } else {
                UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Falha ao criar/encontrar OwnedVideoCaptureComponent. A captura de RenderTarget não funcionará."));
            }
            Cast<UIVRRenderFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool, OwnedVideoCaptureComponent);
        }
        break;
        case EIVRFrameSourceType::Folder:
        {
            CurrentFrameSource = NewObject<UIVRFolderFrameSource>(this);
            Cast<UIVRFolderFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool);
        }
        break;
        case EIVRFrameSourceType::VideoFile:
        {
            CurrentFrameSource = NewObject<UIVRVideoFrameSource>(this);
            Cast<UIVRVideoFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool);
        }
        break;
        case EIVRFrameSourceType::Webcam:
        {
            CurrentFrameSource = NewObject<UIVRWebcamFrameSource>(this);
            Cast<UIVRWebcamFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool);
        }
        break;
        default:
        {
            UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Unknown FrameSourceType selected (%d). Defaulting to RenderTarget."), (int32)VideoSettings.FrameSourceType);
            CurrentFrameSource = NewObject<UIVRRenderFrameSource>(this); // Fallback to RenderTarget
            AActor* OwnerActor = GetOwner(); 
            USceneCaptureComponent2D* ExistingCaptureComp = nullptr;
            UCineCameraComponent* ExistingCineCamComp = nullptr;
            if (OwnerActor)
            {
                ExistingCaptureComp = OwnerActor->FindComponentByClass<USceneCaptureComponent2D>();
                ExistingCineCamComp = OwnerActor->FindComponentByClass<UCineCameraComponent>();
            }
            if (ExistingCaptureComp)
            {
                OwnedVideoCaptureComponent = ExistingCaptureComp;
            }
            else if (ExistingCineCamComp)
            {
                OwnedVideoCaptureComponent = NewObject<USceneCaptureComponent2D>(this, TEXT("OwnedVideoCaptureComponent_FromCineCam_Default"));
                if (OwnedVideoCaptureComponent)
                {
                    OwnedVideoCaptureComponent->RegisterComponent();
                    if (GetAttachParent()) { OwnedVideoCaptureComponent->AttachToComponent(GetAttachParent(), FAttachmentTransformRules::KeepRelativeTransform); }
                    else { OwnedVideoCaptureComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform); }
                    OwnedVideoCaptureComponent->SetRelativeLocation(ExistingCineCamComp->GetRelativeLocation());
                    OwnedVideoCaptureComponent->SetRelativeRotation(ExistingCineCamComp->GetRelativeRotation());
                    OwnedVideoCaptureComponent->FOVAngle = ExistingCineCamComp->FieldOfView;
                    OwnedVideoCaptureComponent->PostProcessSettings = ExistingCineCamComp->PostProcessSettings;
                }
            }
            else
            {
                OwnedVideoCaptureComponent = NewObject<USceneCaptureComponent2D>(this, TEXT("OwnedVideoCaptureComponent_Default_Fallback"));
                if (OwnedVideoCaptureComponent)
                {
                    OwnedVideoCaptureComponent->RegisterComponent();
                    if (GetAttachParent()) { OwnedVideoCaptureComponent->AttachToComponent(GetAttachParent(), FAttachmentTransformRules::KeepRelativeTransform); }
                    else { OwnedVideoCaptureComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform); }
                }
            }
            if (OwnedVideoCaptureComponent)
            {
                OwnedVideoCaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
                OwnedVideoCaptureComponent->FOVAngle = VideoSettings.IVR_CineCameraFOV;
                OwnedVideoCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
                OwnedVideoCaptureComponent->bCaptureEveryFrame = true;
                OwnedVideoCaptureComponent->SetRelativeLocation(FVector::ZeroVector);
                OwnedVideoCaptureComponent->SetRelativeRotation(FRotator::ZeroRotator);
                if (VideoSettings.IVR_EnableCinematicPostProcessing)
                {
                    OwnedVideoCaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
                    OwnedVideoCaptureComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
                }
                else
                {
                    OwnedVideoCaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = false;
                }
            } else {
                 UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Falha ao criar default OwnedVideoCaptureComponent para fallback. A captura de RenderTarget não funcionará."));
            }
            Cast<UIVRRenderFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool, OwnedVideoCaptureComponent);
        }
        break;
    } // Fim do switch

    if (CurrentFrameSource)
    {
        // Pega a resolução real da fonte (especialmente para Webcam/VideoFile que podem ter resoluções nativas diferentes)
        if (UIVRWebcamFrameSource* WebcamSource = Cast<UIVRWebcamFrameSource>(CurrentFrameSource))
        {
            for (int i = 0; i < 10; ++i) 
            {
                ActualFrameWidth = WebcamSource->GetActualFrameWidth();
                ActualFrameHeight = WebcamSource->GetActualFrameHeight();
                if (ActualFrameWidth > 0 && ActualFrameHeight > 0)
                {
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Resolução real da Webcam determinada: %dx%d após %d tentativas."), ActualFrameWidth, ActualFrameHeight, i+1);
                    break;
                }
                FPlatformProcess::Sleep(0.01f);
            }
            if (ActualFrameWidth <= 0 || ActualFrameHeight <= 0)
            {
                ActualFrameWidth = VideoSettings.Width;
                ActualFrameHeight = VideoSettings.Height;
                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: Webcam reportou resolução inválida (%dx%d) após polling. Usando VideoSettings para gravação."), ActualFrameWidth, ActualFrameHeight);
            }
        }
        else if (UIVRVideoFrameSource* VideoFileSource = Cast<UIVRVideoFrameSource>(CurrentFrameSource))
        {
            for (int i = 0; i < 10; ++i)
            {
                ActualFrameWidth = VideoFileSource->GetActualFrameWidth();
                ActualFrameHeight = VideoFileSource->GetActualFrameHeight();
                if (ActualFrameWidth > 0 && ActualFrameHeight > 0)
                {
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Resolução real do VideoFile determinada: %dx%d após %d tentativas."), ActualFrameWidth, ActualFrameHeight, i+1);
                    break;
                }
                FPlatformProcess::Sleep(0.01f);
            }
            if (ActualFrameWidth <= 0 || ActualFrameHeight <= 0)
            {
                ActualFrameWidth = VideoSettings.Width;
                ActualFrameHeight = VideoSettings.Height;
                UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: VideoFileSource reportou resolução inválida (%dx%d) após polling. Usando VideoSettings para gravação."), ActualFrameWidth, ActualFrameHeight);
            }
        }
        else // Para RenderTarget, Folder, Simulated, use as configurações do VideoSettings diretamente
        {
            ActualFrameWidth = VideoSettings.Width;
            ActualFrameHeight = VideoSettings.Height;
        }
        
        // Re-inicializa o FramePool com as dimensões reais da captura
        FramePool->Initialize(FramePoolSize, ActualFrameWidth, ActualFrameHeight, true); // true para forçar re-inicialização
        
        // Liga o delegate para receber frames da nova fonte
        CurrentFrameSource->OnFrameAcquired.AddUObject(this, &UIVRCaptureComponent::OnFrameAcquiredFromSource);
        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Fonte de frames '%s' inicializada e delegate ligado. FramePool configurado para %dx%d."), *CurrentFrameSource->GetName(), ActualFrameWidth, ActualFrameHeight);
    }
    else
    {
        UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Falha ao criar CurrentFrameSource. A gravação não funcionará corretamente."));
    }
}

// O restante do IVRCaptureComponent.cpp permanece o mesmo (UpdateTextureFromRawData, PrepareVideoForRecording, ExportVideoToCompatibleFormat, ProcessFrameAndFeaturesAsync, DeprojectPixelToWorld)
void UIVRCaptureComponent::UpdateTextureFromRawData(UTexture2D* Texture, const TArray<uint8>& RawData, int32 InWidth, int32 InHeight)
{
    if (!Texture || !Texture->IsValidLowLevelFast() || !Texture->GetResource())
    {
        UE_LOG(LogIVR, Error, TEXT("UpdateTextureFromRawData: Textura ou recurso RHI inválido na entrada."));
        return;
    }
    if (Texture->GetSizeX() != InWidth || Texture->GetSizeY() != InHeight)
    {
        UE_LOG(LogIVR, Error, TEXT("UpdateTextureFromRawData: Dimensões da textura (%dx%d) não correspondem às do frame (%dx%d). Frame descartado."),
               Texture->GetSizeX(), Texture->GetSizeY(), InWidth, InHeight);
        return; 
    }
    if (RawData.Num() != InWidth * InHeight * sizeof(FColor))
    {
        UE_LOG(LogIVR, Error, TEXT("UpdateTextureFromRawData: RawData size mismatch! Actual: %d, Expected: %d. Frame descartado."), RawData.Num(), InWidth * InHeight * sizeof(FColor));
        return; 
    }
    uint8* MipData = new uint8[RawData.Num()];
    FMemory::Memcpy(MipData, RawData.GetData(), RawData.Num());
    FUpdateTextureRegion2D Region(0, 0, 0, 0, InWidth, InHeight);
    ENQUEUE_RENDER_COMMAND(UpdateTextureFromRawDataCommand)(
        [Texture, Region, InWidth, MipData](FRHICommandListImmediate& RHICmdList)
        {
            if (Texture && Texture->GetResource())
            {
                Texture->UpdateTextureRegions(
                    0,                                   
                    1,                                   
                    &Region,                             
                    InWidth * sizeof(FColor),            
                    0,                                   
                    MipData,                             
                    [](uint8* InSrcData, const FUpdateTextureRegion2D* InRegions) 
                    {
                        delete[] InSrcData; 
                    }
                );
            }
            else
            {
                UE_LOG(LogIVR, Error, TEXT("UpdateTextureFromRawData: Texture ou recurso RHI tornou-se inválido na Render Thread. MipData será vazado se não for explicitamente deletado aqui."));
                delete[] MipData; 
            }
        });
}

FString UIVRCaptureComponent::PrepareVideoForRecording(const FString& InSourceVideoPath, const FString& OutPreparedVideoPath, bool bOverwrite)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.FileExists(*InSourceVideoPath))
    {
        UE_LOG(LogIVR, Error, TEXT("PrepareVideoForRecording: Arquivo de vídeo de origem não encontrado em: %s"), *InSourceVideoPath);
        return FString();
    }
    if (PlatformFile.FileExists(*OutPreparedVideoPath) && !bOverwrite)
    {
        UE_LOG(LogIVR, Log, TEXT("PrepareVideoForRecording: Vídeo preparado já existe em: %s e sobrescrita desabilitada. Usando arquivo existente."), *OutPreparedVideoPath);
        return OutPreparedVideoPath;
    }
    FString FFmpegPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("IVR"), TEXT("ThirdParty"), TEXT("FFmpeg"), TEXT("Binaries"));
#if PLATFORM_WINDOWS
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Win64"), TEXT("ffmpeg.exe"));
#elif PLATFORM_LINUX
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Linux"), TEXT("ffmpeg"));
#elif PLATFORM_MAC
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Mac"), TEXT("ffmpeg"));
#else
    UE_LOG(LogIVR, Error, TEXT("PrepareVideoForRecording: Caminho do executável FFmpeg não definido para a plataforma atual!"));
    return FString();
#endif
    FPaths::NormalizeDirectoryName(FFmpegPath);
    if (!PlatformFile.FileExists(*FFmpegPath))
    {
        UE_LOG(LogIVR, Error, TEXT("PrepareVideoForRecording: Executável FFmpeg não encontrado em: %s. Não é possível transcodificar vídeo."), *FFmpegPath);
        return FString();
    }
    FString FFmpegArguments = FString::Printf(
        TEXT("-y -i %s -c:v libx264 -preset medium -crf 23 -pix_fmt yuv420p -c:a aac -b:a 128k %s"),
        *InSourceVideoPath,
        *OutPreparedVideoPath
    );
    UE_LOG(LogIVR, Log, TEXT("PrepareVideoForRecording: Transcodificando vídeo. Executável: %s, Argumentos: %s"), *FFmpegPath, *FFmpegArguments);
    if (!UIVRRecordingManager::Get()->LaunchFFmpegProcessBlocking(FFmpegPath, FFmpegArguments))
    {
        UE_LOG(LogIVR, Error, TEXT("PrepareVideoForRecording: Transcodificação de vídeo falhou para: %s"), *InSourceVideoPath);
        if (PlatformFile.FileExists(*OutPreparedVideoPath))
        {
            PlatformFile.DeleteFile(*OutPreparedVideoPath);
        }
        return FString();
    }
    UE_LOG(LogIVR, Log, TEXT("PrepareVideoForRecording: Vídeo transcodificado com sucesso para: %s"), *OutPreparedVideoPath);
    return OutPreparedVideoPath;
}

FString UIVRCaptureComponent::ExportVideoToCompatibleFormat(const FString& InSourceVideoPath, const FString& OutCompatibleVideoPath, bool bOverwrite, const FIVR_VideoSettings& EncodingSettings)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.FileExists(*InSourceVideoPath))
    {
        UE_LOG(LogIVR, Error, TEXT("ExportVideoToCompatibleFormat: Arquivo de vídeo de origem não encontrado em: %s"), *InSourceVideoPath);
        return FString();
    }
    if (PlatformFile.FileExists(*OutCompatibleVideoPath) && !bOverwrite)
    {
        UE_LOG(LogIVR, Log, TEXT("ExportVideoToCompatibleFormat: Vídeo compatível já existe em: %s e sobrescrita desabilitada. Usando arquivo existente."), *OutCompatibleVideoPath);
        return OutCompatibleVideoPath;
    }
    FString FFmpegPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("IVR"), TEXT("ThirdParty"), TEXT("FFmpeg"), TEXT("Binaries"));
#if PLATFORM_WINDOWS
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Win64"), TEXT("ffmpeg.exe"));
#elif PLATFORM_LINUX
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Linux"), TEXT("ffmpeg"));
#elif PLATFORM_MAC
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Mac"), TEXT("ffmpeg"));
#else
    UE_LOG(LogIVR, Error, TEXT("ExportVideoToCompatibleFormat: Caminho do executável FFmpeg não definido para a plataforma atual!"));
    return FString();
#endif
    FPaths::NormalizeDirectoryName(FFmpegPath);
    if (!PlatformFile.FileExists(*FFmpegPath))
    {
        UE_LOG(LogIVR, Error, TEXT("ExportVideoToCompatibleFormat: Executável FFmpeg não encontrado em: %s. Não é possível transcodificar vídeo."), *FFmpegPath);
        return FString();
    }
    FString FFmpegArguments = FString::Printf(
        TEXT("-y -i %s -c:v %s -preset medium -crf 23 -pix_fmt %s -b:v %d -r %f -c:a aac -b:a 128k %s"),
        *InSourceVideoPath,             
        *EncodingSettings.Codec,        
        *EncodingSettings.PixelFormat,  
        EncodingSettings.Bitrate,       
        EncodingSettings.FPS,           
        *OutCompatibleVideoPath         
    );
    UE_LOG(LogIVR, Log, TEXT("ExportVideoToCompatibleFormat: Transcodificando vídeo. Executável: %s, Argumentos: %s"), *FFmpegPath, *FFmpegArguments);
    if (!UIVRRecordingManager::Get()->LaunchFFmpegProcessBlocking(FFmpegPath, FFmpegArguments))
    {
        UE_LOG(LogIVR, Error, TEXT("ExportVideoToCompatibleFormat: Transcodificação de vídeo falhou para: %s"), *InSourceVideoPath);
        if (PlatformFile.FileExists(*OutCompatibleVideoPath))
        {
            PlatformFile.DeleteFile(*OutCompatibleVideoPath);
        }
        return FString();
    }
    UE_LOG(LogIVR, Log, TEXT("ExportVideoToCompatibleFormat: Vídeo transcodificado com sucesso para: %s"), *OutCompatibleVideoPath);
    return OutCompatibleVideoPath;
}

void UIVRCaptureComponent::ProcessFrameAndFeaturesAsync(FIVR_JustRTFrame InOutFrame, FTransform CameraTransform, float CameraFOV, int32 MaxCorners, float QualityLevel, float MinDistance, bool bDebugDrawFeatures)
{
    // [MANUAL_REF_POINT] A lógica de processamento OpenCV foi movida para IVROpenCVBridge::ProcessFrameAndExtractFeatures.
    // Chame a função apropriada do IVROpenCVBridge aqui, passando os parâmetros necessários.
    // Chame a função do bridge com os parâmetros brutos. FramePoolInstance foi removido!
    // Criar uma instância da struct de features do IVROpenCVBridge para receber os resultados
    FOCV_NativeJustRTFeatures TempExtractedFeatures;
    IVROpenCVBridge::ProcessFrameAndExtractFeatures(
            InOutFrame.RawDataBuffer.GetData(), // Passar o ponteiro bruto para os pixels
            InOutFrame.Width,
            InOutFrame.Height,
            CameraTransform,
            CameraFOV,
            VideoSettings.IVR_GFTT_MaxCorners,
            VideoSettings.IVR_GFTT_QualityLevel,
            VideoSettings.IVR_GFTT_MinDistance,
            VideoSettings.IVR_DebugDrawFeatures,
            TempExtractedFeatures // A struct de saída
         );
    TWeakObjectPtr<UIVRCaptureComponent> WeakThis = this;
    AsyncTask(ENamedThreads::GameThread, [TempExtractedFeatures,InOutFrame, CameraTransform, CameraFOV, MaxCorners, QualityLevel, MinDistance, bDebugDrawFeatures, WeakThis]() mutable
    {
        if (!WeakThis.IsValid())
        {
            UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent::ProcessFrameAndFeaturesAsync: Componente IVRCaptureComponent foi destruído antes que a tarefa assíncrona de processamento de features pudesse ser executada."));
            return;
        }
        UIVRCaptureComponent* StrongThis = WeakThis.Get();
        if (StrongThis->RealTimeOutputTexture2D && InOutFrame.RawDataBuffer.Num() > 0)
        {
            StrongThis->UpdateTextureFromRawData(StrongThis->RealTimeOutputTexture2D, InOutFrame.RawDataBuffer, InOutFrame.Width, InOutFrame.Height);
        }
        else
        {
            UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: RealTimeOutputTexture2D ou RawDataBuffer inválido para saída RT APÓS processamento de features."));
        }
        
        // Agora copiar os resultados da struct temporária para a FIVR_JustRTFrame original
        // Fazer a conversão de FIVROCV_InterestPoint para FIVR_JustRTPoint
        InOutFrame.Features.JustRTInterestPoints.Empty();
        for (const FOCV_NativeJustRTPoint& OCVPoint : TempExtractedFeatures.JustRTInterestPoints)
        {
            FIVR_JustRTPoint RTPoint; // Esta struct ainda é do IVRCore
            RTPoint.Point2D = OCVPoint.Point2D;
            RTPoint.Size2D = OCVPoint.Size2D;
            RTPoint.Angle = OCVPoint.Angle;
            RTPoint.IsQuad = OCVPoint.IsQuad;
            
            // Deprojeção para o mundo 3D - Chamada aqui na GameThread para evitar problemas com UWorld
            StrongThis->DeprojectPixelToWorld(OCVPoint.Point2D, CameraTransform, CameraFOV, 
                                            FIntPoint(InOutFrame.Width, InOutFrame.Height), 
                                            RTPoint.Point3D, RTPoint.Direction);
            
            InOutFrame.Features.JustRTInterestPoints.Add(RTPoint);
        }
        InOutFrame.Features.BiggestPointIndex = TempExtractedFeatures.BiggestPointIndex;
        InOutFrame.Features.SmallerPointIndex = TempExtractedFeatures.SmallerPointIndex;
        InOutFrame.Features.NumOfQuads = TempExtractedFeatures.NumOfQuads;
        InOutFrame.Features.NumOfRectangles = TempExtractedFeatures.NumOfRectangles;
        InOutFrame.Features.HistogramRed = TempExtractedFeatures.HistogramRed;
        InOutFrame.Features.HistogramGreen = TempExtractedFeatures.HistogramGreen;
        InOutFrame.Features.HistogramBlue = TempExtractedFeatures.HistogramBlue;


        if (StrongThis->OnRealTimeFrameReady.IsBound())
        {
            StrongThis->OnRealTimeFrameReady.Broadcast(InOutFrame);
        }
    });
}

void UIVRCaptureComponent::DeprojectPixelToWorld(
    const FVector2D& PixelPos,
    const FTransform& CameraTransform,
    float FOVDegrees,
    const FIntPoint& ImageResolution, 
    FVector& OutWorldLocation,
    FVector& OutWorldDirection)
{
    const float FOVRad = FMath::DegreesToRadians(FOVDegrees);
    const float AspectRatio = (float)ImageResolution.X / ImageResolution.Y;
    float NDC_X = (PixelPos.X / ImageResolution.X) * 2.0f - 1.0f;
    float NDC_Y = (1.0f - (PixelPos.Y / ImageResolution.Y)) * 2.0f - 1.0f;
    float ViewX = NDC_X * FMath::Tan(FOVRad * 0.5f) * AspectRatio;
    float ViewY = NDC_Y * FMath::Tan(FOVRad * 0.5f);
    FVector ViewSpaceDirection = FVector(ViewX, ViewY, -1.0f).GetSafeNormal();
    OutWorldDirection = CameraTransform.GetRotation().RotateVector(ViewSpaceDirection);
    OutWorldLocation = CameraTransform.GetLocation();
}