// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "Recording/IVRFrameSource.h"
#include "ImageUtils.h" // Necess�rio para FImageUtils
#include "IImageWrapper.h" // Necess�rio para IImageWrapper
#include "IImageWrapperModule.h" // Necess�rio para IImageWrapperModule
#include "TimerManager.h" // For FTimerHandle and FTimerManager
#include "IVRFolderFrameSource.generated.h"

UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="IVR Folder Frame Source"))
class IVR_API UIVRFolderFrameSource : public UIVRFrameSource
{
    GENERATED_BODY()

public:
    UIVRFolderFrameSource();

    virtual void Initialize(UWorld* World, const FIVR_VideoSettings& Settings, UIVRFramePool* InFramePool) override;
    virtual void Shutdown() override;
    virtual void StartCapture() override;
    virtual void StopCapture() override;

protected:
    FTimerHandle FrameReadTimerHandle;
    TArray<FString> ImageFiles;
    int32 CurrentImageIndex;

    void ReadNextFrameFromFile();
    bool LoadImageFromFile(const FString& FilePath, TArray<uint8>& OutRawData);

private:

    TSharedPtr<IImageWrapper> GetImageWrapperByExtention(FString InImagePath);

};

