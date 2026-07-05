// Copyright 2026 RareBird Games. All Rights Reserved.

using UnrealBuildTool;

public class MCPBridgeGraphBuilder : ModuleRules
{
    public MCPBridgeGraphBuilder(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "BlueprintGraph",
            "KismetCompiler",
            "Kismet",
            "GraphEditor",
            "Json",
            "JsonUtilities",
            "UMG",
            "UMGEditor",
            "Slate",
            "SlateCore",
            "MovieScene",
            "MovieSceneTracks",
            "AIModule",
            "GameplayTasks",
            "AnimGraph",
            "AnimGraphRuntime",
            "GameplayTags",
            // Phase 4.5 Batch 2 — needed for FKey marshaling in UK2Node_InputKey /
            // UK2Node_InputAxisKeyEvent / UK2Node_GetInputAxisKeyValue / etc.
            "InputCore",
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("BehaviorTreeEditor");
            PrivateDependencyModuleNames.Add("AIGraph");
            PrivateDependencyModuleNames.Add("Persona");

            // UK2Node_CreateWidget lives under UMGEditor/Private/Nodes/ — add include path
            // so BPNodeFactory.cpp can #include "K2Node_CreateWidget.h".
            string EngineDir = System.IO.Path.GetFullPath(Target.RelativeEnginePath);
            PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDir, "Source/Editor/UMGEditor/Private/Nodes"));
        }
    }
}
