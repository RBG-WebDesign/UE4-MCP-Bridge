#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MCPPuerTSBridgeService.generated.h"

class FScopedTransaction;
class AActor;

UCLASS()
class MCPBRIDGEPUERTS_API UMCPPuerTSBridgeService : public UObject
{
    GENERATED_BODY()

public:
    virtual ~UMCPPuerTSBridgeService() override;

    bool Initialize(FString& OutError);
    void Shutdown();

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    FString AcceptCommand(const FString& RequestJson);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    FString CompleteCommand(const FString& CommandId, const FString& ResponseJson);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool PrepareObjectMutation(UObject* Object, const FString& PropertyName);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    void FinalizeObjectMutation(UObject* Object);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool IsWritablePropertyAllowed(UObject* Object, const FString& PropertyName) const;

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool IsFunctionAllowed(const FString& QualifiedFunctionName) const;

    TArray<AActor*> GetLevelActors() const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    FString GetLevelActorsJson() const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    FString GetDiagnosticsJson(int32 ActorLimit) const;

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    void SetRuntimeReady(int32 ToolCount);

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    bool IsRuntimeReady() const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    int32 GetRuntimeToolCount() const;

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool FindAssetsJson(
        const FString& Path,
        const FString& TypeFilter,
        const FString& NameFilter,
        bool bRecursive,
        int32 Limit,
        FString& OutAssetsJson,
        FString& OutError) const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    AActor* FindLevelActor(const FString& NameOrPath) const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    UObject* FindObjectByPath(const FString& ObjectPath) const;
    /** Serialize any reflected property of any UObject through
        FJsonObjectConverter, so structs, arrays, maps, and enums marshal the
        same way for actors and for components addressed by object path. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool ReadObjectPropertyJson(
        UObject* Object,
        const FString& PropertyName,
        FString& OutValueJson,
        FString& OutObjectPath,
        FString& OutError) const;

    /** Write any approved reflected property of any UObject from the JSON
        {"value": ...} wrapper, inside the active transaction. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool SetObjectPropertyJson(
        UObject* Object,
        const FString& PropertyName,
        const FString& ValueJson,
        FString& OutObjectPath,
        FString& OutError);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool ReadActorPropertyJson(
        const FString& NameOrPath,
        const FString& PropertyName,
        FString& OutValueJson,
        FString& OutActorPath,
        FString& OutError) const;

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool SetActorPropertyJson(
        const FString& NameOrPath,
        const FString& PropertyName,
        const FString& ValueJson,
        FString& OutActorPath,
        FString& OutError);
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool CallActorFunctionJson(
        const FString& NameOrPath,
        const FString& QualifiedFunctionName,
        const FString& ArgumentsJson,
        FString& OutResultJson,
        FString& OutActorPath,
        FString& OutError);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool SpawnActorJson(
        const FString& ClassPath,
        float X,
        float Y,
        float Z,
        float Pitch,
        float Yaw,
        float Roll,
        FString& OutActorJson,
        FString& OutError);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool DeleteLevelActor(
        const FString& NameOrPath,
        bool bConfirmed,
        FString& OutActorPath,
        FString& OutError);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool CreateAuroraSkyMaterialJson(
        const FString& AssetPath,
        const FString& SkyActorName,
        FString& OutResultJson,
        FString& OutError);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildPhysicsSceneJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool ObservePhysicsSceneJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool CaptureViewportJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool SaveProjectAsset(const FString& AssetPath, FString& OutError);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool SaveCurrentLevel(const FString& AssetPath, FString& OutSavedPath);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool StartPlayInEditor(FString& OutError);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool StopPlayInEditor(FString& OutError);

    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool UndoLastMCPTransaction(
        const FString& ExpectedTransactionId,
        FString& OutTransactionId,
        FString& OutError);

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    FString GetProjectRoot() const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    FString GetRecentLogs(int32 MaximumLines) const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    bool AreShellCommandsAllowed() const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    FString GetPipeName() const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    FString GetBearerToken() const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    int32 GetMaximumRequestBytes() const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    int32 GetRequestTimeoutMilliseconds() const;

    const FString& GetAllowedScriptRoot() const;
    const FString& GetBootstrapModule() const;

private:
    class FBridgeLogCapture;

    TSharedPtr<class FJsonObject> BuildBaseResponse(bool bSuccess, const FString& Message) const;
    TSharedPtr<FJsonObject> BuildErrorResponse(const FString& Message, const FString& Error) const;
    FString SerializeJson(const TSharedPtr<FJsonObject>& Object) const;
    bool ValidateScriptConfiguration(FString& OutError) const;
    bool LoadOrCreateBearerToken(FString& OutError);
    bool IsToolMutating(const FString& ToolName) const;
    void EndActiveCommand();

    FString PipeName = TEXT("\\\\.\\pipe\\UE427PuerTSMCP");
    FString AllowedScriptRoot = TEXT("../Plugins/MCPBridge/Content/JavaScript");
    FString BootstrapModule = TEXT("bootstrap.js");
    int32 RequestTimeoutMilliseconds = 5000;
    int32 MaximumRequestBytes = 262144;
    bool bAllowShellCommands = false;
    bool bRequireBearerToken = true;
    bool bRuntimeReady = false;
    int32 RuntimeToolCount = 0;
    FString BearerToken;
    TSet<FString> AllowedTools;
    TSet<FString> AllowedWritableProperties;
    TSet<FString> AllowedFunctions;

    TUniquePtr<FScopedTransaction> ActiveTransaction;
    FString ActiveCommandId;
    FString ActiveToolName;
    FString ActiveTransactionId;
    FString LastMCPTransactionId;
    FString ActiveUndoActorName;
    FString ActiveUndoPropertyName;
    FString ActiveUndoValueJson;
    FString LastUndoActorName;
    FString LastUndoPropertyName;
    FString LastUndoValueJson;
    int32 ActiveLogMarker = 0;
    double ActiveCommandStartSeconds = 0.0;
    TSharedPtr<FBridgeLogCapture> LogCapture;
};
