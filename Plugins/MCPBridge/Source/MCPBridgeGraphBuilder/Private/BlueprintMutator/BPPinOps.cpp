// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPPinOps.h"
#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "../BlueprintInspector/BPGraphReader.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"

#define LOCTEXT_NAMESPACE "BPPinOps"

namespace
{
    UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName)
    {
        if (!Node) return nullptr;
        for (UEdGraphPin* P : Node->Pins)
        {
            if (P && P->PinName.ToString() == PinName) return P;
        }
        return nullptr;
    }
}

bool FBPPinOps::ConnectPins(UBlueprint* Blueprint, const FString& GraphName,
    const FString& SrcNodeGuid, const FString& SrcPinName,
    const FString& DstNodeGuid, const FString& DstPinName)
{
    if (!Blueprint) { UE_LOG(LogBlueprintMutator, Warning, TEXT("ConnectPins: null BP")); return false; }
    UEdGraph* Graph = FBPGraphReader::FindGraphByName(Blueprint, GraphName);
    if (!Graph) { UE_LOG(LogBlueprintMutator, Warning, TEXT("ConnectPins: graph '%s' not found"), *GraphName); return false; }
    UEdGraphNode* SrcNode = FBPGraphReader::FindNodeByGuid(Graph, SrcNodeGuid);
    UEdGraphNode* DstNode = FBPGraphReader::FindNodeByGuid(Graph, DstNodeGuid);
    if (!SrcNode || !DstNode)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("ConnectPins: node(s) not found - src=%s dst=%s"), *SrcNodeGuid, *DstNodeGuid);
        return false;
    }
    UEdGraphPin* SrcPin = FindPinByName(SrcNode, SrcPinName);
    UEdGraphPin* DstPin = FindPinByName(DstNode, DstPinName);
    if (!SrcPin || !DstPin)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("ConnectPins: pin(s) not found - src.pin=%s dst.pin=%s"), *SrcPinName, *DstPinName);
        return false;
    }

    const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
    if (!K2Schema)
    {
        UE_LOG(LogBlueprintMutator, Error, TEXT("ConnectPins: K2 schema not available"));
        return false;
    }
    const FPinConnectionResponse Resp = K2Schema->CanCreateConnection(SrcPin, DstPin);
    if (Resp.Response == CONNECT_RESPONSE_DISALLOW)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("ConnectPins: schema disallowed - %s"), *Resp.Message.ToString());
        return false;
    }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("ConnectPins", "Soul Juice: Connect Pins"),
        [&]() -> bool
        {
            SrcNode->Modify();
            DstNode->Modify();
            return K2Schema->TryCreateConnection(SrcPin, DstPin);
        });
}

#undef LOCTEXT_NAMESPACE
