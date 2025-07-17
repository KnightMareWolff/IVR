// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/IVRTypes.h" 

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

    // TORNANDO LaunchFFmpegProcessBlocking P�BLICA PARA ACESSO EXTERNO VIA SINGLETON
    // (UIVRCaptureComponent precisar� cham�-la)
    bool LaunchFFmpegProcessBlocking(const FString& ExecPath, const FString& Arguments);

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

};

