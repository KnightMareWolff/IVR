using UnrealBuildTool;
using System.IO;

public class IVRCore : ModuleRules
{
    public IVRCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // IVRCore precisa de Core e CoreUObject para definir USTRUCTs/UENUMs
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject", // Essencial para tipos UObject, USTRUCT, UENUM
                "Engine",        // Funções de motor, atores, componentes, etc.
                "Slate",         // UI (apenas editor ou runtime se for plugin de UI)
                "SlateCore",     // Base para Slate (apenas editor ou runtime se for plugin de UI)
                "UMG",           // UI via Unreal Motion Graphics
                "RenderCore",    // Para acesso a recursos de renderização (e.g., Texturas)
                "RHI",           // Render Hardware Interface (acesso direto à GPU)
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Não há dependências privadas para um módulo de tipos puros normalmente
            }
        );
    }
}