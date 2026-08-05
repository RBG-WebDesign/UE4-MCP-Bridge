#include "MCPPuerTSBridgeService.h"

#include "Editor.h"
#include "AssetRegistryModule.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Engine/Brush.h"
#include "Model.h"
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "ISourceControlModule.h"
#include "ObjectTools.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Containers/Ticker.h"
#include "Interfaces/IPluginManager.h"
#include "PlayInEditorDataTypes.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <processthreadsapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPPuerTSBridge, Log, All);

namespace
{
const TCHAR* BridgeConfigSection = TEXT("MCPPuerTSBridge");

FString BuildDefaultProjectPipeName()
{
    FString Name = FPaths::GetBaseFilename(FPaths::GetProjectFilePath());
    if (Name.IsEmpty())
    {
        Name = TEXT("Project");
    }
    for (TCHAR& Character : Name)
    {
        if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
        {
            Character = TEXT('_');
        }
    }
    Name.LeftInline(32);

    FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeDirectoryName(ProjectRoot);
    ProjectRoot.ToLowerInline();
    return FString::Printf(
        TEXT("\\\\.\\pipe\\UE427PuerTSMCP_%s_%08x"),
        *Name,
        FCrc::StrCrc32(*ProjectRoot));
}

/** Where Initialize advertises the live pipe name and where Shutdown retracts it. */
FString GetPipeAdvertisePath()
{
    return FPaths::ProjectSavedDir() / TEXT("MCPPuerTSBridge") / TEXT("pipe.txt");
}

/** The session manifest. pipe.txt carried a pipe name and nothing else, which is
    enough to reach AN editor and not enough to know WHICH. */
FString GetSessionManifestPath()
{
    return FPaths::ProjectSavedDir() / TEXT("MCPPuerTSBridge") / TEXT("session.json");
}

/** Schema version of session.json. A client that does not recognise the version
    must refuse rather than guess: reading an unknown manifest optimistically is
    how a client ends up confidently addressing the wrong editor. */
constexpr int32 SessionSchemaVersion = 1;

/** This process's real creation time from the OS, not the time the plugin
    happened to start. It is the half of the identity that PID cannot provide:
    Windows reuses process ids, so a stale manifest naming a dead editor's PID
    can match a live unrelated process, and only the creation time separates
    them. */
FString GetProcessStartTimeUtc()
{
#if PLATFORM_WINDOWS
    FILETIME Creation, Exit, Kernel, User;
    if (::GetProcessTimes(::GetCurrentProcess(), &Creation, &Exit, &Kernel, &User))
    {
        // FILETIME is 100-nanosecond ticks since 1601-01-01 UTC, which is exactly
        // what FDateTime counts from, so this is a straight reinterpretation.
        const uint64 Ticks = (static_cast<uint64>(Creation.dwHighDateTime) << 32)
            | static_cast<uint64>(Creation.dwLowDateTime);
        return FDateTime(static_cast<int64>(Ticks)).ToIso8601();
    }
#endif
    return FString();
}

/** The bridge revision this editor is running, taken from the install manifest
    the sync gate wrote rather than recomputed here: the editor has no git and
    the manifest is the record of what was actually installed and built. */
void ReadInstalledManifest(FString& OutCommit, FString& OutHash)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MCPBridge"));
    if (!Plugin.IsValid()) { return; }
    const FString ManifestPath = Plugin->GetBaseDir() / TEXT("MCPBridgeInstall.json");
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *ManifestPath)) { return; }

    FSHA1 Sha;
    const FTCHARToUTF8 Utf8(*Text);
    Sha.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    Sha.Final();
    uint8 Digest[20];
    Sha.GetHash(Digest);
    OutHash = BytesToHex(Digest, 20).ToLower();

    TSharedPtr<FJsonObject> Manifest;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    if (FJsonSerializer::Deserialize(Reader, Manifest) && Manifest.IsValid())
    {
        Manifest->TryGetStringField(TEXT("bridge_commit"), OutCommit);
    }
}

TArray<TSharedPtr<FJsonValue>> ToJsonArray(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    Result.Reserve(Values.Num());
    for (const FString& Value : Values)
    {
        Result.Add(MakeShared<FJsonValueString>(Value));
    }
    return Result;
}

TArray<TSharedPtr<FJsonValue>> CopyArrayField(
    const TSharedPtr<FJsonObject>& Source,
    const FString& FieldName)
{
    const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
    if (Source.IsValid() && Source->TryGetArrayField(FieldName, Existing) && Existing != nullptr)
    {
        return *Existing;
    }
    return TArray<TSharedPtr<FJsonValue>>();
}

bool IsProjectPackagePath(const FString& PackagePath)
{
    return PackagePath.StartsWith(TEXT("/Game/"))
        && FPackageName::IsValidLongPackageName(PackagePath, false);
}

bool ResolveProjectMapFilename(
    const FString& PackagePath,
    FString& OutFilename,
    FString& OutError)
{
    if (!IsProjectPackagePath(PackagePath))
    {
        OutError = TEXT("Level paths must be valid package paths under /Game/.");
        return false;
    }
    if (!FPackageName::DoesPackageExist(PackagePath, nullptr, &OutFilename))
    {
        OutError = FString::Printf(TEXT("Level was not found: %s"), *PackagePath);
        return false;
    }
    if (FPaths::GetExtension(OutFilename, true) != FPackageName::GetMapPackageExtension())
    {
        OutError = FString::Printf(TEXT("Package is not a UE4 map: %s"), *PackagePath);
        return false;
    }
    return true;
}

TArray<FString> DirtyProjectPackageNames()
{
    TArray<UPackage*> Packages;
    FEditorFileUtils::GetDirtyWorldPackages(Packages);
    FEditorFileUtils::GetDirtyContentPackages(Packages);

    TArray<FString> Names;
    for (UPackage* Package : Packages)
    {
        if (Package != nullptr)
        {
            Names.AddUnique(Package->GetName());
        }
    }
    Names.Sort();
    return Names;
}

bool RefuseLevelSwitchWithDirtyPackages(FString& OutError)
{
    TArray<FString> DirtyNames = DirtyProjectPackageNames();
    if (DirtyNames.Num() == 0)
    {
        return true;
    }
    if (DirtyNames.Num() > 10)
    {
        DirtyNames.SetNum(10);
    }
    OutError = FString::Printf(
        TEXT("Refusing to switch levels with unsaved packages: %s. Save first with puerts_level_save."),
        *FString::Join(DirtyNames, TEXT(", ")));
    return false;
}
}

class UMCPPuerTSBridgeService::FBridgeLogCapture final : public FOutputDevice
{
public:
    virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
    {
        FScopeLock Lock(&Mutex);
        Lines.Add(FString::Printf(TEXT("[%s] %s"), *Category.ToString(), Message));
        constexpr int32 MaximumStoredLines = 2000;
        if (Lines.Num() > MaximumStoredLines)
        {
            Lines.RemoveAt(0, Lines.Num() - MaximumStoredLines, false);
        }
    }

    int32 Marker() const
    {
        FScopeLock Lock(&Mutex);
        return Lines.Num();
    }

    TArray<FString> Since(int32 MarkerValue, int32 MaximumLines) const
    {
        FScopeLock Lock(&Mutex);
        const int32 First = FMath::Clamp(MarkerValue, 0, Lines.Num());
        const int32 Available = Lines.Num() - First;
        const int32 Count = FMath::Clamp(MaximumLines, 0, Available);
        TArray<FString> Result;
        for (int32 Index = Lines.Num() - Count; Index < Lines.Num(); ++Index)
        {
            Result.Add(Lines[Index]);
        }
        return Result;
    }

private:
    mutable FCriticalSection Mutex;
    TArray<FString> Lines;
};

UMCPPuerTSBridgeService::~UMCPPuerTSBridgeService() = default;

bool UMCPPuerTSBridgeService::Initialize(FString& OutError)
{
    if (!GConfig->GetString(BridgeConfigSection, TEXT("PipeName"), PipeName, GEngineIni)
        || PipeName.TrimStartAndEnd().IsEmpty())
    {
        PipeName = BuildDefaultProjectPipeName();
    }
    GConfig->GetString(BridgeConfigSection, TEXT("AllowedScriptRoot"), AllowedScriptRoot, GEngineIni);
    GConfig->GetString(BridgeConfigSection, TEXT("BootstrapModule"), BootstrapModule, GEngineIni);
    GConfig->GetInt(BridgeConfigSection, TEXT("RequestTimeoutMilliseconds"), RequestTimeoutMilliseconds, GEngineIni);
    GConfig->GetInt(BridgeConfigSection, TEXT("MaximumRequestBytes"), MaximumRequestBytes, GEngineIni);
    GConfig->GetBool(BridgeConfigSection, TEXT("bAllowShellCommands"), bAllowShellCommands, GEngineIni);
    GConfig->GetBool(BridgeConfigSection, TEXT("bRequireBearerToken"), bRequireBearerToken, GEngineIni);
    GConfig->GetString(BridgeConfigSection, TEXT("BearerToken"), BearerToken, GEngineIni);

    TArray<FString> Values;
    GConfig->GetArray(BridgeConfigSection, TEXT("AllowedTools"), Values, GEngineIni);
    for (const FString& Value : Values) { AllowedTools.Add(Value); }
    Values.Reset();
    GConfig->GetArray(BridgeConfigSection, TEXT("AllowedWritableProperties"), Values, GEngineIni);
    for (const FString& Value : Values) { AllowedWritableProperties.Add(Value); }
    Values.Reset();
    GConfig->GetArray(BridgeConfigSection, TEXT("AllowedFunctions"), Values, GEngineIni);
    for (const FString& Value : Values) { AllowedFunctions.Add(Value); }

    if (AllowedTools.Num() == 0)
    {
        const TCHAR* Defaults[] = {
            // Every native command this build advertises. Deduplicated: five lanes
            // each appended their own block plus the same read-only entries, and a
            // set hides that, so the duplicates are removed here rather than left
            // for the next reader to wonder about.
            TEXT("set_property"), TEXT("call_function"), TEXT("spawn_actor"), TEXT("delete_actor"),
            // Permanent asset deletion. It is not an editor transaction because
            // neither the package file nor broken references can be restored by undo.
            TEXT("delete_asset"),
            TEXT("save"), TEXT("level_create"), TEXT("level_load"), TEXT("level_save"), TEXT("pie_start"), TEXT("pie_stop"), TEXT("undo"),
            TEXT("physics_build"), TEXT("physics_observe"), TEXT("viewport_screenshot"), TEXT("sky_shader_create"),
            TEXT("blueprint_build"), TEXT("blueprint_graph_patch"), TEXT("blueprint_member_patch"), TEXT("widget_build"),
            TEXT("widget_bind"),
            TEXT("behavior_tree_build"), TEXT("anim_blueprint_build"), TEXT("anim_blueprint_patch"),
            TEXT("blackboard_build"), TEXT("ai_perception_build"),
            TEXT("material_instance_build"), TEXT("scene_batch"), TEXT("input_mapping_patch"), TEXT("folder_visibility"),
            TEXT("camera_shake"), TEXT("material_build"), TEXT("texture_import"),
            TEXT("camera_shake"), TEXT("class_defaults_patch"),
            // Starts a Lightmass build and returns; it opens no transaction and
            // is absent from IsToolMutating for the same reason the viewport
            // commands are. A lighting build is not something a transaction can
            // take back.
            TEXT("lighting_build"),
            TEXT("camera_shake"), TEXT("sequence_build"), TEXT("anim_montage_build"),
            TEXT("sequence_event_track_build"),
            TEXT("data_table_build"),
            TEXT("camera_shake"),
            // Mutating, and deliberately absent from IsToolMutating below.
            // Navigation build is derived data regenerated by background tasks
            // that the transaction buffer does not record; the editor's own
            // Build Paths calls ResetTransaction before triggering it
            // (EditorBuildUtils.cpp:395), so a transaction here would record an
            // undo entry that restores nothing.
            TEXT("nav_build"),
            // Read only. Deliberately absent from IsToolMutating below, so they
            // open no transaction and return no transaction id.
            TEXT("diagnostic"), TEXT("find_assets"), TEXT("find_actors"), TEXT("read_property"),
            TEXT("get_logs"), TEXT("graph_inspect"), TEXT("behavior_tree_inspect"), TEXT("widget_inspect"),
            TEXT("anim_blueprint_inspect"), TEXT("anim_montage_inspect"), TEXT("anim_blend_space_inspect"), TEXT("blackboard_inspect"),
            TEXT("eqs_inspect"), TEXT("nav_inspect"), TEXT("nav_query"), TEXT("ai_controller_inspect"),
            TEXT("material_inspect"), TEXT("scene_inspect"), TEXT("input_mapping_info"), TEXT("pie_agent_query"),
            // Runtime-only control. Deliberately absent from IsToolMutating:
            // editor transactions do not record changes inside a PIE world.
            TEXT("pie_agent_control"),
            // The comma after sequence_inspect is load bearing. Without it the
            // C++ preprocessor concatenates the two adjacent string literals
            // into "sequence_inspectaudio_inspect" and BOTH tools fall off the
            // allowlist, refused as unknown at AcceptCommand with nothing in
            // the build to say why.
            TEXT("sequence_inspect"),
            TEXT("audio_inspect"), TEXT("audio_build"), TEXT("cloth_inspect"),
            TEXT("anim_blend_space_build"),
            // The job API. job_status, job_result and job_cancel are short
            // commands that read a small in-memory record and ask the live
            // engine source for one job's state; they never block and never
            // wait. The two *_start commands spawn child processes and return
            // job ids. See MCPPuerTSBridgeJobs.cpp.
            TEXT("job_status"), TEXT("job_result"), TEXT("job_cancel"),
            TEXT("sequence_render_start"), TEXT("project_package_start"),
            TEXT("project_settings_maps"),
            // Reaches every Project Settings page by writing config properties
            // on the page's settings CDO. Deliberately not gated by
            // AllowedWritableProperties: that list is per Class.Property and
            // would need a C++ edit and rebuild for every setting, which is the
            // opposite of what this command is for.
            TEXT("project_settings_patch")
        };
        for (const TCHAR* Value : Defaults) { AllowedTools.Add(Value); }
    }
    if (AllowedWritableProperties.Num() == 0)
    {
        // Widened for scene_batch, by name, one line per capability. This list
        // is the security boundary for every reflected write the bridge makes,
        // so it grows by deliberate edit and never by request: scene_batch
        // refuses a property that is not here rather than reaching past it.
        // Everything added below is level dressing - light shaping, volume
        // settings, and the mesh a placed StaticMeshActor shows. Nothing here
        // reaches gameplay defaults, class layout, or project config.
        const TCHAR* Defaults[] = {
            TEXT("Actor.bHidden"), TEXT("Actor.Tags"), TEXT("Actor.ActorLabel"),
            TEXT("SceneComponent.RelativeLocation"), TEXT("SceneComponent.RelativeRotation"),
            TEXT("SceneComponent.RelativeScale3D"), TEXT("SceneComponent.Mobility"),
            TEXT("LightComponentBase.Intensity"), TEXT("LightComponentBase.LightColor"),
            TEXT("LightComponentBase.CastShadows"),
            TEXT("LightComponentBase.IndirectLightingIntensity"),
            TEXT("LightComponentBase.VolumetricScatteringIntensity"),
            TEXT("LightComponent.Temperature"), TEXT("LightComponent.bUseTemperature"),
            TEXT("LocalLightComponent.AttenuationRadius"),
            TEXT("SpotLightComponent.InnerConeAngle"), TEXT("SpotLightComponent.OuterConeAngle"),
            TEXT("PostProcessVolume.Settings"), TEXT("PostProcessVolume.Priority"),
            TEXT("PostProcessVolume.BlendRadius"), TEXT("PostProcessVolume.BlendWeight"),
            TEXT("PostProcessVolume.bEnabled"), TEXT("PostProcessVolume.bUnbound"),
            TEXT("BoxComponent.BoxExtent"),
            TEXT("StaticMeshComponent.StaticMesh"),
            // Widened for class_defaults_patch, by name. The first two let an
            // authored Pawn use an authored AIController. The third lets an
            // authored player Character possess Player0. All three are inherited
            // APawn class defaults, not Blueprint variables.
            TEXT("Pawn.AIControllerClass"), TEXT("Pawn.AutoPossessAI"),
            TEXT("Pawn.AutoPossessPlayer"),
            // Widened for the World Settings > Game Mode panel: the per-level
            // GameMode Override plus the six class slots it exposes once set.
            // WorldSettings.DefaultGameMode is the reflected name behind the
            // "GameMode Override" DisplayName; the other six live on
            // GameModeBase, not on the level's placed GameModeBase actor.
            TEXT("WorldSettings.DefaultGameMode"),
            TEXT("GameModeBase.DefaultPawnClass"), TEXT("GameModeBase.PlayerControllerClass"),
            TEXT("GameModeBase.HUDClass"), TEXT("GameModeBase.GameStateClass"),
            TEXT("GameModeBase.PlayerStateClass"), TEXT("GameModeBase.SpectatorClass"),
            // Widened so a C++ Character base class can stay generic (no
            // ConstructorHelpers asset path baked in) and a thin Blueprint
            // subclass carries the mesh + Anim Blueprint as desired state,
            // set through class_defaults_patch instead of a hardcoded path.
            TEXT("SkeletalMeshComponent.SkeletalMesh"), TEXT("SkeletalMeshComponent.AnimClass")
        };
        for (const TCHAR* Value : Defaults) { AllowedWritableProperties.Add(Value); }
    }
    if (AllowedFunctions.Num() == 0)
    {
        const TCHAR* Defaults[] = {
            TEXT("Actor.GetActorLocation"), TEXT("Actor.GetActorRotation"), TEXT("Actor.SetActorLabel")
        };
        for (const TCHAR* Value : Defaults) { AllowedFunctions.Add(Value); }
    }

    RequestTimeoutMilliseconds = FMath::Clamp(RequestTimeoutMilliseconds, 100, 30000);
    MaximumRequestBytes = FMath::Clamp(MaximumRequestBytes, 1024, 1024 * 1024);
    if (!PipeName.StartsWith(TEXT("\\\\.\\pipe\\")))
    {
        OutError = TEXT("PipeName must be a local Windows named pipe.");
        return false;
    }
    if (AllowedTools.Num() == 0)
    {
        OutError = TEXT("No tools are allowed by [MCPPuerTSBridge].");
        return false;
    }
    if (bAllowShellCommands)
    {
        OutError = TEXT("Shell commands must remain disabled.");
        return false;
    }
    if (!bRequireBearerToken)
    {
        OutError = TEXT("Named-pipe commands require bearer authentication.");
        return false;
    }
    if (!ValidateScriptConfiguration(OutError) || !LoadOrCreateBearerToken(OutError))
    {
        return false;
    }

    // Advertise the active pipe name beside the token so a client can discover
    // it from the project root alone, whatever [MCPPuerTSBridge] renamed it to.
    // Kept for clients that predate session.json; it is a routing hint only and
    // is no longer sufficient on its own, because a pipe name says how to reach
    // an editor and nothing about which editor answered.
    const FString PipeAdvertisePath = GetPipeAdvertisePath();
    if (!FFileHelper::SaveStringToFile(PipeName, *PipeAdvertisePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogMCPPuerTSBridge, Warning,
            TEXT("Could not advertise the pipe name at %s"), *PipeAdvertisePath);
    }

    BeginSession();

    LogCapture = MakeShared<FBridgeLogCapture>();
    if (GLog != nullptr)
    {
        GLog->AddOutputDevice(LogCapture.Get());
    }
    UE_LOG(LogMCPPuerTSBridge, Display, TEXT("PuerTS command pipe configured: %s"), *PipeName);
    return true;
}

void UMCPPuerTSBridgeService::BeginSession()
{
    SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens).ToLower();
    // Two independent GUIDs of entropy. The nonce is what a request presents to
    // prove it meant THIS editor, so it must not be derivable from the session
    // id, which is published in every response and in the manifest.
    SessionNonce = (FGuid::NewGuid().ToString(EGuidFormats::Digits)
        + FGuid::NewGuid().ToString(EGuidFormats::Digits)).ToLower();
    EditorProcessId = FPlatformProcess::GetCurrentProcessId();
    ProcessStartTimeUtc = GetProcessStartTimeUtc();
    ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    ProjectPath.RemoveFromEnd(TEXT("/"));
    UProjectPath = FPaths::IsProjectFilePathSet()
        ? FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())
        : FString();
    ReadInstalledManifest(BridgeCommit, InstalledManifestHash);
    SessionCreatedAt = FDateTime::UtcNow().ToIso8601();

    WriteSessionManifest(TEXT("running"));

    // The heartbeat is what lets a client tell "this editor is busy compiling"
    // from "this editor is gone". It is deliberately NOT the liveness test on
    // its own: it runs on the game thread, so a long Blueprint compile stalls it
    // while the editor is perfectly alive. Liveness is the PID; the heartbeat is
    // context for the error message.
    HeartbeatHandle = FTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMCPPuerTSBridgeService::TickHeartbeat), 5.0f);

    UE_LOG(LogMCPPuerTSBridge, Display,
        TEXT("MCPBridge session %s started: pid %u, project %s, pipe %s"),
        *SessionId, EditorProcessId, *ProjectPath, *PipeName);
}

void UMCPPuerTSBridgeService::WriteSessionManifest(const TCHAR* ShutdownState) const
{
    TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
    Manifest->SetNumberField(TEXT("schema_version"), SessionSchemaVersion);
    Manifest->SetStringField(TEXT("session_id"), SessionId);
    Manifest->SetStringField(TEXT("session_nonce"), SessionNonce);
    Manifest->SetNumberField(TEXT("editor_pid"), static_cast<double>(EditorProcessId));
    Manifest->SetStringField(TEXT("process_start_time"), ProcessStartTimeUtc);
    Manifest->SetStringField(TEXT("project_path"), ProjectPath);
    Manifest->SetStringField(TEXT("uproject_path"), UProjectPath);
    Manifest->SetStringField(TEXT("pipe_name"), PipeName);
    Manifest->SetStringField(TEXT("bridge_commit"), BridgeCommit);
    Manifest->SetStringField(TEXT("installed_manifest_hash"), InstalledManifestHash);
    Manifest->SetStringField(TEXT("created_at"), SessionCreatedAt);
    Manifest->SetStringField(TEXT("last_heartbeat_at"), FDateTime::UtcNow().ToIso8601());
    Manifest->SetStringField(TEXT("shutdown_state"), ShutdownState);

    const FString Path = GetSessionManifestPath();
    // Write beside the target and move over it. SaveStringToFile truncates first,
    // so a client reading during a heartbeat would see an empty or partial file
    // and, on a lenient parse, could fall through to some other discovery path.
    // A move is the only step a reader can observe, and it is all-or-nothing.
    const FString TempPath = FString::Printf(TEXT("%s.%u.tmp"), *Path, EditorProcessId);
    if (!FFileHelper::SaveStringToFile(SerializeJson(Manifest), *TempPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogMCPPuerTSBridge, Warning, TEXT("Could not stage the session manifest at %s"), *TempPath);
        return;
    }
    if (!IFileManager::Get().Move(*Path, *TempPath, /*bReplace=*/true, /*bEvenIfReadOnly=*/true))
    {
        UE_LOG(LogMCPPuerTSBridge, Warning, TEXT("Could not publish the session manifest at %s"), *Path);
        IFileManager::Get().Delete(*TempPath, false, true, true);
    }
}

bool UMCPPuerTSBridgeService::TickHeartbeat(float /*DeltaSeconds*/)
{
    WriteSessionManifest(TEXT("running"));
    return true;
}

void UMCPPuerTSBridgeService::Shutdown()
{
    bRuntimeReady = false;
    RuntimeToolCount = 0;
    const bool bHadActiveCommand = !ActiveCommandId.IsEmpty();
    EndActiveCommand();

    // Job records die with the editor, and job_status says so rather than
    // reporting a job from a previous session as running. What must not die
    // untidily is the OS handle a sequence render holds: closing the handle
    // does NOT stop the render, which is a separate process and deliberately
    // outlives this editor.
    for (FBridgeJob& Job : Jobs)
    {
        if (Job.ProcessHandle.IsValid()) { FPlatformProcess::CloseProc(Job.ProcessHandle); }
        if (Job.ProcessReadPipe != nullptr)
        {
            FPlatformProcess::ClosePipe(Job.ProcessReadPipe, nullptr);
            Job.ProcessReadPipe = nullptr;
        }
    }
    Jobs.Reset();

    if (HeartbeatHandle.IsValid())
    {
        FTicker::GetCoreTicker().RemoveTicker(HeartbeatHandle);
        HeartbeatHandle.Reset();
    }
    // Mark the session shut down, then remove it. The intermediate write is not
    // ceremony: if the delete fails (a client holding the file open, a locked
    // directory) the manifest that survives says "shut_down" rather than
    // advertising a dead editor as live. Both paths are under this project's own
    // Saved directory, so retracting touches only this editor's advertisement
    // and another editor's session is untouched.
    if (!SessionId.IsEmpty())
    {
        WriteSessionManifest(TEXT("shut_down"));
        const FString SessionPath = GetSessionManifestPath();
        const bool bDeleted = IFileManager::Get().Delete(*SessionPath, false, false, true);
        UE_LOG(LogMCPPuerTSBridge, Display,
            TEXT("MCPBridge session %s ended: manifest %s"),
            *SessionId, bDeleted ? TEXT("retracted") : TEXT("marked shut_down but NOT deleted"));
    }

    // Retract the advertisement Initialize wrote. Without this the file outlives the
    // editor, and the next client to read the project root is handed the pipe name of
    // a session that no longer exists.
    const FString PipeAdvertisePath = GetPipeAdvertisePath();
    const bool bWasAdvertised = IFileManager::Get().FileExists(*PipeAdvertisePath);
    const bool bRetracted =
        bWasAdvertised && IFileManager::Get().Delete(*PipeAdvertisePath, false, false, true);

    UE_LOG(LogMCPPuerTSBridge, Display,
        TEXT("MCPBridge lifecycle: service shutdown, pending command %s, pipe advertisement %s."),
        bHadActiveCommand ? TEXT("cancelled") : TEXT("none"),
        bWasAdvertised ? (bRetracted ? TEXT("retracted") : TEXT("COULD NOT BE DELETED")) : TEXT("already absent"));

    if (GLog != nullptr && LogCapture != nullptr)
    {
        GLog->RemoveOutputDevice(LogCapture.Get());
    }
    LogCapture.Reset();
}

void UMCPPuerTSBridgeService::SetRuntimeReady(const int32 ToolCount)
{
    RuntimeToolCount = FMath::Max(0, ToolCount);
    bRuntimeReady = RuntimeToolCount > 0;
}

bool UMCPPuerTSBridgeService::IsRuntimeReady() const
{
    return bRuntimeReady;
}

int32 UMCPPuerTSBridgeService::GetRuntimeToolCount() const
{
    return RuntimeToolCount;
}

FString UMCPPuerTSBridgeService::AcceptCommand(const FString& RequestJson)
{
    if (!ActiveCommandId.IsEmpty())
    {
        TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
        Envelope->SetBoolField(TEXT("accepted"), false);
        Envelope->SetObjectField(TEXT("response"), BuildErrorResponse(TEXT("Bridge is busy."), TEXT("Commands are serialized on the Unreal game thread.")));
        return SerializeJson(Envelope);
    }

    FTCHARToUTF8 Utf8(*RequestJson);
    if (Utf8.Length() > MaximumRequestBytes)
    {
        TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
        Envelope->SetBoolField(TEXT("accepted"), false);
        Envelope->SetObjectField(TEXT("response"), BuildErrorResponse(TEXT("Request rejected."), TEXT("JSON body exceeds the configured size limit.")));
        return SerializeJson(Envelope);
    }

    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    FString Error;
    FString CommandId;
    FString ToolName;
    FString SuppliedToken;
    FString SuppliedNonce;
    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        Error = TEXT("Request must be a JSON object.");
    }
    else if (!Request->TryGetStringField(TEXT("id"), CommandId) || CommandId.IsEmpty())
    {
        Error = TEXT("id must be a non-empty string.");
    }
    else if (!Request->TryGetStringField(TEXT("tool"), ToolName) || !AllowedTools.Contains(ToolName))
    {
        Error = TEXT("tool is not on the native allowlist.");
    }
    else if (Request->HasField(TEXT("params")) && !Request->TryGetObjectField(TEXT("params"), Params))
    {
        Error = TEXT("params must be a JSON object.");
    }
    else if (!Request->TryGetStringField(TEXT("auth"), SuppliedToken) || SuppliedToken != BearerToken)
    {
        Error = TEXT("A valid local bearer token is required.");
    }
    // Session addressing, checked here and not in the script layer because this
    // is the safety boundary and because a request that reached the wrong editor
    // must be refused before anything runs, not after. The token proves the
    // caller is allowed to talk to an editor; the nonce proves it meant THIS one.
    // With two editors open, every project has its own token, so the token alone
    // would already reject a cross-connection - but only by accident of key
    // material, and a shared or copied token would silently permit it.
    else if (!Request->TryGetStringField(TEXT("session_nonce"), SuppliedNonce) || SuppliedNonce.IsEmpty())
    {
        Error = FString::Printf(
            TEXT("session_nonce is required. This editor is session %s (pid %u, project %s); ")
            TEXT("read Saved/MCPPuerTSBridge/session.json in that project and send its session_nonce."),
            *SessionId, EditorProcessId, *ProjectPath);
    }
    else if (SuppliedNonce != SessionNonce)
    {
        Error = FString::Printf(
            TEXT("session_nonce does not match this editor. The request was addressed to a ")
            TEXT("different or previous session; this editor is session %s (pid %u, project %s). ")
            TEXT("Re-read Saved/MCPPuerTSBridge/session.json: a nonce is regenerated on every start, ")
            TEXT("so a stale one means the editor restarted, not that the caller is untrusted."),
            *SessionId, EditorProcessId, *ProjectPath);
    }
    else if (Request->HasField(TEXT("expect_session_id")))
    {
        FString ExpectedSessionId;
        Request->TryGetStringField(TEXT("expect_session_id"), ExpectedSessionId);
        if (!ExpectedSessionId.IsEmpty() && ExpectedSessionId != SessionId)
        {
            Error = FString::Printf(
                TEXT("expect_session_id %s does not match this editor's session %s (pid %u, project %s)."),
                *ExpectedSessionId, *SessionId, EditorProcessId, *ProjectPath);
        }
    }

    else if (GEditor != nullptr
        && (GEditor->PlayWorld != nullptr || GEditor->GetPlaySessionRequest().IsSet())
        && ToolName != TEXT("pie_stop")
        && ToolName != TEXT("get_logs")
        && ToolName != TEXT("physics_observe")
        && ToolName != TEXT("pie_agent_query"))
    {
        Error = TEXT("Editor operations are blocked during Play In Editor. Stop PIE first.");
    }
    if (!Error.IsEmpty())
    {
        TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
        Envelope->SetBoolField(TEXT("accepted"), false);
        Envelope->SetObjectField(TEXT("response"), BuildErrorResponse(TEXT("Command rejected."), Error));
        return SerializeJson(Envelope);
    }

    Request->RemoveField(TEXT("auth"));
    if (!Request->HasField(TEXT("params")))
    {
        Request->SetObjectField(TEXT("params"), MakeShared<FJsonObject>());
    }
    ActiveCommandId = CommandId;
    ActiveToolName = ToolName;
    ActiveLogMarker = LogCapture != nullptr ? LogCapture->Marker() : 0;
    ActiveCommandStartSeconds = FPlatformTime::Seconds();
    if (IsToolMutating(ToolName))
    {
        ActiveTransactionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        const FText Description = FText::FromString(FString::Printf(TEXT("MCP PuerTS: %s"), *ToolName));
        ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
    }
    Request->SetStringField(TEXT("transaction_id"), ActiveTransactionId);

    TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
    Envelope->SetBoolField(TEXT("accepted"), true);
    Envelope->SetObjectField(TEXT("request"), Request);
    return SerializeJson(Envelope);
}

FString UMCPPuerTSBridgeService::CompleteCommand(const FString& CommandId, const FString& ResponseJson)
{
    if (CommandId.IsEmpty() || CommandId != ActiveCommandId)
    {
        return SerializeJson(BuildErrorResponse(TEXT("Completion rejected."), TEXT("Command id does not match the active command.")));
    }

    TSharedPtr<FJsonObject> ScriptResponse;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseJson);
    if (!FJsonSerializer::Deserialize(Reader, ScriptResponse) || !ScriptResponse.IsValid())
    {
        ScriptResponse = BuildErrorResponse(TEXT("PuerTS returned invalid JSON."), TEXT("Every command result must be a JSON object."));
    }

    bool bSuccess = false;
    ScriptResponse->TryGetBoolField(TEXT("success"), bSuccess);
    FString Message;
    ScriptResponse->TryGetStringField(TEXT("message"), Message);
    TSharedPtr<FJsonObject> Final = BuildBaseResponse(bSuccess, Message);
    const TSharedPtr<FJsonValue> Data = ScriptResponse->TryGetField(TEXT("data"));
    if (Data.IsValid())
    {
        Final->SetField(TEXT("data"), Data);
    }
    Final->SetArrayField(TEXT("changed_assets"), CopyArrayField(ScriptResponse, TEXT("changed_assets")));
    Final->SetArrayField(TEXT("changed_actors"), CopyArrayField(ScriptResponse, TEXT("changed_actors")));
    Final->SetArrayField(TEXT("warnings"), CopyArrayField(ScriptResponse, TEXT("warnings")));
    Final->SetArrayField(TEXT("errors"), CopyArrayField(ScriptResponse, TEXT("errors")));

    TArray<TSharedPtr<FJsonValue>> Logs = CopyArrayField(ScriptResponse, TEXT("log_output"));
    if (LogCapture != nullptr)
    {
        Logs.Append(ToJsonArray(LogCapture->Since(ActiveLogMarker, 200)));
    }
    Final->SetArrayField(TEXT("log_output"), Logs);

    FString ScriptTransactionId;
    ScriptResponse->TryGetStringField(TEXT("transaction_id"), ScriptTransactionId);
    const FString ResultTransactionId = ActiveTransactionId.IsEmpty() ? ScriptTransactionId : ActiveTransactionId;
    Final->SetStringField(TEXT("transaction_id"), ResultTransactionId);
    Final->SetNumberField(TEXT("native_duration_ms"),
        ActiveCommandStartSeconds > 0.0 ? (FPlatformTime::Seconds() - ActiveCommandStartSeconds) * 1000.0 : 0.0);

    if (!ActiveTransactionId.IsEmpty())
    {
        LastMCPTransactionId = ActiveTransactionId;
        LastUndoActorName.Reset();
        LastUndoPropertyName.Reset();
        LastUndoValueJson.Reset();
        if (bSuccess)
        {
            LastUndoActorName = ActiveUndoActorName;
            LastUndoPropertyName = ActiveUndoPropertyName;
            LastUndoValueJson = ActiveUndoValueJson;
        }
        if (!bSuccess)
        {
            TArray<TSharedPtr<FJsonValue>> Warnings = CopyArrayField(Final, TEXT("warnings"));
            Warnings.Add(MakeShared<FJsonValueString>(TEXT("A failed mutating command may be undone with its transaction_id.")));
            Final->SetArrayField(TEXT("warnings"), Warnings);
        }
    }

    EndActiveCommand();
    return SerializeJson(Final);
}

bool UMCPPuerTSBridgeService::PrepareObjectMutation(UObject* Object, const FString& PropertyName)
{
    if (Object == nullptr || ActiveCommandId.IsEmpty() || ActiveTransaction == nullptr)
    {
        return false;
    }
    if (!IsWritablePropertyAllowed(Object, PropertyName))
    {
        return false;
    }
    Object->Modify();
    return true;
}

void UMCPPuerTSBridgeService::FinalizeObjectMutation(UObject* Object)
{
    if (Object == nullptr || ActiveCommandId.IsEmpty() || ActiveTransaction == nullptr)
    {
        return;
    }
    Object->PostEditChange();
    Object->MarkPackageDirty();
}

bool UMCPPuerTSBridgeService::IsWritablePropertyAllowed(UObject* Object, const FString& PropertyName) const
{
    if (Object == nullptr || PropertyName.IsEmpty())
    {
        return false;
    }
    for (UClass* Class = Object->GetClass(); Class != nullptr; Class = Class->GetSuperClass())
    {
        if (AllowedWritableProperties.Contains(Class->GetName() + TEXT(".") + PropertyName))
        {
            return true;
        }
    }
    return false;
}

bool UMCPPuerTSBridgeService::IsFunctionAllowed(const FString& QualifiedFunctionName) const
{
    return AllowedFunctions.Contains(QualifiedFunctionName);
}

TArray<AActor*> UMCPPuerTSBridgeService::GetLevelActors() const
{
    TArray<AActor*> Actors;
    UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World == nullptr)
    {
        return Actors;
    }
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (IsValid(Actor) && !Actor->IsPendingKill())
        {
            Actors.Add(Actor);
        }
    }
    return Actors;
}

FString UMCPPuerTSBridgeService::GetDiagnosticsJson(int32 ActorLimit) const
{
    const int32 Limit = FMath::Clamp(ActorLimit, 1, 500);
    const double QueryStart = FPlatformTime::Seconds();
    const TArray<AActor*> Actors = GetLevelActors();
    const double QueryMilliseconds = (FPlatformTime::Seconds() - QueryStart) * 1000.0;

    TArray<TSharedPtr<FJsonValue>> Snapshot;
    const int32 SnapshotCount = FMath::Min(Limit, Actors.Num());
    Snapshot.Reserve(SnapshotCount);
    for (int32 Index = 0; Index < SnapshotCount; ++Index)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Actors[Index]->GetName());
        Item->SetStringField(TEXT("path"), Actors[Index]->GetPathName());
        Snapshot.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> SnapshotRoot = MakeShared<FJsonObject>();
    SnapshotRoot->SetArrayField(TEXT("actors"), Snapshot);
    const double SerializationStart = FPlatformTime::Seconds();
    const FString SnapshotJson = SerializeJson(SnapshotRoot);
    const double SerializationMilliseconds = (FPlatformTime::Seconds() - SerializationStart) * 1000.0;
    FTCHARToUTF8 SnapshotUtf8(*SnapshotJson);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("transport"), TEXT("named_pipe"));
    Result->SetStringField(TEXT("execution_context"), TEXT("puerts_node_v8_in_process"));
    Result->SetStringField(TEXT("pipe_name"), PipeName);
    Result->SetBoolField(TEXT("is_game_thread"), IsInGameThread());
    Result->SetNumberField(TEXT("thread_id"), static_cast<double>(FPlatformTLS::GetCurrentThreadId()));
    Result->SetStringField(TEXT("service_address"), FString::Printf(TEXT("%p"), this));
    Result->SetNumberField(TEXT("actor_count_total"), Actors.Num());
    Result->SetNumberField(TEXT("actor_count_measured"), SnapshotCount);
    Result->SetNumberField(TEXT("native_actor_query_ms"), QueryMilliseconds);
    Result->SetNumberField(TEXT("json_snapshot_serialization_ms"), SerializationMilliseconds);
    Result->SetNumberField(TEXT("json_snapshot_bytes"), SnapshotUtf8.Length());
    Result->SetBoolField(TEXT("string_evaluation_supported"), false);
    Result->SetStringField(TEXT("string_evaluation_reason"), TEXT("Arbitrary string evaluation is intentionally not exposed."));
    return SerializeJson(Result);
}
FString UMCPPuerTSBridgeService::GetLevelActorsJson() const
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (AActor* Actor : GetLevelActors())
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Actor->GetName());
        Item->SetStringField(TEXT("path"), Actor->GetPathName());
        Item->SetStringField(TEXT("class_name"), Actor->GetClass()->GetName());
        Item->SetStringField(TEXT("label"), Actor->GetActorLabel());
        const FVector Location = Actor->GetActorLocation();
        TSharedPtr<FJsonObject> LocationJson = MakeShared<FJsonObject>();
        LocationJson->SetNumberField(TEXT("x"), Location.X);
        LocationJson->SetNumberField(TEXT("y"), Location.Y);
        LocationJson->SetNumberField(TEXT("z"), Location.Z);
        Item->SetObjectField(TEXT("location"), LocationJson);
        Values.Add(MakeShared<FJsonValueObject>(Item));
    }
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("actors"), Values);
    return SerializeJson(Root);
}
bool UMCPPuerTSBridgeService::FindAssetsJson(
    const FString& Path,
    const FString& TypeFilter,
    const FString& NameFilter,
    bool bRecursive,
    int32 Limit,
    FString& OutAssetsJson,
    FString& OutError) const
{
    if (!Path.StartsWith(TEXT("/Game")) && !Path.StartsWith(TEXT("/Engine")))
    {
        OutError = TEXT("Asset search is limited to /Game and /Engine.");
        return false;
    }

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Assets;
    if (!AssetRegistryModule.Get().GetAssetsByPath(FName(*Path), Assets, bRecursive))
    {
        OutError = TEXT("Asset registry search failed.");
        return false;
    }
    Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
    {
        return Left.ObjectPath.LexicalLess(Right.ObjectPath);
    });

    TArray<TSharedPtr<FJsonValue>> Matches;
    const int32 Maximum = FMath::Clamp(Limit, 1, 500);
    for (const FAssetData& Asset : Assets)
    {
        const FString Name = Asset.AssetName.ToString();
        const FString Type = Asset.AssetClass.ToString();
        if ((!NameFilter.IsEmpty() && !Name.Contains(NameFilter, ESearchCase::IgnoreCase))
            || (!TypeFilter.IsEmpty() && !Type.Contains(TypeFilter, ESearchCase::IgnoreCase)))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), Asset.ObjectPath.ToString());
        Item->SetStringField(TEXT("name"), Name);
        Item->SetStringField(TEXT("type"), Type);
        Matches.Add(MakeShared<FJsonValueObject>(Item));
        if (Matches.Num() >= Maximum)
        {
            break;
        }
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("assets"), Matches);
    Root->SetNumberField(TEXT("count"), Matches.Num());
    OutAssetsJson = SerializeJson(Root);
    return true;
}
bool UMCPPuerTSBridgeService::DeleteAsset(
    const FString& InAssetPath,
    bool bConfirm,
    bool bForce,
    FString& OutResultJson,
    FString& OutError) const
{
    if (!bConfirm)
    {
        OutError = TEXT("delete_asset requires confirm=true because asset deletion is permanent.");
        return false;
    }

    const FString AssetPath = InAssetPath.TrimStartAndEnd();
    FString PackagePath = AssetPath.Contains(TEXT("."))
        ? FPackageName::ObjectPathToPackageName(AssetPath)
        : AssetPath;
    if (!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackagePath))
    {
        OutError = TEXT("Asset deletion is limited to a valid package under /Game/.");
        return false;
    }
    const FString ObjectPath = AssetPath.Contains(TEXT("."))
        ? AssetPath
        : PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath);

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
    const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FName(*ObjectPath));
    if (!AssetData.IsValid())
    {
        if (FPackageName::DoesPackageExist(PackagePath))
        {
            OutError = FString::Printf(
                TEXT("The package '%s' exists but the Asset Registry has no asset at '%s'. Refusing to guess."),
                *PackagePath,
                *ObjectPath);
            return false;
        }
        TSharedPtr<FJsonObject> Absent = MakeShared<FJsonObject>();
        Absent->SetStringField(TEXT("asset_path"), PackagePath);
        Absent->SetStringField(TEXT("object_path"), ObjectPath);
        Absent->SetBoolField(TEXT("force"), bForce);
        Absent->SetArrayField(TEXT("referencers"), TArray<TSharedPtr<FJsonValue>>());
        Absent->SetBoolField(TEXT("deleted"), false);
        Absent->SetBoolField(TEXT("already_absent"), true);
        Absent->SetBoolField(TEXT("registry_absent"), true);
        Absent->SetBoolField(TEXT("package_absent"), true);
        OutResultJson = SerializeJson(Absent);
        return true;
    }

    if (GEditor != nullptr)
    {
        UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
        if (EditorWorld != nullptr && EditorWorld->GetOutermost()->GetFName() == AssetData.PackageName)
        {
            OutError = TEXT("The currently open level cannot be deleted. Load another level first.");
            return false;
        }
    }

    UObject* Asset = AssetData.GetAsset();
    if (Asset == nullptr)
    {
        OutError = FString::Printf(TEXT("The asset at '%s' could not be loaded for deletion."), *ObjectPath);
        return false;
    }

    FCanDeleteAssetResult CanDelete;
    TArray<UObject*> Objects;
    Objects.Add(Asset);
    FEditorDelegates::OnAssetsCanDelete.Broadcast(Objects, CanDelete);
    if (!CanDelete.Get())
    {
        OutError = FString::Printf(TEXT("The editor vetoed deletion of '%s'. See the Output Log."), *ObjectPath);
        return false;
    }

    FString PackageFilename;
    if (!ISourceControlModule::Get().IsEnabled()
        && FPackageName::DoesPackageExist(PackagePath, nullptr, &PackageFilename)
        && IFileManager::Get().IsReadOnly(*PackageFilename))
    {
        OutError = FString::Printf(
            TEXT("'%s' is read-only and source control is disabled. Make the file writable before deleting it."),
            *PackageFilename);
        return false;
    }

    TArray<FName> ReferencerNames;
    AssetRegistry.GetReferencers(AssetData.PackageName, ReferencerNames);
    ReferencerNames.Remove(AssetData.PackageName);
    ReferencerNames.Sort([](const FName& Left, const FName& Right)
    {
        return Left.LexicalLess(Right);
    });
    TArray<TSharedPtr<FJsonValue>> Referencers;
    for (const FName& Referencer : ReferencerNames)
    {
        Referencers.Add(MakeShared<FJsonValueString>(Referencer.ToString()));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), ObjectPath);
    Result->SetBoolField(TEXT("force"), bForce);
    Result->SetArrayField(TEXT("referencers"), Referencers);
    if (ReferencerNames.Num() > 0 && !bForce)
    {
        Result->SetBoolField(TEXT("deleted"), false);
        Result->SetBoolField(TEXT("blocked"), true);
        Result->SetStringField(TEXT("blocked_reason"), TEXT("asset_referenced"));
        OutResultJson = SerializeJson(Result);
        return true;
    }

    int32 DeletedCount = 0;
    if (bForce)
    {
        DeletedCount = ObjectTools::ForceDeleteObjects(Objects, false);
    }
    else
    {
        TArray<FAssetData> Assets;
        Assets.Add(AssetData);
        DeletedCount = ObjectTools::DeleteAssets(Assets, false);
    }

    const bool bRegistryAbsent = !AssetRegistry.GetAssetByObjectPath(FName(*ObjectPath)).IsValid();
    const bool bPackageAbsent = !FPackageName::DoesPackageExist(PackagePath);
    const bool bDeleted = DeletedCount == 1 && bRegistryAbsent && bPackageAbsent;
    Result->SetNumberField(TEXT("deleted_count"), DeletedCount);
    Result->SetBoolField(TEXT("registry_absent"), bRegistryAbsent);
    Result->SetBoolField(TEXT("package_absent"), bPackageAbsent);
    Result->SetBoolField(TEXT("deleted"), bDeleted);
    Result->SetBoolField(TEXT("blocked"), false);
    if (!bDeleted)
    {
        Result->SetStringField(
            TEXT("failure_reason"),
            TEXT("UE4.27 did not report and verify exactly one deleted asset. It may still have an in-memory reference."));
    }
    OutResultJson = SerializeJson(Result);
    return true;
}
AActor* UMCPPuerTSBridgeService::FindLevelActor(const FString& NameOrPath) const
{
    for (AActor* Actor : GetLevelActors())
    {
        if (Actor->GetName().Equals(NameOrPath, ESearchCase::IgnoreCase)
            || Actor->GetPathName().Equals(NameOrPath, ESearchCase::IgnoreCase)
            || Actor->GetActorLabel().Equals(NameOrPath, ESearchCase::IgnoreCase))
        {
            return Actor;
        }
    }
    return nullptr;
}
UObject* UMCPPuerTSBridgeService::FindObjectByPath(const FString& ObjectPath) const
{
    return ObjectPath.IsEmpty()
        ? nullptr
        : StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
}

bool UMCPPuerTSBridgeService::ReadObjectPropertyJson(
    UObject* Object,
    const FString& PropertyName,
    FString& OutValueJson,
    FString& OutObjectPath,
    FString& OutError) const
{
    if (Object == nullptr)
    {
        OutError = TEXT("Object not found.");
        return false;
    }
    FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), *PropertyName);
    if (Property == nullptr)
    {
        OutError = TEXT("Reflected property not found.");
        return false;
    }
    TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(
        Property,
        Property->ContainerPtrToValuePtr<void>(Object));
    if (!Value.IsValid())
    {
        OutError = TEXT("Property could not be serialized.");
        return false;
    }
    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetField(TEXT("value"), Value);
    OutValueJson = SerializeJson(Wrapper);
    OutObjectPath = Object->GetPathName();
    return true;
}

bool UMCPPuerTSBridgeService::SetObjectPropertyJson(
    UObject* Object,
    const FString& PropertyName,
    const FString& ValueJson,
    FString& OutObjectPath,
    FString& OutError)
{
    if (Object == nullptr)
    {
        OutError = TEXT("Object not found.");
        return false;
    }
    FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), *PropertyName);
    if (Property == nullptr)
    {
        OutError = TEXT("Reflected property not found.");
        return false;
    }
    if (!IsWritablePropertyAllowed(Object, PropertyName))
    {
        OutError = TEXT("Writable property is not approved.");
        return false;
    }
    TSharedPtr<FJsonObject> Wrapper;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ValueJson);
    if (!FJsonSerializer::Deserialize(Reader, Wrapper) || !Wrapper.IsValid())
    {
        OutError = TEXT("value must be valid JSON.");
        return false;
    }
    TSharedPtr<FJsonValue> Value = Wrapper->TryGetField(TEXT("value"));
    if (!Value.IsValid())
    {
        OutError = TEXT("value is required.");
        return false;
    }
    TSharedPtr<FJsonValue> PreviousValue = FJsonObjectConverter::UPropertyToJsonValue(
        Property,
        Property->ContainerPtrToValuePtr<void>(Object));
    if (!PreviousValue.IsValid())
    {
        OutError = TEXT("Previous property value could not be serialized.");
        return false;
    }
    TSharedPtr<FJsonObject> PreviousWrapper = MakeShared<FJsonObject>();
    PreviousWrapper->SetField(TEXT("value"), PreviousValue);
    if (!PrepareObjectMutation(Object, PropertyName))
    {
        OutError = TEXT("Native transaction preparation failed.");
        return false;
    }
    if (!FJsonObjectConverter::JsonValueToUProperty(
            Value,
            Property,
            Property->ContainerPtrToValuePtr<void>(Object)))
    {
        OutError = TEXT("Property value is invalid for its reflected type.");
        return false;
    }
    // Name the property that changed. USceneComponent only refreshes its world
    // transform when the event identifies RelativeLocation/Rotation/Scale3D, so
    // the bare PostEditChange() would leave a moved component stale.
    FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
    Object->PostEditChangeProperty(ChangedEvent);
    Object->MarkPackageDirty();
    if (AActor* Actor = Cast<AActor>(Object))
    {
        ActiveUndoActorName = Actor->GetName();
        ActiveUndoPropertyName = PropertyName;
        ActiveUndoValueJson = SerializeJson(PreviousWrapper);
    }
    OutObjectPath = Object->GetPathName();
    return true;
}

bool UMCPPuerTSBridgeService::ReadActorPropertyJson(
    const FString& NameOrPath,
    const FString& PropertyName,
    FString& OutValueJson,
    FString& OutActorPath,
    FString& OutError) const
{
    AActor* Actor = FindLevelActor(NameOrPath);
    if (Actor == nullptr)
    {
        OutError = TEXT("Actor not found.");
        return false;
    }
    return ReadObjectPropertyJson(Actor, PropertyName, OutValueJson, OutActorPath, OutError);
}

bool UMCPPuerTSBridgeService::SetActorPropertyJson(
    const FString& NameOrPath,
    const FString& PropertyName,
    const FString& ValueJson,
    FString& OutActorPath,
    FString& OutError)
{
    AActor* Actor = FindLevelActor(NameOrPath);
    if (Actor == nullptr)
    {
        OutError = TEXT("Actor not found.");
        return false;
    }
    return SetObjectPropertyJson(Actor, PropertyName, ValueJson, OutActorPath, OutError);
}
bool UMCPPuerTSBridgeService::CallActorFunctionJson(
    const FString& NameOrPath,
    const FString& QualifiedFunctionName,
    const FString& ArgumentsJson,
    FString& OutResultJson,
    FString& OutActorPath,
    FString& OutError)
{
    if (!IsFunctionAllowed(QualifiedFunctionName))
    {
        OutError = TEXT("Function is not approved.");
        return false;
    }
    AActor* Actor = FindLevelActor(NameOrPath);
    if (Actor == nullptr)
    {
        OutError = TEXT("Actor not found.");
        return false;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    if (QualifiedFunctionName == TEXT("Actor.GetActorLocation"))
    {
        const FVector Location = Actor->GetActorLocation();
        TSharedPtr<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetNumberField(TEXT("x"), Location.X);
        Value->SetNumberField(TEXT("y"), Location.Y);
        Value->SetNumberField(TEXT("z"), Location.Z);
        Result->SetObjectField(TEXT("result"), Value);
    }
    else if (QualifiedFunctionName == TEXT("Actor.GetActorRotation"))
    {
        const FRotator Rotation = Actor->GetActorRotation();
        TSharedPtr<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetNumberField(TEXT("pitch"), Rotation.Pitch);
        Value->SetNumberField(TEXT("yaw"), Rotation.Yaw);
        Value->SetNumberField(TEXT("roll"), Rotation.Roll);
        Result->SetObjectField(TEXT("result"), Value);
    }
    else if (QualifiedFunctionName == TEXT("Actor.SetActorLabel"))
    {
        TSharedPtr<FJsonObject> Arguments;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        FString Label;
        if (!FJsonSerializer::Deserialize(Reader, Arguments)
            || !Arguments.IsValid()
            || !Arguments->TryGetArrayField(TEXT("arguments"), Values)
            || Values == nullptr
            || Values->Num() != 1
            || !(*Values)[0].IsValid()
            || (*Values)[0]->Type != EJson::String
            || !(*Values)[0]->TryGetString(Label)
            || Label.IsEmpty())
        {
            OutError = TEXT("Actor.SetActorLabel requires one non-empty string argument.");
            return false;
        }
        if (!PrepareObjectMutation(Actor, TEXT("ActorLabel")))
        {
            OutError = TEXT("ActorLabel mutation is not approved.");
            return false;
        }
        Actor->SetActorLabel(Label, true);
        FinalizeObjectMutation(Actor);
        Result->SetStringField(TEXT("result"), Label);
    }
    else
    {
        OutError = TEXT("Approved function has no native executor.");
        return false;
    }

    OutActorPath = Actor->GetPathName();
    OutResultJson = SerializeJson(Result);
    return true;
}

bool UMCPPuerTSBridgeService::SpawnActorJson(
    const FString& ClassPath,
    float X,
    float Y,
    float Z,
    float Pitch,
    float Yaw,
    float Roll,
    FString& OutActorJson,
    FString& OutError)
{
    if ((!ClassPath.StartsWith(TEXT("/Game/"))
            && !ClassPath.StartsWith(TEXT("/Script/Engine."))
            && ClassPath != TEXT("/Script/CinematicCamera.CineCameraActor")
            && ClassPath != TEXT("/Script/LevelSequence.LevelSequenceActor")
            // NavMeshBoundsVolume is NotBlueprintable, so wrapping it in an
            // empty /Game Blueprint - the usual route for a project-native
            // class - is refused by the engine itself. There is no other way
            // to make a level navigable. Kept in sync with the identical
            // allowlist in puerts-runtime/src/registry.ts's spawnActor.
            && ClassPath != TEXT("/Script/NavigationSystem.NavMeshBoundsVolume"))
        || GEditor == nullptr
        || ActiveTransaction == nullptr)
    {
        OutError = TEXT("Actor spawn requires an approved class path and active transaction.");
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    UClass* ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath);
    if (World == nullptr || World->GetCurrentLevel() == nullptr || ActorClass == nullptr)
    {
        OutError = TEXT("Actor class or editor world is unavailable.");
        return false;
    }

    const FTransform Transform(FRotator(Pitch, Yaw, Roll), FVector(X, Y, Z));
    AActor* Actor = GEditor->AddActor(
        World->GetCurrentLevel(),
        ActorClass,
        Transform,
        true,
        RF_Transactional);
    if (!IsValid(Actor))
    {
        OutError = TEXT("Unreal could not spawn the actor.");
        return false;
    }

    // ABrush (NavMeshBoundsVolume and every other Volume) has an empty Model
    // - zero polygons - until something builds one. The editor's own "Place
    // Actors" volume placement runs a brush builder as part of placement;
    // plain SpawnActor/AddActor does not, so a spawned volume has correct
    // transform and PolyFlags but a BrushComponent with nothing in it: its
    // bounds read {0,0,0} and it registers no navigable area no matter what
    // scale is applied afterward, because scaling zero geometry is still
    // zero.
    //
    // The deeper reason: ABrush::Brush (the UModel* holding the actual BSP
    // geometry) is UPROPERTY(Instanced) but is never constructed by
    // ABrush::ABrush - Brush.cpp's constructor only builds BrushComponent.
    // UEditorBrushBuilder::EndBrush (what every Build() call ends with)
    // checks for exactly this: "UModel* Brush = BuilderBrush->Brush; if
    // (Brush == nullptr) { return true; }" - it silently no-ops and reports
    // SUCCESS with zero polys written. Every editor-driven volume placement
    // works because FActorFactoryBoxVolume (or equivalent) creates that
    // UModel before ever calling a builder; SpawnActor has no equivalent
    // step, so the Model has to be created here first.
    if (ABrush* Brush = Cast<ABrush>(Actor))
    {
        Brush->Brush = NewObject<UModel>(Brush, NAME_None, RF_Transactional);
        Brush->Brush->Initialize(Brush, true);
        Brush->GetBrushComponent()->Brush = Brush->Brush;

        // Every Volume this command can reach today is a plain box, so a
        // default 200-unit cube (UE4's own default builder-brush size) is
        // what scene_batch's later SetActorScale3D then resizes to what the
        // caller actually asked for.
        UCubeBuilder* CubeBuilder = NewObject<UCubeBuilder>(GetTransientPackage());
        CubeBuilder->X = 200.0f;
        CubeBuilder->Y = 200.0f;
        CubeBuilder->Z = 200.0f;
        CubeBuilder->Build(World, Brush);
    }

    TSharedPtr<FJsonObject> ActorJson = MakeShared<FJsonObject>();
    ActorJson->SetStringField(TEXT("name"), Actor->GetName());
    ActorJson->SetStringField(TEXT("path"), Actor->GetPathName());
    ActorJson->SetStringField(TEXT("class_name"), Actor->GetClass()->GetName());
    ActorJson->SetStringField(TEXT("label"), Actor->GetActorLabel());
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetObjectField(TEXT("actor"), ActorJson);
    OutActorJson = SerializeJson(Root);
    return true;
}

bool UMCPPuerTSBridgeService::DeleteLevelActor(
    const FString& NameOrPath,
    bool bConfirmed,
    FString& OutActorPath,
    FString& OutError)
{
    if (!bConfirmed)
    {
        OutError = TEXT("Actor deletion requires confirm=true.");
        return false;
    }
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Actor deletion requires an active transaction.");
        return false;
    }
    AActor* Actor = FindLevelActor(NameOrPath);
    if (Actor == nullptr || Actor->GetWorld() == nullptr)
    {
        OutError = TEXT("Actor not found.");
        return false;
    }

    OutActorPath = Actor->GetPathName();
    Actor->Modify();
    if (!Actor->GetWorld()->EditorDestroyActor(Actor, true))
    {
        OutError = TEXT("Unreal could not delete the actor.");
        return false;
    }
    return true;
}

bool UMCPPuerTSBridgeService::SaveProjectAsset(const FString& AssetPath, FString& OutError)
{
    if (!AssetPath.StartsWith(TEXT("/Game/")))
    {
        OutError = TEXT("Only project assets under /Game can be saved.");
        return false;
    }

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*AssetPath));
    UObject* Asset = AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
    if (Asset == nullptr)
    {
        OutError = TEXT("Project asset was not found.");
        return false;
    }

    TArray<UPackage*> Packages;
    Packages.Add(Asset->GetOutermost());
    if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
    {
        OutError = TEXT("Asset package save failed.");
        return false;
    }
    return true;
}
bool UMCPPuerTSBridgeService::SaveCurrentLevel(const FString& AssetPath, FString& OutSavedPath)
{
    if (GEditor == nullptr)
    {
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (World == nullptr || World->PersistentLevel == nullptr)
    {
        return false;
    }
    bool bSaved = false;
    if (AssetPath.IsEmpty())
    {
        // FEditorFileUtils::SaveLevel opens an interactive Save Level As dialog
        // when the level has never been saved before ("SaveAs is performed as
        // necessary" per its own header comment) unless GIsRunningUnattendedScript
        // is set, in which case it refuses cleanly instead. The bridge must never
        // block on a window it cannot see or dismiss, so this scopes the same flag
        // the engine's own Python plugin uses for unattended saves to just this
        // call, leaving a human's own interactive saves elsewhere unaffected.
        TGuardValue<bool> UnattendedGuard(GIsRunningUnattendedScript, true);
        bSaved = FEditorFileUtils::SaveLevel(World->PersistentLevel);
    }
    else if (AssetPath.StartsWith(TEXT("/Game/"))
        && FPackageName::IsValidLongPackageName(AssetPath))
    {
        const FString Filename = FPackageName::LongPackageNameToFilename(
            AssetPath,
            FPackageName::GetMapPackageExtension());
        bSaved = FEditorFileUtils::SaveMap(World, Filename);
    }
    if (bSaved)
    {
        OutSavedPath = AssetPath.IsEmpty() ? World->GetOutermost()->GetName() : AssetPath;
    }
    return bSaved;
}

bool UMCPPuerTSBridgeService::CreateLevelJson(
    const FString& LevelPath,
    const FString& TemplatePath,
    FString& OutResultJson,
    FString& OutError)
{
    if (GEditor == nullptr)
    {
        OutError = TEXT("GEditor is unavailable.");
        return false;
    }
    if (!IsProjectPackagePath(LevelPath))
    {
        OutError = TEXT("level_path must be a valid package path under /Game/.");
        return false;
    }
    if (FPackageName::DoesPackageExist(LevelPath))
    {
        OutError = FString::Printf(TEXT("Level target already exists: %s"), *LevelPath);
        return false;
    }

    FString TemplateFilename;
    if (!TemplatePath.IsEmpty()
        && !ResolveProjectMapFilename(TemplatePath, TemplateFilename, OutError))
    {
        OutError = FString::Printf(TEXT("Invalid template_path: %s"), *OutError);
        return false;
    }
    if (!RefuseLevelSwitchWithDirtyPackages(OutError))
    {
        return false;
    }

    UWorld* PreviousWorld = GEditor->GetEditorWorldContext().World();
    const FString PreviousLevel = PreviousWorld != nullptr
        ? PreviousWorld->GetOutermost()->GetName()
        : FString();

    UWorld* World = TemplatePath.IsEmpty()
        ? UEditorLoadingAndSavingUtils::NewBlankMap(false)
        : UEditorLoadingAndSavingUtils::NewMapFromTemplate(TemplateFilename, false);
    if (World == nullptr)
    {
        OutError = TEXT("Unreal could not create the new level.");
        return false;
    }
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, LevelPath))
    {
        OutError = FString::Printf(
            TEXT("Level creation reached a new unsaved world, but saving %s failed. The unsaved world remains loaded."),
            *LevelPath);
        return false;
    }

    FString SavedFilename;
    FString VerifyError;
    UWorld* LoadedWorld = GEditor->GetEditorWorldContext().World();
    if (LoadedWorld == nullptr
        || LoadedWorld->GetOutermost()->GetName() != LevelPath
        || !ResolveProjectMapFilename(LevelPath, SavedFilename, VerifyError))
    {
        OutError = FString::Printf(
            TEXT("Level save returned success but read-back failed for %s: %s"),
            *LevelPath,
            *VerifyError);
        return false;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("level"), LevelPath);
    Result->SetStringField(TEXT("previous_level"), PreviousLevel);
    Result->SetStringField(TEXT("template"), TemplatePath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("loaded"), true);
    Result->SetBoolField(TEXT("saved"), true);
    OutResultJson = SerializeJson(Result);
    return true;
}

bool UMCPPuerTSBridgeService::LoadLevelJson(
    const FString& LevelPath,
    FString& OutResultJson,
    FString& OutError)
{
    if (GEditor == nullptr)
    {
        OutError = TEXT("GEditor is unavailable.");
        return false;
    }

    FString Filename;
    if (!ResolveProjectMapFilename(LevelPath, Filename, OutError))
    {
        return false;
    }

    UWorld* CurrentWorld = GEditor->GetEditorWorldContext().World();
    const FString PreviousLevel = CurrentWorld != nullptr
        ? CurrentWorld->GetOutermost()->GetName()
        : FString();
    const bool bAlreadyLoaded = PreviousLevel == LevelPath;
    if (!bAlreadyLoaded)
    {
        if (!RefuseLevelSwitchWithDirtyPackages(OutError))
        {
            return false;
        }
        UWorld* LoadedWorld = UEditorLoadingAndSavingUtils::LoadMap(Filename);
        if (LoadedWorld == nullptr || LoadedWorld->GetOutermost()->GetName() != LevelPath)
        {
            OutError = FString::Printf(TEXT("Unreal could not load level: %s"), *LevelPath);
            return false;
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("level"), LevelPath);
    Result->SetStringField(TEXT("previous_level"), PreviousLevel);
    Result->SetBoolField(TEXT("loaded"), true);
    Result->SetBoolField(TEXT("already_loaded"), bAlreadyLoaded);
    OutResultJson = SerializeJson(Result);
    return true;
}

bool UMCPPuerTSBridgeService::SaveLevelJson(
    bool bSaveAll,
    FString& OutResultJson,
    FString& OutError)
{
    if (GEditor == nullptr)
    {
        OutError = TEXT("GEditor is unavailable.");
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (World == nullptr || World->PersistentLevel == nullptr)
    {
        OutError = TEXT("No editor level is loaded.");
        return false;
    }

    const FString LevelPath = World->GetOutermost()->GetName();
    FString ExistingFilename;
    if (!ResolveProjectMapFilename(LevelPath, ExistingFilename, OutError))
    {
        OutError = TEXT("The current level has no saved /Game map package. Create it with puerts_level_create first.");
        return false;
    }

    TArray<UPackage*> DirtyContentPackages;
    FEditorFileUtils::GetDirtyContentPackages(DirtyContentPackages);
    TArray<FString> DirtyContentNames;
    for (UPackage* Package : DirtyContentPackages)
    {
        if (Package != nullptr)
        {
            DirtyContentNames.AddUnique(Package->GetName());
        }
    }
    DirtyContentNames.Sort();

    bool bSaved = false;
    if (bSaveAll)
    {
        bSaved = UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true);
    }
    else
    {
        FString SavedLevel;
        bSaved = SaveCurrentLevel(TEXT(""), SavedLevel);
    }
    if (!bSaved)
    {
        OutError = bSaveAll
            ? TEXT("Saving one or more dirty map or content packages failed.")
            : FString::Printf(TEXT("Current level save failed: %s"), *LevelPath);
        return false;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("level_saved"), LevelPath);
    Result->SetNumberField(TEXT("assets_saved_count"), bSaveAll ? DirtyContentNames.Num() : 0);
    Result->SetArrayField(TEXT("assets_saved"), bSaveAll ? ToJsonArray(DirtyContentNames) : TArray<TSharedPtr<FJsonValue>>());
    Result->SetBoolField(TEXT("save_all"), bSaveAll);
    OutResultJson = SerializeJson(Result);
    return true;
}

bool UMCPPuerTSBridgeService::StartPlayInEditor(FString& OutError)
{
    if (GEditor == nullptr)
    {
        OutError = TEXT("GEditor is unavailable.");
        return false;
    }
    if (GEditor->PlayWorld != nullptr || GEditor->GetPlaySessionRequest().IsSet())
    {
        OutError = TEXT("A Play In Editor session is already active or queued.");
        return false;
    }
    FRequestPlaySessionParams Params;
    GEditor->RequestPlaySession(Params);
    return true;
}

bool UMCPPuerTSBridgeService::StopPlayInEditor(FString& OutError)
{
    if (GEditor == nullptr)
    {
        OutError = TEXT("GEditor is unavailable.");
        return false;
    }
    if (GEditor->PlayWorld == nullptr && !GEditor->GetPlaySessionRequest().IsSet())
    {
        OutError = TEXT("No Play In Editor session is active or queued.");
        return false;
    }
    if (GEditor->GetPlaySessionRequest().IsSet() && GEditor->PlayWorld == nullptr)
    {
        GEditor->CancelRequestPlaySession();
    }
    else
    {
        GEditor->RequestEndPlayMap();
    }
    return true;
}

bool UMCPPuerTSBridgeService::UndoLastMCPTransaction(
    const FString& ExpectedTransactionId,
    FString& OutTransactionId,
    FString& OutError)
{
    if (GEditor == nullptr)
    {
        OutError = TEXT("GEditor is unavailable.");
        return false;
    }
    if (!ActiveCommandId.IsEmpty() && ActiveToolName != TEXT("undo"))
    {
        OutError = TEXT("Another command is active.");
        return false;
    }
    if (ExpectedTransactionId.IsEmpty() || ExpectedTransactionId != LastMCPTransactionId)
    {
        OutError = TEXT("transaction_id does not match the last completed MCP transaction.");
        return false;
    }
    if (!GEditor->UndoTransaction())
    {
        if (LastUndoActorName.IsEmpty() || LastUndoPropertyName.IsEmpty() || LastUndoValueJson.IsEmpty())
        {
            OutError = TEXT("Unreal cleared the transaction and the MCP property snapshot is empty.");
            return false;
        }
        AActor* Actor = FindLevelActor(LastUndoActorName);
        if (Actor == nullptr)
        {
            OutError = FString::Printf(TEXT("Saved undo actor was not found: %s"), *LastUndoActorName);
            return false;
        }
        FProperty* Property = FindFProperty<FProperty>(Actor->GetClass(), *LastUndoPropertyName);
        if (Property == nullptr)
        {
            OutError = FString::Printf(TEXT("Saved undo property was not found: %s"), *LastUndoPropertyName);
            return false;
        }
        TSharedPtr<FJsonObject> Wrapper;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LastUndoValueJson);
        if (!FJsonSerializer::Deserialize(Reader, Wrapper) || !Wrapper.IsValid())
        {
            OutError = TEXT("Saved undo JSON is invalid.");
            return false;
        }
        TSharedPtr<FJsonValue> Value = Wrapper->TryGetField(TEXT("value"));
        if (!Value.IsValid())
        {
            OutError = TEXT("Saved undo value is invalid.");
            return false;
        }
        const FScopedTransaction RestoreTransaction(FText::FromString(TEXT("MCP PuerTS: restore after save")));
        Actor->Modify();
        if (!FJsonObjectConverter::JsonValueToUProperty(
                Value,
                Property,
                Property->ContainerPtrToValuePtr<void>(Actor)))
        {
            OutError = TEXT("Saved undo value could not be restored.");
            return false;
        }
        Actor->PostEditChange();
        Actor->MarkPackageDirty();
    }
    OutTransactionId = LastMCPTransactionId;
    LastMCPTransactionId.Reset();
    LastUndoActorName.Reset();
    LastUndoPropertyName.Reset();
    LastUndoValueJson.Reset();
    return true;
}

FString UMCPPuerTSBridgeService::GetProjectRoot() const
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
}

FString UMCPPuerTSBridgeService::GetRecentLogs(int32 MaximumLines) const
{
    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    const TArray<FString> Lines = LogCapture != nullptr
        ? LogCapture->Since(0, FMath::Clamp(MaximumLines, 0, 500))
        : TArray<FString>();
    Wrapper->SetArrayField(TEXT("lines"), ToJsonArray(Lines));
    return SerializeJson(Wrapper);
}

bool UMCPPuerTSBridgeService::AreShellCommandsAllowed() const { return bAllowShellCommands; }
FString UMCPPuerTSBridgeService::GetPipeName() const { return PipeName; }
FString UMCPPuerTSBridgeService::GetSessionId() const { return SessionId; }
FString UMCPPuerTSBridgeService::GetSessionNonce() const { return SessionNonce; }
FString UMCPPuerTSBridgeService::GetBearerToken() const { return BearerToken; }
int32 UMCPPuerTSBridgeService::GetMaximumRequestBytes() const { return MaximumRequestBytes; }
int32 UMCPPuerTSBridgeService::GetRequestTimeoutMilliseconds() const { return RequestTimeoutMilliseconds; }
const FString& UMCPPuerTSBridgeService::GetAllowedScriptRoot() const { return AllowedScriptRoot; }
const FString& UMCPPuerTSBridgeService::GetBootstrapModule() const { return BootstrapModule; }

TSharedPtr<FJsonObject> UMCPPuerTSBridgeService::BuildBaseResponse(bool bSuccess, const FString& Message) const
{
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetBoolField(TEXT("success"), bSuccess);
    Response->SetStringField(TEXT("message"), Message);
    Response->SetObjectField(TEXT("data"), MakeShared<FJsonObject>());
    Response->SetArrayField(TEXT("changed_assets"), TArray<TSharedPtr<FJsonValue>>());
    Response->SetArrayField(TEXT("changed_actors"), TArray<TSharedPtr<FJsonValue>>());
    Response->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
    Response->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
    Response->SetArrayField(TEXT("log_output"), TArray<TSharedPtr<FJsonValue>>());
    Response->SetStringField(TEXT("transaction_id"), FString());
    Response->SetStringField(TEXT("transport"), TEXT("named_pipe"));
    Response->SetStringField(TEXT("execution_context"), TEXT("puerts_node_v8_in_process"));
    Response->SetBoolField(TEXT("is_game_thread"), IsInGameThread());
    Response->SetNumberField(TEXT("thread_id"), static_cast<double>(FPlatformTLS::GetCurrentThreadId()));
    Response->SetNumberField(TEXT("native_duration_ms"), 0.0);

    // Identity on EVERY response, including rejections. This is the single place
    // every response passes through, which is the point: a caller must be able to
    // tell which editor answered without having to have asked nicely, and an
    // error response is exactly when that question matters most.
    TSharedPtr<FJsonObject> Session = MakeShared<FJsonObject>();
    Session->SetStringField(TEXT("session_id"), SessionId);
    Session->SetNumberField(TEXT("editor_pid"), static_cast<double>(EditorProcessId));
    Session->SetStringField(TEXT("process_start_time"), ProcessStartTimeUtc);
    Session->SetStringField(TEXT("project_path"), ProjectPath);
    Session->SetStringField(TEXT("uproject_path"), UProjectPath);
    Session->SetStringField(TEXT("pipe_name"), PipeName);
    // The nonce is deliberately absent: it is a credential the client already
    // holds, and echoing it would hand it to anything that got a response.
    Response->SetObjectField(TEXT("session"), Session);
    return Response;
}

TSharedPtr<FJsonObject> UMCPPuerTSBridgeService::BuildErrorResponse(const FString& Message, const FString& Error) const
{
    TSharedPtr<FJsonObject> Response = BuildBaseResponse(false, Message);
    Response->SetArrayField(TEXT("errors"), ToJsonArray({ Error }));
    return Response;
}

FString UMCPPuerTSBridgeService::SerializeJson(const TSharedPtr<FJsonObject>& Object) const
{
    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
    return Output;
}

bool UMCPPuerTSBridgeService::ValidateScriptConfiguration(FString& OutError) const
{
    FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FString ScriptRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / AllowedScriptRoot);
    FPaths::NormalizeDirectoryName(ProjectRoot);
    FPaths::NormalizeDirectoryName(ScriptRoot);
    const FString ProjectPrefix = ProjectRoot.EndsWith(TEXT("/")) ? ProjectRoot : ProjectRoot + TEXT("/");
    if (ScriptRoot != ProjectRoot && !ScriptRoot.StartsWith(ProjectPrefix))
    {
        OutError = TEXT("AllowedScriptRoot must stay inside the Unreal project.");
        return false;
    }
    FString BootstrapPath = FPaths::ConvertRelativePathToFull(ScriptRoot / BootstrapModule);
    FPaths::NormalizeFilename(BootstrapPath);
    const FString ScriptPrefix = ScriptRoot.EndsWith(TEXT("/")) ? ScriptRoot : ScriptRoot + TEXT("/");
    if (!BootstrapPath.StartsWith(ScriptPrefix) || !IFileManager::Get().FileExists(*BootstrapPath))
    {
        OutError = FString::Printf(TEXT("Approved bootstrap module is invalid: %s"), *BootstrapPath);
        return false;
    }
    return true;
}

bool UMCPPuerTSBridgeService::LoadOrCreateBearerToken(FString& OutError)
{
    if (!BearerToken.IsEmpty())
    {
        return true;
    }
    const FString TokenDirectory = FPaths::ProjectSavedDir() / TEXT("MCPPuerTSBridge");
    const FString TokenPath = TokenDirectory / TEXT("token.txt");
    IFileManager::Get().MakeDirectory(*TokenDirectory, true);
    if (FPaths::FileExists(TokenPath))
    {
        if (!FFileHelper::LoadFileToString(BearerToken, *TokenPath))
        {
            OutError = TEXT("Could not read the local bearer token.");
            return false;
        }
        BearerToken.TrimStartAndEndInline();
    }
    if (BearerToken.IsEmpty())
    {
        BearerToken = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        if (!FFileHelper::SaveStringToFile(BearerToken, *TokenPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            OutError = TEXT("Could not create the local bearer token.");
            return false;
        }
    }
    return true;
}

bool UMCPPuerTSBridgeService::IsToolMutating(const FString& ToolName) const
{
    return ToolName == TEXT("set_property")
        || ToolName == TEXT("call_function")
        || ToolName == TEXT("spawn_actor")
        || ToolName == TEXT("delete_actor")
        || ToolName == TEXT("physics_build")
        || ToolName == TEXT("sky_shader_create")
        || ToolName == TEXT("blueprint_build")
        || ToolName == TEXT("blueprint_graph_patch")
        || ToolName == TEXT("blueprint_member_patch")
        || ToolName == TEXT("widget_build")
        // widget_bind writes UWidgetBlueprint::Bindings and UWidget::bIsVariable
        // and recompiles. Its plan_only path cancels the transaction it opened
        // rather than returning before one exists, because the alternative is a
        // mutation path with no rollback.
        || ToolName == TEXT("widget_bind")
        || ToolName == TEXT("behavior_tree_build")
        // The two anim inspectors are deliberately absent: they open no
        // transaction and return no transaction id.
        //
        // anim_blueprint_patch needs the transaction for a different reason than
        // the builders do. Its real rollback is FBridgeContentSnapshot, which
        // reloads the package from disk; the transaction exists so the failure
        // path has something to CANCEL before that reload destroys the objects
        // its undo records point at.
        || ToolName == TEXT("anim_blueprint_build")
        || ToolName == TEXT("anim_blueprint_patch")
        || ToolName == TEXT("anim_montage_build")
        // blackboard_build and ai_perception_build write assets. Their
        // plan_only path returns before the mutation section, so a plan still
        // opens a transaction it never uses; that costs an empty undo entry and
        // is the cheap side of the mistake, because the alternative is a build
        // that reaches the mutation section with no rollback.
        || ToolName == TEXT("blackboard_build")
        || ToolName == TEXT("ai_perception_build")
        || ToolName == TEXT("material_instance_build")
        // material_build and texture_import both create assets, and both refuse
        // outright when ActiveTransaction is null rather than writing without a
        // rollback boundary. texture_import answers an already-correct asset
        // before it needs the transaction, so a converged rerun costs an empty
        // undo entry and nothing else.
        || ToolName == TEXT("material_build")
        || ToolName == TEXT("texture_import")
        // Writes a class default object. The transaction it opens provably does
        // NOT cover that write (finding 0r), and the command says so in
        // transaction_covers_cdo rather than relying on it; it is here because
        // the write is still an asset mutation and everything else about the
        // envelope - the transaction id, the undo entry, the changed_assets
        // report - belongs to a mutating command.
        || ToolName == TEXT("class_defaults_patch")
        // sequence_build writes a ULevelSequence asset. sequence_inspect is
        // deliberately absent: it opens no transaction and returns no
        // transaction id, like every other inspector here.
        || ToolName == TEXT("sequence_build")
        || ToolName == TEXT("sequence_event_track_build")
        || ToolName == TEXT("data_table_build")
        || ToolName == TEXT("audio_build")
        // anim_blend_space_build creates a UBlendSpace1D asset. Create-only,
        // same as anim_blueprint_build, for the same reason: this bridge has
        // no restore route for a Blend Space's sample set once it exists.
        || ToolName == TEXT("anim_blend_space_build")
        || ToolName == TEXT("scene_batch");
}

void UMCPPuerTSBridgeService::EndActiveCommand()
{
    ActiveTransaction.Reset();
    ActiveCommandId.Reset();
    ActiveToolName.Reset();
    ActiveTransactionId.Reset();
    ActiveUndoActorName.Reset();
    ActiveUndoPropertyName.Reset();
    ActiveUndoValueJson.Reset();
    ActiveLogMarker = 0;
    ActiveCommandStartSeconds = 0.0;
}
