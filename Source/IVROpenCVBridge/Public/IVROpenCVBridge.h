// D:\william\UnrealProjects\IVRExample\Plugins\IVR\Source\IVROpenCVBridge\Public\IVROpenCVBridge.h
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// --- CORREÇÃO FINAL DA DEFINIÇÃO DA MACRO _API ---
// Esta é a forma padrão e robusta para definir a macro _API em módulos Unreal Engine.
// Ela garante que os símbolos são exportados quando o próprio módulo é compilado
// e importados quando outros módulos o utilizam.

#ifndef IVROPENCVBRIDGE_API
#if defined(IS_PROGRAM) || defined(IS_MONOLITHIC)
    // Para programas monolíticos ou executáveis completos (raro para plugins independentes)
    #define IVROPENCVBRIDGE_API
#else
    // Quando o próprio módulo IVROpenCVBridge está sendo construído, definimos IVROPENCVBRIDGE_API como DLLEXPORT.
    // O símbolo COMPILE_IVROPENCVBRIDGE é definido no IVROpenCVBridge.Build.cs para este propósito.
    #ifdef COMPILE_IVROPENCVBRIDGE
        #define IVROPENCVBRIDGE_API DLLEXPORT
    #else
        // Quando outros módulos (como IVR) incluem este header, eles importam os símbolos.
        #define IVROPENCVBRIDGE_API DLLIMPORT
    #endif
#endif
#endif
// --- FIM DA CORREÇÃO ---

DECLARE_LOG_CATEGORY_EXTERN(LogIVROpenCVBridge, Log, All);

class FIVROpenCVBridgeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};