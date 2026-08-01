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
        // UMG and UMGEditor are here for widget_build only: it reads the built
        // UWidgetTree back out of the UWidgetBlueprint to answer with the
        // hierarchy that actually landed. The tree construction itself stays
        // in MCPBridgeGraphBuilder.
        // AIModule and GameplayTasks are for behavior_tree_build only: the
        // UBehaviorTree and UBlackboardData types it creates live there.
        PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "AssetRegistry", "GameplayTasks", "Json", "JsonUtilities", "JsEnv", "MCPBridgeGraphBuilder", "Projects", "UMG", "UMGEditor", "UnrealEd" });
    }
}
