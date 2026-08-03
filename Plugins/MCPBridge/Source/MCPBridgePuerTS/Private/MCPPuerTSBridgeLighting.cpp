// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Json.h"
#include "LightingBuildOptions.h"
#include "Misc/PackageName.h"
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
            TEXT("action must be \"start\" or \"status\"; got '%s'. To cancel a running build, use "
                 "puerts_job_cancel with the job_id this command returned."), *Action);
        return false;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("action"), Action);
    Result->SetStringField(TEXT("level_path"), LevelPath);
    Result->SetStringField(TEXT("level_name"), FPackageName::GetShortName(LevelPath));

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
        // ADDITIVE, and here because a job id is the only handle job_cancel
        // takes: a caller polling with action="status" (the shape this command
        // shipped with) would otherwise have no way to reach the cancel that
        // now exists. The editor runs one lighting build at a time, so the
        // newest running lighting job IS this build.
        for (int32 Index = Jobs.Num() - 1; Index >= 0; --Index)
        {
            if (Jobs[Index].Kind == EBridgeJobKind::LightingBuild)
            {
                PollJob(Jobs[Index]);
                Result->SetStringField(TEXT("job_id"), Jobs[Index].JobId);
                Result->SetStringField(TEXT("job_state"), Jobs[Index].State);
                break;
            }
        }
        return Answer(true);
    }

    // --- start ---
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
    // Clear the map-build cancel flag before starting, exactly as the editor's
    // own build path does (EditorBuildUtils.cpp:248). It is a single global
    // FUnrealEdMisc flag read all over Lightmass (Lightmass.cpp:726, :1312,
    // :3385, :3479) and by the CSG and streaming-level builders, and
    // UEditorEngine::BuildLighting does NOT reset it. Without this line a
    // job_cancel would poison every later build in the session: the next start
    // would abort on its first check and report a build that never ran.
    GEditor->SetMapBuildCancelled(false);

    GEditor->BuildLighting(Options);

    // Read back rather than assume. CreateStaticLightingSystem returns void and
    // swallows a refused start, so "is a build running now" is the only honest
    // evidence that one actually began.
    const bool bStarted = GEditor->IsLightingBuildCurrentlyRunning();
    Result->SetBoolField(TEXT("started"), bStarted);
    Result->SetBoolField(TEXT("waited"), false);
    Result->SetStringField(TEXT("completion"),
        TEXT("NOT waited for. This call started the build and returned; poll it with "
             "action=\"status\", or with puerts_job_status and the job_id below, until "
             "build_running is false, then read lighting_unbuilt_objects to see whether the level "
             "still needs a rebuild. puerts_job_cancel stops it."));
    if (!bStarted)
    {
        Result->SetStringField(TEXT("job_id"), TEXT(""));
        OutError = TEXT("The lighting build did not start. Lightmass refused it and reported the "
                        "reason to the editor's message log; the usual causes are a level with no "
                        "static geometry to light and a Lightmass configuration error.");
        return Answer(false);
    }
    // ADDITIVE. Nothing above changed: no parameter was renamed, action="start"
    // and action="status" behave exactly as they did, and this command still
    // does not wait. It registers the build it just started as a job so it can
    // be polled and cancelled through the one job API instead of only through
    // this command's own hand-rolled status action. Result is shared with the
    // job record, so job_result hands back this same body.
    const FString JobId = RegisterJob(
        EBridgeJobKind::LightingBuild, TEXT("lighting_build"), LevelPath, Result);
    Result->SetStringField(TEXT("job_id"), JobId);
    return Answer(true);
}
