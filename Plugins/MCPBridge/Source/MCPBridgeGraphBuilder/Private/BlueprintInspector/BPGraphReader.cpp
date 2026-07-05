// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPGraphReader.h"
#include "BPNodeSerializer.h"
#include "BPGLogCategories.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"

UEdGraph* FBPGraphReader::FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
{
    if (!Blueprint) return nullptr;

    auto Find = [&](const TArray<UEdGraph*>& Graphs) -> UEdGraph* {
        for (UEdGraph* G : Graphs)
        {
            if (G && G->GetName() == GraphName) return G;
        }
        return nullptr;
    };

    if (UEdGraph* G = Find(Blueprint->UbergraphPages))          return G;
    if (UEdGraph* G = Find(Blueprint->FunctionGraphs))          return G;
    if (UEdGraph* G = Find(Blueprint->MacroGraphs))             return G;
    if (UEdGraph* G = Find(Blueprint->DelegateSignatureGraphs)) return G;
    return nullptr;
}

UEdGraphNode* FBPGraphReader::FindNodeByGuid(UEdGraph* Graph, const FString& NodeGuidStr)
{
    if (!Graph) return nullptr;
    FGuid Target;
    if (!FGuid::Parse(NodeGuidStr, Target)) return nullptr;
    for (UEdGraphNode* N : Graph->Nodes)
    {
        if (N && N->NodeGuid == Target) return N;
    }
    return nullptr;
}

FString FBPGraphReader::ListNodesInGraph(UBlueprint* Blueprint, const FString& GraphName)
{
    UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
    if (!Graph)
    {
        UE_LOG(LogBlueprintInspector, Warning, TEXT("ListNodesInGraph: graph '%s' not found"), *GraphName);
        return FString::Printf(TEXT("{\"error\":\"graph '%s' not found\"}"), *GraphName);
    }

    UE_LOG(LogBlueprintInspector, Log, TEXT("ListNodesInGraph: %s::%s (%d nodes)"),
           *Blueprint->GetName(), *GraphName, Graph->Nodes.Num());

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("graph_name"), GraphName);

    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        Nodes.Add(MakeShared<FJsonValueObject>(FBPNodeSerializer::Serialize(Node)));
    }
    Root->SetArrayField(TEXT("nodes"), Nodes);

    FString S;
    TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
    FJsonSerializer::Serialize(Root.ToSharedRef(), W);
    return S;
}

FString FBPGraphReader::GetNodeDetail(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuidStr)
{
    UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
    if (!Graph)
    {
        UE_LOG(LogBlueprintInspector, Warning, TEXT("GetNodeDetail: graph '%s' not found"), *GraphName);
        return FString::Printf(TEXT("{\"error\":\"graph '%s' not found\"}"), *GraphName);
    }

    FGuid Target;
    if (!FGuid::Parse(NodeGuidStr, Target))
    {
        UE_LOG(LogBlueprintInspector, Warning, TEXT("GetNodeDetail: invalid guid '%s'"), *NodeGuidStr);
        return TEXT("{\"error\":\"invalid guid\"}");
    }

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node && Node->NodeGuid == Target)
        {
            TSharedPtr<FJsonObject> O = FBPNodeSerializer::Serialize(Node);
            FString S;
            TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
            FJsonSerializer::Serialize(O.ToSharedRef(), W);
            return S;
        }
    }

    UE_LOG(LogBlueprintInspector, Warning, TEXT("GetNodeDetail: node '%s' not found in graph '%s'"),
           *NodeGuidStr, *GraphName);
    return FString::Printf(TEXT("{\"error\":\"node '%s' not found\"}"), *NodeGuidStr);
}

FString FBPGraphReader::FindNodes(UBlueprint* Blueprint, const FString& QueryJson)
{
    if (!Blueprint) return TEXT("[]");

    UE_LOG(LogBlueprintInspector, Log, TEXT("FindNodes: %s query=%s"), *Blueprint->GetName(), *QueryJson);

    TSharedPtr<FJsonObject> Q;
    TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(QueryJson);
    FJsonSerializer::Deserialize(R, Q);

    auto ReadStr = [&](const TCHAR* Key) -> FString {
        if (!Q.IsValid()) return FString();
        FString V;
        return Q->TryGetStringField(Key, V) ? V : FString();
    };

    const FString NodeClassFilter     = ReadStr(TEXT("node_class"));
    const FString CallsFunctionFilter = ReadStr(TEXT("calls_function"));
    const FString VarNameFilter       = ReadStr(TEXT("references_variable"));
    const FString AssetPathFilter     = ReadStr(TEXT("references_asset"));
    const FString GraphNameFilter     = ReadStr(TEXT("graph_name"));

    TArray<TSharedPtr<FJsonValue>> Matches;

    auto SearchGraph = [&](UEdGraph* G)
    {
        if (!G) return;
        if (!GraphNameFilter.IsEmpty() && G->GetName() != GraphNameFilter) return;
        for (UEdGraphNode* N : G->Nodes)
        {
            if (!N) continue;
            if (!NodeClassFilter.IsEmpty() && N->GetClass()->GetName() != NodeClassFilter) continue;

            if (!CallsFunctionFilter.IsEmpty())
            {
                UK2Node_CallFunction* CF = Cast<UK2Node_CallFunction>(N);
                if (!CF || CF->GetFunctionName().ToString() != CallsFunctionFilter) continue;
            }

            if (!VarNameFilter.IsEmpty())
            {
                UK2Node_Variable* V = Cast<UK2Node_Variable>(N);
                if (!V || V->GetVarName().ToString() != VarNameFilter) continue;
            }

            if (!AssetPathFilter.IsEmpty())
            {
                bool bHit = false;
                for (UEdGraphPin* P : N->Pins)
                {
                    if (P && P->DefaultObject && P->DefaultObject->GetPathName() == AssetPathFilter)
                    {
                        bHit = true;
                        break;
                    }
                }
                if (!bHit) continue;
            }

            TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
            M->SetStringField(TEXT("graph_name"), G->GetName());
            M->SetStringField(TEXT("node_guid"), N->NodeGuid.ToString());
            M->SetStringField(TEXT("title"), N->GetNodeTitle(ENodeTitleType::ListView).ToString());
            Matches.Add(MakeShared<FJsonValueObject>(M));
        }
    };

    for (UEdGraph* G : Blueprint->UbergraphPages)          SearchGraph(G);
    for (UEdGraph* G : Blueprint->FunctionGraphs)          SearchGraph(G);
    for (UEdGraph* G : Blueprint->MacroGraphs)             SearchGraph(G);
    for (UEdGraph* G : Blueprint->DelegateSignatureGraphs) SearchGraph(G);

    FString S;
    TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
    FJsonSerializer::Serialize(Matches, W);
    return S;
}
