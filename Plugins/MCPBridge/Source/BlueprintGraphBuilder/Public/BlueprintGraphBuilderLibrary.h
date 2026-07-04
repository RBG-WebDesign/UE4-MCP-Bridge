#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintGraphBuilderLibrary.generated.h"

UCLASS()
class BLUEPRINTGRAPHBUILDER_API UBlueprintGraphBuilderLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Builds a Blueprint event graph from a JSON description.
     *
     * JSON format:
     * {
     *   "nodes": [{"id": "start", "type": "BeginPlay"}, {"id": "print", "type": "PrintString"}],
     *   "connections": [{"from": "start.exec", "to": "print.exec"}]
     * }
     *
     * Supported types (Pass 1): BeginPlay, PrintString
     * Connection pin roles: exec (output Then on source, input Execute on target)
     */
    UFUNCTION(BlueprintCallable, Category="BlueprintGraphBuilder")
    static void BuildBlueprintFromJSON(
        UBlueprint* Blueprint,
        const FString& JsonString,
        bool bClearExistingGraph = true
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
};
