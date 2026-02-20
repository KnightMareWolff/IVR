// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.

using UnrealBuildTool;
using System.IO;

public class IVR : ModuleRules
{
    public IVR(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // Para este módulo de UObjects, desabilitamos RTTI e Exceções em todas as plataformas
        // para garantir total compatibilidade com a arquitetura da Unreal Engine para UObjects.
        bUseRTTI = false;
        bEnableExceptions = false;
        bDisableAutoRTFMInstrumentation = false;
        PublicIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Public"),
                Path.Combine(ModuleDirectory, "Public", "Components"),
                Path.Combine(ModuleDirectory, "Public", "Recording"),
                Path.Combine(ModuleDirectory, "Public", "UI"),
                // --- INÍCIO DA CORREÇÃO: Adicionar explicitamente o caminho Public do IVROpenCVBridge ---
                // O caminho para IVROpenCVBridge/Public é relativo ao diretório do módulo IVR (../IVROpenCVBridge/Public)
                Path.Combine(ModuleDirectory, "../IVROpenCVBridge", "Public")
                // --- FIM DA CORREÇÃO ---
            }
        );

        PrivateIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Private"),
                Path.Combine(ModuleDirectory, "Private", "Components"),
                Path.Combine(ModuleDirectory, "Private", "Recording"),
                Path.Combine(ModuleDirectory, "Private", "UI")
                // O caminho público do OpenCVHelper foi movido para IVROpenCVBridge
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
                "MediaUtils",
                "IVROpenCVBridge", // Mantenha esta, IVR ainda usa IVROpenCVBridge
                "IVRCore"          // <--- NOVO: Adicione esta dependência
                
            }
        );
        // As dependências e includes específicos do OpenCV foram movidos para IVROpenCVBridge.Build.cs
        // Condicional para módulos RHI específicos de plataforma
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.Add("D3D12RHI");
            PrivateDependencyModuleNames.Add("HeadMountedDisplay");
            PrivateDependencyModuleNames.Add("IVROpenCVBridge");
            PrivateDependencyModuleNames.Add("IVRCore");
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
        // Desativa Unity Builds para plugins complexos com compilação condicional.
        // Isso ajuda a evitar problemas de 'macro redefined' e com bibliotecas de terceiros.
        bUseUnity = false;

        // Configurações específicas da plataforma:
        // Para funções da Windows API como CreateNamedPipe.
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
            PublicSystemLibraries.Add("advapi32.lib");
        }
    }
}