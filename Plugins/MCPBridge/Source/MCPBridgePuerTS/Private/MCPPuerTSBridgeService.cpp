#include "MCPPuerTSBridgeService.h"

#include "Editor.h"
#include "AssetRegistryModule.h"
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
#include "Misc/ConfigCacheIni.h"
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
    GConfig->GetString(BridgeConfigSection, TEXT("PipeName"), PipeName, GEngineIni);
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
            TEXT("save"), TEXT("pie_start"), TEXT("pie_stop"), TEXT("undo"),
            TEXT("physics_build"), TEXT("physics_observe"), TEXT("viewport_screenshot"), TEXT("sky_shader_create"),
            TEXT("blueprint_build"), TEXT("blueprint_graph_patch"), TEXT("blueprint_member_patch"), TEXT("widget_build"),
            TEXT("behavior_tree_build"), TEXT("anim_blueprint_build"), TEXT("blackboard_build"), TEXT("ai_perception_build"),
            TEXT("material_instance_build"), TEXT("scene_batch"), TEXT("input_mapping_patch"), TEXT("folder_visibility"),
            TEXT("camera_shake"),
            // Read only. Deliberately absent from IsToolMutating below, so they
            // open no transaction and return no transaction id.
            TEXT("diagnostic"), TEXT("find_assets"), TEXT("find_actors"), TEXT("read_property"),
            TEXT("get_logs"), TEXT("graph_inspect"), TEXT("behavior_tree_inspect"), TEXT("widget_inspect"),
            TEXT("anim_blueprint_inspect"), TEXT("anim_montage_inspect"), TEXT("anim_blend_space_inspect"), TEXT("blackboard_inspect"),
            TEXT("eqs_inspect"), TEXT("nav_inspect"), TEXT("nav_query"), TEXT("ai_controller_inspect"),
            TEXT("material_inspect"), TEXT("scene_inspect"), TEXT("input_mapping_info"), TEXT("pie_agent_query")
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
            TEXT("StaticMeshComponent.StaticMesh")
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
        && ToolName != TEXT("physics_observe"))
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
            && ClassPath != TEXT("/Script/CinematicCamera.CineCameraActor"))
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
        bSaved = FEditorFileUtils::SaveLevel(World->PersistentLevel);
    }
    else if (AssetPath.StartsWith(TEXT("/Game/MCPTests/"))
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
        || ToolName == TEXT("behavior_tree_build")
        // anim_blueprint_build only. The two anim inspectors are deliberately
        // absent: they open no transaction and return no transaction id.
        || ToolName == TEXT("anim_blueprint_build")
        // blackboard_build and ai_perception_build write assets. Their
        // plan_only path returns before the mutation section, so a plan still
        // opens a transaction it never uses; that costs an empty undo entry and
        // is the cheap side of the mistake, because the alternative is a build
        // that reaches the mutation section with no rollback.
        || ToolName == TEXT("blackboard_build")
        || ToolName == TEXT("ai_perception_build")
        || ToolName == TEXT("material_instance_build")
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
