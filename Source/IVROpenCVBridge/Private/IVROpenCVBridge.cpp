// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "IVROpenCVBridge.h"

DEFINE_LOG_CATEGORY(LogIVROpenCVBridge); // Descomente se DECLARE_LOG_CATEGORY_EXTERN foi descomentado no .h

#define LOCTEXT_NAMESPACE "FIVROpenCVBridgeModule"

void FIVROpenCVBridgeModule::StartupModule()
{
    // Lógica de inicialização do módulo nativo
    // (ex: registro de tipos, inicialização de bibliotecas, etc.)
}

void FIVROpenCVBridgeModule::ShutdownModule()
{
    // Lógica de desligamento do módulo nativo
    // (ex: liberação de recursos, etc.)
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FIVROpenCVBridgeModule, IVROpenCVBridge)