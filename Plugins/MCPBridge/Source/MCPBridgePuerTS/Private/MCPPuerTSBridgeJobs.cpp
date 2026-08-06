// Copyright 2026 RareBird Games. All Rights Reserved.

// ===========================================================================
// The asynchronous job API: job_status, job_result, job_cancel.
//
// WHAT THIS IS NOT, first, because that is the part that gets misread.
//
// This does NOT make anything asynchronous. It cannot. Every fact F1 to F6 in
// docs/PERF_AND_LONG_JOBS.md still holds: commands are serialized on the game
// thread, AcceptCommand refuses a concurrent one
// (MCPPuerTSBridgeService.cpp:415-421), the PuerTS runtime is NOT_THREAD_SAFE,
// executionTimeoutMs cannot fire for a straight-line native call, and nothing
// anywhere aborts a running native command. Wrapping a Blueprint compile or a
// package save in a "job" would change none of that: the game thread would
// still be inside the call, job_status would still not be read off the pipe
// until the work had finished, and the job id would be decoration on a
// blocking command.
//
// WHAT IT IS. A record for work that the ENGINE ALREADY ADVANCES WITHOUT THE
// BRIDGE HOLDING THE GAME THREAD. Three such things exist today, and each was
// already half-solving this problem on its own before this file existed:
//
//   LightingBuild     UEditorEngine::BuildLighting hands the level to
//                     FStaticLightingManager and RETURNS; the build then
//                     advances on UpdateBuildLighting from the editor's own
//                     tick (StaticLightingSystem.cpp:2492-2522, :341-386).
//   NavigationBuild   ANavigationData::RebuildAll is public and does not
//                     block; Recast's generator tasks run in the background
//                     (NavigationData.h:619).
//   SequenceRender    a SECOND UE process, launched with a capture manifest
//                     (MovieSceneCaptureDialogModule.cpp:678-744).
//
// Between slices of all three, the game thread is free, so job_status is an
// ordinary short command that answers in milliseconds. That is the whole
// trick, and it is why this file contains no FTicker, no state machine, no
// chunked executor and no job-owned transaction. Sections 6.5 and 6.6 of
// PERF_AND_LONG_JOBS describe those three changes and what each one breaks;
// none of them is needed to track work the engine is already ticking, and none
// of them is done here.
//
// The consequence, stated so nobody tries: a long operation that holds the
// game thread CANNOT be turned into a job by calling RegisterJob around it.
// nav_build with wait:true is exactly that case, and it deliberately gets no
// job id.
//
// NOTHING IS CACHED. A job record holds identity and the start command's
// answer. Every live field - is it running, how many build tasks remain, how
// many frames the render has written - is derived at poll time from the
// engine or the operating system. A stored progress number is the failure this
// repository has already been bitten by: it survives the thing it describes.
//
// CANCELLATION IS PER KIND AND IS NOT UNIFORM. See CancelJob below. The API
// reports cancel_supported and cancel_effect for every job, and refuses rather
// than accepting a cancel it cannot deliver, because a cancel that returns
// success and does nothing is worse than no cancel at all.
//
// ACROSS AN EDITOR RESTART every job record is gone: this map is in memory and
// the session id changes. A job id from a previous editor is REPORTED AS LOST,
// never as running, because the id carries the session that minted it. For a
// sequence render the underlying work outlives the editor - it is a separate
// process - so the lost answer says so and names the output directory, which
// is the only handle left.
// ===========================================================================

#include "MCPPuerTSBridgeService.h"

#include "Editor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Json.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"

namespace
{
    /** Past this many records the oldest finished jobs are dropped. A job map
        is a convenience, not a log: the completed output has already been
        collected by job_result, and an editor session that started forty
        renders does not need the first eight kept forever. */
    constexpr int32 MaxRetainedJobs = 32;

    const TCHAR* JobKindName(EBridgeJobKind Kind)
    {
        switch (Kind)
        {
        case EBridgeJobKind::LightingBuild:   return TEXT("lighting_build");
        case EBridgeJobKind::NavigationBuild: return TEXT("navigation_build");
        case EBridgeJobKind::SequenceRender:  return TEXT("sequence_render");
        case EBridgeJobKind::ProjectPackage:  return TEXT("project_package");
        default:                              return TEXT("unknown");
        }
    }

    bool IsTerminal(const FBridgeJob& Job)
    {
        return Job.State != TEXT("running");
    }
    bool HasSequenceFrame(const FString& Directory)
    {
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*"), true, false);
        for (const FString& File : Files)
        {
            const FString Extension = FPaths::GetExtension(File, false).ToLower();
            if (Extension == TEXT("png") || Extension == TEXT("jpg")
                || Extension == TEXT("jpeg") || Extension == TEXT("bmp") || Extension == TEXT("exr"))
            {
                return true;
            }
        }
        return false;
    }

    bool HasProjectPackageArtifacts(const FString& Directory)
    {
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*"), true, false);
        const FString ExpectedExecutable = FString(FApp::GetProjectName()) + TEXT(".exe");
        return Files.Num() > 0 && Files.ContainsByPredicate([&ExpectedExecutable](const FString& File)
        {
            return FPaths::GetCleanFilename(File).Equals(ExpectedExecutable, ESearchCase::IgnoreCase);
        });
    }

    /**
     * Whether this KIND of work can be aborted at all, and what a cancel
     * actually does when it is accepted. Every claim here is a public UE4.27
     * entry point read out of D:/UE/UE_4.27, not an assumption.
     *
     * "immediate" means the work stops as a direct result of the call.
     * "deferred" means the call sets a flag or asks a generator to unwind, and
     * the work stops at its own next check, which can be seconds later. Only
     * one of the three is immediate, and it is immediate for a boring reason:
     * it is a separate process and the operating system can kill it.
     */
    void CancelCapability(EBridgeJobKind Kind, bool& bOutSupported, FString& OutEffect, FString& OutNote)
    {
        switch (Kind)
        {
        case EBridgeJobKind::LightingBuild:
            bOutSupported = true;
            OutEffect = TEXT("deferred");
            OutNote = TEXT("GEditor->SetMapBuildCancelled(true) is public in 4.27 "
                "(EditorEngine.h:816, overridden at UnrealEdEngine.h:201) and is exactly what the "
                "editor's own cancel button reaches "
                "(StaticLightingSystem.cpp:182-193 via SBuildProgress.cpp:212). Lightmass reads the "
                "flag between units of its export and import work "
                "(Lightmass.cpp:726, :1312, :3385, :3479), so the build stops at its NEXT check, "
                "not at this call. It cannot interrupt a single engine call already in progress.");
            return;
        case EBridgeJobKind::NavigationBuild:
            bOutSupported = true;
            OutEffect = TEXT("deferred");
            OutNote = TEXT("UNavigationSystemV1::CancelBuild is public in 4.27 "
                "(NavigationSystem.h:823) and cancels every running generator. The already-queued "
                "Recast tile tasks unwind rather than stopping mid-tile, so poll "
                "remaining_build_tasks rather than assuming zero on return. A navmesh cancelled "
                "part way through is partial, not restored: navigation data is derived, and the "
                "recovery is another build.");
            return;
        case EBridgeJobKind::SequenceRender:
        case EBridgeJobKind::ProjectPackage:
            bOutSupported = true;
            OutEffect = TEXT("immediate");
            OutNote = TEXT("The work runs in a child process, so cancelling is "
                "FPlatformProcess::TerminateProc. Files already written to the output directory "
                "stay written: this stops the process, it does not clean up after it.");
            return;
        default:
            break;
        }
        bOutSupported = false;
        OutEffect = TEXT("none");
        OutNote = TEXT("This job kind exposes no abort.");
    }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

FString UMCPPuerTSBridgeService::RegisterJob(
    EBridgeJobKind Kind,
    const FString& Tool,
    const FString& Detail,
    const TSharedPtr<FJsonObject>& StartResult)
{
    ReapJobs();

    FBridgeJob Job;
    // The session id is IN the job id on purpose. It is the only thing that
    // lets job_status tell "this editor restarted and your job is gone" from
    // "no such job", and those are different answers to a caller holding an id.
    Job.JobId = FString::Printf(TEXT("%s-%d"), *SessionId, ++JobSerial);
    Job.Kind = Kind;
    Job.Tool = Tool;
    Job.Detail = Detail;
    Job.State = TEXT("running");
    Job.StartedAtSeconds = FPlatformTime::Seconds();
    Job.StartResult = StartResult;
    const FString NewJobId = Job.JobId;
    Jobs.Add(MoveTemp(Job));
    return NewJobId;
}

void UMCPPuerTSBridgeService::ReapJobs()
{
    // Close the process handle of anything that has finished. A FProcHandle is
    // an OS handle; leaving it open per render leaks one per job.
    for (FBridgeJob& Job : Jobs)
    {
        if (IsTerminal(Job) && Job.ProcessHandle.IsValid())
        {
            FPlatformProcess::CloseProc(Job.ProcessHandle);
            Job.ProcessHandle.Reset();
        }
        if (IsTerminal(Job) && Job.ProcessReadPipe != nullptr)
        {
            FPlatformProcess::ClosePipe(Job.ProcessReadPipe, nullptr);
            Job.ProcessReadPipe = nullptr;
        }
    }
    while (Jobs.Num() > MaxRetainedJobs)
    {
        int32 OldestFinished = INDEX_NONE;
        for (int32 Index = 0; Index < Jobs.Num(); ++Index)
        {
            if (IsTerminal(Jobs[Index])) { OldestFinished = Index; break; }
        }
        // Never evict a running job to make room. If every retained job is
        // still running, the cap yields rather than losing a live handle.
        if (OldestFinished == INDEX_NONE) { break; }
        Jobs.RemoveAt(OldestFinished);
    }
}

// ---------------------------------------------------------------------------
// Polling: ask the live source, latch a terminal state, cache nothing
// ---------------------------------------------------------------------------

void UMCPPuerTSBridgeService::PollJob(FBridgeJob& Job)
{
    if (IsTerminal(Job)) { return; }

    const auto Finish = [&Job](const TCHAR* State)
    {
        Job.State = State;
        Job.FinishedAtSeconds = FPlatformTime::Seconds();
    };

    switch (Job.Kind)
    {
    case EBridgeJobKind::LightingBuild:
    {
        if (GEditor == nullptr) { Finish(TEXT("failed")); return; }
        // FStaticLightingManager holds ONE active build for the whole editor,
        // so this is the state of that one build and not of a job among
        // several. lighting_build refuses a second start while one runs, which
        // is what keeps the two the same thing.
        if (GEditor->IsLightingBuildCurrentlyRunning()) { return; }
        // Cancelled counts either way it was asked for: through job_cancel, or
        // by a human clicking Cancel on the editor's own build notification.
        Finish(Job.bCancelRequested || GEditor->GetMapBuildCancelled()
            ? TEXT("cancelled") : TEXT("succeeded"));
        return;
    }
    case EBridgeJobKind::NavigationBuild:
    {
        UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
        UNavigationSystemV1* NavSystem = World != nullptr
            ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
        // A world swap (a different level opened) takes the navigation system
        // with it. The job is not running any more and there is nothing left to
        // report about it, which is a failure and not a success.
        if (NavSystem == nullptr) { Finish(TEXT("failed")); return; }
        if (NavSystem->GetNumRemainingBuildTasks() > 0) { return; }
        Finish(Job.bCancelRequested ? TEXT("cancelled") : TEXT("succeeded"));
        return;
    }
    case EBridgeJobKind::SequenceRender:
    case EBridgeJobKind::ProjectPackage:
    {
        if (Job.ProcessReadPipe != nullptr)
        {
            Job.OutputTail += FPlatformProcess::ReadPipe(Job.ProcessReadPipe);
            constexpr int32 TailLimit = 32768;
            if (Job.OutputTail.Len() > TailLimit)
            {
                Job.OutputTail.RightInline(TailLimit, false);
            }
        }
        if (!Job.ProcessHandle.IsValid()) { Finish(TEXT("failed")); return; }
        if (FPlatformProcess::IsProcRunning(Job.ProcessHandle)) { return; }
        FPlatformProcess::GetProcReturnCode(Job.ProcessHandle, &Job.ReturnCode);
        // A cancelled render exits non-zero because it was killed, so the
        // cancel flag decides the state before the return code does.
        if (Job.bCancelRequested)
        {
            Finish(TEXT("cancelled"));
            return;
        }
        if (Job.ReturnCode != 0)
        {
            Finish(TEXT("failed"));
            return;
        }
        const bool bArtifactsPresent = Job.Kind == EBridgeJobKind::SequenceRender
            ? HasSequenceFrame(Job.OutputDirectory)
            : HasProjectPackageArtifacts(Job.OutputDirectory);
        if (!bArtifactsPresent)
        {
            Job.Detail += Job.Kind == EBridgeJobKind::SequenceRender
                ? TEXT(" Child process exited successfully but wrote no supported image frame.")
                : TEXT(" Child process exited successfully but the archive is empty or lacks the project executable.");
            Finish(TEXT("failed"));
            return;
        }
        Finish(TEXT("succeeded"));
        return;
    }
    default:
        Finish(TEXT("failed"));
        return;
    }
}

// ---------------------------------------------------------------------------
// Description: one shape for every job, every answer
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> UMCPPuerTSBridgeService::DescribeJob(const FBridgeJob& Job) const
{
    TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
    const double Now = FPlatformTime::Seconds();

    Out->SetStringField(TEXT("job_id"), Job.JobId);
    Out->SetStringField(TEXT("kind"), JobKindName(Job.Kind));
    Out->SetStringField(TEXT("tool"), Job.Tool);
    Out->SetStringField(TEXT("state"), Job.State);
    Out->SetStringField(TEXT("detail"), Job.Detail);
    Out->SetBoolField(TEXT("running"), !IsTerminal(Job));
    Out->SetBoolField(TEXT("cancel_requested"), Job.bCancelRequested);
    Out->SetBoolField(TEXT("result_consumed"), Job.bConsumed);
    Out->SetNumberField(TEXT("elapsed_ms"),
        ((IsTerminal(Job) ? Job.FinishedAtSeconds : Now) - Job.StartedAtSeconds) * 1000.0);

    // The same promise the command status record makes, for the same reason:
    // a caller must not be able to read a fraction into any of this.
    Out->SetStringField(TEXT("reports"), TEXT("stage"));
    Out->SetBoolField(TEXT("percent_available"), false);
    Out->SetStringField(TEXT("percent_unavailable_reason"),
        TEXT("No UE4.27 entry point on any of these paths reports a completion fraction. What is "
             "reported instead is the live counter the engine itself keeps for that kind of work, "
             "which has no denominator: unbuilt objects for lighting, remaining build tasks for "
             "navigation, files written so far for a render."));

    bool bCancelSupported = false;
    FString CancelEffect;
    FString CancelNote;
    CancelCapability(Job.Kind, bCancelSupported, CancelEffect, CancelNote);
    Out->SetBoolField(TEXT("cancel_supported"), bCancelSupported);
    Out->SetStringField(TEXT("cancel_effect"), CancelEffect);
    Out->SetStringField(TEXT("cancel_note"), CancelNote);

    // Live fields. Read now, from the engine, never from the record.
    switch (Job.Kind)
    {
    case EBridgeJobKind::LightingBuild:
    {
        UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
        Out->SetBoolField(TEXT("build_running"),
            GEditor != nullptr && GEditor->IsLightingBuildCurrentlyRunning());
        Out->SetBoolField(TEXT("build_exporting"),
            GEditor != nullptr && GEditor->IsLightingBuildCurrentlyExporting());
#if !UE_BUILD_SHIPPING
        const int32 Unbuilt = World != nullptr ? static_cast<int32>(World->NumLightingUnbuiltObjects) : 0;
        Out->SetNumberField(TEXT("lighting_unbuilt_objects"), Unbuilt);
        Out->SetBoolField(TEXT("needs_rebuild"), Unbuilt > 0);
#endif
        Out->SetStringField(TEXT("completion_note"),
            TEXT("state succeeded means the build STOPPED RUNNING, which is all the bridge can "
                 "observe: FStaticLightingManager reports failures to the editor's message log and "
                 "not to a return value. lighting_unbuilt_objects is the independent read - it is "
                 "the counter behind the editor's own \"Lighting needs to be rebuilt\" banner - and "
                 "it is the field that says whether the build actually helped."));
        return Out;
    }
    case EBridgeJobKind::NavigationBuild:
    {
        UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
        UNavigationSystemV1* NavSystem = World != nullptr
            ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
        const int32 Remaining = NavSystem != nullptr ? NavSystem->GetNumRemainingBuildTasks() : 0;
        Out->SetNumberField(TEXT("remaining_build_tasks"), Remaining);
        Out->SetBoolField(TEXT("is_building"), Remaining > 0);
        Out->SetBoolField(TEXT("navigation_system_present"), NavSystem != nullptr);
        Out->SetStringField(TEXT("completion_note"),
            TEXT("Verify with puerts_nav_inspect. remaining_build_tasks reaching zero says the "
                 "generator stopped, not that the navmesh covers what you meant."));
        return Out;
    }
    case EBridgeJobKind::SequenceRender:
    case EBridgeJobKind::ProjectPackage:
    {
        Out->SetNumberField(TEXT("process_id"), static_cast<double>(Job.ProcessId));
        Out->SetBoolField(TEXT("process_running"),
            Job.ProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(
                const_cast<FProcHandle&>(Job.ProcessHandle)));
        Out->SetNumberField(TEXT("return_code"), Job.ReturnCode);
        Out->SetStringField(TEXT("output_directory"), Job.OutputDirectory);
        Out->SetStringField(TEXT("manifest_path"), Job.ManifestPath);
        // The closest thing to progress anywhere in this file, and it is still
        // not a fraction: how many files the render has written so far. There
        // is no denominator here because the frame count lives in the child
        // process, which this editor cannot ask.
        int32 FileCount = 0;
        if (!Job.OutputDirectory.IsEmpty())
        {
            TArray<FString> Files;
            IFileManager::Get().FindFilesRecursive(
                Files, *Job.OutputDirectory, TEXT("*"), true, false);
            FileCount = Files.Num();
        }
        Out->SetNumberField(TEXT("output_file_count"), FileCount);
        Out->SetStringField(TEXT("output_tail"), Job.OutputTail);
        Out->SetStringField(TEXT("completion_note"), Job.Kind == EBridgeJobKind::ProjectPackage
            ? TEXT("output_file_count recursively counts staged and archived files, not progress. "
                   "A zero UAT exit code and a non-empty archive both need release verification.")
            : TEXT("output_file_count is a count, not a fraction: the total frame count is known "
                   "only inside the render process. A render that exits zero having written nothing "
                   "did not render - check the output directory before believing the state."));
        return Out;
    }
    default:
        return Out;
    }
}

// ---------------------------------------------------------------------------
// job_status / job_result / job_cancel
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::ControlJobJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError)
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Job request must be a JSON object.");
        return false;
    }

    FString Op = TEXT("status");
    Request->TryGetStringField(TEXT("op"), Op);
    if (Op != TEXT("status") && Op != TEXT("result") && Op != TEXT("cancel"))
    {
        OutError = FString::Printf(
            TEXT("op must be \"status\", \"result\" or \"cancel\"; got '%s'."), *Op);
        return false;
    }

    FString JobId;
    Request->TryGetStringField(TEXT("job_id"), JobId);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("op"), Op);
    Result->SetStringField(TEXT("session_id"), SessionId);

    const auto Answer = [&](bool bSuccess) -> bool
    {
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        return bSuccess;
    };

    // ---- list every job this editor holds -------------------------------
    if (Op == TEXT("status") && JobId.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> Listed;
        for (FBridgeJob& Job : Jobs)
        {
            PollJob(Job);
            Listed.Add(MakeShared<FJsonValueObject>(DescribeJob(Job)));
        }
        Result->SetArrayField(TEXT("jobs"), Listed);
        Result->SetNumberField(TEXT("job_count"), Listed.Num());
        Result->SetStringField(TEXT("listing_note"),
            TEXT("Every job THIS editor session holds. The list is empty after an editor restart "
                 "even if work is still running, because the records live in the editor's memory. "
                 "A sequence render outlives the editor; nothing else here does."));
        return Answer(true);
    }

    if (JobId.IsEmpty())
    {
        OutError = FString::Printf(TEXT("op=\"%s\" needs a job_id."), *Op);
        return false;
    }
    Result->SetStringField(TEXT("job_id"), JobId);

    FBridgeJob* Found = Jobs.FindByPredicate(
        [&JobId](const FBridgeJob& Job) { return Job.JobId == JobId; });

    if (Found == nullptr)
    {
        // Three different answers, because they mean three different things to
        // a caller holding an id. Reporting any of them as "still running"
        // would be the worst possible outcome, and reporting them all as
        // "unknown" would hide an editor restart behind a typo.
        const bool bOurSession = !SessionId.IsEmpty() && JobId.StartsWith(SessionId + TEXT("-"));
        if (!bOurSession)
        {
            Result->SetStringField(TEXT("job_error_code"), TEXT("job_lost_editor_restarted"));
            Result->SetStringField(TEXT("state"), TEXT("lost"));
            OutError = TEXT("This job id was minted by a different editor session, so this editor "
                "knows nothing about it. Job records live in the editor's memory and do not "
                "survive a restart. A lighting build and a navigation build died with that "
                "editor. A SEQUENCE RENDER DID NOT: it is a separate process, so it may still be "
                "running and still writing frames - look in the output directory the start "
                "command reported, and in Task Manager for a second UE4Editor process.");
            return Answer(false);
        }
        Result->SetStringField(TEXT("job_error_code"), TEXT("job_unknown"));
        Result->SetStringField(TEXT("state"), TEXT("unknown"));
        OutError = FString::Printf(
            TEXT("No job '%s' in this editor session. It was either never started or, if it "
                 "finished long ago, dropped: this editor keeps at most %d records and evicts the "
                 "oldest FINISHED ones first. Call job_status with no job_id to list what is left."),
            *JobId, MaxRetainedJobs);
        return Answer(false);
    }

    PollJob(*Found);

    // ---- cancel ----------------------------------------------------------
    if (Op == TEXT("cancel"))
    {
        bool bSupported = false;
        FString Effect;
        FString Note;
        CancelCapability(Found->Kind, bSupported, Effect, Note);

        if (IsTerminal(*Found))
        {
            Result->SetObjectField(TEXT("job"), DescribeJob(*Found));
            Result->SetStringField(TEXT("job_error_code"), TEXT("job_not_running"));
            OutError = FString::Printf(
                TEXT("Job '%s' is already %s. There is nothing to cancel, and reporting a "
                     "successful cancel of finished work would be a lie."),
                *JobId, *Found->State);
            return Answer(false);
        }
        if (!bSupported)
        {
            Result->SetObjectField(TEXT("job"), DescribeJob(*Found));
            Result->SetStringField(TEXT("job_error_code"), TEXT("cancel_unsupported"));
            OutError = FString::Printf(
                TEXT("A %s job cannot be cancelled in UE4.27. %s The job keeps running; this call "
                     "changed nothing."), JobKindName(Found->Kind), *Note);
            return Answer(false);
        }

        bool bIssued = false;
        switch (Found->Kind)
        {
        case EBridgeJobKind::LightingBuild:
            if (GEditor != nullptr) { GEditor->SetMapBuildCancelled(true); bIssued = true; }
            break;
        case EBridgeJobKind::NavigationBuild:
        {
            UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
            UNavigationSystemV1* NavSystem = World != nullptr
                ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
            if (NavSystem != nullptr) { NavSystem->CancelBuild(); bIssued = true; }
            break;
        }
        case EBridgeJobKind::SequenceRender:
        case EBridgeJobKind::ProjectPackage:
            if (Found->ProcessHandle.IsValid())
            {
                // true kills the whole tree: the child is a UE editor process
                // that spawns its own helpers, and leaving those behind is how
                // an "cancelled" render keeps holding the output files open.
                FPlatformProcess::TerminateProc(Found->ProcessHandle, true);
                bIssued = true;
            }
            break;
        default:
            break;
        }

        if (!bIssued)
        {
            Result->SetObjectField(TEXT("job"), DescribeJob(*Found));
            Result->SetStringField(TEXT("job_error_code"), TEXT("cancel_target_gone"));
            OutError = TEXT("The thing this job was tracking is no longer reachable from this "
                "editor (no editor engine, no navigation system, or no process handle), so no "
                "cancel was issued. Nothing was changed.");
            return Answer(false);
        }

        Found->bCancelRequested = true;
        // Re-poll: an immediate cancel is already terminal by now, a deferred
        // one is not, and the answer must say which of the two happened rather
        // than reporting "cancelled" for both.
        PollJob(*Found);
        Result->SetObjectField(TEXT("job"), DescribeJob(*Found));
        Result->SetStringField(TEXT("cancel_effect"), Effect);
        Result->SetBoolField(TEXT("stopped_now"), IsTerminal(*Found));
        Result->SetStringField(TEXT("cancel_note"), Note);
        return Answer(true);
    }

    // ---- result ----------------------------------------------------------
    if (Op == TEXT("result"))
    {
        if (!IsTerminal(*Found))
        {
            Result->SetObjectField(TEXT("job"), DescribeJob(*Found));
            Result->SetStringField(TEXT("job_error_code"), TEXT("job_still_running"));
            OutError = FString::Printf(
                TEXT("Job '%s' is still running. Poll job_status until state is no longer "
                     "\"running\", then collect."), *JobId);
            return Answer(false);
        }
        if (Found->bConsumed)
        {
            Result->SetObjectField(TEXT("job"), DescribeJob(*Found));
            Result->SetStringField(TEXT("job_error_code"), TEXT("job_result_consumed"));
            OutError = FString::Printf(
                TEXT("The result of job '%s' has already been collected. job_result delivers a "
                     "finished job's output once; job_status still reports its state."), *JobId);
            return Answer(false);
        }
        Found->bConsumed = true;
        Result->SetObjectField(TEXT("job"), DescribeJob(*Found));
        if (Found->StartResult.IsValid())
        {
            Result->SetObjectField(TEXT("start_result"), Found->StartResult);
        }
        Result->SetStringField(TEXT("result_note"),
            TEXT("start_result is what the starting command answered, kept so a caller that lost "
                 "it (or never had it, because another client started the job) can still read it. "
                 "The finished state and every live counter are in job."));
        ReapJobs();
        return Answer(true);
    }

    // ---- status ----------------------------------------------------------
    Result->SetObjectField(TEXT("job"), DescribeJob(*Found));
    return Answer(true);
}
