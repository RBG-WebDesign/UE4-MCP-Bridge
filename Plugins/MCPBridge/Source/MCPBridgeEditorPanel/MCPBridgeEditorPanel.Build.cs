using UnrealBuildTool;

public class MCPBridgeEditorPanel : ModuleRules
{
    public MCPBridgeEditorPanel(ReadOnlyTargetRules Target) : base(Target)
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
