using UnrealBuildTool;

public class MCPBridgePanelBundledInactive : ModuleRules
{
    public MCPBridgePanelBundledInactive(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "ApplicationCore",
            "HTTP",
            "Json",
            "JsonUtilities",
            "Projects",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
