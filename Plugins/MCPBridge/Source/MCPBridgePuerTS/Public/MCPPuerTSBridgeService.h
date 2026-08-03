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
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildAnimBlueprintJson(
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
    bool BuildBlackboardJson(
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

        There is deliberately no companion command for master material graphs.
        UE4.27's graph mutators write outside the undo record, so a failed
        multi-node build cannot be rolled back; material_inspect is the read
        half and there is no write half. */
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool BuildMaterialInstanceJson(
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
    UFUNCTION(BlueprintCallable, Category="MCP PuerTS Bridge")
    bool QueryNavigationJson(
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
