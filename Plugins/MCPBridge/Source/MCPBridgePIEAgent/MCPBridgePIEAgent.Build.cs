// Copyright 2026 RareBird Games. All Rights Reserved.

using UnrealBuildTool;

public class MCPBridgePIEAgent : ModuleRules
{
    public MCPBridgePIEAgent(ReadOnlyTargetRules Target) : base(Target)
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
            "InputCore",
            "NavigationSystem",
            "UMG",
            "Json",
            "JsonUtilities",
        });
    }
}
