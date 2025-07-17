// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "IVR.h"

DEFINE_LOG_CATEGORY(LogIVR);

#define LOCTEXT_NAMESPACE "FIVRModule"

void FIVRModule::StartupModule()
{
    UE_LOG(LogIVR, Log, TEXT("IVR Module Started"));
}

void FIVRModule::ShutdownModule()
{
    UE_LOG(LogIVR, Log, TEXT("IVR Module Shutdown"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FIVRModule, IVR)

