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
     * through the same array.
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

    /** The node types BuildBlueprintFromJSON can spawn today, in dispatch
     *  order. This is the gate the node loop checks, not a parallel list, so a
     *  dispatch case that is not named here stops working the moment it is
     *  added: that failure is loud, and a silently stale list is not. */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static TArray<FString> GetSupportedNodeTypes();

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
