// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
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
#include "Core/IVRFramePool.h" 

// Inicializa��o do Singleton Instance
UIVRRecordingManager* UIVRRecordingManager::Instance = nullptr;

UIVRRecordingManager* UIVRRecordingManager::Get()
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
    UtilityVideoEncoder = NewObject<UIVRVideoEncoder>(this); // Cria uma inst�ncia para uso geral (ex: concatena��o)
    UE_LOG(LogIVR, Log, TEXT("IVR Recording Manager initialized"));
}

void UIVRRecordingManager::Cleanup()
{
    // Limpa todas as sess�es ativas no momento do Cleanup
    for (int32 i = ActiveSessions.Num() - 1; i >= 0; --i)
    {
        if (ActiveSessions[i])
        {
            StopRecording(ActiveSessions[i]); // Chamar StopRecording para cada sess�o
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

    UE_LOG(LogIVR, Log, TEXT("IVR Recording Manager cleaned up"));
}

UIVRRecordingSession* UIVRRecordingManager::StartRecording(const FIVR_VideoSettings& VideoSettings, int32 ActualFrameWidth, int32 ActualFrameHeight, UIVRFramePool* InFramePool)
{
    UIVRRecordingSession* NewSession = NewObject<UIVRRecordingSession>(this);
    if (!NewSession)
    {
        UE_LOG(LogIVR, Error, TEXT("Failed to create new IVRRecordingSession."));
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
    NewSession->StartRecording(); 
    
    ActiveSessions.Add(NewSession);
    
    UE_LOG(LogIVR, Log, TEXT("Started new recording session. FFmpeg path: %s"), *FFmpegPath);
    return NewSession;
}

void UIVRRecordingManager::StopRecording(UIVRRecordingSession* Session)
{
    if (!Session)
        return;

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
        
        CompletedTakes.Add(TakeInfo);
        UE_LOG(LogIVR, Log, TEXT("Take %d completed and added to list. File: %s"), TakeInfo.TakeNumber, *TakeInfo.FilePath);
    }
    else
    {
        UE_LOG(LogIVR, Warning, TEXT("Take completed, but file not found or path invalid: %s. Not added to CompletedTakes list."), *SessionOutputPath);
    }

    ActiveSessions.Remove(Session);
}

void UIVRRecordingManager::FinalizeAllRecordings(FString MasterVideoPath, const FIVR_VideoSettings& VideoSettings, const FString& FFmpegExecutablePath)
{
    if (CompletedTakes.Num() == 0)
    {
        UE_LOG(LogIVR, Warning, TEXT("No completed takes to finalize into a master video."));
        return;
    }

    if (!UtilityVideoEncoder)
    {
        UE_LOG(LogIVR, Error, TEXT("UtilityVideoEncoder is null. Cannot finalize recordings."));
        return;
    }

    if (!UtilityVideoEncoder->IsInitialized())
    {
        if (!UtilityVideoEncoder->Initialize(VideoSettings, FFmpegExecutablePath, VideoSettings.Width, VideoSettings.Height, nullptr)) 
        {
            UE_LOG(LogIVR, Error, TEXT("Failed to initialize UtilityVideoEncoder for concatenation."));
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

FString UIVRRecordingManager::GenerateMasterVideoAndCleanup()
{
    if (CompletedTakes.Num() == 0)
    {
        UE_LOG(LogIVR, Warning, TEXT("No completed takes to generate master video."));
        return FString();
    }

    FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    FString BaseDir = FPaths::ProjectSavedDir() / TEXT("Recordings"); 
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.DirectoryExists(*BaseDir))
    {
        PlatformFile.CreateDirectoryTree(*BaseDir);
    }
    
    FString CurrentSessionID = CompletedTakes.Num() > 0 ? CompletedTakes.Last().FilePath.Mid(CompletedTakes.Last().FilePath.Len() - 17, 5) : FGuid::NewGuid().ToString(EGuidFormats::Digits).Mid(0,5);
    MasterVideoFilePath = FString::Printf(TEXT("%s/%s_%s_Master.mp4"), *BaseDir, *Timestamp, *CurrentSessionID);
    FString ConcatListFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Recordings"), FString::Printf(TEXT("concat_list_%s.txt"), *Timestamp));
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
        return FString();
    }
    IFileManager::Get().Delete(*ConcatListFilePath);

    UE_LOG(LogIVR, Log, TEXT("Master video generated successfully: %s"), *MasterVideoFilePath);

    CleanupIndividualTakes();
    IFileManager::Get().Delete(*ConcatListFilePath);
    CompletedTakes.Empty(); 
    
    return MasterVideoFilePath;
}

// Implementa��o da fun��o LaunchFFmpegProcessBlocking (AGORA P�BLICA)
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

