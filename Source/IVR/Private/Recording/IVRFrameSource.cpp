// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "Recording/IVRFrameSource.h"
#include "IVR.h" // Inclua a LogCategory

DEFINE_LOG_CATEGORY(LogIVRFrameSource);

UIVRFrameSource::UIVRFrameSource()
{
}

void UIVRFrameSource::BeginDestroy()
{
    Super::BeginDestroy();
}

TSharedPtr<TArray<uint8>> UIVRFrameSource::AcquireFrameBufferFromPool()
{
    if (!FramePool)
    {
        UE_LOG(LogIVRFrameSource, Error, TEXT("FramePool is invalid. Cannot acquire frame buffer."));
        return nullptr;
    }
    return FramePool->AcquireFrame();
}

