using UnrealBuildTool;

public class MCPBridgePuerTS : ModuleRules
{
    public MCPBridgePuerTS(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // One command per .cpp, each with its own anonymous-namespace helpers,
        // is the shape this module has settled into. A unity build concatenates
        // those files into one translation unit, so two commands that both
        // define a local ValueToJsonText, StringsToJson or FResolvedOp collide
        // at C2084 even though neither file is wrong on its own and each
        // compiles alone.
        //
        // That collision is invisible until link time and scales with the
        // number of commands: it appeared the first time five independently
        // written command files were compiled together. Renaming the helpers
        // would fix today's pairs and leave the next command author to
        // rediscover it. Turning unity off costs compile time and removes the
        // class of failure.
        //
        // ponytail: whole-module opt out, not per-file. If build time becomes
        // the problem, the upgrade is bUseUnityBuild with explicit
        // MinSourceFilesForUnityBuild tuning, not reinstating name collisions.
        bUseUnity = false;
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
        // AnimGraph is for anim_blueprint_inspect only: UAnimGraphNode_StateMachineBase,
        // UAnimStateNode and UAnimStateTransitionNode are editor types in that
        // module, and the state machine structure cannot be read without them.
        // The AnimBlueprint construction itself stays in MCPBridgeGraphBuilder.
        PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "AnimGraph", "AssetRegistry", "BlueprintGraph", "GameplayTasks", "Json", "JsonUtilities", "JsEnv", "MCPBridgeGraphBuilder", "Projects", "SourceControl", "UMG", "UMGEditor", "UnrealEd" });
        // BlueprintGraph is for UK2Node_Variable and UK2Node_CallFunction:
        // remove_unlisted has to find the graph nodes that read or write a
        // variable before it may remove it, and ai_controller_inspect finds the
        // RunBehaviorTree call sites that wire a controller to its tree.
        // NavigationSystem is for nav_inspect and nav_query only, both read
        // only: UNavigationSystemV1, ANavigationData, ANavMeshBoundsVolume and
        // ANavModifierVolume live there. AIModule already covers the blackboard,
        // Environment Query and AIPerception types.
        PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "AssetRegistry", "BlueprintGraph", "GameplayTasks", "Json", "JsonUtilities", "JsEnv", "MCPBridgeGraphBuilder", "NavigationSystem", "Projects", "SourceControl", "UMG", "UMGEditor", "UnrealEd" });
        // MaterialEditor is for material_inspect and material_instance_build:
        // UMaterialEditingLibrary is the only 4.27 API that reads and writes
        // material instance parameters by plain FName, and it is an editor-only
        // module, which this one already is.
        // RenderCore and RHI are for the compile report: GMaxRHIFeatureLevel
        // selects the FMaterialResource whose compile errors are reported
        // instead of assumed.
        PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "AssetRegistry", "BlueprintGraph", "GameplayTasks", "Json", "JsonUtilities", "JsEnv", "MaterialEditor", "MCPBridgeGraphBuilder", "Projects", "RenderCore", "RHI", "SourceControl", "UMG", "UMGEditor", "UnrealEd" });
        // InputCore is for input_mapping_info and input_mapping_patch: an input
        // mapping is identified by its FKey, which is that module's type, and
        // reading one back requires resolving and printing it.
        // MCPBridgePIEAgent is a dependency for the same reason
        // MCPBridgeGraphBuilder is: pie_agent_query re-fronts UPIEAgentLibrary
        // rather than reimplementing the runtime observation it already owns.
        PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "AssetRegistry", "BlueprintGraph", "GameplayTasks", "InputCore", "Json", "JsonUtilities", "JsEnv", "MCPBridgeGraphBuilder", "MCPBridgePIEAgent", "Projects", "SourceControl", "UMG", "UMGEditor", "UnrealEd" });
    }
}
