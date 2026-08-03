// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Json.h"
#include "LightingBuildOptions.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
    /** The four Lightmass quality levels, spelled the way the editor's own
        Build Lighting menu spells them. */
    bool ParseLightingQuality(const FString& Name, ELightingBuildQuality& OutQuality)
    {
        if (Name.Equals(TEXT("Preview"), ESearchCase::IgnoreCase)) { OutQuality = Quality_Preview; return true; }
        if (Name.Equals(TEXT("Medium"), ESearchCase::IgnoreCase)) { OutQuality = Quality_Medium; return true; }
        if (Name.Equals(TEXT("High"), ESearchCase::IgnoreCase)) { OutQuality = Quality_High; return true; }
        if (Name.Equals(TEXT("Production"), ESearchCase::IgnoreCase)) { OutQuality = Quality_Production; return true; }
        return false;
    }

    /**
     * What the level says about its own lighting, right now.
     *
     * NumLightingUnbuiltObjects is the counter behind the editor's "Lighting
     * needs to be rebuilt" banner, so it is the independent read that decides
     * whether a build was needed and whether it helped. It is compiled out of a
     * shipping build; this module is editor-only, but the guard is here so the
     * field is reported as absent rather than as zero if that ever changes.
     */
    void FillLightingStatus(const TSharedPtr<FJsonObject>& Out, UWorld* World)
    {
        const bool bRunning = GEditor != nullptr && GEditor->IsLightingBuildCurrentlyRunning();
        Out->SetBoolField(TEXT("build_running"), bRunning);
        Out->SetBoolField(TEXT("build_exporting"),
            GEditor != nullptr && GEditor->IsLightingBuildCurrentlyExporting());
#if !UE_BUILD_SHIPPING
        const int32 Unbuilt = World != nullptr ? static_cast<int32>(World->NumLightingUnbuiltObjects) : 0;
        Out->SetNumberField(TEXT("lighting_unbuilt_objects"), Unbuilt);
        Out->SetNumberField(TEXT("unbuilt_reflection_captures"),
            World != nullptr ? static_cast<int32>(World->NumUnbuiltReflectionCaptures) : 0);
        Out->SetBoolField(TEXT("needs_rebuild"), Unbuilt > 0);
#else
        Out->SetStringField(TEXT("unbuilt_counts_unavailable_reason"),
            TEXT("UWorld::NumLightingUnbuiltObjects is compiled out of a shipping build."));
#endif
    }

    /**
     * Whether the thing that actually does the work is there.
     *
     * CPU Lightmass does not run inside the editor. The editor exports the
     * scene and hands it to Swarm Agent, a separate .NET service under the
     * ENGINE, not under this plugin. When Swarm cannot start, BuildLighting
     * still returns, IsLightingBuildCurrentlyRunning still says true, and the
     * build then makes no progress forever: the command reports what it
     * requested rather than what happened, which is the failure shape this
     * program exists to remove (finding 0u).
     *
     * Reported on every call, including action="status", so a caller can ask
     * whether a bake is even possible without starting one.
     */
    void FillSwarmStatus(const TSharedPtr<FJsonObject>& Out, FString& OutAgentPath, bool& bOutAvailable)
    {
        OutAgentPath = FPaths::ConvertRelativePathToFull(
            FPaths::EngineDir() / TEXT("Binaries/DotNET/SwarmAgent.exe"));
        const FString LightmassPath = FPaths::ConvertRelativePathToFull(
            FPaths::EngineDir() / TEXT("Binaries/Win64/UnrealLightmass.exe"));
        const bool bAgentPresent = FPaths::FileExists(OutAgentPath);
        const bool bLightmassPresent = FPaths::FileExists(LightmassPath);
        const bool bAgentRunning = FPlatformProcess::IsApplicationRunning(TEXT("SwarmAgent"));

        Out->SetStringField(TEXT("swarm_agent_path"), OutAgentPath);
        Out->SetBoolField(TEXT("swarm_agent_present"), bAgentPresent);
        Out->SetBoolField(TEXT("swarm_agent_running"), bAgentRunning);
        Out->SetBoolField(TEXT("lightmass_present"), bLightmassPresent);

        bOutAvailable = (bAgentPresent || bAgentRunning) && bLightmassPresent;
        Out->SetBoolField(TEXT("swarm_available"), bOutAvailable);
        if (bOutAvailable && !bAgentRunning)
        {
            Out->SetStringField(TEXT("swarm_note"),
                TEXT("Swarm Agent is installed but not running, so the editor will try to start one. "
                     "That auto-start is the step that fails on a machine with a firewall prompt, a "
                     "stuck previous agent, or a damaged cache under "
                     "%LOCALAPPDATA%/UnrealEngine/4.27/Saved/Swarm, and it fails without an error "
                     "this command can see. Start SwarmAgent.exe by hand first if a build stalls at "
                     "0%."));
        }
    }
}

bool UMCPPuerTSBridgeService::BuildLightingJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Lighting build spec must be a JSON object.");
        return false;
    }

    if (GEditor == nullptr)
    {
        OutError = TEXT("No editor engine, so there is nothing to build lighting with.");
        return false;
    }
    // Lightmass builds the EDITOR world, and FStaticLightingManager reads GWorld
    // rather than a world it is handed. During play GWorld is the PIE world, so
    // a build started here would gather the wrong scene.
    if (GEditor->PlayWorld != nullptr || GEditor->GetPlaySessionRequest().IsSet())
    {
        OutError = TEXT("Play In Editor is running. A lighting build reads the editor world through "
                        "GWorld, which is the PIE world while play is active. Stop play first.");
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (World == nullptr)
    {
        OutError = TEXT("No editor world is loaded, so there is no level to light.");
        return false;
    }
    const FString LevelPath = World->GetOutermost()->GetName();

    FString ExpectedLevel;
    if (Spec->TryGetStringField(TEXT("level_path"), ExpectedLevel) && !ExpectedLevel.IsEmpty()
        && ExpectedLevel != LevelPath)
    {
        OutError = FString::Printf(
            TEXT("The editor has %s loaded, not %s. lighting_build lights the level the editor "
                 "actually has open and never loads one."), *LevelPath, *ExpectedLevel);
        return false;
    }

    FString Action = TEXT("start");
    Spec->TryGetStringField(TEXT("action"), Action);
    if (Action != TEXT("start") && Action != TEXT("status"))
    {
        OutError = FString::Printf(
            TEXT("action must be \"start\" or \"status\"; got '%s'. There is no cancel: UE4.27 "
                 "exposes no public entry point for aborting a Lightmass build."), *Action);
        return false;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("action"), Action);
    Result->SetStringField(TEXT("level_path"), LevelPath);
    Result->SetStringField(TEXT("level_name"), FPackageName::GetShortName(LevelPath));

    FString SwarmAgentPath;
    bool bSwarmAvailable = false;
    FillSwarmStatus(Result, SwarmAgentPath, bSwarmAvailable);

    auto Answer = [&](bool bSuccess) -> bool
    {
        FillLightingStatus(Result, World);
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        return bSuccess;
    };

    if (Action == TEXT("status"))
    {
        Result->SetBoolField(TEXT("started"), false);
        Result->SetBoolField(TEXT("waited"), false);
        return Answer(true);
    }

    // --- start ---
    //
    // Refused by name, the way nav_build refuses a level with no bounds volume.
    // Without this the command starts a build that cannot progress, returns
    // success, and leaves the caller polling a status that never changes.
    if (!bSwarmAvailable)
    {
        Result->SetBoolField(TEXT("started"), false);
        Result->SetBoolField(TEXT("waited"), false);
        OutError = FString::Printf(
            TEXT("Swarm Agent is not available, so a CPU Lightmass build cannot run. Expected "
                 "SwarmAgent.exe at %s and UnrealLightmass.exe beside it under the engine. Swarm is "
                 "part of the ENGINE install, not this plugin: reinstall or repair the engine's "
                 "Binaries, or start SwarmAgent.exe by hand. Nothing was started."),
            *SwarmAgentPath);
        return Answer(false);
    }

    if (GEditor->IsLightingBuildCurrentlyRunning())
    {
        Result->SetBoolField(TEXT("started"), false);
        Result->SetBoolField(TEXT("waited"), false);
        OutError = TEXT("A lighting build is already running. Poll it with action=\"status\" rather "
                        "than starting a second one; Lightmass has one active build.");
        return Answer(false);
    }

    ELightingBuildQuality Quality = Quality_Preview;
    FString QualityName = TEXT("Preview");
    Spec->TryGetStringField(TEXT("quality"), QualityName);
    if (!ParseLightingQuality(QualityName, Quality))
    {
        OutError = FString::Printf(
            TEXT("quality must be Preview, Medium, High or Production; got '%s'."), *QualityName);
        return false;
    }
    Result->SetStringField(TEXT("quality"), QualityName);

    FLightingBuildOptions Options;
    Options.QualityLevel = Quality;
    Options.bOnlyBuildCurrentLevel = !Spec->HasTypedField<EJson::Boolean>(TEXT("only_current_level"))
        || Spec->GetBoolField(TEXT("only_current_level"));
    Options.bOnlyBuildSelected = false;
    Options.bOnlyBuildVisibility = false;
    Result->SetBoolField(TEXT("only_current_level"), Options.bOnlyBuildCurrentLevel);

    // The refusals a failed BeginLightmassProcess would otherwise report only as
    // an editor toast nobody reading this response can see.
    const AWorldSettings* Settings = World->GetWorldSettings();
    static const auto* AllowStaticLightingVar =
        IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
    const bool bAllowStaticLighting =
        AllowStaticLightingVar == nullptr || AllowStaticLightingVar->GetValueOnGameThread() != 0;
    if (Settings != nullptr && Settings->bForceNoPrecomputedLighting)
    {
        OutError = TEXT("This level's World Settings has bForceNoPrecomputedLighting set, so "
                        "Lightmass would refuse the build and every light stays dynamic.");
        return Answer(false);
    }
    if (!bAllowStaticLighting)
    {
        OutError = TEXT("r.AllowStaticLighting is 0 for this project, so there is no static lighting "
                        "to build and Lightmass would refuse.");
        return Answer(false);
    }

    // THIS DOES NOT WAIT, and the response says so rather than implying it.
    //
    // UEditorEngine::BuildLighting hands the level to FStaticLightingManager,
    // which gathers the scene, starts Lightmass and returns; the build then
    // advances on UpdateBuildLighting from the editor's own tick, over minutes
    // at Production quality. A command that blocked on that would sit far past
    // any request timeout this bridge allows, and a timeout reported to the
    // client while the editor kept building is a lie in both directions: the
    // caller is told it failed, and the work continues anyway.
    //
    // What the call DOES do synchronously is the scene gather and the Lightmass
    // export, which is seconds on a small level and can be much longer on a
    // large one. That is the part this command pays for, and it is why the tool
    // carries a long execution budget despite not waiting for the build.
    GEditor->BuildLighting(Options);

    // Read back rather than assume. CreateStaticLightingSystem returns void and
    // swallows a refused start, so "is a build running now" is the only honest
    // evidence that one actually began.
    const bool bStarted = GEditor->IsLightingBuildCurrentlyRunning();
    Result->SetBoolField(TEXT("started"), bStarted);
    Result->SetBoolField(TEXT("waited"), false);
    Result->SetStringField(TEXT("completion"),
        TEXT("NOT waited for. This call started the build and returned; poll it with "
             "action=\"status\" until build_running is false, then read "
             "lighting_unbuilt_objects to see whether the level still needs a rebuild."));
    if (!bStarted)
    {
        OutError = TEXT("The lighting build did not start. Lightmass refused it and reported the "
                        "reason to the editor's message log; the usual causes are a level with no "
                        "static geometry to light and a Lightmass configuration error.");
        return Answer(false);
    }
    return Answer(true);
}
