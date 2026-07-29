// Copyright 2026 RareBird Games. All Rights Reserved.

using UnrealBuildTool;

public class MCPBridgeClothOptimizer : ModuleRules
{
    public MCPBridgeClothOptimizer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetRegistry",
            "AssetTools",
            "ClothingSystemRuntimeCommon",
            "ClothingSystemRuntimeInterface",
            "ClothingSystemRuntimeNv",
            "InputCore",
            "Json",
            "JsonUtilities",
            "PhysicsCore",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnrealEd",
            "WorkspaceMenuStructure"
        });
    }
}
