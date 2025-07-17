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

        // M�dulos p�blicos dos quais este m�dulo depende.
        // Estes s�o essenciais para tipos b�sicos, objetos da UE e funcionalidades gerais.
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",          // Tipos b�sicos, FString, TArray, FText, etc.
            "CoreUObject",   // UObjects, Classes, Structs, etc.
            "Engine",        // Fun��es de motor, atores, componentes, etc.
            "Slate",         // UI (apenas editor ou runtime se for plugin de UI)
            "SlateCore",     // Base para Slate (apenas editor ou runtime se for plugin de UI)
            "UMG",
            "RenderCore",    // (Geralmente para rendering)
            "RHI",           // (Render Hardware Interface)
            
            "InputCore",         // (Opcional, mas �til para intera��o com entrada)
            "Projects",          // Necess�rio para FGuid, FPaths, IFileManager (manipula��o de arquivos/caminhos)
            "Renderer",
            "CinematicCamera",
            "ImageWrapper",
            "OpenCV",
            "OpenCVHelper",
            "MediaAssets",
            "MediaUtils"
        });

        // M�dulos privados dos quais este m�dulo depende.
        // S�o para uso interno do seu m�dulo e n�o expostos a outros.
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core",               // Explicitamente inclu�do para garantir que o compilador encontre tudo (mesmo se j� p�blico)
            "CoreUObject",        // Explicitamente inclu�do
            "Engine",             // Explicitamente inclu�do
            "RenderCore",         // .
            "RHI",                // .
            "D3D12RHI",           // .
            "HeadMountedDisplay", // .
            "Projects"            // Redundante se j� em Public, mas n�o causa problema.
        });


        // Permite o uso de OpenCV
        //bEnableUndefinedIdentifierWarnings = false; // Desativado pois est� obsoleto
        PrivateDefinitions.Add("WITH_OPENCV=1");

        // Configura��es espec�ficas da plataforma:
        // Para fun��es da Windows API como CreateNamedPipe.
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
            PublicSystemLibraries.Add("advapi32.lib");
        }
    }
}