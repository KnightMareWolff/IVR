using UnrealBuildTool;
using System.IO;

public class IVROpenCVBridge : ModuleRules
{
    public IVROpenCVBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // Este mÃ³dulo IVROpenCVBridge Ã© dedicado a encapsular a lÃ³gica nativa C++ e a integraÃ§Ã£o com OpenCV.
        // Ele nÃ£o conterÃ¡ UObjects. Portanto, RTTI e ExceÃ§Ãµes podem/devem ser habilitados aqui,
        // pois bibliotecas externas como OpenCV podem depender deles, especialmente no Linux.
        bUseRTTI = true;
        bEnableExceptions = true;
        bDisableAutoRTFMInstrumentation = true; // NecessÃ¡rio quando exceÃ§Ãµes sÃ£o habilitadas

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",          // Tipos básicos, FString, TArray, FText, etc.
                "CoreUObject",   // UObjects, Classes, Structs, etc.
                "Engine",        // Funções de motor, atores, componentes, etc.
                "Slate",         // UI (apenas editor ou runtime se for plugin de UI)
                "SlateCore",     // Base para Slate (apenas editor ou runtime se for plugin de UI)
                "UMG",           // UI via Unreal Motion Graphics
                "RenderCore",    // Para acesso a recursos de renderização (e.g., Texturas)
                "RHI",           // Render Hardware Interface (acesso direto à GPU)
                "InputCore",     // Interação com entrada (teclado, mouse, gamepad)
                "Projects",      // Necessário para FGuid, FPaths, IFileManager (manipulação de arquivos/caminhos)
                "Renderer",      // Funções de renderização de alto nível
                "CinematicCamera", // Se usa componentes de câmera cinematográfica
                "ImageWrapper",  // Para carregar/salvar imagens (UIVRFolderFrameSource)
                "MediaAssets",   // Se o plugin interage com assets de mídia da UE
                "MediaUtils",
                "IVRCore"
            }
        );

        // *** ADICIONE ESTES BLOCOS ***
        PublicIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Public") // Adiciona explicitamente a pasta Public do módulo
            }
        );

        PrivateIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Private") // Adiciona explicitamente a pasta Private do módulo
            }
        );
        // ***************************

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Se precisar de alguma funcionalidade bÃ¡sica interna do Engine aqui (ex: FileManager)
                // "Core", // JÃ¡ estÃ¡ em Public, mas se tiver dependÃªncias privadas adicionais
            }
        );

        // DependÃªncias do OpenCV (da Epic Games)
        // O OpenCV Ã© uma dependÃªncia crucial para este mÃ³dulo, por isso Ã© incluÃ­do em todas as plataformas aplicÃ¡veis.
        if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Mac) || (Target.Platform == UnrealTargetPlatform.Linux))
        {
            PublicDependencyModuleNames.Add("OpenCV");
            PublicDependencyModuleNames.Add("OpenCVHelper");
            // AdiÃ§Ã£o explÃ­cita do caminho pÃºblico do OpenCVHelper, necessÃ¡rio para encontrar cabeÃ§alhos.
            PrivateIncludePaths.AddRange(
                new string[] {
                    Path.Combine(EngineDirectory, "Plugins", "Runtime", "OpenCV", "Source", "OpenCVHelper", "Public")
                }
            );
        }

        // Desativa Unity Builds se houver problemas de 'macro redefined' com bibliotecas de terceiros,
        // especialmente comum em mÃ³dulos que integram muito C++ nativo e bibliotecas externas.
        bUseUnity = false;

        // DefiniÃ§Ãµes especÃ­ficas da plataforma (ex: _CRT_SECURE_NO_WARNINGS para Windows).
        // Isso ajuda na compatibilidade de cÃ³digo entre plataformas.
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
            PublicSystemLibraries.Add("advapi32.lib");
        }
    }
}
