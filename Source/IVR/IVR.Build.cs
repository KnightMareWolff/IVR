// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Prohibited copy or distribution without expressed authorization of the Author.

using UnrealBuildTool;
using System.IO;

public class IVR : ModuleRules
{
    public IVR(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // C1: Ativar RTTI para o módulo IVR para compatibilidade com certas partes do OpenCV no Linux.
        bUseRTTI = true;

        PublicIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Public"),
                Path.Combine(ModuleDirectory, "Public", "Core"),
                Path.Combine(ModuleDirectory, "Public", "Components"),
                Path.Combine(ModuleDirectory, "Public", "Recording"),
                Path.Combine(ModuleDirectory, "Public", "UI")
            }
        );

        PrivateIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Private"),
                Path.Combine(ModuleDirectory, "Private", "Core"),
                Path.Combine(ModuleDirectory, "Private", "Components"),
                Path.Combine(ModuleDirectory, "Private", "Recording"),
                Path.Combine(ModuleDirectory, "Private", "UI"),
                // **CORREÇÃO**: Adição explícita do caminho público do OpenCVHelper
                // Isso ajuda o compilador a encontrar 'OpenCVHelper.h', 'PreOpenCVHeaders.h', 'PostOpenCVHeaders.h'
                // quando o plugin OpenCV está habilitado.
                Path.Combine(EngineDirectory, "Plugins", "Runtime", "OpenCV", "Source", "OpenCVHelper", "Public")
            }
        );

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
                "MediaUtils"
            }
        );

        //OpenCV Definitions
        if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Mac) || (Target.Platform == UnrealTargetPlatform.Linux))
        {
            // **CORREÇÃO**: Adição explícita do caminho público do OpenCVHelper
            // Isso ajuda o compilador a encontrar 'OpenCVHelper.h', 'PreOpenCVHeaders.h', 'PostOpenCVHeaders.h'
            // quando o plugin OpenCV está habilitado.
            PrivateIncludePaths.AddRange(
            new string[] {
                Path.Combine(EngineDirectory, "Plugins", "Runtime", "OpenCV", "Source", "OpenCVHelper", "Public")
            });

            //OpenCV Only for the Target Platforms.
            //To make possible it works on Android a really big platform change will be needed on the Code Infrastructure, mainly on the Pipes!
            PublicDependencyModuleNames.Add("OpenCV");
            PublicDependencyModuleNames.Add("OpenCVHelper");
        }

        // Condicional para módulos RHI específicos de plataforma
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.Add("D3D12RHI");
            PrivateDependencyModuleNames.Add("HeadMountedDisplay");
            // Se você tiver código que se beneficia diretamente do D3D12, adicione aqui.
            // Ex: PrivateDependencyModuleNames.Add("D3D12Core");
        }
        else if (Target.Platform == UnrealTargetPlatform.Android)
        {
            // Para Android, você pode precisar de VulkanRHI ou OpenGLDRHI
            // se o seu plugin interage diretamente com a renderização de baixo nível.
            // Se não, você pode não precisar adicionar RHIs específicas aqui.
            // PrivateDependencyModuleNames.Add("VulkanRHI");
            // PrivateDependencyModuleNames.Add("OpenGLDRHI");
            PCHUsage = PCHUsageMode.NoPCHs;
        }
        // Repita para outras plataformas como Linux, Mac, etc., com seus RHIs correspondentes
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            // OpenGL ou Metal RHI para Mac, dependendo da versão do UE e necessidades.
            // PrivateDependencyModuleNames.Add("MetalRHI");
        }

        // **CORREÇÃO**: Desativa Unity Builds para plugins complexos com compilação condicional.
        // Isso ajuda a evitar problemas de 'macro redefined' e com bibliotecas de terceiros.
        bUseUnity = false;

        // **NÃO DEFINIR WITH_OPENCV AQUI**. O próprio plugin nativo "OpenCV" da Unreal Engine
        // (que agora é uma dependência) já gerencia a definição desta macro globalmente
        // com base em sua própria habilitação/desabilitação.

        // Configurações específicas da plataforma:
        // Para funções da Windows API como CreateNamedPipe.
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
            PublicSystemLibraries.Add("advapi32.lib");
        }
    }
}