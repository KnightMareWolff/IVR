// D:\william\UnrealProjects\IVRExample\Plugins\IVR\Source\IVRCore\Private\IVRCore.cpp
#include "IVRCore.h" // Inclui o cabeçalho público do seu módulo

// Define a categoria de log declarada em IVRCore.h
DEFINE_LOG_CATEGORY(LogIVRCore);

// Usado para strings literais localizáveis
#define LOCTEXT_NAMESPACE "FIVRCoreModule"

void FIVRCoreModule::StartupModule()
{
    // Este código será executado após o seu módulo ser carregado na memória.
    // O timing exato é especificado no arquivo .uplugin por módulo.
    UE_LOG(LogIVRCore, Log, TEXT("IVRCore Module Started"));
}

void FIVRCoreModule::ShutdownModule()
{
    // Esta função pode ser chamada durante o desligamento para limpar o seu módulo.
    // Para módulos que suportam recarregamento dinâmico, esta função é chamada antes de descarregar o módulo.
    UE_LOG(LogIVRCore, Log, TEXT("IVRCore Module Shutdown"));
}

#undef LOCTEXT_NAMESPACE

// Este macro é obrigatório para todo módulo e o registra com a Unreal Engine.
IMPLEMENT_MODULE(FIVRCoreModule, IVRCore)