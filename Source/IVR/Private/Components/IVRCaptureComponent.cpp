// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Components/IVRCaptureComponent.h"
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
// Adicione includes para OpenCV
#if WITH_OPENCV
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h"
// Para evitar conflitos de macros se for usar o check do Unreal:
#undef check 
#pragma warning(disable: 4668) 
#pragma warning(disable: 4828) 
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp> // Para cv::waitKey, imshow (apenas para debug)
#include <vector> 
#include "PostOpenCVHeaders.h"
#endif


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
    // Certifique-se de que CurrentFrameSource ainda � um UObject v�lido
    // e que sua tabela de fun��es virtuais (vtable) n�o foi corrompida.
    // IsValidLowLevelFast() � uma verifica��o r�pida e suficiente para a maioria dos casos.
    if (CurrentFrameSource && CurrentFrameSource->IsValidLowLevelFast())
    {
        // Esta � a chamada que est� causando o erro fatal
        CurrentFrameSource->Shutdown(); 
    }
    else if (CurrentFrameSource) // Se n�o � null, mas � inv�lido (vtable corrompida)
    {
        UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: CurrentFrameSource � um UObject inv�lido. Pulando a chamada de Shutdown()."));
    }
    CurrentFrameSource = nullptr; // Limpe o ponteiro de qualquer forma
    
    if (RealTimeOutputTexture2D)
    {
        RealTimeOutputTexture2D->ReleaseResource(); 
        RealTimeOutputTexture2D = nullptr;
    }

    if (OwnedVideoCaptureComponent && OwnedVideoCaptureComponent->GetOwner() == this->GetOwner() && OwnedVideoCaptureComponent->GetOuter() == this)
    {
        OwnedVideoCaptureComponent->DestroyComponent(); 
        OwnedVideoCaptureComponent = nullptr;
    }
    Super::BeginDestroy();
}

void UIVRCaptureComponent::BeginPlay()
{
    Super::BeginPlay();

    switch (VideoSettings.FrameSourceType)
    {
    case EIVRFrameSourceType::Simulated:
    {
        CurrentFrameSource = NewObject<UIVRSimulatedFrameSource>(this);
        Cast<UIVRSimulatedFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool, VideoSettings.IVR_FrameTint);
    }break;
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
            // Se um CineCameraComponent existe, criamos nosso prprio USceneCaptureComponent2D
            // e copiamos suas configuraes relevantes.
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

                // Copiar configuraes de cmera relevantes do CineCam para nosso USceneCaptureComponent2D
                OwnedVideoCaptureComponent->SetRelativeLocation(ExistingCineCamComp->GetRelativeLocation());
                OwnedVideoCaptureComponent->SetRelativeRotation(ExistingCineCamComp->GetRelativeRotation());
                OwnedVideoCaptureComponent->FOVAngle = ExistingCineCamComp->FieldOfView; 
                OwnedVideoCaptureComponent->PostProcessSettings = ExistingCineCamComp->PostProcessSettings; // Copiar todas as configuraes de ps-processamento
            }
            else
            {
                UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Failed to create new USceneCaptureComponent2D from CineCameraComponent. RenderTarget capture will likely fail."));
            }
        }
        else
        {
            // Se nenhum componente de captura existente foi encontrado, cria um USceneCaptureComponent2D padro.
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
            // Aplicar configuraes da VideoSettings (podem sobrescrever as copiadas, se houver)
            OwnedVideoCaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
            OwnedVideoCaptureComponent->FOVAngle = VideoSettings.IVR_CineCameraFOV; 
            OwnedVideoCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
            OwnedVideoCaptureComponent->bCaptureEveryFrame = true;

            // Define a localizao e rotao relativas ao componente pai como padro (0,0,0)
            OwnedVideoCaptureComponent->SetRelativeLocation(FVector::ZeroVector);
            OwnedVideoCaptureComponent->SetRelativeRotation(FRotator::ZeroRotator);

            // Aplicar configuraes de ps-processamento do VideoSettings
            if (VideoSettings.IVR_EnableCinematicPostProcessing)
            {
                OwnedVideoCaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
                OwnedVideoCaptureComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
            }
            else
            {
                OwnedVideoCaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = false;
            }
        }
        Cast<UIVRRenderFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool, OwnedVideoCaptureComponent);
    }break;
    case EIVRFrameSourceType::Folder: // CORRIGIDO: Adicionado case para Folder
    {
        CurrentFrameSource = NewObject<UIVRFolderFrameSource>(this);
        Cast<UIVRFolderFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool);
    }break;
    case EIVRFrameSourceType::VideoFile: // CORRIGIDO: Adicionado case para VideoFile
    {
        CurrentFrameSource = NewObject<UIVRVideoFrameSource>(this);
        Cast<UIVRVideoFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool);
    }break;
    case EIVRFrameSourceType::Webcam: // CORRIGIDO: Adicionado case para Webcam
    {
        CurrentFrameSource = NewObject<UIVRWebcamFrameSource>(this);
        Cast<UIVRWebcamFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool);
    }break;
    default: // Fallback se o tipo de fonte no for reconhecido (nunca deveria acontecer agora)
    {
        UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Unknown FrameSourceType selected (%d). Defaulting to RenderTarget."), (int32)VideoSettings.FrameSourceType);
        CurrentFrameSource = NewObject<UIVRRenderFrameSource>(this); // Fallback to RenderTarget

        // Lgica de fallback para USceneCaptureComponent2D
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
            if (OwnedVideoCaptureComponent) // Registrar e anexar o componente de fallback recm-criado
            {
                OwnedVideoCaptureComponent->RegisterComponent();
                if (GetAttachParent()) { OwnedVideoCaptureComponent->AttachToComponent(GetAttachParent(), FAttachmentTransformRules::KeepRelativeTransform); }
                else { OwnedVideoCaptureComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform); }
            }
        }

        if (OwnedVideoCaptureComponent) // Apenas se o componente de fallback foi criado/encontrado com sucesso
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
             UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Failed to create default OwnedVideoCaptureComponent for fallback. RenderTarget capture will not function."));
        }
        Cast<UIVRRenderFrameSource>(CurrentFrameSource)->Initialize(GetWorld(), VideoSettings, FramePool, OwnedVideoCaptureComponent);
    }break;
    }

    if (CurrentFrameSource)
    {
        // A lgica de obteno de ActualFrameWidth/Height para fontes de hardware (Webcam/VideoFile)
        // precisa de tempo para o worker thread iniciar e obter as dimenses reais.
        // As fontes Simulated, RenderTarget e Folder usam as dimenses do VideoSettings.
        if (UIVRWebcamFrameSource* WebcamSource = Cast<UIVRWebcamFrameSource>(CurrentFrameSource))
        {
            // D um tempo para o worker da webcam obter as dimenses reais
            for (int i = 0; i < 10; ++i) 
            {
                ActualFrameWidth = WebcamSource->GetActualFrameWidth();
                ActualFrameHeight = WebcamSource->GetActualFrameHeight();
                if (ActualFrameWidth > 0 && ActualFrameHeight > 0)
                {
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Webcam actual resolution determined: %dx%d after %d retries."), ActualFrameWidth, ActualFrameHeight, i+1);
                    break;
                }
                FPlatformProcess::Sleep(0.01f); // Espera 10ms
            }

            if (ActualFrameWidth <= 0 || ActualFrameHeight <= 0)
            {
                // Fallback para as dimenses do VideoSettings se a webcam no reportar vlido
                ActualFrameWidth = VideoSettings.Width;
                ActualFrameHeight = VideoSettings.Height;
                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: Webcam reported invalid resolution (%dx%d) after polling. Using VideoSettings for initial FramePool."), ActualFrameWidth, ActualFrameHeight);
            }
        }
        else if (UIVRVideoFrameSource* VideoFileSource = Cast<UIVRVideoFrameSource>(CurrentFrameSource))
        {
            // D um tempo para o worker do arquivo de vdeo obter as dimenses reais
            for (int i = 0; i < 10; ++i)
            {
                ActualFrameWidth = VideoFileSource->GetActualFrameWidth();
                ActualFrameHeight = VideoFileSource->GetActualFrameHeight();
                if (ActualFrameWidth > 0 && ActualFrameHeight > 0)
                {
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: VideoFile actual resolution determined: %dx%d after %d retries."), ActualFrameWidth, ActualFrameHeight, i+1);
                    break;
                }
                FPlatformProcess::Sleep(0.01f);
            }

            if (ActualFrameWidth <= 0 || ActualFrameHeight <= 0)
            {
                // Fallback para as dimenses do VideoSettings se o arquivo no reportar vlido
                ActualFrameWidth = VideoSettings.Width;
                ActualFrameHeight = VideoSettings.Height;
                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: VideoFileSource reported invalid resolution (%dx%d) after polling. Using VideoSettings for initial FramePool."), ActualFrameWidth, ActualFrameHeight);
            }
        }
        else // Para RenderTarget, Folder, Simulated, use as configuraes do VideoSettings diretamente
        {
            ActualFrameWidth = VideoSettings.Width;
            ActualFrameHeight = VideoSettings.Height;
        }


        FramePool->Initialize(FramePoolSize, ActualFrameWidth, ActualFrameHeight, true); 
        
        CurrentFrameSource->OnFrameAcquired.AddUObject(this, &UIVRCaptureComponent::OnFrameAcquiredFromSource);
        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Frame source '%s' initialized and delegate bound. FramePool initially configured for %dx%d."), *CurrentFrameSource->GetName(), ActualFrameWidth, ActualFrameHeight);
    }
    else
    {
        UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Failed to create CurrentFrameSource. Recording will not function correctly."));
    }
}

void UIVRCaptureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopRecording(); 

    if (CurrentFrameSource)
    {
        CurrentFrameSource->Shutdown();
        CurrentFrameSource = nullptr;
        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: CurrentFrameSource shutdown."));
    }
    // No destrumos OwnedVideoCaptureComponent aqui, pois ele pode ter sido apenas referenciado
    // ou criado como parte de um fallback e ser destrudo com o prprio UIVRCaptureComponent.
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
            UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: IVR_FollowActor is no longer valid. Disabling follow behavior."));
            VideoSettings.IVR_FollowActor = nullptr;
        }
    }

    if (bIsRecording) 
    {
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
    AsyncTask(ENamedThreads::GameThread, [this]()
        {
            if (!bIsRecording)
            {
                bIsRecording = true;
                CurrentTakeNumber = 0; 
                RecordingStartTimeSeconds = GetWorld()->GetTimeSeconds();

                if (CurrentFrameSource)
                {
                    CurrentFrameSource->StartCapture(); 
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Frame source capture started."));
// --- INCIO: Ajuste VideoSettings.FPS com base no FrameSourceType ativo ---
                    // Isso garante que o processo FFmpeg (via UIVRVideoEncoder) use a taxa de quadros de entrada correta,
                    // alinhando com a velocidade real de produo de frames pelo CurrentFrameSource.
                    if (VideoSettings.FrameSourceType == EIVRFrameSourceType::Folder)
                    {
                        VideoSettings.FPS = VideoSettings.IVR_FolderPlaybackFPS;
                        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Ajustando FPS da sesso para IVR_FolderPlaybackFPS: %.2f"), VideoSettings.FPS);
                    }
                    else if (VideoSettings.FrameSourceType == EIVRFrameSourceType::Webcam)
                    {
                        // Garante que o FPS da Webcam seja usado. IVR_WebcamFPS  o alvo para a produo de frames.
                        VideoSettings.FPS = VideoSettings.IVR_WebcamFPS;
                        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Ajustando FPS da sesso para IVR_WebcamFPS: %.2f"), VideoSettings.FPS);
                    }
                    else if (VideoSettings.FrameSourceType == EIVRFrameSourceType::VideoFile)
                    {
                        // Para VideoFile, o FPS de reproduo efetivo  determinado pelo FPS nativo do vdeo * velocidade de reproduo.
                        UIVRVideoFrameSource* VideoFileSource = Cast<UIVRVideoFrameSource>(CurrentFrameSource);
                        if (VideoFileSource)
                        {
                            // Consulta o FPS efetivo real. Isso pode levar algumas tentativas para o worker thread inicializar.
                            float effectiveFPS = 0.0f;
                            for (int i = 0; i < 10; ++i) // Consulta por no mximo 100ms (10 * 10ms de sleep)
                            {
                                effectiveFPS = VideoFileSource->GetEffectivePlaybackFPS();
                                if (effectiveFPS > 0.0f)
                                {
                                    break;
                                }
                                FPlatformProcess::Sleep(0.01f); // Espera 10ms
                            }
                            if (effectiveFPS > 0.0f)
                            {
                                VideoSettings.FPS = effectiveFPS;
                                UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Ajustando FPS da sesso para FPS efetivo do VideoFile: %.2f"), VideoSettings.FPS);
                            }
                            else
                            {
                                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: No foi possvel determinar o FPS efetivo do VideoFile. Usando VideoSettings.FPS padro (%.2f) para a entrada do FFmpeg. A velocidade de sada do vdeo pode estar incorreta."), VideoSettings.FPS);
                            }
                        }
                    }
                    // Para Simulated e RenderTarget, VideoSettings.FPS j  o FPS pretendido, nenhuma sobrescrita  necessria.
                    // --- FIM: Ajuste VideoSettings.FPS ---

                    // Atualiza as dimenses reais no momento do StartRecording, caso a fonte j tenha determinado.
                    // Isso  essencial para webcam/videofile que podem demorar a reportar o tamanho.
                    if (UIVRWebcamFrameSource* WebcamSource = Cast<UIVRWebcamFrameSource>(CurrentFrameSource))
                    {
                        // Espera um pouco mais para a webcam reportar a resoluo real se ainda no o fez
                        for (int i = 0; i < 10; ++i) 
                        {
                            ActualFrameWidth = WebcamSource->GetActualFrameWidth();
                            ActualFrameHeight = WebcamSource->GetActualFrameHeight();
                            if (ActualFrameWidth > 0 && ActualFrameHeight > 0)
                            {
                                UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Webcam actual resolution determined: %dx%d after %d retries."), ActualFrameWidth, ActualFrameHeight, i+1);
                                break;
                            }
                            FPlatformProcess::Sleep(0.01f);
                        }

                        if (ActualFrameWidth <= 0 || ActualFrameHeight <= 0)
                        {
                            // Fallback para as dimenses do VideoSettings se a webcam no reportar vlido
                            ActualFrameWidth = VideoSettings.Width;
                            ActualFrameHeight = VideoSettings.Height;
                            UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Webcam reported invalid resolution (%dx%d) after polling. Using VideoSettings for recording."), ActualFrameWidth, ActualFrameHeight);
                        }
                    }
                    else if (UIVRVideoFrameSource* VideoFileSource = Cast<UIVRVideoFrameSource>(CurrentFrameSource))
                    {
                        // Espera um pouco mais para o arquivo de vdeo reportar a resoluo real
                        for (int i = 0; i < 10; ++i)
                        {
                            ActualFrameWidth = VideoFileSource->GetActualFrameWidth();
                            ActualFrameHeight = VideoFileSource->GetActualFrameHeight();
                            if (ActualFrameWidth > 0 && ActualFrameHeight > 0)
                            {
                                UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: VideoFile actual resolution determined: %dx%d after %d retries."), ActualFrameWidth, ActualFrameHeight, i+1);
                                break;
                            }
                            FPlatformProcess::Sleep(0.01f);
                        }

                        if (ActualFrameWidth <= 0 || ActualFrameHeight <= 0)
                        {
                            // Fallback para as dimenses do VideoSettings
                            ActualFrameWidth = VideoSettings.Width;
                            ActualFrameHeight = VideoSettings.Height;
                            UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: VideoFileSource reported invalid resolution (%dx%d) after polling. Using VideoSettings for recording."), ActualFrameWidth, ActualFrameHeight);
                        }
                    }
                    else // Para RenderTarget, Folder, Simulated, use as configuraes do VideoSettings diretamente
                    {
                        ActualFrameWidth = VideoSettings.Width;
                        ActualFrameHeight = VideoSettings.Height;
                    }

                    // Re-inicializa o FramePool com as dimenses reais obtidas
                    FramePool->Initialize(FramePoolSize, ActualFrameWidth, ActualFrameHeight, true); 
                    
                    if (!VideoSettings.bEnableRTFrames) 
                    {
                        StartNewTake(); 
                        if (!CurrentSession) 
                        {
                            UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: StartNewTake failed to create initial recording session. Aborting."));
                            bIsRecording = false;
                            CurrentFrameSource->StopCapture();
                            return;
                        }
                    }
                    else 
                    {
                        if (!RealTimeOutputTexture2D || RealTimeOutputTexture2D->GetSizeX() != ActualFrameWidth || RealTimeOutputTexture2D->GetSizeY() != ActualFrameHeight)
                        {
                            if (RealTimeOutputTexture2D)
                            {
                                RealTimeOutputTexture2D->ReleaseResource(); 
                                RealTimeOutputTexture2D = nullptr;
                            }

                            RealTimeOutputTexture2D = UTexture2D::CreateTransient(ActualFrameWidth, ActualFrameHeight, PF_B8G8R8A8); 
                            if (RealTimeOutputTexture2D)
                            {
                                RealTimeOutputTexture2D->UpdateResource(); 
                                RealTimeOutputTexture2D->Filter = TF_Bilinear;
                                RealTimeOutputTexture2D->CompressionSettings = TC_EditorIcon; 
                                RealTimeOutputTexture2D->SRGB = true; 
                                UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Real-Time Output Texture2D created/recreated: %dx%d."), ActualFrameWidth, ActualFrameHeight);
                            }
                            else
                            {
                                UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Failed to create RealTimeOutputTexture2D. JustRTCapture will not function correctly."));
                                bIsRecording = false;
                                CurrentFrameSource->StopCapture();
                                return;
                            }
                        }
                        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: JustRTCapture enabled. Frames will be sent via delegate."));
                    }
                    
                    OnRecordingStarted.Broadcast(); 
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Recording/Capture started. Actual Frame Size: %dx%d (used for FFmpeg and FramePool)"), ActualFrameWidth, ActualFrameHeight);
                }
                else 
                {
                    UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Cannot start recording, CurrentFrameSource is null."));
                    bIsRecording = false;
                    return;
                }
            }
        });
}

void UIVRCaptureComponent::StopRecording()
{
    AsyncTask(ENamedThreads::GameThread, [this]()
        {
            if (bIsRecording)
            {
                if (!VideoSettings.bEnableRTFrames) 
                {
                    EndCurrentTake();
                    // Gerar o master aps todos os takes terem sido finalizados individualmente
                    UIVRRecordingManager::Get()->GenerateMasterVideoAndCleanup();
                }
                
                if (CurrentFrameSource)
                {
                    CurrentFrameSource->StopCapture();
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Frame source capture stopped."));
                }
                
                bIsRecording = false;
                OnRecordingStopped.Broadcast(); 
                UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Recording stopped."));
            }
        });
}

void UIVRCaptureComponent::PauseTake()
{
    AsyncTask(ENamedThreads::GameThread, [this]()
        {
            if (bIsRecording && CurrentSession)
            {
                CurrentSession->PauseRecording();
                
                if (CurrentFrameSource)
                {
                    CurrentFrameSource->StopCapture(); 
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Frame source paused."));
                }
                OnRecordingPaused.Broadcast(); 
            }
        });
}

void UIVRCaptureComponent::ResumeTake()
{
    AsyncTask(ENamedThreads::GameThread, [this]()
        {
            if (bIsRecording && CurrentSession)
            {
                CurrentSession->ResumeRecording();

                if (CurrentFrameSource)
                {
                    CurrentFrameSource->StartCapture();
                    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Frame source resumed."));
                }
                OnRecordingResumed.Broadcast(); 
            }
        });
}

void UIVRCaptureComponent::StartNewTake()
{
    if (CurrentSession)
    {
        // Garante que o take anterior seja finalizado corretamente antes de iniciar um novo
        UIVRRecordingManager::Get()->StopRecording(CurrentSession);
        CurrentSession = nullptr; 
    }

    // Inicia uma nova sesso de gravao
    CurrentSession = UIVRRecordingManager::Get()->StartRecording(VideoSettings, ActualFrameWidth, ActualFrameHeight, FramePool);

    if (!CurrentSession)
    {
        UE_LOG(LogIVR, Error, TEXT("UIVRCaptureComponent: Failed to create new recording session for take %d. Aborting future takes."), CurrentTakeNumber + 1);
        bIsRecording = false; 
        CurrentFrameSource->StopCapture();
        return;
    }

    CurrentTakeTime = 0.0f;
    CurrentTakeNumber++;
    UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Started take %d"), CurrentTakeNumber);
}

void UIVRCaptureComponent::EndCurrentTake()
{
    if (CurrentSession)
    {
        UIVRRecordingManager::Get()->StopRecording(CurrentSession);
        CurrentSession = nullptr;
        UE_LOG(LogIVR, Log, TEXT("UIVRCaptureComponent: Ended take %d"), CurrentTakeNumber);
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
                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: Dropping frame - no recording session available."));
                if (FramePool && Frame.RawDataPtr.IsValid())
                {
                    FramePool->ReleaseFrame(Frame.RawDataPtr);
                }
            }
        }
        else // Modo JustRTCapture (saida em tempo real)
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

            // Aplica a cor de tintura do RealTimeDisplay
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
            
            // Obter a transforma��o da c�mera de captura e o FOV (na Game Thread)
            FTransform CameraTransform = FTransform::Identity;
            float CameraFOV = 90.0f; // Valor padr�o
            if (OwnedVideoCaptureComponent)
            {
                CameraTransform = OwnedVideoCaptureComponent->GetComponentTransform();
                CameraFOV = OwnedVideoCaptureComponent->FOVAngle;
            }
            
            // Chamar a fun��o de processamento de features em um thread de segundo plano, passando os par�metros
            // da goodFeaturesToTrack e a flag de debug visual.
            ProcessFrameAndFeaturesAsync(
                FrameOutput,
                CameraTransform,
                CameraFOV,
                FramePool,
                VideoSettings.IVR_GFTT_MaxCorners,
                VideoSettings.IVR_GFTT_QualityLevel,
                VideoSettings.IVR_GFTT_MinDistance,
                VideoSettings.IVR_DebugDrawFeatures
            );
        }
    }
    else 
    {
        UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: Descartando frame da fonte - no gravando ou no no modo de captura RT."));
        if (FramePool && Frame.RawDataPtr.IsValid())
        {
            FramePool->ReleaseFrame(Frame.RawDataPtr);
        }
    }
}

void UIVRCaptureComponent::UpdateTextureFromRawData(UTexture2D* Texture, const TArray<uint8>& RawData, int32 InWidth, int32 InHeight)
{
    if (!Texture || !Texture->IsValidLowLevelFast() || !Texture->GetResource())
    {
        UE_LOG(LogIVR, Error, TEXT("UpdateTextureFromRawData: Textura ou recurso RHI invlido na entrada."));
        return;
    }

    if (Texture->GetSizeX() != InWidth || Texture->GetSizeY() != InHeight)
    {
        UE_LOG(LogIVR, Error, TEXT("UpdateTextureFromRawData: Dimenses da textura (%dx%d) no correspondem s do frame (%dx%d). Frame descartado."),
               Texture->GetSizeX(), Texture->GetSizeY(), InWidth, InHeight);
        return; 
    }

    if (RawData.Num() != InWidth * InHeight * sizeof(FColor))
    {
        UE_LOG(LogIVR, Error, TEXT("UpdateTextureFromRawData: RawData size mismatch! Actual: %d, Expected: %d. Frame discarded."), RawData.Num(), InWidth * InHeight * sizeof(FColor));
        return; 
    }

    // UE_LOG(LogIVR, Warning, TEXT("UpdateTextureFromRawData: Preparing data for GPU update. RawData.Num(): %d"), RawData.Num());
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
                UE_LOG(LogIVR, Error, TEXT("UpdateTextureFromRawData: Texture or RHI resource became invalid on Render Thread. MipData will be leaked if not explicitly deleted here."));
                delete[] MipData; 
            }
        });

    // UE_LOG(LogIVR, Warning, TEXT("UpdateTextureFromRawData: UpdateTextureRegions command enqueued."));
}

FString UIVRCaptureComponent::PrepareVideoForRecording(const FString& InSourceVideoPath, const FString& OutPreparedVideoPath, bool bOverwrite)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.FileExists(*InSourceVideoPath))
    {
        UE_LOG(LogIVR, Error, TEXT("PrepareVideoForRecording: Source video file not found at: %s"), *InSourceVideoPath);
        return FString();
    }

    if (PlatformFile.FileExists(*OutPreparedVideoPath) && !bOverwrite)
    {
        UE_LOG(LogIVR, Log, TEXT("PrepareVideoForRecording: Prepared video already exists at: %s and overwrite is disabled. Using existing file."), *OutPreparedVideoPath);
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
    UE_LOG(LogIVR, Error, TEXT("PrepareVideoForRecording: FFmpeg executable path not defined for current platform!"));
    return FString();
#endif
    FPaths::NormalizeDirectoryName(FFmpegPath);

    if (!PlatformFile.FileExists(*FFmpegPath))
    {
        // CORREO: Argumento FFMpegPath adicionado ao UE_LOG
        UE_LOG(LogIVR, Error, TEXT("PrepareVideoForRecording: FFmpeg executable not found at: %s. Cannot transcode video."), *FFmpegPath);
        return FString();
    }

    FString FFmpegArguments = FString::Printf(
        TEXT("-y -i %s -c:v libx264 -preset medium -crf 23 -pix_fmt yuv420p -c:a aac -b:a 128k %s"),
        *InSourceVideoPath,
        *OutPreparedVideoPath
    );

    UE_LOG(LogIVR, Log, TEXT("PrepareVideoForRecording: Transcoding video. Executable: %s, Arguments: %s"), *FFmpegPath, *FFmpegArguments);

    if (!UIVRRecordingManager::Get()->LaunchFFmpegProcessBlocking(FFmpegPath, FFmpegArguments))
    {
        UE_LOG(LogIVR, Error, TEXT("PrepareVideoForRecording: Video transcoding failed for: %s"), *InSourceVideoPath);
        if (PlatformFile.FileExists(*OutPreparedVideoPath))
        {
            PlatformFile.DeleteFile(*OutPreparedVideoPath);
        }
        return FString();
    }

    UE_LOG(LogIVR, Log, TEXT("PrepareVideoForRecording: Video successfully transcoded to: %s"), *OutPreparedVideoPath);
    return OutPreparedVideoPath;
}

FString UIVRCaptureComponent::ExportVideoToCompatibleFormat(const FString& InSourceVideoPath, const FString& OutCompatibleVideoPath, bool bOverwrite, const FIVR_VideoSettings& EncodingSettings)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.FileExists(*InSourceVideoPath))
    {
        UE_LOG(LogIVR, Error, TEXT("ExportVideoToCompatibleFormat: Source video file not found at: %s"), *InSourceVideoPath);
        return FString();
    }

    if (PlatformFile.FileExists(*OutCompatibleVideoPath) && !bOverwrite)
    {
        UE_LOG(LogIVR, Log, TEXT("ExportVideoToCompatibleFormat: Compatible video already exists at: %s and overwrite is disabled. Using existing file."), *OutCompatibleVideoPath);
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
    UE_LOG(LogIVR, Error, TEXT("ExportVideoToCompatibleFormat: FFmpeg executable path not defined for current platform!"));
    return FString();
#endif
    FPaths::NormalizeDirectoryName(FFmpegPath);

    if (!PlatformFile.FileExists(*FFmpegPath))
    {
        // CORREO: Argumento FFMpegPath adicionado ao UE_LOG
        UE_LOG(LogIVR, Error, TEXT("ExportVideoToCompatibleFormat: FFmpeg executable not found at: %s. Cannot transcode video."), *FFmpegPath);
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

    UE_LOG(LogIVR, Log, TEXT("ExportVideoToCompatibleFormat: Transcoding video. Executable: %s, Arguments: %s"), *FFmpegPath, *FFmpegArguments);
    if (!UIVRRecordingManager::Get()->LaunchFFmpegProcessBlocking(FFmpegPath, FFmpegArguments))
    {
        UE_LOG(LogIVR, Error, TEXT("ExportVideoToCompatibleFormat: Video transcoding failed for: %s"), *InSourceVideoPath);
        if (PlatformFile.FileExists(*OutCompatibleVideoPath))
        {
            PlatformFile.DeleteFile(*OutCompatibleVideoPath);
        }
        return FString();
    }

    UE_LOG(LogIVR, Log, TEXT("ExportVideoToCompatibleFormat: Video successfully transcoded to: %s"), *OutCompatibleVideoPath);
    return OutCompatibleVideoPath;
}

void UIVRCaptureComponent::ProcessFrameAndFeaturesAsync(FIVR_JustRTFrame InOutFrame, FTransform CameraTransform, float CameraFOV, UIVRFramePool* FramePoolInstance, int32 MaxCorners, float QualityLevel, float MinDistance, bool bDebugDrawFeatures)
{
    AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [InOutFrame, CameraTransform, CameraFOV, FramePoolInstance, MaxCorners, QualityLevel, MinDistance, bDebugDrawFeatures, this]() mutable
    {
        #if WITH_OPENCV
        if (InOutFrame.RawDataBuffer.Num() > 0)
        {
            // O InputMat j� estar� na resolu��o da grava��o (InOutFrame.Width x InOutFrame.Height)
            cv::Mat InputMat(InOutFrame.Height, InOutFrame.Width, CV_8UC4, InOutFrame.RawDataBuffer.GetData());

            cv::Mat GrayMat;
            cv::cvtColor(InputMat, GrayMat, cv::COLOR_BGRA2GRAY);

            // NOVO: Limiariza��o Adaptativa para Contornos
            cv::Mat BinaryMat;
            // Adaptive Threshold: Calcula um limiar para pequenas regi�es da imagem, o que � bom para ilumina��o n�o uniforme.
            // THRESH_BINARY_INV: Inverte a imagem bin�ria (objetos claros no fundo escuro se tornam objetos escuros no fundo claro)
            // se o seu fundo for branco e os objetos escuros. Caso contr�rio, use THRESH_BINARY.
            cv::adaptiveThreshold(GrayMat, BinaryMat, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV, 11, 2);
            // Os par�metros 11 e 2 s�o block size e C. Voc� pode ajust�-los.

            // 1. **Detec��o de Cantos (Shi-Tomasi)**
            std::vector<cv::Point2f> OpenCVInterestPoints; // <<== DECLARADA AQUI, VIS�VEL EM TODA A FUN��O
            cv::goodFeaturesToTrack(GrayMat, OpenCVInterestPoints, MaxCorners, QualityLevel, MinDistance);

            // 2. **Detec��o de Quadril�teros/Ret�ngulos e Coleta de Dados Adicionais**
            std::vector<std::vector<cv::Point>> Contours;
            cv::findContours(BinaryMat, Contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

            float MaxShapeArea = FLT_MIN;
            int32 MaxShapeIndex = INDEX_NONE;
            float MinShapeArea = FLT_MAX;
            int32 MinShapeIndex = INDEX_NONE;
            
            int32 QuadsCount = 0;
            int32 RectanglesCount = 0;

            std::vector<cv::RotatedRect> AllDetectedShapesForDrawing; // Coleta todas as formas v�lidas aqui para desenho

            // Itera sobre todos os contornos detectados
            for (const auto& Contour : Contours)
            {
                double contourArea = cv::contourArea(Contour);
                // Filtra contornos muito pequenos (ru�do) ou muito grandes (borda da imagem, etc.)
                if (contourArea < 100.0 || contourArea > (InputMat.cols * InputMat.rows * 0.9)) // Exemplo de limiares de �rea
                {
                    continue;
                }

                std::vector<cv::Point> Approx;
                // Aproxima o contorno por um pol�gono
                cv::approxPolyDP(Contour, Approx, cv::arcLength(Contour, true) * 0.02, true);

                // Se for um pol�gono de 4 lados e convexo (potencialmente um quadrado/ret�ngulo)
                if (Approx.size() == 4 && cv::isContourConvex(Approx))
                {
                    cv::Rect BoundingRect = cv::boundingRect(Approx);
                    
                    FIVR_JustRTPoint ShapeData;
                    // O Point2D � o centro da forma detectada
                    ShapeData.Point2D = FVector2D(BoundingRect.x + BoundingRect.width / 2.0f, BoundingRect.y + BoundingRect.height / 2.0f); 
                    ShapeData.Size2D = FVector2D(BoundingRect.width, BoundingRect.height);

                    cv::RotatedRect RotatedRect = cv::minAreaRect(Contour); // Obt�m o ret�ngulo rotacionado que engloba o contorno
                    ShapeData.Angle = RotatedRect.angle;

                    const float SquareTolerance = 0.15f; // Toler�ncia para considerar um ret�ngulo como quadrado
                    if (BoundingRect.height > 0)
                    {
                        float AspectRatio = (float)BoundingRect.width / BoundingRect.height; // Corrigido aqui: deveria ser width/height
                        if (FMath::Abs(AspectRatio - 1.0f) < SquareTolerance)
                        {
                            ShapeData.IsQuad = true;
                            QuadsCount++;
                        }
                        else
                        {
                            ShapeData.IsQuad = false;
                            RectanglesCount++;
                        }
                    }
                    else
                    {
                        ShapeData.IsQuad = false; // Altura zero, n�o � uma forma v�lida
                    }

                    // Deprojetar o centro da forma detectada
                    DeprojectPixelToWorld(ShapeData.Point2D, CameraTransform, CameraFOV, FIntPoint(InOutFrame.Width, InOutFrame.Height), ShapeData.Point3D, ShapeData.Direction);
                    
                    InOutFrame.Features.JustRTInterestPoints.Add(ShapeData);
                    AllDetectedShapesForDrawing.push_back(RotatedRect); // Armazena a forma para desenho

                    // Atualiza �ndices do maior e menor ponto (baseado na �rea do contorno)
                    if (contourArea > MaxShapeArea)
                    {
                        MaxShapeArea = contourArea;
                        MaxShapeIndex = InOutFrame.Features.JustRTInterestPoints.Num() - 1;
                    }
                    if (contourArea < MinShapeArea)
                    {
                        MinShapeArea = contourArea;
                        MinShapeIndex = InOutFrame.Features.JustRTInterestPoints.Num() - 1;
                    }
                }
            }

            // Atribui os �ndices de maior/menor ponto e contagens de formas
            InOutFrame.Features.BiggestPointIndex = MaxShapeIndex;
            InOutFrame.Features.SmallerPointIndex = MinShapeIndex;
            InOutFrame.Features.NumOfQuads = QuadsCount;
            InOutFrame.Features.NumOfRectangles = RectanglesCount;


            // 3. **Histograma de Cor** (continua inalterado)
            std::vector<cv::Mat> BGRChannels(3); 
            cv::split(InputMat, BGRChannels); 

            int HistSize = 256; 
            float Range[] = {0, 256}; 
            const float* HistRange = {Range};
            bool Uniform = true; 
            bool Accumulate = false; 

            cv::Mat B_hist, G_hist, R_hist;
            cv::calcHist(&BGRChannels[0], 1, 0, cv::Mat(), B_hist, 1, &HistSize, &HistRange, Uniform, Accumulate);
            cv::calcHist(&BGRChannels[1], 1, 0, cv::Mat(), G_hist, 1, &HistSize, &HistRange, Uniform, Accumulate);
            cv::calcHist(&BGRChannels[2], 1, 0, cv::Mat(), R_hist, 1, &HistSize, &HistRange, Uniform, Accumulate);

            cv::normalize(B_hist, B_hist, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());
            cv::normalize(G_hist, G_hist, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());
            cv::normalize(R_hist, R_hist, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());

            InOutFrame.Features.HistogramBlue.SetNumUninitialized(HistSize);
            InOutFrame.Features.HistogramGreen.SetNumUninitialized(HistSize);
            InOutFrame.Features.HistogramRed.SetNumUninitialized(HistSize);

            for (int j = 0; j < HistSize; ++j)
            {
                InOutFrame.Features.HistogramBlue[j] = B_hist.at<float>(j);
                InOutFrame.Features.HistogramGreen[j] = G_hist.at<float>(j);
                InOutFrame.Features.HistogramRed[j] = R_hist.at<float>(j);
            }
            
            // 4. **Debug Visual (Desenhar Quadrados e Ret�ngulos e Cantos Shi-Tomasi)**
            if (bDebugDrawFeatures)
            {
                // Desenhar as formas detectadas
                for (const auto& RotatedRect : AllDetectedShapesForDrawing)
                {
                    cv::Point2f Points[4];
                    RotatedRect.points(Points); 

                    std::vector<std::vector<cv::Point>> Polygon(1);
                    for (int i = 0; i < 4; ++i)
                    {
                        Polygon[0].push_back(Points[i]);
                    }

                    // Cor verde para as formas
                    cv::Scalar Color = cv::Scalar(0, 255, 0, 255); 
                    
                    cv::polylines(InputMat, Polygon, true, Color, 5); 
                }

                // Desenhar os cantos Shi-Tomasi detectados (independentemente de serem parte de um shape)
                for (const cv::Point2f& Corner : OpenCVInterestPoints) { // <<== AQUI EST� OK, POIS OpenCVInterestPoints est� declarada no in�cio da fun��o
                    cv::circle(InputMat, Corner, 5, cv::Scalar(0, 0, 255, 255), -1); // C�rculo vermelho (BGR)
                }
            }
        }
        else 
        {
            UE_LOG(LogIVR, Error, TEXT("ProcessFrameAndFeaturesAsync: RawDataBuffer is invalid or empty. Cannot process features."));
        }
        #else 
        UE_LOG(LogIVR, Warning, TEXT("OpenCV est� desabilitado. A extra��o de caracter�sticas visuais foi ignorada."));
        #endif

        // Transmite a FIVR_JustRTFrame COMPLETA (incluindo features) para a Game Thread
        AsyncTask(ENamedThreads::GameThread, [this, InOutFrame]()
        {
            // AGORA SIM, ATUALIZA A TEXTURA DE SA�DA COM OS DADOS MODIFICADOS (incluindo desenhos)
            if (RealTimeOutputTexture2D && InOutFrame.RawDataBuffer.Num() > 0)
            {
                UpdateTextureFromRawData(RealTimeOutputTexture2D, InOutFrame.RawDataBuffer, InOutFrame.Width, InOutFrame.Height);
            }
            else
            {
                UE_LOG(LogIVR, Warning, TEXT("UIVRCaptureComponent: RealTimeOutputTexture2D ou RawDataBuffer inv�lido para sa�da RT AP�S processamento de features."));
            }

            if (OnRealTimeFrameReady.IsBound())
            {
                OnRealTimeFrameReady.Broadcast(InOutFrame);
            }
        });
    });
}

// Implementa��o da fun��o de deproje��o
void UIVRCaptureComponent::DeprojectPixelToWorld(
    const FVector2D& PixelPos,
    const FTransform& CameraTransform,
    float FOVDegrees,
    const FIntPoint& ImageResolution, 
    FVector& OutWorldLocation,
    FVector& OutWorldDirection)
{
    // Converte FOV para radianos
    const float FOVRad = FMath::DegreesToRadians(FOVDegrees);
    const float AspectRatio = (float)ImageResolution.X / ImageResolution.Y;

    // Converte coordenadas de pixel (0 a ImageSize) para NDC (-1 a 1)
    // PixelPos.X vai de 0 a ImageResolution.X
    // PixelPos.Y vai de 0 a ImageResolution.Y
    float NDC_X = (PixelPos.X / ImageResolution.X) * 2.0f - 1.0f;
    // Y-axis � invertido para coordenadas de tela vs OpenGL/NDC
    float NDC_Y = (1.0f - (PixelPos.Y / ImageResolution.Y)) * 2.0f - 1.0f; 

    // Calcula coordenadas no espa�o de visualiza��o (no plano pr�ximo de proje��o)
    // A f�rmula exata pode variar dependendo da conven��o da matriz de proje��o.
    // Esta � uma aproxima��o comum para um plano de proje��o.
    float ViewX = NDC_X * FMath::Tan(FOVRad * 0.5f) * AspectRatio;
    float ViewY = NDC_Y * FMath::Tan(FOVRad * 0.5f);
    // Assumimos que a c�mera olha ao longo do eixo -Z no seu espa�o local
    FVector ViewSpaceDirection = FVector(ViewX, ViewY, -1.0f).GetSafeNormal(); 

    // Transforma a dire��o do espa�o de visualiza��o para o espa�o do mundo
    OutWorldDirection = CameraTransform.GetRotation().RotateVector(ViewSpaceDirection);
    OutWorldLocation = CameraTransform.GetLocation(); // O raio come�a da localiza��o da c�mera
}

