// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "IVRTypes.h" 
#include "HAL/ThreadSafeBool.h" // <--- NOVA LINHA: Para FThreadSafeBool
#include "HAL/CriticalSection.h" // <--- NOVA LINHA: Para FCriticalSection

#include "IVRRecordingManager.generated.h"

class UIVRRecordingSession;
class UIVRVideoEncoder; 
// REMOVIDO: class UIVRAudioCaptureSystem; 
class UIVRFramePool; 

UCLASS()
class IVR_API UIVRRecordingManager : public UObject
{
    GENERATED_BODY()

public:
    static UIVRRecordingManager* Get();

    UFUNCTION(BlueprintCallable, Category = "IVR")
    UIVRRecordingSession* StartRecording(const FIVR_VideoSettings& VideoSettings, int32 pActualFrameWidth, int32 pActualFrameHeight, UIVRFramePool* InFramePool);

    UFUNCTION(BlueprintCallable, Category = "IVR")
    void StopRecording(UIVRRecordingSession* Session);

    UFUNCTION(BlueprintCallable, Category = "IVR")
    void FinalizeAllRecordings(FString MasterVideoPath, const FIVR_VideoSettings& VideoSettings, const FString& FFmpegExecutablePath);

    UFUNCTION(BlueprintCallable, Category = "IVR")
    TArray<FIVR_TakeInfo> GetAllTakes() const;

    UFUNCTION(BlueprintCallable, Category = "IVR")
    void ClearAllTakes();

    UFUNCTION(BlueprintCallable, Category = "IVR|Recording")
    FString GenerateMasterVideoAndCleanup();

    // TORNANDO LaunchFFmpegProcessBlocking PÚBLICA PARA ACESSO EXTERNO VIA SINGLETON
    // (UIVRCaptureComponent precisará chamá-la)
    bool LaunchFFmpegProcessBlocking(const FString& ExecPath, const FString& Arguments);

    // <--- ALTERAÇÃO: Novo getter para a flag de geração de vídeo mestre
    UFUNCTION(BlueprintPure, Category = "IVR")
    bool IsGeneratingMasterVideo() const { return bIsGeneratingMasterVideo; }
    
    // <--- ALTERAÇÃO: Novo setter para a flag de geração de vídeo mestre
    void SetGeneratingMasterVideo(bool bState) { bIsGeneratingMasterVideo.AtomicSet(bState); } 

private:
    static UIVRRecordingManager* Instance;
    
    UPROPERTY()
    TArray<UIVRRecordingSession*> ActiveSessions;

    UPROPERTY()
    TArray<FIVR_TakeInfo> CompletedTakes;

    // REMOVIDO: UPROPERTY() UIVRAudioCaptureSystem* AudioCapture; 

    UPROPERTY()
    UIVRVideoEncoder* UtilityVideoEncoder; 

    void Initialize();
    void Cleanup();

    UPROPERTY()
    FString MasterVideoFilePath;

    FString BuildFFmpegConcatCommand(const TArray<FString>& TakeFilePaths, const FString& OutputMasterPath);
    
    void CleanupIndividualTakes();

    // <--- ALTERAÇÃO: Renomeado bIsManagerBusy para bIsGeneratingMasterVideo
    FThreadSafeBool bIsGeneratingMasterVideo = false; 
    TWeakObjectPtr<UIVRRecordingSession> CurrentActiveRecordingSession; // Manter uma referência fraca para garantir apenas uma sessão principal ativa
    FCriticalSection ManagerMutex; // Mutex para acesso thread-safe ao estado do manager
};