#pragma once
#include "CoreMinimal.h"
class UBlueprint;
class UEdGraph;
class UEdGraphNode;

class FBPGraphReader
{
public:
    static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName);
    /** Find a node by its GUID string within a graph. Returns nullptr on not-found or invalid GUID. */
    static UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& NodeGuidStr);
    /** List all nodes in a named graph with pins and link info, serialized to JSON. */
    static FString ListNodesInGraph(UBlueprint* Blueprint, const FString& GraphName);
    /** Fetch a single node by GUID in a named graph with full detail (serialized node JSON). */
    static FString GetNodeDetail(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuidStr);
    /** Search for nodes matching optional filters (AND-combined). Returns bare JSON array [...]. */
    static FString FindNodes(UBlueprint* Blueprint, const FString& QueryJson);
};
