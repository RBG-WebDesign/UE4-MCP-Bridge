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
        // NavigationSystem is for nav_inspect, nav_query and nav_build, all of
        // them reading UNavigationSystemV1, ANavigationData, ANavMeshBoundsVolume
        // and ANavModifierVolume. AIModule already covers blackboard,
        // Environment Query and AIPerception types.
        // MaterialEditor is for material_inspect, material_instance_build and
        // material_build: UMaterialEditingLibrary is the only 4.27 API that reads
        // and writes material instance parameters by plain FName, and it is
        // editor-only, which this module already is.
        // RenderCore and RHI are for the material compile report:
        // GMaxRHIFeatureLevel selects the FMaterialResource whose compile errors
        // are reported instead of assumed.
        // InputCore is for input_mapping_info and input_mapping_patch: an input
        // mapping is identified by its FKey, which is that module's type.
        // MCPBridgePIEAgent is a dependency for the same reason
        // MCPBridgeGraphBuilder is: pie_agent_query re-fronts UPIEAgentLibrary
        // rather than reimplementing the runtime observation it already owns.
        //
        // ONE list, not one per lane. Five lanes each appended their own
        // AddRange carrying a copy of the base set, which UBT tolerates because
        // AddRange is additive and duplicates are harmless, but which left five
        // lines each reading as though it were the whole list. Consolidated at
        // integration; add to this list rather than beside it.
        PrivateDependencyModuleNames.AddRange(new string[] {
            "AIModule", "AnimGraph", "AssetRegistry", "BlueprintGraph", "GameplayTasks",
            "InputCore", "Json", "JsonUtilities", "JsEnv", "MaterialEditor",
            "MCPBridgeGraphBuilder", "MCPBridgePIEAgent", "NavigationSystem", "Projects",
            "RenderCore", "RHI", "SourceControl", "UMG", "UMGEditor", "UnrealEd",
        });
        // LevelSequence, MovieScene and MovieSceneTracks are for sequence_inspect
        // and sequence_build only, and all three are RUNTIME modules
        // (Engine/Source/Runtime/...), not plugins and not editor-only. That
        // matters because the obvious fourth name is not here: ULevelSequenceFactoryNew
        // lives in the LevelSequenceEditor PLUGIN, in a Private header, so it
        // cannot be included from this module at all. sequence_build reproduces
        // the five lines that factory runs instead of depending on it.
        //
        // MovieSceneCapture and MovieSceneTools ARE here now, for
        // sequence_render_start only. MovieSceneCapture (Runtime) holds
        // UMovieSceneCapture, FMovieSceneCaptureSettings and the five image
        // capture protocols; MovieSceneTools (Editor) holds
        // UAutomatedLevelSequenceCapture, which is the class the render
        // manifest names and the one the second process reconstructs. This
        // module is already editor-only, so an editor dependency costs nothing
        // new. Sequencer is still NOT here: nothing in the render path needs
        // the editor UI.
        //
        // The frame types (FFrameRate, FFrameNumber, FFrameTime) are in Core, so
        // TimeManagement is not needed and is not added; LevelSequence exports it
        // publicly anyway.
        PrivateDependencyModuleNames.AddRange(new string[] {
            "LevelSequence", "MovieScene", "MovieSceneTracks",
            "MovieSceneCapture", "MovieSceneTools",
        });
        // MCPBridgeClothOptimizer is a dependency for the same reason
        // MCPBridgePIEAgent is: cloth_inspect re-fronts
        // UClothOptimizerLibrary::InspectClothAsset rather than reimplementing
        // the NvCloth snapshot that module already owns and that its editor
        // panel already reads. Only the READ entry point is called; the three
        // writers stay behind the legacy listener until a failed apply provably
        // restores the mask.
        PrivateDependencyModuleNames.AddRange(new string[] { "MCPBridgeClothOptimizer" });
    }
}
