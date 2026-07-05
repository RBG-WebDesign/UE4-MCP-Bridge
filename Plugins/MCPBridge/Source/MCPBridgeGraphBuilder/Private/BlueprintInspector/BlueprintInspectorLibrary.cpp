// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BlueprintInspectorLibrary.h"
#include "BPMemberReader.h"
#include "BPGraphReader.h"

FString UBlueprintInspectorLibrary::ListGraphs(UBlueprint* Blueprint)
{
    return FBPMemberReader::ListGraphs(Blueprint);
}

FString UBlueprintInspectorLibrary::ListFunctions(UBlueprint* Blueprint)
{
    return FBPMemberReader::ListFunctions(Blueprint);
}

FString UBlueprintInspectorLibrary::ListVariables(UBlueprint* Blueprint)
{
    return FBPMemberReader::ListVariables(Blueprint);
}
FString UBlueprintInspectorLibrary::ListMacros(UBlueprint* Blueprint)
{
    return FBPMemberReader::ListMacros(Blueprint);
}
FString UBlueprintInspectorLibrary::ListInterfaces(UBlueprint* Blueprint)
{
    return FBPMemberReader::ListInterfaces(Blueprint);
}
FString UBlueprintInspectorLibrary::ListEventDispatchers(UBlueprint* Blueprint)
{
    return FBPMemberReader::ListEventDispatchers(Blueprint);
}
FString UBlueprintInspectorLibrary::ListSCSNodes(UBlueprint* Blueprint)
{
    return FBPMemberReader::ListSCSNodes(Blueprint);
}
FString UBlueprintInspectorLibrary::ListNodesInGraph(UBlueprint* Blueprint, const FString& GraphName)
{
    return FBPGraphReader::ListNodesInGraph(Blueprint, GraphName);
}
FString UBlueprintInspectorLibrary::GetNodeDetail(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid)
{
    return FBPGraphReader::GetNodeDetail(Blueprint, GraphName, NodeGuid);
}
FString UBlueprintInspectorLibrary::FindNodes(UBlueprint* Blueprint, const FString& QueryJson)
{
    return FBPGraphReader::FindNodes(Blueprint, QueryJson);
}
