// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
class UBlueprint;

class FBPMemberReader
{
public:
    /** List all graphs (EventGraph, function graphs, macro graphs) on the blueprint as JSON. */
    static FString ListGraphs(UBlueprint* Blueprint);

    /** List all user functions on the blueprint with their inputs/outputs as JSON array. */
    static FString ListFunctions(UBlueprint* Blueprint);

    /** List user variables on the blueprint with type, default value, editability, replication, and tooltip. */
    static FString ListVariables(UBlueprint* Blueprint);

    /** List user macros on the blueprint with their inputs/outputs as JSON array. */
    static FString ListMacros(UBlueprint* Blueprint);

    /** List interfaces implemented by the blueprint as a JSON array of name+path entries. */
    static FString ListInterfaces(UBlueprint* Blueprint);

    /** List event dispatchers (multicast delegates) declared on the blueprint with their parameters. */
    static FString ListEventDispatchers(UBlueprint* Blueprint);

    /** List Simple Construction Script nodes (components) on the blueprint with parent hierarchy. */
    static FString ListSCSNodes(UBlueprint* Blueprint);
};
