// D:\william\UnrealProjects\IVRExample\Plugins\IVR\Source\IVRCore\Public\IVRCore.h
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Declara uma categoria de log específica para este módulo
DECLARE_LOG_CATEGORY_EXTERN(LogIVRCore, Log, All);

/**
 * A interface pública para o módulo IVRCore.
 * É responsável por gerenciar o ciclo de vida do módulo (inicialização/desligamento).
 */
class FIVRCoreModule : public IModuleInterface
{
public:

    /**
     * Chamado quando o módulo IVRCore é carregado na memória.
     * Use este método para realizar inicializações essenciais.
     */
    virtual void StartupModule() override;

    /**
     * Chamado quando o módulo IVRCore está sendo descarregado da memória.
     * Use este método para liberar recursos alocados.
     */
    virtual void ShutdownModule() override;
};