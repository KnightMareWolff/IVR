using UnrealBuildTool;
using System.IO;

public class IVROpenCVBridge : ModuleRules
{
    public IVROpenCVBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // ESTE MÓDULO NÃO CONTÉM UOBJECTS. ELE DEVE SER COMPILADO COM RTTI E EXCEÇÕES ATIVADOS.
        bUseRTTI = true; // <<< CORREÇÃO IMPORTANTE: Ativar RTTI para compatibilidade com OpenCV.
        bEnableExceptions = true; // OK.
        bDisableAutoRTFMInstrumentation = true; // OK.

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",          // Tipos básicos da Unreal (FString, TArray, etc.)
                "Projects",      // FPaths, FGuid, IFileManager (essencial para pipes, caminhos de arquivo)
                "RenderCore",    // Pode ser necessário para tipos de renderização como FLinearColor, ou se RHI for usado.
                // "RHI",           // Adicione se houver necessidade de acesso direto ao Render Hardware Interface neste módulo.
                                 // Removido por padrão, se não for estritamente usado aqui.
                "ImageWrapper",  // Necessário para as funções de carregamento de imagem que usam IImageWrapper.
                "IVRCore",       // OK. Se este módulo contém as USTRUCTs que o IVR usa, mas não introduz UObjects problemáticos próprios.
            }
        );

        // Remova todas as dependências de UObject-modules de PrivateDependencyModuleNames também,
        // a menos que haja uma razão muito específica e bem compreendida para mantê-las aqui (raro para uma bridge pura).
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Nenhum por padrão para um módulo de bridge C++ puro.
            }
        );

        // Dependências do OpenCV (da Epic Games)
        if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Mac) || (Target.Platform == UnrealTargetPlatform.Linux))
        {
            PublicDependencyModuleNames.Add("OpenCV");
            PublicDependencyModuleNames.Add("OpenCVHelper");
            PrivateIncludePaths.AddRange(
                new string[] {
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