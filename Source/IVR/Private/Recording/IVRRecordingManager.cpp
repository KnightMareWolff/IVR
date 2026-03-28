// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "Recording/IVRRecordingManager.h"
#include "Recording/IVRRecordingSession.h"
#include "Recording/IVRVideoEncoder.h" 
#include "Misc/Paths.h" 
#include "Misc/DateTime.h" 
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h" 
#include "IVR.h" 
#include "IVRFramePool.h"
// Inicialização do Singleton Instance
UIVRRecordingManager* UIVRRecordingManager::Instance = nullptr;
UIVRRecordingManager* UIVRRecordingManager::Get() // <--- ÚNICA DEFINIÇÃO MANTIDA
{
    if (!Instance)
    {
        Instance = NewObject<UIVRRecordingManager>();
        Instance->AddToRoot(); // Impede o Garbage Collection de destruir o singleton
        Instance->Initialize();
    }
    return Instance;
}

void UIVRRecordingManager::Initialize()
{
    // REMOVIDO: AudioCapture = NewObject<UIVRAudioCaptureSystem>(this); 
    UtilityVideoEncoder = NewObject<UIVRVideoEncoder>(this); // Cria uma instância para uso geral (ex: concatenação)
    bIsGeneratingMasterVideo.AtomicSet(false); // <--- ALTERAÇÃO: Inicializar a nova flag
    UE_LOG(LogIVR, Log, TEXT("IVR Recording Manager initialized"));
}
void UIVRRecordingManager::Cleanup()
{
    // Limpa todas as sessões ativas no momento do Cleanup
    for (int32 i = ActiveSessions.Num() - 1; i >= 0; --i)
    {
        if (ActiveSessions[i])
        {
            StopRecording(ActiveSessions[i]); // Chamar StopRecording para cada sessão
        }
    }
    ActiveSessions.Empty(); 
    
    // REMOVIDO: AudioCapture = nullptr;
    if (UtilityVideoEncoder)
    {
        UtilityVideoEncoder->ShutdownEncoder(); // Garante que o encoder de utilidade seja desligado
        UtilityVideoEncoder = nullptr;
    }
    
    if (Instance == this) 
    {
        RemoveFromRoot(); 
        Instance = nullptr;
    }

    CurrentActiveRecordingSession.Reset(); // Limpar a referência da sessão ativa
    bIsGeneratingMasterVideo.AtomicSet(false); // <--- ALTERAÇÃO: Garantir que seja resetado

    UE_LOG(LogIVR, Log, TEXT("IVR Recording Manager cleaned up"));
}
UIVRRecordingSession* UIVRRecordingManager::StartRecording(const FIVR_VideoSettings& VideoSettings, int32 ActualFrameWidth, int32 ActualFrameHeight, UIVRFramePool* InFramePool)
{
    FScopeLock Lock(&ManagerMutex); // Adquirir lock para garantir que apenas uma operação de gravação ocorra por vez

    // <--- ALTERAÇÃO: Checagem se já há uma sessão ativa
    if (CurrentActiveRecordingSession.IsValid()) 
    {
        UE_LOG(LogIVR, Warning, TEXT("IVRRecordingManager: Uma sessão de gravação (take individual) já está ativa. Por favor, pare-a primeiro para iniciar outra."));
        return nullptr;
    }

    // <--- ALTERAÇÃO: Checagem se o manager está gerando o vídeo mestre
    if (bIsGeneratingMasterVideo) 
    {
        UE_LOG(LogIVR, Warning, TEXT("IVRRecordingManager: Manager está ocupado gerando o vídeo mestre. Não é possível iniciar uma nova sessão de gravação."));
        return nullptr;
    }

    // bIsGeneratingMasterVideo.AtomicSet(true); // <--- REMOVIDO: Esta flag não é para takes individuais

    UIVRRecordingSession* NewSession = NewObject<UIVRRecordingSession>(this);
    if (!NewSession)
    {
        UE_LOG(LogIVR, Error, TEXT("Failed to create new IVRRecordingSession."));
        // bIsGeneratingMasterVideo.AtomicSet(false); // <--- REMOVIDO: Não aplicável aqui
        return nullptr;
    }
    FString FFmpegPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("IVR"), TEXT("ThirdParty"), TEXT("FFmpeg"), TEXT("Binaries"));
#if PLATFORM_WINDOWS
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Win64"), TEXT("ffmpeg.exe"));
#elif PLATFORM_LINUX
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Linux"), TEXT("ffmpeg"));
#elif PLATFORM_MAC
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Mac"), TEXT("ffmpeg"));
#else
    UE_LOG(LogIVR, Warning, TEXT("FFmpeg path not defined for current platform. Using default path."));
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Unsupported"), TEXT("ffmpeg")); 
#endif
    FPaths::NormalizeDirectoryName(FFmpegPath);
    NewSession->Initialize(VideoSettings, FFmpegPath, ActualFrameWidth, ActualFrameHeight, InFramePool); 
    
    if (!NewSession->StartRecording()) // Checar se o StartRecording da sessão falhou
    {
        UE_LOG(LogIVR, Error, TEXT("IVRRecordingManager: Falha ao iniciar gravação na nova sessão."));
        // bIsGeneratingMasterVideo.AtomicSet(false); // <--- REMOVIDO: Não aplicável aqui
        return nullptr;
    }

    ActiveSessions.Add(NewSession);
    CurrentActiveRecordingSession = NewSession; // Manter rastreamento da sessão ativa, este é o "ocupado" para takes
    
    UE_LOG(LogIVR, Log, TEXT("Started new recording session. FFmpeg path: %s"), *FFmpegPath);
    return NewSession;
}
void UIVRRecordingManager::StopRecording(UIVRRecordingSession* Session)
{
    if (!Session)
        return;

    // <--- ALTERAÇÃO: Adicionar um lock aqui também, para proteger o acesso a CurrentActiveRecordingSession
    FScopeLock Lock(&ManagerMutex);
    // Liberar a referência da sessão ativa.
    if (CurrentActiveRecordingSession.Get() == Session)
    {
        CurrentActiveRecordingSession.Reset();
    }

    Session->StopRecording(); 
    
    FString SessionOutputPath = Session->GetOutputPath();
    if (!SessionOutputPath.IsEmpty() && FPlatformFileManager::Get().GetPlatformFile().FileExists(*SessionOutputPath))
    {
        FIVR_TakeInfo TakeInfo;
        TakeInfo.TakeNumber = CompletedTakes.Num() + 1;
        TakeInfo.Duration = Session->GetDuration(); 
        TakeInfo.StartTime = Session->GetStartTime(); 
        TakeInfo.EndTime = FDateTime::Now(); 
        TakeInfo.FilePath = SessionOutputPath; 
        TakeInfo.SessionID = Session->GetSessionID(); 
        
        // --- INÍCIO DA ALTERAÇÃO: SALVAR CONFIGURAÇÕES DE NOME CUSTOMIZADO NO TAKEINFO ---
        TakeInfo.CustomOutputFolderName = Session->UserRecordingSettings.IVR_CustomOutputFolderName;
        TakeInfo.CustomOutputBaseFilename = Session->UserRecordingSettings.IVR_CustomOutputBaseFilename;
        // --- FIM DA ALTERAÇÃO ---
        CompletedTakes.Add(TakeInfo);
        UE_LOG(LogIVR, Log, TEXT("Take %d completed and added to list. File: %s"), TakeInfo.TakeNumber, *TakeInfo.FilePath);
    }
    else
    {
        UE_LOG(LogIVR, Warning, TEXT("Take completed, but file not found or path invalid: %s. Not added to CompletedTakes list."), *SessionOutputPath);
    }
    ActiveSessions.Remove(Session);
    // <--- ALTERAÇÃO: A flag `bIsGeneratingMasterVideo` NÃO é resetada aqui, pois a concatenação pode acontecer depois.
}

void UIVRRecordingManager::FinalizeAllRecordings(FString MasterVideoPath, const FIVR_VideoSettings& VideoSettings, const FString& FFmpegExecutablePath)
{
    // <--- ALTERAÇÃO: Bloquear para operações de finalização
    FScopeLock Lock(&ManagerMutex);
    
    // <--- ALTERAÇÃO: Definir flag de ocupado no início
    bIsGeneratingMasterVideo.AtomicSet(true); 

    if (CompletedTakes.Num() == 0)
    {
        UE_LOG(LogIVR, Warning, TEXT("No completed takes to finalize into a master video."));
        bIsGeneratingMasterVideo.AtomicSet(false); // <--- ALTERAÇÃO: Resetar flag de ocupado se nenhum trabalho foi feito
        return;
    }

    if (!UtilityVideoEncoder)
    {
        UE_LOG(LogIVR, Error, TEXT("UtilityVideoEncoder is null. Cannot finalize recordings."));
        bIsGeneratingMasterVideo.AtomicSet(false); // <--- ALTERAÇÃO: Resetar flag de ocupado em caso de falha
        return;
    }
    if (!UtilityVideoEncoder->IsInitialized())
    {
        if (!UtilityVideoEncoder->Initialize(VideoSettings, FFmpegExecutablePath, VideoSettings.Width, VideoSettings.Height, nullptr)) 
        {
            UE_LOG(LogIVR, Error, TEXT("Failed to initialize UtilityVideoEncoder for concatenation."));
            bIsGeneratingMasterVideo.AtomicSet(false); // <--- ALTERAÇÃO: Resetar flag de ocupado em caso de falha
            return;
        }
    }
    TArray<FString> TakeFilePaths;
    for (const FIVR_TakeInfo& Take : CompletedTakes)
    {
        TakeFilePaths.Add(Take.FilePath);
    }
    UE_LOG(LogIVR, Log, TEXT("Starting master video concatenation of %d takes to: %s"), TakeFilePaths.Num(), *MasterVideoPath);
    if (UtilityVideoEncoder->ConcatenateVideos(TakeFilePaths, MasterVideoPath))
    {
        UE_LOG(LogIVR, Log, TEXT("Master video successfully created at: %s"), *MasterVideoPath);
        ClearAllTakes(); 
    }
    else
    {
        UE_LOG(LogIVR, Error, TEXT("Failed to create master video at: %s"), *MasterVideoPath);
    }

    UtilityVideoEncoder->ShutdownEncoder();
    bIsGeneratingMasterVideo.AtomicSet(false); // <--- ALTERAÇÃO: Resetar flag de ocupado após a finalização
}

TArray<FIVR_TakeInfo> UIVRRecordingManager::GetAllTakes() const
{
    return CompletedTakes;
}

void UIVRRecordingManager::ClearAllTakes()
{
    CompletedTakes.Empty();
    UE_LOG(LogIVR, Log, TEXT("Cleared all takes"));
}
// --- INÍCIO DA ALTERAÇÃO: CUSTOMIZAÇÃO DE NOME DE ARQUIVO E PASTA ABSOLUTA ---
FString UIVRRecordingManager::GenerateMasterVideoAndCleanup()
{
    FScopeLock Lock(&ManagerMutex); // Garantir acesso exclusivo durante a geração do vídeo mestre

    // <--- ALTERAÇÃO: Definir flag de ocupado no início
    bIsGeneratingMasterVideo.AtomicSet(true); 

    if (CompletedTakes.Num() == 0)
    {
        UE_LOG(LogIVR, Warning, TEXT("No completed takes to generate master video."));
        bIsGeneratingMasterVideo.AtomicSet(false); // <--- ALTERAÇÃO: Resetar flag de ocupado se nenhum trabalho foi feito
        return FString();
    }
    FString BaseDir;
    // Usa a subpasta customizada do último take (assumindo consistência entre takes)
    // ou o nome padrão, e verifica se é um caminho absoluto usando !FPaths::IsRelative
    if (CompletedTakes.Num() > 0 && !CompletedTakes.Last().CustomOutputFolderName.IsEmpty() && !FPaths::IsRelative(CompletedTakes.Last().CustomOutputFolderName))
    {
        BaseDir = CompletedTakes.Last().CustomOutputFolderName;
    }
    else
    {
        BaseDir = FPaths::ProjectSavedDir() / TEXT("Recordings"); 
        if (CompletedTakes.Num() > 0 && !CompletedTakes.Last().CustomOutputFolderName.IsEmpty())
        {
            BaseDir = FPaths::Combine(BaseDir, CompletedTakes.Last().CustomOutputFolderName);
        }
    }
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*BaseDir))
    {
        PlatformFile.CreateDirectoryTree(*BaseDir);
    }
    
    FString CurrentSessionID = CompletedTakes.Num() > 0 ? CompletedTakes.Last().SessionID : FGuid::NewGuid().ToString(EGuidFormats::Digits).Mid(0,5);

    // Usa o nome base customizado do último take, se disponível
    FString MasterBaseFilename = CompletedTakes.Num() > 0 && !CompletedTakes.Last().CustomOutputBaseFilename.IsEmpty()
                               ? CompletedTakes.Last().CustomOutputBaseFilename
                               : FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")); // Usa timestamp como padrão
    MasterBaseFilename = FPaths::MakeValidFileName(MasterBaseFilename); // Garante nome de arquivo válido
    MasterVideoFilePath = FPaths::Combine(BaseDir, FString::Printf(TEXT("%s_%s_Master.mp4"), *MasterBaseFilename, *CurrentSessionID));
    // --- FIM DA ALTERAÇÃO ---
    FString ConcatListFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Recordings"), FString::Printf(TEXT("concat_list_%s.txt"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
    FString ConcatListContent;
    TArray<FString> TakeFilePaths;
    for (const FIVR_TakeInfo& Take : CompletedTakes)
    {
        ConcatListContent += FString::Printf(TEXT("file '%s'\n"), *Take.FilePath);
        TakeFilePaths.Add(Take.FilePath); 
    }
    if (!FFileHelper::SaveStringToFile(ConcatListContent, *ConcatListFilePath))
    {
        UE_LOG(LogIVR, Error, TEXT("Failed to save concat list file to: %s"), *ConcatListFilePath);
        bIsGeneratingMasterVideo.AtomicSet(false); // <--- ALTERAÇÃO: Resetar flag de ocupado em caso de falha
        return FString();
    }
    FString FFmpegPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("IVR"), TEXT("ThirdParty"), TEXT("FFmpeg"), TEXT("Binaries"));
#if PLATFORM_WINDOWS
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Win64"), TEXT("ffmpeg.exe"));
#elif PLATFORM_LINUX
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Linux"), TEXT("ffmpeg"));
#elif PLATFORM_MAC
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Mac"), TEXT("ffmpeg"));
#else
    UE_LOG(LogIVR, Warning, TEXT("FFmpeg path not defined for current platform. Using default path."));
    FFmpegPath = FPaths::Combine(FFmpegPath, TEXT("Unsupported"), TEXT("ffmpeg"));
#endif
    FPaths::NormalizeDirectoryName(FFmpegPath);
    FString FFmpegArguments = FString::Printf(TEXT("-y -f concat -safe 0 -i %s -c copy -map 0:v %s"), *ConcatListFilePath, *MasterVideoFilePath); // Adicionado -map 0:v
    UE_LOG(LogIVR, Log, TEXT("Launching FFmpeg for concatenation. Executable: %s , Arguments: %s"), *FFmpegPath, *FFmpegArguments);
    if (!LaunchFFmpegProcessBlocking(FFmpegPath, FFmpegArguments))
    {
        UE_LOG(LogIVR, Error, TEXT("FFmpeg concatenation process failed."));
        IFileManager::Get().Delete(*ConcatListFilePath);
        bIsGeneratingMasterVideo.AtomicSet(false); // <--- ALTERAÇÃO: Resetar flag de ocupado em caso de falha
        return FString();
    }
    IFileManager::Get().Delete(*ConcatListFilePath);
    UE_LOG(LogIVR, Log, TEXT("Master video generated successfully: %s"), *MasterVideoFilePath);

    CleanupIndividualTakes();
    IFileManager::Get().Delete(*ConcatListFilePath);
    CompletedTakes.Empty(); 
    
    bIsGeneratingMasterVideo.AtomicSet(false); // <--- ALTERAÇÃO: CRÍTICO: Resetar flag de ocupado SOMENTE APÓS TODA A FINALIZAÇÃO DO MESTRE

    return MasterVideoFilePath;
}
// Implementação da função LaunchFFmpegProcessBlocking (AGORA PÚBLICA)
bool UIVRRecordingManager::LaunchFFmpegProcessBlocking(const FString& ExecPath, const FString& Arguments)
{
    FProcHandle ProcHandle = FPlatformProcess::CreateProc(
        *ExecPath,
        *Arguments,
        false,   // bLaunchDetached
        true,    // bLaunchHidden
        true,    // bLaunchReallyHidden
        nullptr, // OutProcessID
        -1,      // PriorityModifier
        nullptr, // OptionalWorkingDirectory
        nullptr, // StdOutPipeWrite
        nullptr  // StdErrPipeWrite
    );
    if (!ProcHandle.IsValid())
    {
        UE_LOG(LogIVR, Error, TEXT("Failed to launch FFmpeg concat process. Check path and arguments."));
        return false;
    }
    FPlatformProcess::WaitForProc(ProcHandle);
    int32 ReturnCode = -1;
    FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
    FPlatformProcess::CloseProc(ProcHandle);
    if (ReturnCode != 0)
    {
        UE_LOG(LogIVR, Error, TEXT("FFmpeg concat process exited with error code: %d"), ReturnCode);
        return false;
    }
    return true;
}
void UIVRRecordingManager::CleanupIndividualTakes()
{
    IFileManager& FileManager = IFileManager::Get();
    for (const FIVR_TakeInfo& Take : CompletedTakes)
    {
        if (FileManager.FileExists(*Take.FilePath))
        {
            if (FileManager.Delete(*Take.FilePath))
            {
                UE_LOG(LogIVR, Log, TEXT("Deleted individual take file: %s"), *Take.FilePath);
            }
            else
            {
                UE_LOG(LogIVR, Warning, TEXT("Failed to delete individual take file: %s"), *Take.FilePath);
            }
        }
    }
}