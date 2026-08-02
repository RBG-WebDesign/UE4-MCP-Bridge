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
        // SourceControl is read-only here: the failed-build rollback boundary
        // reports whether a file is opened for add or checked out, so a caller
        // can see that a failure performed no source-control operation. It
        // never adds, checks out, or reverts.
        // BlueprintGraph is for UK2Node_Variable only: remove_unlisted has to find
        // the graph nodes that read or write a variable before it may remove it.
        PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "AssetRegistry", "BlueprintGraph", "GameplayTasks", "Json", "JsonUtilities", "JsEnv", "MCPBridgeGraphBuilder", "Projects", "SourceControl", "UMG", "UMGEditor", "UnrealEd" });
    }
}
