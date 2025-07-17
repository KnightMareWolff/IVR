// IVR.Build.cs
using UnrealBuildTool;
using UnrealBuildTool.Rules;

public class IVR : ModuleRules
{
    public IVR(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[] {
            
        });

        PrivateIncludePaths.AddRange(new string[] {
            
        });

        // Módulos públicos dos quais este módulo depende.
        // Estes são essenciais para tipos básicos, objetos da UE e funcionalidades gerais.
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",          // Tipos básicos, FString, TArray, FText, etc.
            "CoreUObject",   // UObjects, Classes, Structs, etc.
            "Engine",        // Funções de motor, atores, componentes, etc.
            "Slate",         // UI (apenas editor ou runtime se for plugin de UI)
            "SlateCore",     // Base para Slate (apenas editor ou runtime se for plugin de UI)
            "UMG",
            "RenderCore",    // (Geralmente para rendering)
            "RHI",           // (Render Hardware Interface)
            
            "InputCore",         // (Opcional, mas útil para interação com entrada)
            "Projects",          // Necessário para FGuid, FPaths, IFileManager (manipulação de arquivos/caminhos)
            "Renderer",
            "CinematicCamera",
            "ImageWrapper",
            "OpenCV",
            "OpenCVHelper",
            "MediaAssets",
            "MediaUtils"
        });

        // Módulos privados dos quais este módulo depende.
        // São para uso interno do seu módulo e não expostos a outros.
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core",               // Explicitamente incluído para garantir que o compilador encontre tudo (mesmo se já público)
            "CoreUObject",        // Explicitamente incluído
            "Engine",             // Explicitamente incluído
            "RenderCore",         // .
            "RHI",                // .
            "D3D12RHI",           // .
            "HeadMountedDisplay", // .
            "Projects"            // Redundante se já em Public, mas não causa problema.
        });


        // Permite o uso de OpenCV
        //bEnableUndefinedIdentifierWarnings = false; // Desativado pois está obsoleto
        PrivateDefinitions.Add("WITH_OPENCV=1");

        // Configurações específicas da plataforma:
        // Para funções da Windows API como CreateNamedPipe.
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
            PublicSystemLibraries.Add("advapi32.lib");
        }
    }
}