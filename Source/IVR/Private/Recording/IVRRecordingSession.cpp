// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
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
// [MANUAL_REF_POINT] Includes para classes nativas agora no IVROpenCVBridge
#include "../IVRGlobalStatics.h"
#include "IVROpenCVBridge/Public/IVR_PipeWrapper.h"
#include "IVRVideoEncoder.h" // Inclui o novo encoder centralizado
#include "IVRFramePool.h" // Adicionar este include!
// Implementação do LogCategory
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
    // <--- ALTERAÇÃO: Garante que a gravação seja interrompida e os recursos liberados.
    // Isso é uma rede de segurança para casos em que StopRecording() explícito pode ter sido perdido ou interrompido.
    if (bIsRecording || bIsPaused || RecordingThread != nullptr || (VideoEncoder != nullptr && VideoEncoder->IsInitialized()))
    {
        UE_LOG(LogIVRRecSession, Warning, TEXT("~UIVRRecordingSession: Forcibly stopping recording during destruction. SessionID: %s"), *SessionID);
        StopRecording(); 
    }
    
    // <--- ALTERAÇÃO: Explicitamente juntar a thread de gravação aqui se ela ainda estiver ativa.
    // Isso é crucial para prevenir crashes se a thread ainda estiver rodando quando o UObject é destruído.
    if (RecordingThread)
    {
        UE_LOG(LogIVRRecSession, Warning, TEXT("~UIVRRecordingSession: Waiting for RecordingThread to complete during destruction. SessionID: %s"), *SessionID);
        RecordingThread->WaitForCompletion();
        delete RecordingThread;
        RecordingThread = nullptr;
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
    if (!FramePool) // Verifica se o FramePool é válido
    {
        UE_LOG(LogIVRRecSession, Error, TEXT("UIVRRecordingSession::Initialize: FramePool é nulo. Cannot initialize."));
        return;
    }
    // Garante que o VideoEncoder seja inicializado uma única vez para esta sessão/take.
    if (!VideoEncoder)
    {
        VideoEncoder = NewObject<UIVRVideoEncoder>(this);
        if (!VideoEncoder)
        {
            UE_LOG(LogIVRRecSession, Error, TEXT("Failed to create UIVRVideoEncoder instance."));
            return;
        }
        // Inicializa o VideoEncoder com as configurações, o caminho do FFmpeg e a resolução real e o FramePool.
        if (!VideoEncoder->Initialize(UserRecordingSettings, InFFmpegExecutablePath, InActualFrameWidth, InActualFrameHeight, InFramePool)) 
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
    // Lança o processo FFmpeg através do VideoEncoder.
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
    // Inicia o thread de gravação local (que apenas enfileira frames no VideoEncoder)
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
    // Validação robusta para evitar warnings falsos ou tentar parar algo que já está parado.
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

    // <--- ALTERAÇÃO: 1. Sinaliza ao VideoEncoder que não haverá mais frames e ESPERA que ele termine de escrever.
    if (VideoEncoder && VideoEncoder->IsInitialized())
    {
        // Esta é uma chamada bloqueante CRÍTICA na game thread para garantir que todos os frames sejam escritos
        // e o pipe seja fechado para o FFmpeg.
        UE_LOG(LogIVRRecSession, Log, TEXT("StopRecording for SessionID %s: Calling VideoEncoder->FinishEncoding() and waiting."), *SessionID);
        VideoEncoder->FinishEncoding(); // Esta função já aguarda a FrameQueue esvaziar e o pipe fechar.
    }

    // <--- ALTERAÇÃO: 2. Espera o thread de gravação local terminar (já sinalizado com bStopThread = true)
    if (RecordingThread)
    {
        UE_LOG(LogIVRRecSession, Log, TEXT("StopRecording for SessionID %s: Waiting for RecordingThread to complete."), *SessionID);
        RecordingThread->WaitForCompletion(); 
        delete RecordingThread;
        RecordingThread = nullptr;
        UE_LOG(LogIVRRecSession, Log, TEXT("Recording thread stopped for SessionID %s."), *SessionID);
    }

    // <--- ALTERAÇÃO: 3. Agora, desliga o processo principal do FFmpeg e libera todos os recursos.
    // Esta chamada é crucial para garantir que o processo FFmpeg seja fechado e o handle do pipe liberado corretamente.
    if (VideoEncoder && VideoEncoder->IsInitialized())
    {
        UE_LOG(LogIVRRecSession, Log, TEXT("StopRecording for SessionID %s: Calling VideoEncoder->ShutdownEncoder()."), *SessionID);
        VideoEncoder->ShutdownEncoder(); // Isso internamente aguarda o processo FFmpeg.
    }

    // Calcula a duração final da gravação
    RecordingDuration = (FDateTime::Now() - StartTime).GetTotalSeconds();
    UE_LOG(LogIVRRecSession, Log, TEXT("Take recording stopped. Final duration: %.2f seconds. File intended for: %s"), RecordingDuration, *CurrentTakeFilePath);
    
    // A validação se o arquivo foi realmente criado será feita pelo UIVRRecordingManager.
    // NENHUMA LÓGICA DE CONCATENAÇÃO AQUI.
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
    while (VideoFrameProducerQueue.Dequeue(DummyVideoFrame))
    {
        // Se a fila tiver frames e houver um FramePool válido, libera-os para evitar vazamentos.
        if (FramePool && DummyVideoFrame.RawDataPtr.IsValid())
        {
            FramePool->ReleaseFrame(DummyVideoFrame.RawDataPtr);
        }
    }
    VideoConsumerQCounter = 0;
    VideoProducerQCounter = 0;
}
// Adiciona um frame de vídeo à fila. Timestamp já é o tempo global.
void UIVRRecordingSession::AddVideoFrame(FIVR_VideoFrame Frame) // Assinatura mudada
{
    if (!bIsRecording || bIsPaused) 
    {
        // Se não estiver gravando ou estiver pausado, libera o frame imediatamente.
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
    // LOG DE DEBUG: Confirma o tamanho do frame que está sendo enfileirado
    // UE_LOG(LogIVRRecSession, Warning, TEXT("UIVRRecordingSession: Enqueuing frame. RawDataPtr size: %d"), 
    //    Frame.RawDataPtr.IsValid() ? Frame.RawDataPtr->Num() : 0); // Descomente para debug intenso
    // Enfileira o frame para a worker thread processar. Usa MoveTemp para mover o TSharedPtr eficientemente.
    VideoFrameProducerQueue.Enqueue(MoveTemp(Frame)); 
    VideoProducerQCounter++;
    if (HasNewFrameEvent)
    {
        HasNewFrameEvent->Trigger(); 
    }
}
// --- INÍCIO DA ALTERAÇÃO: CUSTOMIZAÇÃO DE NOME DE ARQUIVO ABSOLUTO (USANDO !FPaths::IsRelative) ---
FString UIVRRecordingSession::GenerateTakeFilePath()
{
    FString BaseDir;
    // Verifica se o CustomOutputFolderName é um caminho absoluto usando !FPaths::IsRelative
    if (!UserRecordingSettings.IVR_CustomOutputFolderName.IsEmpty() && !FPaths::IsRelative(UserRecordingSettings.IVR_CustomOutputFolderName))
    {
        BaseDir = UserRecordingSettings.IVR_CustomOutputFolderName;
    }
    else
    {
        // Caso contrário, usa o caminho padrão dentro de Saved/Recordings
        BaseDir = FPaths::ProjectSavedDir() / TEXT("Recordings");
        // Adiciona subpasta customizada se especificado e não for absoluto
        if (!UserRecordingSettings.IVR_CustomOutputFolderName.IsEmpty())
        {
            BaseDir = FPaths::Combine(BaseDir, UserRecordingSettings.IVR_CustomOutputFolderName);
        }
    }
    
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    // Garante que o diretório base exista.
    if (!PlatformFile.DirectoryExists(*BaseDir))
    {
        PlatformFile.CreateDirectoryTree(*BaseDir);
    }
    
    FString BaseFilenamePart = UserRecordingSettings.IVR_CustomOutputBaseFilename;
    if (BaseFilenamePart.IsEmpty())
    {
        // Padrão se nenhum nome customizado for fornecido
        BaseFilenamePart = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    }
    // Garante que o nome do arquivo seja válido para o sistema de arquivos
    BaseFilenamePart = FPaths::MakeValidFileName(BaseFilenamePart);
    FString NewTakePath = FPaths::Combine(BaseDir, FString::Printf(TEXT("%s_%s_Take.mp4"), *BaseFilenamePart, *SessionID));
    UE_LOG(LogIVRRecSession, Log, TEXT("Generated take file: %s"), *NewTakePath);
    return NewTakePath;
}
// --- FIM DA ALTERAÇÃO ---
// C4: Ajuste para gerar o Master File Path de forma independente
FString UIVRRecordingSession::GenerateMasterFilePath() const
{
    FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    FString BaseDir = FPaths::ProjectSavedDir() / TEXT("Recordings"); 
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    // C4: Garante que o diretório "Recordings" exista.
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
    
    FIVR_VideoFrame VideoFrame; // Onde o frame é dequeued
    
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
                // Garante que o frame seja liberado de volta ao pool mesmo se o encoder for nulo
                if (FramePool && VideoFrame.RawDataPtr.IsValid())
                {
                    FramePool->ReleaseFrame(VideoFrame.RawDataPtr);
                }
            }
        }
        
        // Se a fila estiver vazia e o thread não for para parar, espera por um novo evento.
        if (VideoFrameProducerQueue.IsEmpty() && !bStopThread)
        {
            HasNewFrameEvent->Wait(100); // Espera por até 100ms por um novo frame ou sinal de parada
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
        else
        {
             // Garante que o frame seja liberado de volta ao pool mesmo se o encoder for nulo durante o shutdown
            if (FramePool && VideoFrame.RawDataPtr.IsValid())
            {
                FramePool->ReleaseFrame(VideoFrame.RawDataPtr);
            }
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