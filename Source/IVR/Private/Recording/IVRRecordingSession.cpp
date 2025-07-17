// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Recording/IVRRecordingSession.h"
#include "IVR.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"     
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"    
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Async/Async.h" 

// Implementa��o do LogCategory
DEFINE_LOG_CATEGORY(LogIVRRecSession);

UIVRRecordingSession::UIVRRecordingSession()
    : VideoEncoder(nullptr) 
    , RecordingThread(nullptr)
    , HasNewFrameEvent(FPlatformProcess::GetSynchEventFromPool(true)) 
    , FramePool(nullptr) // Inicializa o FramePool
{
}

UIVRRecordingSession::~UIVRRecordingSession()
{
    // Garante que a grava��o seja interrompida e os recursos liberados.
    // Apenas chame StopRecording se a sess�o estiver ativa, para evitar warnings desnecess�rios.
    if (bIsRecording || bIsPaused || RecordingThread != nullptr || (VideoEncoder != nullptr && VideoEncoder->IsInitialized()))
    {
        StopRecording(); 
    }
    
    if (HasNewFrameEvent)
    {
        FGenericPlatformProcess::ReturnSynchEventToPool(HasNewFrameEvent);
        HasNewFrameEvent = nullptr;
    }
}

void UIVRRecordingSession::Initialize(const FIVR_VideoSettings& InVideoSettings, const FString& InFFmpegExecutablePath, int32 InActualFrameWidth, int32 InActualFrameHeight, UIVRFramePool* InFramePool)
{
    UserRecordingSettings = InVideoSettings;
    
    FramePool = InFramePool;
    if (!FramePool) // Verifica se o FramePool � v�lido
    {
        UE_LOG(LogIVRRecSession, Error, TEXT("UIVRRecordingSession::Initialize: FramePool is null. Cannot initialize."));
        return;
    }

    // Garante que o VideoEncoder seja inicializado uma �nica vez para esta sess�o/take.
    if (!VideoEncoder)
    {
        VideoEncoder = NewObject<UIVRVideoEncoder>(this);
        if (!VideoEncoder)
        {
            UE_LOG(LogIVRRecSession, Error, TEXT("Failed to create UIVRVideoEncoder instance."));
            return;
        }

        // Inicializa o VideoEncoder com as configura��es, o caminho do FFmpeg e a resolu��o real e o FramePool.
        if (!VideoEncoder->Initialize(UserRecordingSettings, InFFmpegExecutablePath, InActualFrameWidth, InActualFrameHeight, FramePool)) 
        {
            UE_LOG(LogIVRRecSession, Error, TEXT("Failed to initialize UIVRVideoEncoder."));
            VideoEncoder = nullptr; 
            return;
        }
    }
    
    SessionID = FGuid::NewGuid().ToString(EGuidFormats::Digits).Mid(0,5);
    CurrentTakeFilePath.Empty(); // Limpa o caminho do take anterior, se houver.
    
     UE_LOG(LogIVRRecSession, Log, TEXT("IVR Recording Session Initialized. FFmpeg Path: %s. Actual Frame Size: %dx%d"), *InFFmpegExecutablePath, InActualFrameWidth, InActualFrameHeight);
}

bool UIVRRecordingSession::StartRecording()
{
    if (bIsRecording || bIsPaused)
    {
        UE_LOG(LogIVRRecSession, Warning, TEXT("Recording is already in progress. Call StopRecording() first."));
        return false;
    }

    if (!VideoEncoder || !VideoEncoder->IsInitialized())
    {
        UE_LOG(LogIVRRecSession, Error, TEXT("VideoEncoder is not initialized. Cannot start recording."));
        return false;
    }

    bIsRecording.AtomicSet(true);
    bIsPaused.AtomicSet(false);
    bStopThread.AtomicSet(false);
    StartTime = FDateTime::Now();

    ClearQueues(); // Limpa as filas de frames
    
    // Gera o caminho completo para o take atual.
    CurrentTakeFilePath = GenerateTakeFilePath(); 

    // Lan�a o processo FFmpeg atrav�s do VideoEncoder.
    // Passa o caminho do take atual para o encoder.
    if (!VideoEncoder->LaunchEncoder(CurrentTakeFilePath))
    {
        UE_LOG(LogIVRRecSession, Error, TEXT("Failed to launch FFmpeg process via VideoEncoder. Aborting recording."));
        bIsRecording.AtomicSet(false);
        if (VideoEncoder && VideoEncoder->IsInitialized()) 
        {
            VideoEncoder->ShutdownEncoder();
        }
        return false;
    }

    // Inicia o thread de grava��o local (que apenas enfileira frames no VideoEncoder)
    RecordingThread = FRunnableThread::Create(this, TEXT("IVRecThread"), 0, TPri_Normal);
    if (!RecordingThread)
    {
        UE_LOG(LogIVRRecSession, Error, TEXT("Failed to create recording thread."));
        if (VideoEncoder && VideoEncoder->IsInitialized()) 
        {
            VideoEncoder->ShutdownEncoder(); 
        }
        bIsRecording.AtomicSet(false);
        return false;
    }

    UE_LOG(LogIVRRecSession, Log, TEXT("FFmpeg recording session started for take: %s"), *CurrentTakeFilePath);
    return true; 
}

void UIVRRecordingSession::StopRecording()
{
    // Valida��o robusta para evitar warnings falsos ou tentar parar algo que j� est� parado.
    bool bNeedsStopping = bIsRecording || bIsPaused || RecordingThread != nullptr || (VideoEncoder != nullptr && VideoEncoder->IsInitialized());

    if (!bNeedsStopping)
    {
        UE_LOG(LogIVRRecSession, Warning, TEXT("Attempted to stop recording, but session is not active or already stopped. No actions taken."));
        return;
    }

    UE_LOG(LogIVRRecSession, Log, TEXT("Stopping IVR Recording Session for take..."));

    bIsRecording.AtomicSet(false);
    bIsPaused.AtomicSet(false);
    bStopThread.AtomicSet(true); 

    if (HasNewFrameEvent)
    {
        HasNewFrameEvent->Trigger();
    }

    // Primeiro, sinaliza ao VideoEncoder que n�o haver� mais frames para grava��o do take atual.
    if (VideoEncoder && VideoEncoder->IsInitialized())
    {
        VideoEncoder->FinishEncoding();
    }

    // Espera o thread de grava��o local terminar
    if (RecordingThread)
    {
        UE_LOG(LogIVRRecSession, Log, TEXT("Waiting for recording thread to complete..."));
        RecordingThread->WaitForCompletion(); 
        delete RecordingThread;
        RecordingThread = nullptr;
        UE_LOG(LogIVRRecSession, Log, TEXT("Recording thread stopped."));
    }

    // Agora, desliga o processo principal do FFmpeg atrav�s do VideoEncoder.
    if (VideoEncoder && VideoEncoder->IsInitialized())
    {
        VideoEncoder->ShutdownEncoder();
    }

    // Calcula a dura��o final da grava��o
    RecordingDuration = (FDateTime::Now() - StartTime).GetTotalSeconds();
    UE_LOG(LogIVRRecSession, Log, TEXT("Take recording stopped. Final duration: %.2f seconds. File intended for: %s"), RecordingDuration, *CurrentTakeFilePath);
    
    // A valida��o se o arquivo foi realmente criado ser� feita pelo UIVRRecordingManager.
    // NENHUMA L�GICA DE CONCATENA��O AQUI.
}

void UIVRRecordingSession::PauseRecording()
{
    if (bIsRecording && !bIsPaused)
    {
        bIsPaused.AtomicSet(true);
        UE_LOG(LogIVRRecSession, Log, TEXT("Recording session paused for take: %s"), *SessionID);
    }
}

void UIVRRecordingSession::ResumeRecording()
{
    if (bIsRecording && bIsPaused)
    {
        bIsPaused.AtomicSet(false);
        UE_LOG(LogIVRRecSession, Log, TEXT("Recording session resumed for take: %s"), *SessionID);
    }
}

float UIVRRecordingSession::GetDuration() const
{
    if (bIsRecording && !bIsPaused)
    {
        return (FDateTime::Now() - StartTime).GetTotalSeconds();
    }
    return RecordingDuration;
}

void UIVRRecordingSession::ClearQueues()
{
    FIVR_VideoFrame DummyVideoFrame;
    while (VideoFrameProducerQueue.Dequeue(DummyVideoFrame)) {}

    VideoConsumerQCounter = 0;
    VideoProducerQCounter = 0;
}

// Adiciona um frame de v�deo � fila. Timestamp j� � o tempo global.
void UIVRRecordingSession::AddVideoFrame(FIVR_VideoFrame Frame) // Assinatura mudada
{
    if (!bIsRecording || bIsPaused) 
    {
        // Se n�o estiver gravando ou estiver pausado, libera o frame imediatamente.
        if (FramePool && Frame.RawDataPtr.IsValid())
        {
            FramePool->ReleaseFrame(Frame.RawDataPtr);
        }
        return;
    }

    // Use a taxa de quadros alvo para estimar um limite de buffer mais preciso
    int32 MaxBufferedFrames = (UserRecordingSettings.FPS > 0) ? FMath::CeilToInt(UserRecordingSettings.FPS * 1.0f) : 30; 
    if (VideoProducerQCounter >= MaxBufferedFrames) 
    {
        UE_LOG(LogIVRRecSession, Warning, TEXT("Video frame producer queue is full (%d frames, max %d). Dropping frame."), VideoProducerQCounter, MaxBufferedFrames);
        // Libera o frame para o pool se a fila estiver cheia.
        if (FramePool && Frame.RawDataPtr.IsValid())
        {
            FramePool->ReleaseFrame(Frame.RawDataPtr);
        }
        return; 
    }

    // LOG DE DEBUG: Confirma o tamanho do frame que est� sendo enfileirado
    UE_LOG(LogIVRRecSession, Warning, TEXT("UIVRRecordingSession: Enqueuing frame. RawDataPtr size: %d"), 
        Frame.RawDataPtr.IsValid() ? Frame.RawDataPtr->Num() : 0);

    // Enfileira o frame para a worker thread processar. Usa MoveTemp para mover o TSharedPtr eficientemente.
    VideoFrameProducerQueue.Enqueue(MoveTemp(Frame)); 
    VideoProducerQCounter++;

    if (HasNewFrameEvent)
    {
        HasNewFrameEvent->Trigger(); 
    }
}

FString UIVRRecordingSession::GenerateTakeFilePath()
{
    FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    FString BaseDir = FPaths::ProjectSavedDir() / TEXT("Recordings"); 
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.DirectoryExists(*BaseDir))
    {
        PlatformFile.CreateDirectoryTree(*BaseDir);
    }
    
    FString NewTakePath = FString::Printf(TEXT("%s/%s_%s_Take.mp4"), *BaseDir, *Timestamp, *SessionID);
    UE_LOG(LogIVRRecSession, Log, TEXT("Generated take file: %s"), *NewTakePath);
    return NewTakePath;
}

FString UIVRRecordingSession::GenerateMasterFilePath() const
{
    FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    FString BaseDir = FPaths::ProjectSavedDir() / TEXT("Recordings"); 
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.DirectoryExists(*BaseDir))
    {
        PlatformFile.CreateDirectoryTree(*BaseDir);
    }
    
    FString NewTakePath = FString::Printf(TEXT("%s/%s_%s_Master.mp4"), *BaseDir, *Timestamp, *SessionID);
    UE_LOG(LogIVRRecSession, Log, TEXT("Generated Master file: %s"), *NewTakePath);
    return NewTakePath;
}

// --- FRunnable implementation ---
bool UIVRRecordingSession::Init()
{
    UE_LOG(LogIVRRecSession, Log, TEXT("IVRecThread: Initialized."));
    return true;
}

uint32 UIVRRecordingSession::Run()
{
    
    FIVR_VideoFrame VideoFrame; // Onde o frame � dequeued
    
    while (!bStopThread)
    {
        // Tenta processar todos os frames na fila
        while (VideoFrameProducerQueue.Dequeue(VideoFrame))
        {
            VideoProducerQCounter--; 
            if (VideoEncoder)
            {
                // O VideoEncoder->EncodeFrame agora aceita a FIVR_VideoFrame completa
                // e acessa VideoFrame.RawDataPtr.
                VideoEncoder->EncodeFrame(VideoFrame); 
                VideoConsumerQCounter++; 
            }
            else
            {
                UE_LOG(LogIVRRecSession, Error, TEXT("IVRecThread: VideoEncoder is null. Dropping frame."));
            }
        }
        
        // Se a fila estiver vazia e o thread n�o for para parar, espera por um novo evento.
        if (VideoFrameProducerQueue.IsEmpty() && !bStopThread)
        {
            HasNewFrameEvent->Wait(100); // Espera por at� 100ms por um novo frame ou sinal de parada
        }
    }
    
    // Processa quaisquer frames remanescentes na fila antes de sair
    while (VideoFrameProducerQueue.Dequeue(VideoFrame))
    {
        VideoProducerQCounter--;
        if (VideoEncoder)
        {
            VideoEncoder->EncodeFrame(VideoFrame);
            VideoConsumerQCounter++;
        }
    }

    UE_LOG(LogIVRRecSession, Log, TEXT("IVRecThread: Run loop finished."));
    return 0;
}

void UIVRRecordingSession::Stop()
{
    UE_LOG(LogIVRRecSession, Log, TEXT("IVRecThread: Stop signal received."));
}

void UIVRRecordingSession::Exit()
{
    UE_LOG(LogIVRRecSession, Log, TEXT("IVRecThread: Exited."));
}

