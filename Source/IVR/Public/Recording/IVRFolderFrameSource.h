// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#pragma once

#include "CoreMinimal.h"
#include "Recording/IVRFrameSource.h"
// #include "ImageUtils.h" // Não é mais necessário diretamente aqui
// #include "IImageWrapper.h" // Não é mais necessário diretamente aqui
// #include "IImageWrapperModule.h" // Não é mais necessário diretamente aqui
#include "TimerManager.h" // For FTimerHandle and FTimerManager

// NOVO: Incluído para chamar a função LoadAndResizeImage do IVROpenCVBridge
#include "IVROpenCVGlobals.h"

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
    // A implementação de LoadImageFromFile agora chama IVROpenCVBridge::LoadAndResizeImage
    bool LoadImageFromFile(const FString& FilePath, TArray<uint8>& OutRawData);

private:
    // GetImageWrapperByExtention foi movido para IVROpenCVBridgeBridge.cpp
    // TSharedPtr<IImageWrapper> GetImageWrapperByExtention(FString InImagePath);

};