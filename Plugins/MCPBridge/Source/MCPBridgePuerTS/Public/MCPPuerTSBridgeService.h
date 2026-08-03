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

    /** Bind widget properties to Blueprint functions or variables, and expose
        widgets as members of the generated class.

        The write half of what widget_inspect could already read and nothing
        could author: a widget built by widget_build shows the constant its spec
        gave it and cannot be reached from a graph by name, because
        UWidgetBlueprint::Bindings is empty and every UWidget::bIsVariable is
        false. Desired state: the caller lists the bindings it wants, and a
        rerun applies nothing, compiles nothing and saves nothing.

        UE4.27 binds the delegate named "<PropertyName>Delegate", falling back
        to PropertyName for an event delegate, so the caller writes "Percent"
        and the engine drives "PercentDelegate"; the resolved name is in the
        response. Validation is the engine's own FEditorPropertyPath::Validate,
        run before anything is written, and a compile that comes back Error
        restores the previous bindings and variable flags and recompiles. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BindWidgetJson(
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

    /** Read an Animation Blueprint back as machine-readable JSON: target
        skeleton, stored compile status, every anim graph and its nodes, state
        machines with their entry state, states, conduits and transitions,
        each transition's rule (an explicit rule graph or the engine's automatic
        remaining-time rule, which are not interchangeable), cached poses with
        the nodes that read them, blend nodes, member variables, and a canonical
        structure hash.

        The independent read half of anim_blueprint_build, and the reason that
        command can verify instead of assert. READ ONLY: not in IsToolMutating,
        so no transaction opens and the response carries no transaction id;
        nothing here calls Modify, MarkPackageDirty or a compile; and the
        package dirty flag is reported before and after. Node identity is
        DERIVED from the traversal path and labeled identity_kind = "derived",
        because the "id" a build spec wrote is not persisted on the node. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectAnimBlueprintJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Read an Animation Montage back as machine-readable JSON: sections in
        montage order with their NextSectionName chain, slot tracks and the
        animation segments inside them, notifies (read explicitly, because they
        are UPROPERTY() with no Edit flag and a reflection walk would drop
        them), blend in/out options, sync group, and a canonical structure hash.

        READ ONLY, on the same terms as the other inspectors. Montages are
        assets, not graph nodes, so there is no montage half of
        anim_blueprint_build: section and notify authoring needs
        FAnimLinkableElement re-linking and a NextSectionName chain rebuild that
        UE4.27 exposes no atomic operation for, and a half-applied edit would
        leave a montage that plays the wrong thing. This reads; it never
        writes. */

    /** Read an Animation Montage back as machine-readable JSON: sections in
        montage order with their NextSectionName chain, slot tracks and the
        animation segments inside them, notifies (read explicitly, because they
        are UPROPERTY() with no Edit flag and a reflection walk would drop
        them), blend in/out options, sync group, and a canonical structure hash.

        READ ONLY, on the same terms as the other inspectors. Montages are
        assets, not graph nodes, so there is no montage half of
        anim_blueprint_build: section and notify authoring needs
        FAnimLinkableElement re-linking and a NextSectionName chain rebuild that
        UE4.27 exposes no atomic operation for, and a half-applied edit would
        leave a montage that plays the wrong thing. This reads; it never
        writes. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectAnimMontageJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Read a Blend Space, Blend Space 1D or Aim Offset back as
        machine-readable JSON: the target skeleton, all three blend axes with
        their display name, range and grid divisions, and every sample with its
        animation, position and rate scale.

        READ ONLY, on the same terms as the other inspectors, and read-only for
        the same kind of reason the montage reader is: UE4.27 rebuilds a blend
        space's triangulation from its sample set, so a partially applied sample
        edit leaves a space that interpolates wrong rather than one that fails,
        and there is no atomic sample-set replacement to wrap.

        Samples are sorted by position and animation rather than reported in
        array order, because a blend space's array order carries no meaning:
        sorting is what makes two reads of an unchanged asset agree by hash. All
        three axes are reported because UE4.27 exposes no dimension count; the
        class is what distinguishes 1D from 2D. */

    /** Read a Blend Space, Blend Space 1D or Aim Offset back as
        machine-readable JSON: the target skeleton, all three blend axes with
        their display name, range and grid divisions, and every sample with its
        animation, position and rate scale.

        READ ONLY, on the same terms as the other inspectors, and read-only for
        the same kind of reason the montage reader is: UE4.27 rebuilds a blend
        space's triangulation from its sample set, so a partially applied sample
        edit leaves a space that interpolates wrong rather than one that fails,
        and there is no atomic sample-set replacement to wrap.

        Samples are sorted by position and animation rather than reported in
        array order, because a blend space's array order carries no meaning:
        sorting is what makes two reads of an unchanged asset agree by hash. All
        three axes are reported because UE4.27 exposes no dimension count; the
        class is what distinguishes 1D from 2D. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectAnimBlendSpaceJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Create a NEW Animation Blueprint from one JSON spec, handed to the
        existing UAnimBlueprintBuilderLibrary: variables, the anim graph
        pipeline, a state machine with its states and transitions, and an
        optional event graph.

        CREATE-ONLY by design. It refuses when the target asset already exists,
        because the library's rebuild path clears nothing
        (AnimBlueprintBuilder/ABPBuilder.cpp:147) and would append a second
        state machine rather than converge - damage no rollback boundary can
        undo, since restoring the previous contents of a pre-existing asset is
        not something FBridgeAssetRollback can do. Creating a new asset is
        failure-atomic: the only state a failure has to undo is state this
        command created.

        What the command owns: the path limit, the validate-before-create pass,
        the transaction, the rollback boundary, a compile whose result is
        returned rather than swallowed, and a read-back through
        anim_blueprint_inspect that must find every requested state and
        transition before the asset is saved. */

    /** Create a NEW Animation Blueprint from one JSON spec, handed to the
        existing UAnimBlueprintBuilderLibrary: variables, the anim graph
        pipeline, a state machine with its states and transitions, and an
        optional event graph.

        CREATE-ONLY by design. It refuses when the target asset already exists,
        because the library's rebuild path clears nothing
        (AnimBlueprintBuilder/ABPBuilder.cpp:147) and would append a second
        state machine rather than converge - damage no rollback boundary can
        undo, since restoring the previous contents of a pre-existing asset is
        not something FBridgeAssetRollback can do. Creating a new asset is
        failure-atomic: the only state a failure has to undo is state this
        command created.

        What the command owns: the path limit, the validate-before-create pass,
        the transaction, the rollback boundary, a compile whose result is
        returned rather than swallowed, and a read-back through
        anim_blueprint_inspect that must find every requested state and
        transition before the asset is saved. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildAnimBlueprintJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Create or update a Blackboard asset from one desired-state spec: keys
        with their types, per-key instance sync, editor description and
        category, and the parent blackboard.

        puerts_behavior_tree_build already creates a blackboard and adds keys to
        it, and that path is unchanged. This one owns the blackboard as an asset
        in its own right: it can UPDATE a key, REMOVE one, and set the parent,
        none of which add-only key creation can express.

        UE4.27 blackboard keys have NO default values. FBlackboardEntry carries
        EntryName, an instanced UBlackboardKeyType, bInstanceSynced and two
        editor-only strings, and that is all; a key's value exists only on a
        running UBlackboardComponent. A spec that asked for a default would be
        asking for something the asset format cannot hold, so there is no such
        field and this comment is why.

        Convergent: a rerun that finds everything already in place returns
        before the mutation section, so no package is dirtied and nothing is
        saved. Failure-atomic: the transaction is cancelled and the rollback
        boundary removes an asset this command created. Independently verified:
        every key is read back off the asset after the write, and a shortfall
        rolls the whole build back rather than reporting success. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildBlackboardJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Read a Blackboard back as JSON: every key with its type, base class,
        instance-sync flag, description and category, the parent chain and the
        keys inherited through it, the asset's own IsValid verdict, and a
        canonical structure hash.

        The read half of blackboard_build. READ ONLY: not in IsToolMutating, so
        no transaction opens; nothing here calls Modify or MarkPackageDirty; and
        the package dirty flag is reported before and after.

        Unlike a Behavior Tree node, a blackboard key has an AUTHORED identity:
        its name is what every FBlackboardKeySelector binds to, so identity_kind
        is "authored_name" rather than "derived". */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectBlackboardJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Read an Environment Query back as JSON: query name, options in order,
        each option's generator and its tests with their scoring and filtering
        properties, and a canonical structure hash.

        READ ONLY, and there is no matching builder on purpose.
        UEnvironmentQueryGraph::UpdateAsset resets UEnvQuery::Options and
        rebuilds them from the editor graph, which makes Options a compiled
        artifact rather than the source of truth: a command that wrote Options
        without authoring the matching UEdGraph would verify against its own
        write and then be wiped the next time a human opened the asset. The
        response says so in build_unsupported_reason rather than leaving a
        caller to discover it. */

    /** Read an Environment Query back as JSON: query name, options in order,
        each option's generator and its tests with their scoring and filtering
        properties, and a canonical structure hash.

        READ ONLY, and there is no matching builder on purpose.
        UEnvironmentQueryGraph::UpdateAsset resets UEnvQuery::Options and
        rebuilds them from the editor graph, which makes Options a compiled
        artifact rather than the source of truth: a command that wrote Options
        without authoring the matching UEdGraph would verify against its own
        write and then be wiped the next time a human opened the asset. The
        response says so in build_unsupported_reason rather than leaving a
        caller to discover it. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectEnvQueryJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Read the editor world's navigation configuration: the navigation system
        and its build state, nav data actors with their agent and generation
        settings, NavMeshBoundsVolumes and NavModifierVolumes with world-space
        boxes, and the bounds the navigation system actually registered.

        The registered bounds are not the same list as the volumes: a volume in
        an unloaded sublevel is an actor and is not registered, which is the
        usual reason a navmesh is missing where a level looks like it has one.

        READ ONLY: no transaction, no Modify, no MarkPackageDirty. */

    /** Read the editor world's navigation configuration: the navigation system
        and its build state, nav data actors with their agent and generation
        settings, NavMeshBoundsVolumes and NavModifierVolumes with world-space
        boxes, and the bounds the navigation system actually registered.

        The registered bounds are not the same list as the volumes: a volume in
        an unloaded sublevel is an actor and is not registered, which is the
        usual reason a navmesh is missing where a level looks like it has one.

        READ ONLY: no transaction, no Modify, no MarkPackageDirty. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectNavigationJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Answer a batch of navigation queries against the editor world's navmesh:
        project a point onto the navmesh, ask whether one point is reachable
        from another and at what path length and cost, raycast along the
        navmesh, and pick a random navigable point in a radius.

        Batched because a placement decision needs several of these at once and
        one round trip per point is the interface this bridge exists to avoid.
        The whole batch is validated before the first query runs.

        READ ONLY: every one of these is a const query on UNavigationSystemV1.
        Nothing is spawned, nothing is rebuilt, and no navmesh is generated. */

    /** Answer a batch of navigation queries against the editor world's navmesh:
        project a point onto the navmesh, ask whether one point is reachable
        from another and at what path length and cost, raycast along the
        navmesh, and pick a random navigable point in a radius.

        Batched because a placement decision needs several of these at once and
        one round trip per point is the interface this bridge exists to avoid.
        The whole batch is validated before the first query runs.

        READ ONLY: every one of these is a const query on UNavigationSystemV1.
        Nothing is spawned, nothing is rebuilt, and no navmesh is generated. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool QueryNavigationJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Rebuild the editor world's navigation so a placed NavMeshBoundsVolume
        produces an actual navmesh, and report what the world looks like
        afterwards through InspectNavigationJson rather than through the
        build's own claim.

        MUTATING and deliberately NOT transactional. Navigation data is derived
        data, generated by background tasks into a generator that the
        transaction buffer does not record, and the editor's own Build Paths
        agrees: FEditorBuildUtils::EditorBuild calls
        GEditor->ResetTransaction("Rebuilding Navigation")
        (EditorBuildUtils.cpp:395) before triggering it, discarding the undo
        stack outright. Wrapping this in a transaction would produce an undo
        entry that restores nothing. It is absent from IsToolMutating for that
        reason, and it is still a write: it regenerates navmesh tiles, and with
        wait it can spawn a missing RecastNavMesh actor.

        Every precondition is checked and refused by name BEFORE anything runs,
        because UNavigationSystemV1::Build returns silently when it has no work
        (NavigationSystem.cpp:3297-3302) and a command that called it blind
        would report success over a level that still has no navmesh.

        Two shapes, and the difference is honesty about time.
        wait: false, the default, calls the public non-blocking
        ANavigationData::RebuildAll on every registered nav data and answers
        status "building"; poll puerts_nav_inspect until remaining_build_tasks
        is zero. wait: true calls FNavigationSystem::Build, which is what the
        editor does (EditorBuildUtils.cpp:868) and which BLOCKS until every nav
        data reports EnsureBuildCompletion (NavigationSystem.cpp:3329-3335), so
        on a large level it can outlast the named pipe deadline while the build
        keeps running in the editor. Only the blocking path spawns a missing
        RecastNavMesh, so wait: false refuses a level that has bounds volumes
        and no nav data actor rather than pretending to build one.

        plan_only runs every precondition and no generator. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildNavigationJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError);

    /** Read a Sound Cue or a Sound Wave back as JSON: the common USoundBase
        fields, every EditAnywhere property by reflected name, and for a cue the
        whole node graph walked from FirstNode with each node's class, title,
        ordered children and properties, plus a canonical structure hash.

        Node properties are read by reflection rather than from a field list,
        because roughly thirty USoundNode subclasses share no useful base and a
        list would need extending for every node class a project uses. A
        property that cannot be converted is named in unsupported_fields rather
        than dropped.

        The node array is canonically sorted with the shared
        MCPBridgeBlueprintMembers sort, so the hash is stable across reads.
        Each node's CHILDREN are deliberately not sorted: for a Mixer, a Random
        or a Concatenator the child index is the meaning.

        READ ONLY: not in IsToolMutating, nothing calls Modify or
        MarkPackageDirty, and the response reports the package dirty flag before
        and after so a caller can see that. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectAudioAssetJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Read a skeletal mesh's cloth setup: render-section cloth bindings,
        physical cloth LOD counts, authoring masks, NvCloth config, topology
        hash and Physics Asset coverage.

        A straight pass-through to UClothOptimizerLibrary::InspectClothAsset,
        which the module's own editor panel also reads, so an MCP read and what
        a human sees in the panel are the same function's output.

        THE READ HALF ONLY. The module's three writers open a transaction and do
        not cancel it on the failure path, and what they mutate is cloth paint:
        authored data a re-run cannot regenerate. They are not fronted here and
        the response says why in write_unsupported_reason.

        READ ONLY: not in IsToolMutating, nothing calls Modify or
        MarkPackageDirty. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectClothJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Reconcile the whole AIPerceptionComponent configuration on an existing
        AIController Blueprint in one call: the senses it has, each sense's
        properties, and the dominant sense. The component is created if it is
        missing.

        Desired-state rather than a set of setters, because a perception config
        is only meaningful as a whole: sight radius without lose-sight radius,
        or a dominant sense that is not configured, are the states this refuses
        rather than writes.

        UAIPerceptionComponent declares SensesConfig and DominantSense
        protected, so the write goes through reflection on the UPROPERTY; the
        read half uses the public GetSensesConfigIterator. A listed sense is
        replaced wholesale, so the config that lands is the spec plus class
        defaults and never the residue of an earlier spec.

        Transactional, compiled, verified by reading the component template
        again, and saved only after that passes. A failure cancels the
        transaction and saves nothing.

        Convergent: a rerun compares every property the spec names against the
        config that is already there and returns before the mutation section
        when nothing differs, so a satisfied rerun costs no Blueprint compile
        and no save. The dominant sense counts as a difference in its own
        right. */

    /** Reconcile the whole AIPerceptionComponent configuration on an existing
        AIController Blueprint in one call: the senses it has, each sense's
        properties, and the dominant sense. The component is created if it is
        missing.

        Desired-state rather than a set of setters, because a perception config
        is only meaningful as a whole: sight radius without lose-sight radius,
        or a dominant sense that is not configured, are the states this refuses
        rather than writes.

        UAIPerceptionComponent declares SensesConfig and DominantSense
        protected, so the write goes through reflection on the UPROPERTY; the
        read half uses the public GetSensesConfigIterator. A listed sense is
        replaced wholesale, so the config that lands is the spec plus class
        defaults and never the residue of an earlier spec.

        Transactional, compiled, verified by reading the component template
        again, and saved only after that passes. A failure cancels the
        transaction and saves nothing.

        Convergent: a rerun compares every property the spec names against the
        config that is already there and returns before the mutation section
        when nothing differs, so a satisfied rerun costs no Blueprint compile
        and no save. The dominant sense counts as a difference in its own
        right. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildAIPerceptionJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Read an AIController Blueprint back as JSON: parent class, every
        AIPerceptionComponent it declares with each sense's configuration and
        the dominant sense, and every RunBehaviorTree call site in its graphs
        with the Behavior Tree and Blackboard each one names.

        The controller-to-BT wiring is reported as call sites because that is
        what it is: UE4.27 has no data-driven field for it, a controller starts
        a tree by calling AAIController::RunBehaviorTree, and a tree chosen
        through a variable at runtime resolves to nothing and is listed under
        dynamic_behavior_tree_call_sites instead of being guessed at.

        READ ONLY: not in IsToolMutating, nothing calls Modify or
        MarkPackageDirty, and the package dirty flag is reported both sides. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectAIControllerJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;


    /** Read a UMaterial or a UMaterialInstanceConstant back as machine-readable
        JSON: for a master material the expression nodes with their class paths,
        editor positions, per-input connection state, the links between them and
        the links into the material's own outputs; for either kind the full
        parameter list with types, effective values and (for an instance) which
        of them the instance overrides; plus a canonical structure hash.

        The read half material authoring never had. asset_kind says whether a
        material or an instance answered, and the response uses the same field
        names for both so a caller does not have to branch on it.

        READ ONLY: not in IsToolMutating, so no transaction opens and the
        response carries no transaction id; nothing here calls Modify,
        MarkPackageDirty or a compile; and the package dirty flag is reported
        before and after. Expression identity is OBSERVED, not derived: a
        material expression's UObject name is unique within its package and is
        serialized, unlike a UMG widget or a Behavior Tree node. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectMaterialJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Author a UMaterial graph from one desired-state spec: expression nodes,
        named scalar / vector / texture / static switch parameters, the links
        between them, and the links into BaseColor, EmissiveColor, Roughness,
        Normal and the rest.

        The spec is the WHOLE graph, so a rerun converges and a build aimed at
        an existing material replaces the graph it had.

        Failure atomicity, which an earlier note in this header said was
        impossible: UMaterialEditingLibrary's graph mutators do not call
        Modify(), but the engine's own material editor does not rely on them to
        (FMaterialEditor::CreateNewMaterialExpression, MaterialEditor.cpp:4344,
        opens a transaction and calls Material->Modify() itself). This command
        does the same and CHECKS the return value, refusing before writing
        anything if it is false. One Modify() is enough because full replacement
        means every expression this command connects is one it created inside
        the same transaction, so the UMaterial is the only pre-existing object
        it mutates. Whether a rollback worked is decided by re-reading: the
        Asset Registry on the create path, the structure hash on the update
        path.

        The compile result is reported rather than assumed, and the save runs
        only after an independent read-back through the material_inspect reader
        agrees that every requested link, output and parameter landed. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildMaterialJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Generate a UTexture2D asset so a texture parameter can be set.

        material_instance_build accepts a textures map and material_build
        accepts a texture parameter default, and nothing in the catalog could
        produce a texture for either. A solid colour or a checker is enough,
        needs no asset on disk, and is reproducible from the spec, which is what
        makes it convergent: identical bytes compare equal and the second run
        writes nothing.

        Generation only. Importing an arbitrary file needs an allowed import
        root this bridge does not define, so a spec naming source_file is
        refused by name rather than reaching past the question. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool ImportTextureJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Create or update a UMaterialInstanceConstant and set its scalar, vector,
        texture and static switch parameters from one desired-state spec.

        A re-front of UMaterialEditingLibrary plus the boundary that library
        does not have. Every parameter is resolved against the parent and
        validated before the asset is created or touched, so an unknown name is
        refused with the closest matching names rather than silently dropped. A
        parameter already at the requested value and already overridden is
        reported unchanged and not rewritten, so a rerun dirties nothing.

        Modify() is called before any write because the library's setters do
        not, which is what makes the failure path atomic: on any failure the
        transaction is cancelled, the rollback boundary runs, and whether the
        parameters actually came back is decided by reading them again rather
        than by trusting the undo. The compile is run and its result reported
        in the response; the save happens only after an independent read-back
        agrees with every requested value.

        The companion command for master material graphs is BuildMaterialJson,
        above. This one stays the parameter-override half: an instance is a set
        of named overrides on a parent, which is a different operation from
        authoring the parent's graph. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildMaterialInstanceJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

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

    /** Start a Lightmass lighting build for the editor's current level, or
        report the state of one, so a level authored through scene_batch can be
        lit rather than left showing the "Lighting needs to be rebuilt" banner.

        IT DOES NOT WAIT, and the response says so in `waited` and `completion`
        rather than implying otherwise. UEditorEngine::BuildLighting hands the
        level to FStaticLightingManager and returns; the build then advances on
        UpdateBuildLighting from the editor's own tick, over minutes at
        Production quality. A command that blocked on that would sit far past
        any request timeout this bridge allows, and a timeout reported while the
        editor kept building is wrong in both directions.

        What the start DOES pay for synchronously is the scene gather and the
        Lightmass export. On a large level that alone is slow, which is why the
        tool carries a long budget for a command that does not wait.

        Whether a build actually began is read back from
        IsLightingBuildCurrentlyRunning rather than assumed:
        CreateStaticLightingSystem returns void and swallows a refused start.
        Poll with action="status", and read lighting_unbuilt_objects - the
        counter behind the editor's own banner - to decide whether the level
        still needs a rebuild. Refused during PIE, because Lightmass reads GWorld
        and that is the play world while play is running. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildLightingJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Set INHERITED class-default (CDO) values on a generated Blueprint:
        AIControllerClass and AutoPossessAI on a pawn, and anything else on the
        writable-property allowlist. blueprint_build writes component template
        properties and member variable defaults and has no section for the
        actor's own class defaults, so an authored pawn could never be possessed
        by an authored controller.

        A variable the Blueprint itself declares is REFUSED here and pointed at
        blueprint_member_patch: its default lives on FBPVariableDescription as
        well as on the CDO, and writing only one of the two leaves them
        disagreeing until the next compile picks a winner.

        The transaction is deliberately not the rollback. Finding 0r, measured:
        UObject::Modify delegates to SaveToTransactionBuffer, which silently does
        nothing for an object that is not RF_Transactional, and a class default
        object is not - so a cancelled transaction leaves a CDO write standing.
        This command snapshots the exported text of every property it will touch,
        restores it on any failure, and decides whether the restore worked by
        reading the values again. Modify()'s return value is reported as
        transaction_covers_cdo rather than discarded.

        Convergent: a value already equal to the request is reported unchanged
        and not rewritten, so a rerun dirties nothing and saves nothing. The
        comparison is between two exported strings produced by the same
        import-and-export path, not by a hand-written JSON comparator, and an
        invalid value is rejected on a scratch copy before the CDO is touched. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool PatchClassDefaultsJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Read the project's input action and axis mappings back as JSON.
        THE MISSING INSPECTOR. UMCPBridgeInputLibrary could add and remove
        mappings and had no reader at all, so a mutation could only ever be
        confirmed by the mutator's own report. Nothing else in this module
        would accept that from a Blueprint builder, and input settings are not
        a lesser kind of state for being an ini rather than a uasset.

        READ ONLY: absent from IsToolMutating, so no transaction opens; it
        touches only const accessors on UInputSettings. Both arrays are
        canonically ordered and reduced to mapping_hash_sha1, which is what
        input_mapping_patch reports as pre_mapping_hash / post_mapping_hash, so
        a patch can be verified against an independent read. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectInputMappingsJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Reconcile the project's input mappings against a desired set.

        A re-front of UMCPBridgeInputLibrary, which exists because UE4.27
        Python cannot construct an FKey. The library's per-mapping validation
        and exact-duplicate rejection are reused; what this command adds is the
        boundary the library never had. The whole batch is classified against
        the mappings that exist before the first write, so a mapping already
        present is reported unchanged rather than reapplied, and a rerun writes
        nothing.

        Input mappings live in DefaultInput.ini, not in a UObject, so the asset
        transaction boundary does not apply and this command is deliberately
        absent from IsToolMutating. The boundary it owns instead is a snapshot:
        the full action and axis arrays are copied before the first mutation,
        restored exactly on any failure, and whether the restore worked is
        decided by reading the mappings again and comparing the hash, not by
        trusting the restore. */

    /** Reconcile the project's input mappings against a desired set.

        A re-front of UMCPBridgeInputLibrary, which exists because UE4.27
        Python cannot construct an FKey. The library's per-mapping validation
        and exact-duplicate rejection are reused; what this command adds is the
        boundary the library never had. The whole batch is classified against
        the mappings that exist before the first write, so a mapping already
        present is reported unchanged rather than reapplied, and a rerun writes
        nothing.

        Input mappings live in DefaultInput.ini, not in a UObject, so the asset
        transaction boundary does not apply and this command is deliberately
        absent from IsToolMutating. The boundary it owns instead is a snapshot:
        the full action and axis arrays are copied before the first mutation,
        restored exactly on any failure, and whether the restore worked is
        decided by reading the mappings again and comparing the hash, not by
        trusting the restore. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool PatchInputMappingsJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Hide or show Content Browser folders, or read the hidden set.

        A re-front of UFolderVisibilityLibrary. Display-only editor state
        persisted in Config/FolderVisibility.ini: hidden folders stay on disk,
        referenced and cookable. Not a UObject, so no transaction, and absent
        from IsToolMutating for the same reason viewport commands are.

        Desired-state first: "hidden" is the whole set and the command
        converges onto it. "hide" and "show" are the delta form the legacy
        pair used, and the two shapes are mutually exclusive rather than
        merged, because a request that says both a full set and a delta has no
        single correct reading. The hidden set is always read back from
        GetHiddenFolders after the work, never echoed from the request. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool SetFolderVisibilityJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);

    /** Play a camera shake on the running PIE session's player camera.

        A re-front of UAutoPIEHelper::PlayCameraShakeOnPlayer, which owns the
        PIE World -> PlayerController(0) -> PlayerCameraManager chain. Runtime
        effect only: nothing is persisted, no asset is touched, and there is
        nothing to roll back. Refuses with a named reason when PIE is not
        running, rather than reporting a shake nobody could have seen.

        UE4.27 note: this build uses UCameraShakeBase with StartCameraShake(),
        which is what AutoPIEHelper already calls. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool PlayCameraShakeJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** The read-only half of the PIE agent: observe, status, expect.

        A re-front of UPIEAgentLibrary. These three change no asset and no
        world state; they read the running session, poll an operation, or start
        an in-engine condition check that only reads. The write half (move_to,
        look_at, press, record, replay) is deliberately NOT here: AGENTS.md
        requires the user to ask before any pie_agent_* tool runs, so the read
        half is both the safe half and the useful one.

        The library answers with its own {success, data, error} JSON; this
        command forwards that verbatim rather than reshaping it, so the native
        lane and the legacy listener cannot disagree about what the agent
        said. */

    /** The read-only half of the PIE agent: observe, status, expect.

        A re-front of UPIEAgentLibrary. These three change no asset and no
        world state; they read the running session, poll an operation, or start
        an in-engine condition check that only reads. The write half (move_to,
        look_at, press, record, replay) is deliberately NOT here: AGENTS.md
        requires the user to ask before any pie_agent_* tool runs, so the read
        half is both the safe half and the useful one.

        The library answers with its own {success, data, error} JSON; this
        command forwards that verbatim rather than reshaping it, so the native
        lane and the legacy listener cannot disagree about what the agent
        said. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool QueryPIEAgentJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;


    /** Read a ULevelSequence back as machine-readable JSON: display rate, tick
        resolution, playback range, every possessable and spawnable, every
        master and object track with its sections, and every key on every
        channel with its time, value and interpolation.

        The read half of BuildLevelSequenceJson, and READ ONLY on the same terms
        as graph_inspect: absent from IsToolMutating, so no transaction opens and
        the response carries no transaction id; nothing here calls Modify or
        MarkPackageDirty; and the package's dirty flag is reported before and
        after the read.

        Binding identity is OBSERVED (identity_kind = "observed"): the id is the
        FGuid UMovieScene already stores, which survives a rename and a restart.

        Frames are reported in DISPLAY-RATE frames, with the raw tick values
        beside them, so a caller never has to know that UE4.27 stores key times
        at 60000 ticks per second to compare a read against a spec.

        Whether a possessable resolves to a live actor is reported under
        "resolution", which is deliberately OUTSIDE the hashed set: resolution
        depends on which level the editor has open, and a hash that moved when
        someone opened a different map would report a change nobody made. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool InspectLevelSequenceJson(
        const FString& RequestJson,
        FString& OutResultJson,
        FString& OutError) const;

    /** Create or update a ULevelSequence from one desired-state spec: display
        rate, playback range, actor bindings, tracks, sections and keyframes, in
        ONE transaction.

        Convergent and ADDITIVE. Every binding, track, section and key is an
        upsert keyed on its own identity, and each one's satisfied-ness is
        re-evaluated immediately before it is written rather than read from a
        plan, because the spec is ordered and an earlier entry can move the
        state a later one depends on. Nothing is ever removed: pruning a
        sequence a human authored in Sequencer is a different and more dangerous
        operation, and it is a stated gap rather than a half-built feature.

        Failure-atomic on the same terms as scene_batch: any refusal cancels the
        transaction, runs FBridgeAssetRollback over the package, and then decides
        whether the sequence actually came back by hashing it again rather than
        trusting the undo.

        The whole spec is validated before an asset exists - actor labels
        resolved, classes loaded, track types and properties checked, duplicate
        identities refused - so a rejected request creates nothing at all. */

    /** Create or update a ULevelSequence from one desired-state spec: display
        rate, playback range, actor bindings, tracks, sections and keyframes, in
        ONE transaction.

        Convergent and ADDITIVE. Every binding, track, section and key is an
        upsert keyed on its own identity, and each one's satisfied-ness is
        re-evaluated immediately before it is written rather than read from a
        plan, because the spec is ordered and an earlier entry can move the
        state a later one depends on. Nothing is ever removed: pruning a
        sequence a human authored in Sequencer is a different and more dangerous
        operation, and it is a stated gap rather than a half-built feature.

        Failure-atomic on the same terms as scene_batch: any refusal cancels the
        transaction, runs FBridgeAssetRollback over the package, and then decides
        whether the sequence actually came back by hashing it again rather than
        trusting the undo.

        The whole spec is validated before an asset exists - actor labels
        resolved, classes loaded, track types and properties checked, duplicate
        identities refused - so a rejected request creates nothing at all. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildLevelSequenceJson(
        const FString& SpecJson,
        FString& OutResultJson,
        FString& OutError);
};
