// Copyright 2026 RareBird Games. All Rights Reserved.

// ===========================================================================
// sequence_render_start: render a ULevelSequence to disk, as a job.
//
// Lane S recorded (docs/CAPABILITY_FINDINGS.md, finding 0s) that this command
// could not ship synchronously, and that verdict is not revisited here - it is
// the reason this file exists at all. Neither UE4.27 render path completes
// inside one command:
//
//   Legacy MovieSceneCapture. The editor's own Render Movie button, in
//   "separate process" mode, serializes a UAutomatedLevelSequenceCapture to a
//   JSON manifest and launches a SECOND UE process with
//   -MovieSceneCaptureManifest (MovieSceneCaptureDialogModule.cpp:678-744,
//   read at D:/UE/UE_4.27). Out of process, asynchronous, and finished when
//   that process exits.
//
//   Movie Render Queue. Present in 4.27 at
//   Engine/Plugins/MovieScene/MovieRenderPipeline and "EnabledByDefault":
//   false. A command built on it would refuse on every project that has not
//   turned the plugin on, which is most of them.
//
// This takes the legacy path, for the plugin reason and for one more: a second
// process is the only long job in this bridge that can genuinely be cancelled,
// because killing a process is something the operating system can actually do.
// Everything else in docs/PERF_AND_LONG_JOBS.md 6.4 about cooperative,
// deferred cancellation still stands; this one is the exception, and it is an
// exception for a boring reason rather than a clever one.
//
// THE REFUSALS ARE THE WORK. The second process loads the level and the
// sequence FROM DISK. An unsaved level or an unsaved sequence renders the
// previous contents of both and exits zero, which is the empty-success failure
// this repository has been bitten by before and which a caller cannot tell
// from a correct render. So both are refused by name, before anything is
// spawned.
// ===========================================================================

#include "MCPPuerTSBridgeService.h"

#include "AutomatedLevelSequenceCapture.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "LevelSequence.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MovieSceneCapture.h"
#include "Protocols/ImageSequenceProtocol.h"
#include "UObject/Package.h"

namespace
{
    /** The five image formats UE4.27 ships a capture protocol for, spelled the
        way a caller would spell them. avi is the only one that produces a
        single file; the rest are image sequences. */
    UClass* ResolveCaptureProtocol(const FString& Format)
    {
        if (Format.Equals(TEXT("png"), ESearchCase::IgnoreCase)) { return UImageSequenceProtocol_PNG::StaticClass(); }
        if (Format.Equals(TEXT("jpg"), ESearchCase::IgnoreCase)) { return UImageSequenceProtocol_JPG::StaticClass(); }
        if (Format.Equals(TEXT("bmp"), ESearchCase::IgnoreCase)) { return UImageSequenceProtocol_BMP::StaticClass(); }
        if (Format.Equals(TEXT("exr"), ESearchCase::IgnoreCase)) { return UImageSequenceProtocol_EXR::StaticClass(); }
        // avi is deliberately absent. UVideoCaptureProtocol's header includes
        // AVIWriter.h, a private header an INSTALLED engine build does not
        // ship, so supporting it would make the plugin compile only against a
        // source-built engine. The caller gets a refusal naming the formats
        // that work rather than a link error at install time.
        return nullptr;
    }
}

bool UMCPPuerTSBridgeService::RenderLevelSequenceJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Sequence render spec must be a JSON object.");
        return false;
    }

    FString AssetPath;
    if (!Spec->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutError = TEXT("asset_path is required and must name a ULevelSequence under /Game.");
        return false;
    }
    if (!AssetPath.StartsWith(TEXT("/Game/")))
    {
        OutError = TEXT("Sequence rendering is limited to /Game.");
        return false;
    }

    if (GEditor == nullptr)
    {
        OutError = TEXT("No editor engine, so there is no level to render from.");
        return false;
    }
    if (GEditor->PlayWorld != nullptr || GEditor->GetPlaySessionRequest().IsSet())
    {
        OutError = TEXT("Play In Editor is running. The render loads the level from disk in a "
                        "second process; starting one while play is active competes for the same "
                        "audio device and window focus. Stop play first.");
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (World == nullptr)
    {
        OutError = TEXT("No editor world is loaded, so there is no level for the render process "
                        "to open.");
        return false;
    }

    // ---- refusals, all of them about what the SECOND process can see -------

    UPackage* WorldPackage = World->GetOutermost();
    const FString LevelPackageName = WorldPackage->GetName();
    if (!FPackageName::DoesPackageExist(LevelPackageName))
    {
        OutError = FString::Printf(
            TEXT("The level %s has never been saved to disk. The render runs in a second process "
                 "that opens the level by package name, and there is nothing on disk for it to "
                 "open. Save the level first."), *LevelPackageName);
        return false;
    }
    if (WorldPackage->IsDirty())
    {
        OutError = FString::Printf(
            TEXT("The level %s has unsaved changes. The render process reads the level FROM DISK, "
                 "so it would render the last saved version and exit successfully - a correct "
                 "looking render of the wrong scene. Save the level (puerts_save) and start "
                 "again."), *LevelPackageName);
        return false;
    }

    ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *AssetPath);
    if (Sequence == nullptr)
    {
        OutError = FString::Printf(
            TEXT("No ULevelSequence at %s. Confirm the path with puerts_sequence_inspect."), *AssetPath);
        return false;
    }
    UPackage* SequencePackage = Sequence->GetOutermost();
    if (SequencePackage != nullptr && SequencePackage->IsDirty())
    {
        OutError = FString::Printf(
            TEXT("The sequence %s has unsaved changes, and the render process reads it from disk. "
                 "It would render the last saved cut. Save it (puerts_save) and start again."),
            *AssetPath);
        return false;
    }

    // ---- resolved settings -------------------------------------------------

    const FString Format = Spec->HasTypedField<EJson::String>(TEXT("format"))
        ? Spec->GetStringField(TEXT("format")) : TEXT("png");
    UClass* ProtocolClass = ResolveCaptureProtocol(Format);
    if (ProtocolClass == nullptr)
    {
        OutError = FString::Printf(
            TEXT("format must be png, jpg, bmp or exr; got '%s'. Those are the capture "
                 "protocols UE4.27 ships in the MovieSceneCapture module."), *Format);
        return false;
    }

    const int32 ResX = Spec->HasTypedField<EJson::Number>(TEXT("resolution_x"))
        ? FMath::Clamp(static_cast<int32>(Spec->GetNumberField(TEXT("resolution_x"))), 16, 7680) : 1280;
    const int32 ResY = Spec->HasTypedField<EJson::Number>(TEXT("resolution_y"))
        ? FMath::Clamp(static_cast<int32>(Spec->GetNumberField(TEXT("resolution_y"))), 16, 7680) : 720;
    const int32 WarmUpFrames = Spec->HasTypedField<EJson::Number>(TEXT("warm_up_frames"))
        ? FMath::Clamp(static_cast<int32>(Spec->GetNumberField(TEXT("warm_up_frames"))), 0, 600) : 0;
    const bool bOverwrite = !Spec->HasTypedField<EJson::Boolean>(TEXT("overwrite_existing"))
        || Spec->GetBoolField(TEXT("overwrite_existing"));
    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));

    // Default output goes under the project's own Saved directory, one folder
    // per sequence, so two renders of different cuts do not interleave frames.
    const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FString OutputDirectory = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("MCPRenders") / FPackageName::GetShortName(AssetPath));
    FString RequestedOutput;
    if (Spec->TryGetStringField(TEXT("output_directory"), RequestedOutput) && !RequestedOutput.IsEmpty())
    {
        OutputDirectory = FPaths::ConvertRelativePathToFull(
            FPaths::IsRelative(RequestedOutput) ? ProjectDir / RequestedOutput : RequestedOutput);
    }
    // A render writes files. An MCP tool that writes files to an arbitrary
    // absolute path is a hole, not a feature, so the output stays inside the
    // project the bridge is serving.
    if (!OutputDirectory.StartsWith(ProjectDir))
    {
        OutError = FString::Printf(
            TEXT("output_directory must be inside this project (%s). Resolved to %s."),
            *ProjectDir, *OutputDirectory);
        return false;
    }

    const FString ExecutablePath = FPlatformProcess::GenerateApplicationPath(
        FApp::GetName(), FApp::GetBuildConfiguration());
    const bool bProjectFileSet = FPaths::IsProjectFilePathSet();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("level_path"), LevelPackageName);
    Result->SetStringField(TEXT("format"), Format);
    Result->SetStringField(TEXT("capture_protocol"), ProtocolClass->GetPathName());
    Result->SetNumberField(TEXT("resolution_x"), ResX);
    Result->SetNumberField(TEXT("resolution_y"), ResY);
    Result->SetNumberField(TEXT("warm_up_frames"), WarmUpFrames);
    Result->SetBoolField(TEXT("overwrite_existing"), bOverwrite);
    Result->SetStringField(TEXT("output_directory"), OutputDirectory);
    Result->SetStringField(TEXT("executable"), ExecutablePath);
    Result->SetStringField(TEXT("render_path"), TEXT("legacy_movie_scene_capture_separate_process"));
    Result->SetStringField(TEXT("render_path_note"),
        TEXT("Legacy MovieSceneCapture in a second process. Movie Render Queue exists in 4.27 but "
             "ships EnabledByDefault=false, so it is not used: a command that refused on every "
             "project without an optional plugin turned on would be worse than one that renders."));
    Result->SetBoolField(TEXT("plan_only"), bPlanOnly);

    if (bPlanOnly)
    {
        Result->SetStringField(TEXT("job_id"), TEXT(""));
        Result->SetStringField(TEXT("status"), TEXT("planned"));
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        TSharedRef<TJsonWriter<>> PlanWriter = TJsonWriterFactory<>::Create(&OutResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), PlanWriter);
        return true;
    }

    if (!FPaths::FileExists(ExecutablePath))
    {
        OutError = FString::Printf(
            TEXT("The render needs a second copy of this editor and %s does not exist. "
                 "FPlatformProcess::GenerateApplicationPath derived it from FApp::GetName() and "
                 "the current build configuration; a non-standard install layout is the usual "
                 "cause."), *ExecutablePath);
        return false;
    }

    // ---- the capture object, and its manifest ------------------------------
    //
    // Built as the real UObject and serialized with the engine's own converter
    // rather than hand-written JSON. The child process reconstructs it with
    // FJsonObjectConverter::JsonAttributesToUStruct
    // (MovieSceneCaptureModule.cpp:104-160), so a hand-rolled manifest would
    // have to match property names this file cannot check.

    UAutomatedLevelSequenceCapture* Capture = NewObject<UAutomatedLevelSequenceCapture>(
        GetTransientPackage(), UAutomatedLevelSequenceCapture::StaticClass(), NAME_None, RF_Transient);
    if (Capture == nullptr)
    {
        OutError = TEXT("Could not construct a UAutomatedLevelSequenceCapture.");
        return false;
    }
    Capture->LevelSequenceAsset = FSoftObjectPath(AssetPath);
    Capture->WarmUpFrameCount = WarmUpFrames;
    // Both default to true and both write extra files beside the frames. A
    // render command should render; edit decision lists are a separate ask.
    Capture->bWriteEditDecisionList = false;
    Capture->bWriteFinalCutProXML = false;
    Capture->Settings.OutputDirectory.Path = OutputDirectory;
    Capture->Settings.Resolution.ResX = ResX;
    Capture->Settings.Resolution.ResY = ResY;
    Capture->Settings.bOverwriteExisting = bOverwrite;
    if (Spec->HasTypedField<EJson::Number>(TEXT("frame_rate")))
    {
        const int32 Rate = FMath::Clamp(
            static_cast<int32>(Spec->GetNumberField(TEXT("frame_rate"))), 1, 200);
        // FMovieSceneCaptureSettings::GetFrameRate returns CustomFrameRate only
        // when bUseCustomFrameRate is set, so setting one without the other is
        // silently ignored. Left untouched when the caller says nothing, so the
        // sequence's own display rate wins.
        Capture->Settings.bUseCustomFrameRate = true;
        Capture->Settings.CustomFrameRate = FFrameRate(Rate, 1);
        Capture->Settings.FrameRate = FFrameRate(Rate, 1);
        Result->SetNumberField(TEXT("frame_rate"), Rate);
    }
    FString OutputFormat;
    if (Spec->TryGetStringField(TEXT("output_format"), OutputFormat) && !OutputFormat.IsEmpty())
    {
        Capture->Settings.OutputFormat = OutputFormat;
    }
    Result->SetStringField(TEXT("output_format"), Capture->Settings.OutputFormat);
    Capture->SetImageCaptureProtocolType(ProtocolClass);

    // One manifest per render, under this bridge's own Saved directory. The
    // editor's dialog writes a single Saved/MovieSceneCapture/Manifest.json,
    // which two concurrent renders would clobber.
    const FString ManifestPath = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("MCPPuerTSBridge")
        / FString::Printf(TEXT("render-%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));

    TSharedRef<FJsonObject> CaptureObject = MakeShared<FJsonObject>();
    if (!FJsonObjectConverter::UStructToJsonObject(Capture->GetClass(), Capture, CaptureObject, 0, 0))
    {
        OutError = TEXT("The capture settings could not be serialized to a manifest, so no render "
                        "was started.");
        return false;
    }
    TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetField(TEXT("Type"), MakeShared<FJsonValueString>(Capture->GetClass()->GetPathName()));
    RootObject->SetField(TEXT("Data"), MakeShared<FJsonValueObject>(CaptureObject));
    TSharedRef<FJsonObject> AdditionalJson = MakeShared<FJsonObject>();
    Capture->SerializeJson(*AdditionalJson);
    RootObject->SetField(TEXT("AdditionalData"), MakeShared<FJsonValueObject>(AdditionalJson));

    FString ManifestText;
    TSharedRef<TJsonWriter<>> ManifestWriter = TJsonWriterFactory<>::Create(&ManifestText, 0);
    if (!FJsonSerializer::Serialize(RootObject, ManifestWriter)
        || !FFileHelper::SaveStringToFile(ManifestText, *ManifestPath))
    {
        OutError = FString::Printf(
            TEXT("The capture manifest could not be written to %s, so no render was started."),
            *ManifestPath);
        return false;
    }
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);

    // The command line the editor's own separate-process render uses
    // (MovieSceneCaptureDialogModule.cpp:705-744), with nothing added.
    const FString EditorCommandLine = FString::Printf(
        TEXT("%s -MovieSceneCaptureManifest=\"%s\" -game -NoLoadingScreen -ForceRes -Windowed "
             "-ResX=%d -ResY=%d"),
        *LevelPackageName, *ManifestPath, ResX, ResY);
    const FString Parameters = bProjectFileSet
        ? FString::Printf(TEXT("\"%s\" %s %s"), *FPaths::GetProjectFilePath(), *EditorCommandLine,
            *FCommandLine::GetSubprocessCommandline())
        : FString::Printf(TEXT("%s %s %s"), FApp::GetProjectName(), *EditorCommandLine,
            *FCommandLine::GetSubprocessCommandline());

    uint32 ProcessId = 0;
    FProcHandle Handle = FPlatformProcess::CreateProc(
        *ExecutablePath, *Parameters, true, false, false, &ProcessId, 0, nullptr, nullptr);
    if (!Handle.IsValid())
    {
        OutError = FString::Printf(
            TEXT("The render process did not start. Command line: %s %s"),
            *ExecutablePath, *Parameters);
        return false;
    }

    Result->SetStringField(TEXT("manifest_path"), ManifestPath);
    Result->SetNumberField(TEXT("process_id"), static_cast<double>(ProcessId));
    Result->SetStringField(TEXT("status"), TEXT("rendering"));
    Result->SetStringField(TEXT("completion"),
        TEXT("NOTHING IS RENDERED YET. This call spawned a second UE process and returned. Poll "
             "puerts_job_status with the job_id until state is no longer \"running\", then collect "
             "with puerts_job_result. The render is cancellable at any point with "
             "puerts_job_cancel, which kills that process; frames already written stay written."));
    Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

    const FString JobId = RegisterJob(
        EBridgeJobKind::SequenceRender, TEXT("sequence_render_start"), AssetPath, Result);
    if (FBridgeJob* Job = Jobs.FindByPredicate(
        [&JobId](const FBridgeJob& Candidate) { return Candidate.JobId == JobId; }))
    {
        Job->ProcessHandle = Handle;
        Job->ProcessId = ProcessId;
        Job->OutputDirectory = OutputDirectory;
        Job->ManifestPath = ManifestPath;
    }
    Result->SetStringField(TEXT("job_id"), JobId);

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
    FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
    return true;
}
