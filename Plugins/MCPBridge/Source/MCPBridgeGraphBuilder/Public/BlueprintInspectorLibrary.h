#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintInspectorLibrary.generated.h"

class UBlueprint;

UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UBlueprintInspectorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** List all graphs on the blueprint (EventGraph, function graphs, macro graphs). */
    UFUNCTION(BlueprintCallable, Category="BlueprintInspector")
    static FString ListGraphs(UBlueprint* Blueprint);

    /** List all user functions on the blueprint. */
    UFUNCTION(BlueprintCallable, Category="BlueprintInspector")
    static FString ListFunctions(UBlueprint* Blueprint);

    /** List all user variables on the blueprint. */
    UFUNCTION(BlueprintCallable, Category="BlueprintInspector")
    static FString ListVariables(UBlueprint* Blueprint);

    /** List all user macros on the blueprint. */
    UFUNCTION(BlueprintCallable, Category="BlueprintInspector")
    static FString ListMacros(UBlueprint* Blueprint);

    /** List interfaces implemented by the blueprint. */
    UFUNCTION(BlueprintCallable, Category="BlueprintInspector")
    static FString ListInterfaces(UBlueprint* Blueprint);

    /** List event dispatchers (multicast delegates) declared on the blueprint. */
    UFUNCTION(BlueprintCallable, Category="BlueprintInspector")
    static FString ListEventDispatchers(UBlueprint* Blueprint);

    /** List simple-construction-script nodes (components) with hierarchy. */
    UFUNCTION(BlueprintCallable, Category="BlueprintInspector")
    static FString ListSCSNodes(UBlueprint* Blueprint);

    /** List all nodes in a named graph with pins and link info. */
    UFUNCTION(BlueprintCallable, Category="BlueprintInspector")
    static FString ListNodesInGraph(UBlueprint* Blueprint, const FString& GraphName);

    /** Fetch a single node by GUID in a graph with full detail. */
    UFUNCTION(BlueprintCallable, Category="BlueprintInspector")
    static FString GetNodeDetail(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid);

    /** Search for nodes matching filters. Query JSON: {node_class?, calls_function?, references_variable?, references_asset?, graph_name?} */
    UFUNCTION(BlueprintCallable, Category="BlueprintInspector")
    static FString FindNodes(UBlueprint* Blueprint, const FString& QueryJson);
};
