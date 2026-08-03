// Copyright 2026 RareBird Games. All Rights Reserved.
//
// Read-only Animation Blueprint and Animation Montage inspection: the
// independent read half of anim_blueprint_build, in the same shape and spelling
// graph_inspect, behavior_tree_inspect and widget_inspect use.
//
// Why it exists first: docs/REFRONT_MAP.md lists AnimBlueprintBuilderLibrary in
// group 5 with no independent inspector, which is why its native front was
// BLOCKED. Without a reader, "the build reported three states" and "the asset
// contains three states" are the same claim, made by the same code. Everything
// else in this lane is unverifiable until this exists.
//
// UE4.27 anim graph nodes carry a NodeGuid, but the "id" a build spec wrote is
// not persisted on the node, so identity here is DERIVED from the traversal
// path (graph / index : class : name) and labeled identity_kind = "derived",
// exactly as the Behavior Tree and Widget inspectors do. A renamed or reordered
// node is a different identity on purpose: the identity IS the structure.
//
// READ ONLY, and that is the contract rather than a hope: neither command is in
// IsToolMutating so no transaction opens, nothing here calls Modify,
// MarkPackageDirty or a compile, and the package dirty flag is read before and
// after the walk and reported both ways.

#include "MCPPuerTSBridgeService.h"

#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_BlendListBase.h"
#include "AnimGraphNode_BlendSpaceBase.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_TwoWayBlend.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
    struct FAnimWalkContext
    {
        TArray<TSharedPtr<FJsonValue>> UnsupportedFields;
        FString HashLines;
        int32 StateMachineCount = 0;
        int32 StateCount = 0;
        int32 ConduitCount = 0;
        int32 TransitionCount = 0;
        int32 AnimNodeCount = 0;
        int32 BlendNodeCount = 0;
    };

    /** Structural fields are the graph itself and are reported as hierarchy
        rather than as properties; serializing them here would duplicate the
        walk and, for the bound graphs, recurse back up into the owner. */
    bool IsStructuralAnimField(const FName& Name)
    {
        static const TSet<FName> Structural = {
            FName(TEXT("BoundGraph")), FName(TEXT("CustomTransitionGraph")),
            FName(TEXT("EditorStateMachineGraph")), FName(TEXT("Nodes")),
            FName(TEXT("SubGraphs")), FName(TEXT("Pins")),
        };
        return Structural.Contains(Name);
    }

    /** Every editable property of a node, through FJsonObjectConverter. This is
        also how asset references reach the response: a sequence player's
        Sequence, a blend space player's BlendSpace and a slot node's SlotName
        all live inside the node's EditAnywhere Node struct, so no per-class
        reader is needed and a node type added later is described without a code
        change. A property the converter cannot express is recorded in
        unsupported_fields rather than silently dropped. */
    TSharedPtr<FJsonObject> DescribeAnimProperties(
        const UObject* Object,
        const FString& OwnerId,
        FAnimWalkContext& Context)
    {
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        if (Object == nullptr)
        {
            return Properties;
        }
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Edit)
                || IsStructuralAnimField(Property->GetFName()))
            {
                continue;
            }
            const TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(
                Property, Property->ContainerPtrToValuePtr<void>(Object), 0, 0);
            if (Value.IsValid())
            {
                Properties->SetField(Property->GetName(), Value);
            }
            else
            {
                Context.UnsupportedFields.Add(MakeShared<FJsonValueString>(
                    FString::Printf(TEXT("%s.%s"), *OwnerId, *Property->GetName())));
            }
        }
        return Properties;
    }

    FString NodeIdentity(const UEdGraphNode* Node, const FString& GraphId, int32 Index)
    {
        return FString::Printf(TEXT("%s/%d:%s:%s"),
            *GraphId, Index, *Node->GetClass()->GetName(), *Node->GetName());
    }

    /** One anim graph node as JSON. Kind is coarse on purpose: the exact class
        is always reported as class_path, and kind is what a caller filters on
        without having to know the UE4.27 class hierarchy. */
    TSharedPtr<FJsonObject> DescribeAnimNode(
        UEdGraphNode* Node,
        const FString& NodeId,
        FAnimWalkContext& Context)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("id"), NodeId);
        Out->SetStringField(TEXT("identity"), TEXT("derived"));
        Out->SetStringField(TEXT("name"), Node->GetName());
        Out->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        Out->SetStringField(TEXT("class_path"), Node->GetClass()->GetPathName());
        Out->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());

        const bool bIsBlend =
            Node->IsA(UAnimGraphNode_BlendSpaceBase::StaticClass())
            || Node->IsA(UAnimGraphNode_BlendListBase::StaticClass())
            || Node->IsA(UAnimGraphNode_LayeredBoneBlend::StaticClass())
            || Node->IsA(UAnimGraphNode_TwoWayBlend::StaticClass());
        const bool bIsSaveCachedPose = Node->IsA(UAnimGraphNode_SaveCachedPose::StaticClass());
        const bool bIsUseCachedPose = Node->IsA(UAnimGraphNode_UseCachedPose::StaticClass());

        FString Kind = TEXT("anim_node");
        if (bIsBlend) { Kind = TEXT("blend"); }
        else if (bIsSaveCachedPose) { Kind = TEXT("save_cached_pose"); }
        else if (bIsUseCachedPose) { Kind = TEXT("use_cached_pose"); }
        else if (Node->IsA(UAnimGraphNode_StateMachineBase::StaticClass())) { Kind = TEXT("state_machine"); }
        Out->SetStringField(TEXT("kind"), Kind);
        Out->SetBoolField(TEXT("is_blend"), bIsBlend);

        if (bIsBlend) { Context.BlendNodeCount++; }
        if (Node->IsA(UAnimGraphNode_Base::StaticClass())) { Context.AnimNodeCount++; }

        Out->SetObjectField(TEXT("properties"),
            DescribeAnimProperties(Node, NodeId, Context));

        // Identity, class and kind only. Properties are deliberately not hashed:
        // this is a STRUCTURE hash, so retuning a blend time does not read as a
        // reshape of the graph.
        Context.HashLines += NodeId + TEXT("|") + Node->GetClass()->GetPathName()
            + TEXT("|") + Kind + TEXT("\n");
        return Out;
    }

    /** Every node of one graph, in a canonical order that does not depend on
        the order Unreal happens to hold them in. UE4.27 gives anim graph nodes
        no authored id, so sorting by class then name then position is what
        makes two reads of an unchanged asset produce the same hash. */
    TArray<TSharedPtr<FJsonValue>> DescribeGraphNodes(
        const UEdGraph* Graph,
        const FString& GraphId,
        FAnimWalkContext& Context)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        if (Graph == nullptr)
        {
            return Out;
        }
        TArray<UEdGraphNode*> Sorted;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node != nullptr) { Sorted.Add(Node); }
        }
        Sorted.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
        {
            const FString ClassA = A.GetClass()->GetPathName();
            const FString ClassB = B.GetClass()->GetPathName();
            if (ClassA != ClassB) { return ClassA < ClassB; }
            const FString NameA = A.GetName();
            const FString NameB = B.GetName();
            if (NameA != NameB) { return NameA < NameB; }
            if (A.NodePosX != B.NodePosX) { return A.NodePosX < B.NodePosX; }
            return A.NodePosY < B.NodePosY;
        });
        for (int32 Index = 0; Index < Sorted.Num(); ++Index)
        {
            Out.Add(MakeShared<FJsonValueObject>(
                DescribeAnimNode(Sorted[Index], NodeIdentity(Sorted[Index], GraphId, Index), Context)));
        }
        return Out;
    }

    /** A transition's rule, which is where a caller checks that "walk leaves for
        idle when bIsMoving is false" actually landed.

        Two shapes exist and they are not interchangeable. An explicit rule is a
        graph of nodes feeding bCanEnterTransition. An automatic rule is the
        engine's own remaining-time rule, driven by
        bAutomaticRuleBasedOnSequencePlayerInState with no graph at all, which is
        what the builder writes for a "time_remaining" condition. Reporting only
        the graph would make an automatic rule look like an empty condition. */
    TSharedPtr<FJsonObject> DescribeTransitionCondition(
        UAnimStateTransitionNode* Transition,
        const FString& TransitionId,
        FAnimWalkContext& Context)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        const bool bAutomatic = Transition->bAutomaticRuleBasedOnSequencePlayerInState;
        Out->SetBoolField(TEXT("automatic_rule_based_on_sequence_player"), bAutomatic);

        const UEdGraph* RuleGraph = Transition->GetBoundGraph();
        const FString RuleGraphId = TransitionId + TEXT("/rule");
        TArray<TSharedPtr<FJsonValue>> RuleNodes =
            DescribeGraphNodes(RuleGraph, RuleGraphId, Context);
        Out->SetStringField(TEXT("rule_graph"),
            RuleGraph != nullptr ? RuleGraph->GetName() : FString());
        Out->SetArrayField(TEXT("rule_nodes"), RuleNodes);
        Out->SetNumberField(TEXT("rule_node_count"), RuleNodes.Num());

        // A rule graph that holds only its own result node is an empty rule: the
        // transition never fires unless the automatic rule is on. Naming that is
        // the difference between "no condition" and "a condition that is missing".
        const bool bHasRuleLogic = RuleNodes.Num() > 1;
        FString Kind = TEXT("none");
        if (bAutomatic) { Kind = TEXT("automatic_time_remaining"); }
        else if (bHasRuleLogic) { Kind = TEXT("rule_graph"); }
        Out->SetStringField(TEXT("kind"), Kind);
        Context.HashLines += TransitionId + TEXT("|condition|") + Kind + TEXT("\n");
        return Out;
    }

    TSharedPtr<FJsonObject> DescribeStateMachine(
        UAnimGraphNode_StateMachineBase* Machine,
        const FString& MachineId,
        FAnimWalkContext& Context)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Context.StateMachineCount++;
        Out->SetStringField(TEXT("id"), MachineId);
        Out->SetStringField(TEXT("identity"), TEXT("derived"));
        Out->SetStringField(TEXT("name"), Machine->GetName());
        Out->SetStringField(TEXT("class_path"), Machine->GetClass()->GetPathName());

        UAnimationStateMachineGraph* Graph = Machine->EditorStateMachineGraph;
        if (Graph == nullptr)
        {
            Out->SetArrayField(TEXT("states"), TArray<TSharedPtr<FJsonValue>>());
            Out->SetArrayField(TEXT("transitions"), TArray<TSharedPtr<FJsonValue>>());
            Out->SetStringField(TEXT("entry_state"), FString());
            return Out;
        }
        Out->SetStringField(TEXT("state_machine_graph"), Graph->GetName());

        // States and conduits first, so a transition can name its endpoints by
        // the same identity the state carries rather than by a raw object name.
        TMap<const UAnimStateNodeBase*, FString> StateIds;
        TArray<UAnimStateNodeBase*> States;
        TArray<UAnimStateTransitionNode*> Transitions;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node))
            {
                Transitions.Add(Transition);
            }
            else if (UAnimStateNodeBase* State = Cast<UAnimStateNodeBase>(Node))
            {
                States.Add(State);
            }
        }
        States.Sort([](const UAnimStateNodeBase& A, const UAnimStateNodeBase& B)
        {
            const FString NameA = const_cast<UAnimStateNodeBase&>(A).GetStateName();
            const FString NameB = const_cast<UAnimStateNodeBase&>(B).GetStateName();
            if (NameA != NameB) { return NameA < NameB; }
            return A.GetName() < B.GetName();
        });

        TArray<TSharedPtr<FJsonValue>> StateJson;
        for (int32 Index = 0; Index < States.Num(); ++Index)
        {
            UAnimStateNodeBase* State = States[Index];
            const FString StateName = State->GetStateName();
            const FString StateId = FString::Printf(TEXT("%s/state:%s"), *MachineId, *StateName);
            StateIds.Add(State, StateId);

            const bool bIsConduit = State->IsA(UAnimStateConduitNode::StaticClass());
            if (bIsConduit) { Context.ConduitCount++; } else { Context.StateCount++; }

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("id"), StateId);
            Entry->SetStringField(TEXT("identity"), TEXT("derived"));
            Entry->SetStringField(TEXT("name"), StateName);
            Entry->SetStringField(TEXT("class_path"), State->GetClass()->GetPathName());
            Entry->SetStringField(TEXT("kind"), bIsConduit ? TEXT("conduit") : TEXT("state"));
            Entry->SetObjectField(TEXT("properties"),
                DescribeAnimProperties(State, StateId, Context));
            // The state's own pose graph: this is where the sequence player, the
            // blend space player and the slot node that reference actual assets
            // live, so a state with the wrong animation is visible here.
            Entry->SetArrayField(TEXT("graph_nodes"),
                DescribeGraphNodes(State->GetBoundGraph(), StateId + TEXT("/graph"), Context));
            StateJson.Add(MakeShared<FJsonValueObject>(Entry));
            Context.HashLines += StateId + TEXT("|")
                + (bIsConduit ? TEXT("conduit") : TEXT("state")) + TEXT("\n");
        }
        Out->SetArrayField(TEXT("states"), StateJson);

        // The entry state is the one property of a state machine that cannot be
        // read off any single state: a machine whose entry link is missing
        // compiles and then plays nothing.
        FString EntryStateId;
        if (Graph->EntryNode != nullptr)
        {
            if (const UEdGraphNode* EntryTarget = Graph->EntryNode->GetOutputNode())
            {
                if (const UAnimStateNodeBase* EntryState = Cast<UAnimStateNodeBase>(EntryTarget))
                {
                    if (const FString* Found = StateIds.Find(EntryState))
                    {
                        EntryStateId = *Found;
                    }
                }
            }
        }
        Out->SetStringField(TEXT("entry_state"), EntryStateId);
        Context.HashLines += MachineId + TEXT("|entry|") + EntryStateId + TEXT("\n");

        // Transitions are keyed by their endpoints, not by their object name,
        // because the object name is an allocation detail and the endpoints are
        // what the request asked for.
        struct FTransitionEntry
        {
            FString Key;
            TSharedPtr<FJsonObject> Json;
        };
        TArray<FTransitionEntry> TransitionEntries;
        for (UAnimStateTransitionNode* Transition : Transitions)
        {
            Context.TransitionCount++;
            const UAnimStateNodeBase* Previous = Transition->GetPreviousState();
            const UAnimStateNodeBase* Next = Transition->GetNextState();
            const FString* FromId = Previous != nullptr ? StateIds.Find(Previous) : nullptr;
            const FString* ToId = Next != nullptr ? StateIds.Find(Next) : nullptr;
            const FString From = FromId != nullptr ? *FromId : FString();
            const FString To = ToId != nullptr ? *ToId : FString();
            const FString TransitionId = FString::Printf(
                TEXT("%s/transition:%s->%s"), *MachineId, *From, *To);

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("id"), TransitionId);
            Entry->SetStringField(TEXT("identity"), TEXT("derived"));
            Entry->SetStringField(TEXT("from"), From);
            Entry->SetStringField(TEXT("to"), To);
            Entry->SetStringField(TEXT("class_path"), Transition->GetClass()->GetPathName());
            Entry->SetNumberField(TEXT("crossfade_duration"), Transition->CrossfadeDuration);
            Entry->SetNumberField(TEXT("priority_order"), Transition->PriorityOrder);
            Entry->SetBoolField(TEXT("bidirectional"), Transition->Bidirectional);
            Entry->SetNumberField(TEXT("blend_mode"), static_cast<int32>(Transition->BlendMode));
            Entry->SetNumberField(TEXT("logic_type"), static_cast<int32>(Transition->LogicType.GetValue()));
            Entry->SetBoolField(TEXT("shared_rules"), Transition->bSharedRules);
            Entry->SetStringField(TEXT("shared_rules_name"), Transition->SharedRulesName);
            Entry->SetObjectField(TEXT("condition"),
                DescribeTransitionCondition(Transition, TransitionId, Context));
            // An endpoint that could not be resolved is named rather than left
            // as an empty string with no explanation.
            if (Previous == nullptr || Next == nullptr)
            {
                Context.UnsupportedFields.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("%s (a transition endpoint is not connected to a state)"), *TransitionId)));
            }
            TransitionEntries.Add({ TransitionId, Entry });
            Context.HashLines += TEXT("transition|") + From + TEXT("->") + To + TEXT("\n");
        }
        TransitionEntries.Sort([](const FTransitionEntry& A, const FTransitionEntry& B)
        {
            return A.Key < B.Key;
        });
        TArray<TSharedPtr<FJsonValue>> TransitionJson;
        for (const FTransitionEntry& Entry : TransitionEntries)
        {
            TransitionJson.Add(MakeShared<FJsonValueObject>(Entry.Json));
        }
        Out->SetArrayField(TEXT("transitions"), TransitionJson);
        return Out;
    }

    /** Resolve an asset path (package or object form) to loaded AssetData.
        Reading is allowed anywhere under the two content roots, like every other
        read in this bridge; authoring stays limited to /Game/MCPGenerated/. */
    bool ResolveReadableAsset(
        const FString& AssetPath,
        const TCHAR* Subject,
        FAssetData& OutAssetData,
        FString& OutPackagePath,
        FString& OutObjectPath,
        FString& OutError)
    {
        const FString Trimmed = AssetPath.TrimStartAndEnd();
        if (Trimmed.IsEmpty())
        {
            OutError = TEXT("asset_path is required.");
            return false;
        }
        if (!Trimmed.StartsWith(TEXT("/Game/")) && !Trimmed.StartsWith(TEXT("/Engine/")))
        {
            OutError = FString::Printf(TEXT("%s inspection is limited to /Game and /Engine."), Subject);
            return false;
        }
        OutObjectPath = Trimmed;
        OutPackagePath = Trimmed;
        if (!Trimmed.Contains(TEXT(".")))
        {
            if (!FPackageName::IsValidLongPackageName(Trimmed))
            {
                OutError = FString::Printf(TEXT("'%s' is not a valid package path."), *Trimmed);
                return false;
            }
            OutObjectPath = Trimmed + TEXT(".") + FPackageName::GetLongPackageAssetName(Trimmed);
        }
        else
        {
            OutPackagePath = FPackageName::ObjectPathToPackageName(Trimmed);
        }

        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        OutAssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*OutObjectPath));
        if (!OutAssetData.IsValid())
        {
            OutError = FString::Printf(TEXT("No asset was found at '%s'."), *OutObjectPath);
            return false;
        }
        return true;
    }

    FString BlueprintStatusLabel(EBlueprintStatus Status)
    {
        switch (Status)
        {
            case BS_UpToDate:             return TEXT("UpToDate");
            case BS_UpToDateWithWarnings: return TEXT("UpToDateWithWarnings");
            case BS_Error:                return TEXT("Error");
            case BS_Dirty:                return TEXT("Dirty");
            case BS_BeingCreated:         return TEXT("BeingCreated");
            default:                      return TEXT("Unknown");
        }
    }

    FString HashHex(const FString& Lines)
    {
        uint8 Sha1[FSHA1::DigestSize];
        const FTCHARToUTF8 Utf8(*Lines);
        FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Sha1);
        return BytesToHex(Sha1, FSHA1::DigestSize);
    }
}

bool UMCPPuerTSBridgeService::InspectAnimBlueprintJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Inspection request must be a JSON object.");
        return false;
    }

    FString AssetPath;
    Request->TryGetStringField(TEXT("asset_path"), AssetPath);

    FAssetData AssetData;
    FString PackagePath;
    FString ObjectPath;
    if (!ResolveReadableAsset(
            AssetPath, TEXT("Animation Blueprint"), AssetData, PackagePath, ObjectPath, OutError))
    {
        return false;
    }
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
    if (AnimBlueprint == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The asset at '%s' is a %s, not an AnimBlueprint."),
            *ObjectPath, *AssetData.AssetClass.ToString());
        return false;
    }

    const UPackage* Package = AnimBlueprint->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    TArray<TSharedPtr<FJsonValue>> Warnings;
    FAnimWalkContext Context;

    // The anim graphs, found the same way the builder finds them: by schema
    // class name rather than by graph name, because the graph can be renamed.
    // Kept as the inverse of ABPAnimGraphBuilder::Build so the two cannot drift.
    TArray<UEdGraph*> AnimGraphs;
    for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
    {
        if (Graph != nullptr && Graph->Schema != nullptr
            && Graph->Schema->GetClass()->GetName().Contains(TEXT("AnimationGraphSchema")))
        {
            AnimGraphs.Add(Graph);
        }
    }
    AnimGraphs.Sort([](const UEdGraph& A, const UEdGraph& B)
    {
        return A.GetName() < B.GetName();
    });
    if (AnimGraphs.Num() == 0)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This Animation Blueprint has no anim graph.")));
    }

    TArray<TSharedPtr<FJsonValue>> AnimGraphJson;
    TArray<TSharedPtr<FJsonValue>> StateMachineJson;
    TArray<TSharedPtr<FJsonValue>> BlendNodeJson;
    // Cached poses are a graph-wide namespace, not a per-graph one: a
    // SaveCachedPose in one anim graph is reachable by name from another, so
    // they are collected across every graph and reported once.
    TMap<FString, TArray<FString>> CachedPoseUsers;
    TMap<FString, FString> CachedPoseProducers;

    for (UEdGraph* Graph : AnimGraphs)
    {
        const FString GraphId = FString::Printf(TEXT("animgraph:%s"), *Graph->GetName());
        TSharedPtr<FJsonObject> GraphEntry = MakeShared<FJsonObject>();
        GraphEntry->SetStringField(TEXT("id"), GraphId);
        GraphEntry->SetStringField(TEXT("name"), Graph->GetName());
        GraphEntry->SetStringField(TEXT("schema_class"), Graph->Schema->GetClass()->GetPathName());
        const TArray<TSharedPtr<FJsonValue>> Nodes = DescribeGraphNodes(Graph, GraphId, Context);
        GraphEntry->SetArrayField(TEXT("nodes"), Nodes);
        GraphEntry->SetNumberField(TEXT("node_count"), Nodes.Num());
        AnimGraphJson.Add(MakeShared<FJsonValueObject>(GraphEntry));

        // A second pass over the same nodes, for the three things a caller
        // usually wants without walking the node array itself.
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr) { continue; }
            if (UAnimGraphNode_StateMachineBase* Machine = Cast<UAnimGraphNode_StateMachineBase>(Node))
            {
                const FString MachineId = FString::Printf(
                    TEXT("%s/statemachine:%s"), *GraphId, *Machine->GetName());
                StateMachineJson.Add(MakeShared<FJsonValueObject>(
                    DescribeStateMachine(Machine, MachineId, Context)));
            }
            else if (const UAnimGraphNode_SaveCachedPose* Save = Cast<UAnimGraphNode_SaveCachedPose>(Node))
            {
                CachedPoseProducers.Add(Save->CacheName, Node->GetName());
                CachedPoseUsers.FindOrAdd(Save->CacheName);
            }
            else if (const UAnimGraphNode_UseCachedPose* Use = Cast<UAnimGraphNode_UseCachedPose>(Node))
            {
                const UAnimGraphNode_SaveCachedPose* Source = Use->SaveCachedPoseNode.Get();
                const FString CacheName = Source != nullptr ? Source->CacheName : FString();
                CachedPoseUsers.FindOrAdd(CacheName).Add(Node->GetName());
            }

            if (Node->IsA(UAnimGraphNode_BlendSpaceBase::StaticClass())
                || Node->IsA(UAnimGraphNode_BlendListBase::StaticClass())
                || Node->IsA(UAnimGraphNode_LayeredBoneBlend::StaticClass())
                || Node->IsA(UAnimGraphNode_TwoWayBlend::StaticClass()))
            {
                TSharedPtr<FJsonObject> Blend = MakeShared<FJsonObject>();
                Blend->SetStringField(TEXT("graph"), Graph->GetName());
                Blend->SetStringField(TEXT("name"), Node->GetName());
                Blend->SetStringField(TEXT("class_path"), Node->GetClass()->GetPathName());
                Blend->SetObjectField(TEXT("properties"), DescribeAnimProperties(
                    Node, GraphId + TEXT("/blend:") + Node->GetName(), Context));
                BlendNodeJson.Add(MakeShared<FJsonValueObject>(Blend));
            }
        }
    }

    TArray<FString> CacheNames;
    CachedPoseUsers.GetKeys(CacheNames);
    CacheNames.Sort();
    TArray<TSharedPtr<FJsonValue>> CachedPoseJson;
    for (const FString& CacheName : CacheNames)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("cache_name"), CacheName);
        const FString* Producer = CachedPoseProducers.Find(CacheName);
        Entry->SetStringField(TEXT("save_node"), Producer != nullptr ? *Producer : FString());
        TArray<FString> Users = CachedPoseUsers[CacheName];
        Users.Sort();
        TArray<TSharedPtr<FJsonValue>> UserJson;
        for (const FString& User : Users) { UserJson.Add(MakeShared<FJsonValueString>(User)); }
        Entry->SetArrayField(TEXT("used_by"), UserJson);
        Entry->SetNumberField(TEXT("use_count"), Users.Num());
        // A UseCachedPose whose source is gone resolves to an empty cache name.
        // That compiles to a reference pointing at nothing, so it is a warning
        // rather than a row nobody reads.
        if (Producer == nullptr)
        {
            Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("%d cached-pose reader(s) point at a cache named '%s' that no "
                     "SaveCachedPose node produces."), Users.Num(), *CacheName)));
        }
        CachedPoseJson.Add(MakeShared<FJsonValueObject>(Entry));
        Context.HashLines += TEXT("cachedpose|") + CacheName + TEXT("\n");
    }

    // Member variables, in the shape blueprint_build and graph_inspect use, so
    // an anim transition rule that reads bIsMoving can be checked against the
    // variable that is supposed to exist.
    TArray<TSharedPtr<FJsonValue>> VariableJson;
    {
        TArray<const FBPVariableDescription*> Variables;
        for (const FBPVariableDescription& Variable : AnimBlueprint->NewVariables)
        {
            Variables.Add(&Variable);
        }
        Variables.Sort([](const FBPVariableDescription& A, const FBPVariableDescription& B)
        {
            return A.VarName.LexicalLess(B.VarName);
        });
        for (const FBPVariableDescription* Variable : Variables)
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Variable->VarName.ToString());
            Entry->SetStringField(TEXT("type"), Variable->VarType.PinCategory.ToString());
            Entry->SetStringField(TEXT("sub_type"), Variable->VarType.PinSubCategory.ToString());
            Entry->SetStringField(TEXT("default_value"), Variable->DefaultValue);
            VariableJson.Add(MakeShared<FJsonValueObject>(Entry));
            Context.HashLines += TEXT("variable|") + Variable->VarName.ToString()
                + TEXT("|") + Variable->VarType.PinCategory.ToString() + TEXT("\n");
        }
    }

    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this Animation Blueprint dirtied its package. That is a defect in the "
                 "inspector, not a property of the asset: report it.")));
    }

    TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
    Counts->SetNumberField(TEXT("anim_graph_count"), AnimGraphs.Num());
    Counts->SetNumberField(TEXT("state_machine_count"), Context.StateMachineCount);
    Counts->SetNumberField(TEXT("state_count"), Context.StateCount);
    Counts->SetNumberField(TEXT("conduit_count"), Context.ConduitCount);
    Counts->SetNumberField(TEXT("transition_count"), Context.TransitionCount);
    Counts->SetNumberField(TEXT("cached_pose_count"), CachedPoseJson.Num());
    Counts->SetNumberField(TEXT("blend_node_count"), BlendNodeJson.Num());
    Counts->SetNumberField(TEXT("anim_node_count"), Context.AnimNodeCount);
    Counts->SetNumberField(TEXT("variable_count"), VariableJson.Num());

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), AnimBlueprint->GetPathName());
    Result->SetStringField(TEXT("anim_blueprint_class"), AnimBlueprint->GetClass()->GetPathName());
    Result->SetStringField(TEXT("generated_class_path"),
        AnimBlueprint->GeneratedClass != nullptr
            ? AnimBlueprint->GeneratedClass->GetPathName()
            : FString());
    Result->SetStringField(TEXT("parent_class"),
        AnimBlueprint->ParentClass != nullptr ? AnimBlueprint->ParentClass->GetPathName() : FString());
    Result->SetStringField(TEXT("target_skeleton"),
        AnimBlueprint->TargetSkeleton != nullptr
            ? AnimBlueprint->TargetSkeleton->GetPathName()
            : FString());
    // The stored compile status, not a compile: reading must never compile, and
    // a caller that wants a fresh verdict runs anim_blueprint_build.
    Result->SetStringField(TEXT("blueprint_status"), BlueprintStatusLabel(AnimBlueprint->Status));
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    Result->SetStringField(TEXT("identity_kind"), TEXT("derived"));
    Result->SetArrayField(TEXT("anim_graphs"), AnimGraphJson);
    Result->SetArrayField(TEXT("state_machines"), StateMachineJson);
    Result->SetArrayField(TEXT("cached_poses"), CachedPoseJson);
    Result->SetArrayField(TEXT("blend_nodes"), BlendNodeJson);
    Result->SetArrayField(TEXT("variables"), VariableJson);
    Result->SetObjectField(TEXT("counts"), Counts);
    Result->SetArrayField(TEXT("unsupported_fields"), Context.UnsupportedFields);
    Result->SetStringField(TEXT("structure_hash_sha1"), HashHex(Context.HashLines));
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}

bool UMCPPuerTSBridgeService::InspectAnimMontageJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Inspection request must be a JSON object.");
        return false;
    }

    FString AssetPath;
    Request->TryGetStringField(TEXT("asset_path"), AssetPath);

    FAssetData AssetData;
    FString PackagePath;
    FString ObjectPath;
    if (!ResolveReadableAsset(
            AssetPath, TEXT("Animation Montage"), AssetData, PackagePath, ObjectPath, OutError))
    {
        return false;
    }
    UAnimMontage* Montage = Cast<UAnimMontage>(AssetData.GetAsset());
    if (Montage == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The asset at '%s' is a %s, not an AnimMontage."),
            *ObjectPath, *AssetData.AssetClass.ToString());
        return false;
    }

    const UPackage* Package = Montage->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    TArray<TSharedPtr<FJsonValue>> Warnings;
    FString HashLines;

    // Sections in montage order, not sorted: a montage's section order is
    // meaningful and NextSectionName is what chains them, so reordering would
    // destroy the thing being inspected.
    TArray<TSharedPtr<FJsonValue>> SectionJson;
    for (int32 Index = 0; Index < Montage->CompositeSections.Num(); ++Index)
    {
        const FCompositeSection& Section = Montage->CompositeSections[Index];
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), Index);
        Entry->SetStringField(TEXT("name"), Section.SectionName.ToString());
        Entry->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
        Entry->SetNumberField(TEXT("start_time"), Section.GetTime());
        SectionJson.Add(MakeShared<FJsonValueObject>(Entry));
        HashLines += FString::Printf(TEXT("section|%d|%s|%s\n"),
            Index, *Section.SectionName.ToString(), *Section.NextSectionName.ToString());
    }

    TArray<TSharedPtr<FJsonValue>> SlotJson;
    for (int32 Index = 0; Index < Montage->SlotAnimTracks.Num(); ++Index)
    {
        const FSlotAnimationTrack& Slot = Montage->SlotAnimTracks[Index];
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), Index);
        Entry->SetStringField(TEXT("slot_name"), Slot.SlotName.ToString());
        TArray<TSharedPtr<FJsonValue>> SegmentJson;
        for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
        {
            TSharedPtr<FJsonObject> SegmentEntry = MakeShared<FJsonObject>();
            SegmentEntry->SetStringField(TEXT("animation"),
                Segment.AnimReference != nullptr ? Segment.AnimReference->GetPathName() : FString());
            SegmentEntry->SetNumberField(TEXT("start_position"), Segment.AnimStartTime);
            SegmentEntry->SetNumberField(TEXT("end_position"), Segment.AnimEndTime);
            SegmentEntry->SetNumberField(TEXT("start_time"), Segment.StartPos);
            SegmentEntry->SetNumberField(TEXT("play_rate"), Segment.AnimPlayRate);
            SegmentEntry->SetNumberField(TEXT("loop_count"), Segment.LoopingCount);
            SegmentJson.Add(MakeShared<FJsonValueObject>(SegmentEntry));
            HashLines += FString::Printf(TEXT("segment|%s|%s\n"),
                *Slot.SlotName.ToString(),
                Segment.AnimReference != nullptr ? *Segment.AnimReference->GetPathName() : TEXT(""));
        }
        Entry->SetArrayField(TEXT("segments"), SegmentJson);
        SlotJson.Add(MakeShared<FJsonValueObject>(Entry));
        HashLines += FString::Printf(TEXT("slot|%d|%s\n"), Index, *Slot.SlotName.ToString());
    }

    // Notifies are UPROPERTY() with no Edit flag, so a reflection walk filtered
    // on CPF_Edit would drop them entirely. They are read explicitly for that
    // reason, sorted by trigger time the way the engine stores them.
    TArray<TSharedPtr<FJsonValue>> NotifyJson;
    for (int32 Index = 0; Index < Montage->Notifies.Num(); ++Index)
    {
        const FAnimNotifyEvent& Notify = Montage->Notifies[Index];
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), Index);
        Entry->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
        Entry->SetNumberField(TEXT("trigger_time"), Notify.GetTriggerTime());
        Entry->SetNumberField(TEXT("end_trigger_time"), Notify.GetEndTriggerTime());
        Entry->SetNumberField(TEXT("duration"), Notify.GetDuration());
        Entry->SetNumberField(TEXT("trigger_weight_threshold"), Notify.TriggerWeightThreshold);
        Entry->SetStringField(TEXT("notify_class"),
            Notify.Notify != nullptr ? Notify.Notify->GetClass()->GetPathName() : FString());
        Entry->SetStringField(TEXT("notify_state_class"),
            Notify.NotifyStateClass != nullptr
                ? Notify.NotifyStateClass->GetClass()->GetPathName()
                : FString());
        // A state notify holds a window; an event notify is a single instant.
        // The two are different rows in the editor and behave differently at
        // runtime, so they are distinguished here rather than merged.
        Entry->SetBoolField(TEXT("is_state"), Notify.NotifyStateClass != nullptr);
        Entry->SetBoolField(TEXT("is_branching_point"), Notify.IsBranchingPoint());
        NotifyJson.Add(MakeShared<FJsonValueObject>(Entry));
        HashLines += FString::Printf(TEXT("notify|%s|%s\n"),
            *Notify.NotifyName.ToString(),
            Notify.NotifyStateClass != nullptr ? TEXT("state") : TEXT("event"));
    }

    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this Animation Montage dirtied its package. That is a defect in the "
                 "inspector, not a property of the asset: report it.")));
    }

    TSharedPtr<FJsonObject> Blend = MakeShared<FJsonObject>();
    Blend->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
    Blend->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());
    Blend->SetNumberField(TEXT("blend_out_trigger_time"), Montage->BlendOutTriggerTime);
    Blend->SetBoolField(TEXT("enable_auto_blend_out"), Montage->bEnableAutoBlendOut);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), Montage->GetPathName());
    Result->SetStringField(TEXT("montage_class"), Montage->GetClass()->GetPathName());
    Result->SetStringField(TEXT("skeleton"),
        Montage->GetSkeleton() != nullptr ? Montage->GetSkeleton()->GetPathName() : FString());
    Result->SetNumberField(TEXT("sequence_length"), Montage->SequenceLength);
    Result->SetStringField(TEXT("sync_group"), Montage->SyncGroup.ToString());
    Result->SetObjectField(TEXT("blend"), Blend);
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    Result->SetStringField(TEXT("identity_kind"), TEXT("derived"));
    Result->SetArrayField(TEXT("sections"), SectionJson);
    Result->SetArrayField(TEXT("slots"), SlotJson);
    Result->SetArrayField(TEXT("notifies"), NotifyJson);
    Result->SetNumberField(TEXT("section_count"), SectionJson.Num());
    Result->SetNumberField(TEXT("slot_count"), SlotJson.Num());
    Result->SetNumberField(TEXT("notify_count"), NotifyJson.Num());
    Result->SetStringField(TEXT("structure_hash_sha1"), HashHex(HashLines));
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}
