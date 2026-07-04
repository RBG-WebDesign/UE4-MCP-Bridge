using UnrealBuildTool;

public class BlueprintGraphBuilderBundledInactive : ModuleRules
{
    public BlueprintGraphBuilderBundledInactive(ReadOnlyTargetRules Target) : base(Target)
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
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("BehaviorTreeEditor");
            PrivateDependencyModuleNames.Add("AIGraph");
            PrivateDependencyModuleNames.Add("Persona");
        }
    }
}
