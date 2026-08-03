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

    /** Create or update a Blueprint actor asset from one JSON spec: parent
        class, SimpleConstructionScript components, and an event graph handed
        to the existing MCPBridgeGraphBuilder executor. A component may carry a
        properties object applied to its SCS template through
        FJsonObjectConverter, with UObject references resolved by an explicit
        load rather than by ImportText. The whole spec is validated before any
        asset is created or mutated, so a rejected request never leaves a
        half-built Blueprint behind.

        Graph connections are counted rather than trusted: the builder reports
        the links it actually made, and a shortfall against the number
        requested is an error with the dropped pairs named, so a graph with a
        hole in it fails the build instead of compiling clean and saving.
        graph.connection_count in the response is the number made.

        The parent class is whatever FKismetEditorUtilities::CanCreateBlueprintOfClass
        allows, which includes USaveGame, UActorComponent and plain UObject.
        Actor-only features are gated by capability rather than by refusing the
        parent: components need an Actor's SimpleConstructionScript, and the
        BeginPlay, Tick, ActorBeginOverlap, ActorEndOverlap and InputKey node
        types bind actor entry points. Both are rejected by name, before the
        asset exists, when the parent does not derive from AActor. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildBlueprintJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Change selected existing graph state without rebuilding the graph.

        blueprint_build is desired-state: it takes a whole graph and makes the
        asset match, which is right for authoring and wrong for changing one pin
        default on a graph of forty nodes, where the caller has to restate the
        whole graph correctly or lose what they did not mention.

        This command owns the boundary around the change: one transaction, the
        compile, an independent read-back, verification that every requested
        change is actually present, and the save that only happens after that
        verification passes. The resolution and mutation themselves belong to
        MCPBridgeGraphBuilder. Nothing is cleared: a patch that fails leaves the
        graph it found. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool PatchBlueprintGraphJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Change a Blueprint's MEMBERS: variables, functions, interfaces, event
        dispatchers and components. The other half of blueprint_graph_patch,
        which owns nodes and pins and cannot reach any of these.

        A re-front of UBlueprintMutatorLibrary, which was compiled into
        MCPBridgeGraphBuilder and reachable only through the legacy Python
        listener. The mutations are entirely the library's; what this command
        adds is the boundary the library never had. Every operation is resolved
        and classified before the first one runs, because each mutator entry
        point compiles the Blueprint, so a batch that fails halfway has already
        paid for the damage a rollback then has to undo. An operation whose
        result is already present is reported unchanged and not repeated, so a
        rerun dirties nothing.

        On failure the transaction is cancelled, the rollback boundary runs, and
        whether the members actually came back is decided by reading them again
        rather than by trusting the undo. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool PatchBlueprintMembersJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Create or replace a UMG Widget Blueprint from one JSON widget tree,
        handed to the existing MCPBridgeGraphBuilder widget builder. The tree
        grammar (widget types, child-count rules per category, property names
        and their JSON types) is the builder's; this command owns the asset
        path limit, the validate-before-mutate pass, the create-versus-rebuild
        decision, and the hierarchy read-back that proves the tree exists in
        the asset rather than only in the request. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildWidgetJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Read a Widget Blueprint back as machine-readable JSON: parent class,
        the whole widget hierarchy in child order, each widget's class, variable
        flag, slot (with CanvasPanel anchors, offsets, alignment and z-order)
        and editable properties, named-slot content, exposed variables,
        bindings, animations, and a canonical structure hash.

        The independent read half of widget_build, so a desired spec can be
        compared against saved state without trusting the builder that wrote
        it. READ ONLY: not in IsToolMutating, so no transaction opens and the
        response carries no transaction id; nothing here calls Modify or
        MarkPackageDirty; and the package dirty flag is reported before and
        after. UE4.27 UMG widgets carry no GUID, so node identity is DERIVED
        from the traversal path and labeled identity_kind = "derived". */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectWidgetJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Read a Blueprint back as machine-readable JSON: parent class, SCS
        components, member variables, interfaces, functions, the graph list,
        and one graph in the shape blueprint_build writes it.

        READ ONLY, and that is the contract rather than a hope. The command is
        not in IsToolMutating, so no transaction is opened and the response
        carries no transaction id; nothing here calls Modify,
        MarkPackageDirty or a compile; and the package's dirty flag is read
        before and after the work and reported as package_dirty_before /
        package_dirty_after, so a caller can see that reading did not write
        instead of trusting the annotation.

        The member half is UBlueprintInspectorLibrary's readers, which were
        already compiled into MCPBridgeGraphBuilder with no caller. What this
        command adds is the asset resolution, the /Game and /Engine limit,
        canonical ordering, and the graph view, whose node types come from
        UBlueprintGraphBuilderLibrary::GetNodeTypeForNode - the inverse of the
        builder's own dispatch, kept beside it so the two cannot drift.

        Node identity in the response is OBSERVED, not authored: a node is
        addressed by its object name and its NodeGuid, because the "id" a
        build spec wrote is not persisted on the node. Matching an inspected
        node back to the spec line that made it needs an authored identity
        that does not exist yet. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectBlueprintJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Create or update a BehaviorTree asset with its Blackboard from one
        spec: keys, assignment, and the full node graph. A re-front of three
        libraries already compiled into MCPBridgeGraphBuilder -
        UMCPBridgeAILibrary for the reflection-protected blackboard
        operations and UBehaviorTreeBuilderLibrary for the graph, which
        replaces the tree's root only on full success, so a failed build
        leaves an existing tree untouched. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildBehaviorTreeJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Read a BehaviorTree and its Blackboard back as JSON: root, composites,
        tasks, decorators (attached to the child they guard), services, child
        order, referenced blackboard keys, key names and types, editor-visible
        node properties, and a canonical structure hash. READ ONLY: not in
        IsToolMutating, nothing calls Modify or MarkPackageDirty, and the
        package dirty flag is reported before and after the read. UE4.27 BT
        nodes have no GUIDs, so node identity is DERIVED from the traversal
        path and labeled identity_kind = "derived". */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectBehaviorTreeJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Read the editor's current level back as machine-readable JSON: every
        actor with its class, world transform, folder, tags, attachment,
        components and optionally selected reflected properties, plus every
        PlayerStart and a canonical structure hash.

        The read half of scene_batch, and READ ONLY on the same terms as
        graph_inspect: not in IsToolMutating, so no transaction opens and the
        response carries no transaction id; nothing here calls Modify or
        MarkPackageDirty; and the level package's dirty flag is reported before
        and after the read.

        Actor identity is OBSERVED (identity_kind = "observed"): the object name
        is unique within a level and stable, unlike the label, which a user
        renames freely and which is not unique at all.

        The structure hash always covers the WHOLE level. An actors filter
        narrows what is reported and never what is hashed, because a hash of a
        filter is a hash of the request. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectSceneJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Apply a desired-state description of many actors to the current level in
        ONE transaction: spawn, modify, delete, reparent, folder, tags, and
        reflected properties on actors and on the components they already have.

        Two operations, because a desired-state description of a level needs
        two: upsert_actor and delete_actor. Everything a caller would otherwise
        do with a dozen single-actor round trips is one of those.

        The command owns the boundary, not the mutation: the whole batch is
        resolved and refused before the first change, each operation's
        satisfied-ness is re-evaluated immediately before it runs rather than
        read from the plan, one rollback boundary wraps the batch, the level is
        read back independently and every operation re-checked against it, and
        any failure cancels the transaction and then decides whether the level
        actually came back by hashing it again.

        It never saves. Writing a level to disk is puerts_save's job, and
        keeping it out of here is what lets a failed batch leave nothing on
        disk to clean up.

        Trigger volumes carry the AGENTS.md PlayerStart rule: a volume that
        contains a PlayerStart refuses the whole batch, and one inside 1.5x its
        own extent of a PlayerStart warns. The check runs against the actor's
        real GetActorBounds after it is placed, inside the rollback boundary,
        because the extent of an unspawned brush volume is a guess. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool ApplySceneBatchJson(
        const FString& SpecJson,
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

    /** This editor's session identity. Two editors open at once must never be
        interchangeable to a client, and the session id plus nonce are what makes
        them distinguishable: the pipe name alone is a routing detail that a
        misconfigured or stale client can still get wrong. */
    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    FString GetSessionId() const;

    UFUNCTION(BlueprintPure, Category="MCP PuerTS Bridge")
    FString GetSessionNonce() const;

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

    /** Establish this editor's identity and write the first session manifest. */
    void BeginSession();
    /** Write Saved/MCPPuerTSBridge/session.json atomically: a client that reads
        it mid-write must never see a half-written manifest, because the failure
        that produces is a client silently talking to the wrong editor. */
    void WriteSessionManifest(const TCHAR* ShutdownState) const;
    bool TickHeartbeat(float DeltaSeconds);

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

    // Session identity. SessionNonce is the shared secret that proves a request
    // was addressed to THIS editor rather than merely arriving at it; it is
    // regenerated on every start, so a client holding a previous session's
    // manifest is rejected instead of silently retargeted.
    FString SessionId;
    FString SessionNonce;
    uint32 EditorProcessId = 0;
    FString ProcessStartTimeUtc;
    FString ProjectPath;
    FString UProjectPath;
    FString BridgeCommit;
    FString InstalledManifestHash;
    FString SessionCreatedAt;
    FDelegateHandle HeartbeatHandle;

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
