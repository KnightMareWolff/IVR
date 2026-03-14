using UnrealBuildTool;
using System.IO;

public class IVROpenCVBridge : ModuleRules
{
    public IVROpenCVBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // --- INÍCIO DA ALTERAÇÃO ---
        // É CRUCIAL especificar o tipo do módulo para que o Unreal Build Tool saiba como tratá-lo.
        // Como este é um módulo de "runtime" que não contém UObjects diretamente, mas fornece funcionalidade
        // para o módulo principal, 'Runtime' é o tipo apropriado.
        Type = ModuleRules.ModuleType.CPlusPlus;
        // --- FIM DA ALTERAÇÃO ---

        // ESTE MÓDULO CONTÉM APENAS TIPOS NATIVOS E INTERAGE COM BIBLIOTECAS QUE REQUEREM RTTI.
        bUseRTTI = true; // Ativar RTTI para compatibilidade com OpenCV.
        bEnableExceptions = true; // OK.
        bDisableAutoRTFMInstrumentation = true; // OK.

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",          // Tipos básicos da Unreal (FString, TArray, etc.)
                "Projects",      // FPaths, FGuid, IFileManager (essencial para pipes, caminhos de arquivo)
                "RenderCore",    // Pode ser necessário para tipos de renderização como FLinearColor, ou se RHI for usado.
                "ImageWrapper",  // Necessário para as funções de carregamento de imagem que usam IImageWrapper.
                "IVRCore",       // Se este módulo contém as USTRUCTs que o IVR usa, mas não introduz UObjects problemáticos próprios.
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Nenhum por padrão para um módulo de bridge C++ puro.
            }
        );

        // --- INÍCIO DA CORREÇÃO: Adicionar definição para a macro _API ---
        PublicDefinitions.AddRange(
            new string[]
            {
                "COMPILE_IVROPENCVBRIDGE=1" // <--- Adicione esta linha aqui
            }
        );
        // --- FIM DA CORREÇÃO ---

        // Dependências do OpenCV (da Epic Games)
        if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Mac) || (Target.Platform == UnrealTargetPlatform.Linux))
        {
            PublicDependencyModuleNames.Add("OpenCV");
            PublicDependencyModuleNames.Add("OpenCVHelper");
            PrivateIncludePaths.AddRange(
                new string[] {
                    Path.Combine(EngineDirectory, "Plugins", "Runtime", "OpenCV", "Source", "ThirdParty", "OpenCV" , "include"),
                    Path.Combine(EngineDirectory, "Plugins", "Runtime", "OpenCV", "Source", "OpenCVHelper", "Public")
                }
            );
        }

        // Desativa Unity Builds.
        bUseUnity = false; // OK.

        // Definições específicas da plataforma (ex: _CRT_SECURE_NO_WARNINGS para Windows).
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
            PublicSystemLibraries.Add("advapi32.lib");
        }
    }
}