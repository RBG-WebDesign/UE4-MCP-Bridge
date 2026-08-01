using UnrealBuildTool;

public class MCPBridgePuerTS : ModuleRules
{
    public MCPBridgePuerTS(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        // MCPBridgeGraphBuilder is a dependency, not a copy: the blueprint_build
        // command re-fronts the existing Blueprint graph builder rather than
        // reimplementing node spawning inside this module.
        PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry", "Json", "JsonUtilities", "JsEnv", "MCPBridgeGraphBuilder", "Projects", "UnrealEd" });
    }
}
