using UnrealBuildTool;

public class ShootingArenaEditorTools : ModuleRules
{
    public ShootingArenaEditorTools( ReadOnlyTargetRules Target )
        : base( Target )
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "UnrealEd",
                "AssetRegistry"
            }
        );
    }
}