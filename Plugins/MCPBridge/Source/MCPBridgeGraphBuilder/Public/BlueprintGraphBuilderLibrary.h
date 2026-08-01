// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/UserDefinedEnum.h"
#include "BlueprintGraphBuilderLibrary.generated.h"

UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UBlueprintGraphBuilderLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Builds a Blueprint event graph from a JSON description.
     *
     * JSON format:
     * {
     *   "nodes": [{"id": "start", "type": "BeginPlay"},
     *             {"id": "print", "type": "PrintString", "params": {"InString": "hi"}, "x": 300, "y": 0}],
     *   "connections": [{"from": "start.exec", "to": "print.exec"}]
     * }
     *
     * Supported node types are exactly the list GetSupportedNodeTypes returns;
     * anything else is skipped with a warning. Optional per-node fields:
     * "params" (pin name -> default value, applied to CallFunction, PrintString,
     * Sequence.num_outputs and Comment.text/width/height), "x" and "y".
     *
     * Connection endpoints are "nodeId.pinRole". The role "exec" is
     * direction-aware (Then on the source, Execute on the target), "then" is
     * always Then, and anything else is a literal pin name, so data pins wire
     * through the same array. Literal pin names worth knowing:
     *   Branch          Condition, then, else
     *   Sequence        then_0, then_1, ... (K2Node_ExecutionSequence.cpp:263)
     *   CallFunction    self for the target, ReturnValue for the result,
     *                   otherwise the UFUNCTION parameter name
     *   Operator        A, B (or X/Y/Z, Value/Min/Max), ReturnValue
     *   VariableGet     the variable's own name
     *   VariableSet     the variable's own name for the value pin
     *   Delay           Duration
     *   Cast            Object, then, CastFailed, "As<ClassName>"
     *   Knot            InputPin, OutputPin
     *   MakeStruct      the struct's name; BreakStruct takes the same name in
     *   BreakStruct     and gives one pin per field
     *   MultiGate       Out 0, Out 1, ...
     *   SwitchInt       Default plus one pin per case
     *   ActorEndOverlap OtherActor
     *   InputKey        Pressed, Released, Key; params.fkey_name is the FKey,
     *                   which is a literal key rather than a project input
     *                   mapping, so the node needs nothing in DefaultInput.ini
     *
     * Two node types have pin names built from localized text rather than from
     * a stable FName, so they are spawnable but were not wired by name in the
     * acceptance graph: DoOnceMultiInput (K2Node_DoOnceMultiInput.cpp:185 names
     * its pins through GetNameForPin) and the Spawn Transform pin of
     * SpawnActor, which is a by-ref parameter that refuses a literal default.
     *
     * Node params are pin defaults by pin name, plus the routing keys each
     * type needs (CallFunction: class/function; Operator: op; VariableGet and
     * VariableSet: var_name, and VariableSet also takes value as an alias for
     * the value pin).
     *
     * This is the low-level executor. It returns nothing and reports problems
     * only to the log; callers that need a structured result should go through
     * the blueprint_build native command, which validates the spec before any
     * asset is touched and reports compiler messages.
     */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static void BuildBlueprintFromJSON(
        UBlueprint* Blueprint,
        const FString& JsonString,
        bool bClearExistingGraph = true
    );

    /**
     * The same builder, with the connection outcome reported to the caller.
     *
     * A connection whose endpoints cannot be resolved to real pins is dropped.
     * That used to be a log line and nothing else, so a graph with a hole in it
     * compiled clean and reported complete success: the worst failure shape
     * there is, because the caller cannot tell it from a working graph. This
     * overload counts the links actually made and hands back one entry per
     * dropped connection, so the native command can fail the build instead of
     * saving it.
     *
     * @param OutUnresolvedConnections  One "from -> to (reason)" per dropped
     *                                  connection. Empty means the graph is
     *                                  whole.
     * @param OutConnectionsMade        Links actually created by MakeLinkTo.
     *
     * A connection is dropped for one of three reasons, each named in the
     * entry: an endpoint that does not read nodeId.pinRole, a node id that
     * spawned no node (a factory refusal earlier in the build), or a pin role
     * that names no pin of that direction on the node. The third is the common
     * one, and limitation 21 is its usual cause: a const BlueprintCallable
     * UFUNCTION is a pure node with no exec pins.
     */
    static void BuildBlueprintFromJSONWithReport(
        UBlueprint* Blueprint,
        const FString& JsonString,
        bool bClearExistingGraph,
        TArray<FString>& OutUnresolvedConnections,
        int32& OutConnectionsMade
    );

    /** The node types BuildBlueprintFromJSON can spawn today, in dispatch
     *  order. This is the gate the node loop checks, not a parallel list, so a
     *  dispatch case that is not named here stops working the moment it is
     *  added: that failure is loud, and a silently stale list is not.
     *
     *  The list is two groups. The first eleven have a dispatch case in this
     *  file. The rest are served by FBPNodeRegistry, the mutator's factory
     *  table, and a name is only listed once a factory for it is registered:
     *  the loop asks the registry and reports a miss as drift, exactly as it
     *  does for a missing local case. */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static TArray<FString> GetSupportedNodeTypes();

    /** The symbolic operator names the "Operator" node type accepts, each one
     *  a verified UKismetMathLibrary or UKismetStringLibrary function. Named
     *  operators exist so the vocabulary is gated and documented; the same
     *  functions stay reachable through a raw CallFunction node. */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static TArray<FString> GetSupportedOperators();

    /** The variable type keywords ConfigureVariablesFromJSON accepts.
     *  "object:<class>", "class:<class>", "struct:<path>" and "enum:<path>"
     *  take a suffix and are listed with the colon. */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static TArray<FString> GetSupportedVariableTypes();

    /**
     * Create or converge Blueprint member variables from a JSON array.
     *
     * [{"name": "bIsOpen", "type": "bool", "default": false,
     *   "category": "Door", "container": "none"}]
     *
     * Idempotent by convergence, like components: a variable that already
     * exists with the same type is left in place and only its default is
     * reapplied. A variable that exists with a DIFFERENT type is an error,
     * never a silent retype, because retyping drops every graph node that
     * reads it.
     *
     * With bValidateOnly the Blueprint is not touched and every check that can
     * run without mutating runs, so the caller can reject a spec before an
     * asset exists. Blueprint may be null in that mode; the checks that need
     * it (name collision, type conflict) are then skipped.
     *
     * Returns {"success": bool, "errors": [..], "variables": [{"name",
     * "type", "created", "default_applied"}]}.
     */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static FString ConfigureVariablesFromJSON(
        UBlueprint* Blueprint,
        const FString& VariablesJson,
        bool bValidateOnly = false
    );

    /** Add a component to a Blueprint's SimpleConstructionScript.
     *  This is the missing piece that lets Python build proper Blueprint actors
     *  with components (BoxComponent, CameraComponent, etc.) instead of
     *  falling back to spawning raw actors in the world.
     *
     *  @param Blueprint      Target Blueprint to add the component to
     *  @param ComponentClass The component class (e.g. UBoxComponent, UCameraComponent)
     *  @param ComponentName  Name for the new component
     *  @param AttachToName   Name of parent component to attach to (empty = root)
     *  @return True if the component was added successfully
     */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static bool AddComponentToBlueprint(
        UBlueprint* Blueprint,
        TSubclassOf<UActorComponent> ComponentClass,
        const FString& ComponentName,
        const FString& AttachToName = TEXT("")
    );

    /** Set a property on a Blueprint component template by name.
     *  Works on components added via AddComponentToBlueprint.
     *
     *  @param Blueprint      Target Blueprint
     *  @param ComponentName  Name of the component to modify
     *  @param PropertyName   Property to set (e.g. "BoxExtent", "CollisionProfileName")
     *  @param JsonValue      Value as JSON string (e.g. "{\"X\":200,\"Y\":200,\"Z\":200}")
     *  @return True if the property was set successfully
     */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static bool SetComponentProperty(
        UBlueprint* Blueprint,
        const FString& ComponentName,
        const FString& PropertyName,
        const FString& JsonValue
    );

    /** Compile a Blueprint and return a JSON report with {success, status, errors[], warnings[]}.
     *  status values: "UpToDate" | "UpToDateWithWarnings" | "Error" | "Dirty" | "Unknown" */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static FString CompileAndReport(UBlueprint* Blueprint);

    /** Replace a UserDefinedEnum's entries with the supplied display names. */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static bool ConfigureUserDefinedEnum(
        UUserDefinedEnum* Enum,
        const TArray<FString>& DisplayNames
    );
};
