// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "MCPBridgeAssetRollback.h"
#include "MCPBridgeContentSnapshot.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeConcatenator.h"
#include "Sound/SoundNodeDelay.h"
#include "Sound/SoundNodeLooping.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
    constexpr int32 MaximumBuildNodes = 100;

    struct FSoundNodeSpec
    {
        FString Id;
        FString Type;
        TArray<FString> Children;
        FString SoundWave;
        TSharedPtr<FJsonObject> Properties;
    };

    bool HasOnlyFields(
        const TSharedPtr<FJsonObject>& Object,
        const TSet<FString>& Allowed,
        const FString& Location,
        FString& OutError)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
        {
            if (!Allowed.Contains(Field.Key))
            {
                OutError = FString::Printf(
                    TEXT("Unknown field '%s' at %s."), *Field.Key, *Location);
                return false;
            }
        }
        return true;
    }

    UClass* SoundNodeClass(const FString& Type)
    {
        if (Type == TEXT("wave_player")) { return USoundNodeWavePlayer::StaticClass(); }
        if (Type == TEXT("mixer")) { return USoundNodeMixer::StaticClass(); }
        if (Type == TEXT("random")) { return USoundNodeRandom::StaticClass(); }
        if (Type == TEXT("modulator")) { return USoundNodeModulator::StaticClass(); }
        if (Type == TEXT("delay")) { return USoundNodeDelay::StaticClass(); }
        if (Type == TEXT("looping")) { return USoundNodeLooping::StaticClass(); }
        if (Type == TEXT("concatenator")) { return USoundNodeConcatenator::StaticClass(); }
        return nullptr;
    }

    bool ApplyEditableProperties(
        UObject* Object,
        const TSharedPtr<FJsonObject>& Properties,
        const FString& Location,
        FString& OutError)
    {
        if (!Properties.IsValid()) { return true; }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Properties->Values)
        {
            FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*Field.Key));
            if (Property == nullptr || !Property->HasAnyPropertyFlags(CPF_Edit))
            {
                OutError = FString::Printf(
                    TEXT("'%s' at %s is not an editable property on %s."),
                    *Field.Key, *Location, *Object->GetClass()->GetName());
                return false;
            }
            if (Field.Key == TEXT("ChildNodes") || Field.Key == TEXT("GraphNode"))
            {
                OutError = FString::Printf(
                    TEXT("'%s' at %s is structural and must be expressed through children."),
                    *Field.Key, *Location);
                return false;
            }
            if (!FJsonObjectConverter::JsonValueToUProperty(
                Field.Value, Property, Property->ContainerPtrToValuePtr<void>(Object)))
            {
                OutError = FString::Printf(
                    TEXT("The value for '%s' at %s cannot be converted to %s."),
                    *Field.Key, *Location, *Property->GetCPPType());
                return false;
            }
        }
        return true;
    }

    bool ResolveBuildPath(
        const FString& AssetPath,
        FString& OutPackagePath,
        FString& OutObjectPath,
        FString& OutAssetName,
        FString& OutError)
    {
        OutPackagePath = AssetPath.TrimStartAndEnd();
        if (!OutPackagePath.StartsWith(TEXT("/Game/MCPGenerated/")))
        {
            OutError = TEXT("audio_build is limited to /Game/MCPGenerated/.");
            return false;
        }
        if (OutPackagePath.Contains(TEXT("."))
            || !FPackageName::IsValidLongPackageName(OutPackagePath))
        {
            OutError = TEXT("asset_path must be a valid package path without an object suffix.");
            return false;
        }
        OutAssetName = FPackageName::GetLongPackageAssetName(OutPackagePath);
        OutObjectPath = OutPackagePath + TEXT(".") + OutAssetName;
        return true;
    }

    bool ParseNodeSpecs(
        const TSharedPtr<FJsonObject>& Request,
        TArray<FSoundNodeSpec>& OutSpecs,
        TMap<FString, int32>& OutById,
        FString& OutFirstNode,
        FString& OutError)
    {
        if (!Request->TryGetStringField(TEXT("first_node"), OutFirstNode)
            || OutFirstNode.TrimStartAndEnd().IsEmpty())
        {
            OutError = TEXT("first_node is required.");
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (!Request->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr
            || Nodes->Num() == 0 || Nodes->Num() > MaximumBuildNodes)
        {
            OutError = FString::Printf(
                TEXT("nodes must contain between 1 and %d entries."), MaximumBuildNodes);
            return false;
        }

        const TSet<FString> Allowed = {
            TEXT("id"), TEXT("type"), TEXT("children"), TEXT("sound_wave"), TEXT("properties") };
        for (int32 Index = 0; Index < Nodes->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject> Node = (*Nodes)[Index]->AsObject();
            if (!Node.IsValid())
            {
                OutError = FString::Printf(TEXT("nodes[%d] must be an object."), Index);
                return false;
            }
            if (!HasOnlyFields(Node, Allowed, FString::Printf(TEXT("nodes[%d]"), Index), OutError))
            {
                return false;
            }

            FSoundNodeSpec Spec;
            if (!Node->TryGetStringField(TEXT("id"), Spec.Id) || Spec.Id.IsEmpty()
                || !Node->TryGetStringField(TEXT("type"), Spec.Type) || Spec.Type.IsEmpty())
            {
                OutError = FString::Printf(TEXT("nodes[%d] requires id and type."), Index);
                return false;
            }
            FText InvalidReason;
            if (!FName(*Spec.Id).IsValidObjectName(InvalidReason))
            {
                OutError = FString::Printf(
                    TEXT("nodes[%d].id '%s' is not a valid UObject name: %s"),
                    Index, *Spec.Id, *InvalidReason.ToString());
                return false;
            }
            if (OutById.Contains(Spec.Id))
            {
                OutError = FString::Printf(TEXT("Duplicate sound node id '%s'."), *Spec.Id);
                return false;
            }
            if (SoundNodeClass(Spec.Type) == nullptr)
            {
                OutError = FString::Printf(TEXT("Unsupported sound node type '%s'."), *Spec.Type);
                return false;
            }

            const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
            if (Node->TryGetArrayField(TEXT("children"), Children) && Children != nullptr)
            {
                for (const TSharedPtr<FJsonValue>& Child : *Children)
                {
                    FString ChildId;
                    if (!Child.IsValid() || !Child->TryGetString(ChildId) || ChildId.IsEmpty())
                    {
                        OutError = FString::Printf(TEXT("nodes[%d].children must contain ids."), Index);
                        return false;
                    }
                    Spec.Children.Add(ChildId);
                }
            }
            Node->TryGetStringField(TEXT("sound_wave"), Spec.SoundWave);
            const TSharedPtr<FJsonObject>* Properties = nullptr;
            if (Node->TryGetObjectField(TEXT("properties"), Properties) && Properties != nullptr)
            {
                Spec.Properties = *Properties;
            }

            const int32 ChildCount = Spec.Children.Num();
            if (Spec.Type == TEXT("wave_player") && (ChildCount != 0 || Spec.SoundWave.IsEmpty()))
            {
                OutError = FString::Printf(
                    TEXT("Wave player '%s' requires sound_wave and cannot have children."), *Spec.Id);
                return false;
            }
            if (Spec.Type != TEXT("wave_player") && !Spec.SoundWave.IsEmpty())
            {
                OutError = FString::Printf(
                    TEXT("sound_wave is only valid on wave_player '%s'."), *Spec.Id);
                return false;
            }
            const bool bUnary = Spec.Type == TEXT("modulator") || Spec.Type == TEXT("delay")
                || Spec.Type == TEXT("looping");
            const bool bVariadic = Spec.Type == TEXT("mixer") || Spec.Type == TEXT("random")
                || Spec.Type == TEXT("concatenator");
            if ((bUnary && ChildCount != 1) || (bVariadic && (ChildCount < 1 || ChildCount > 32)))
            {
                OutError = FString::Printf(
                    TEXT("Node '%s' has %d children, which is invalid for type '%s'."),
                    *Spec.Id, ChildCount, *Spec.Type);
                return false;
            }
            OutById.Add(Spec.Id, OutSpecs.Num());
            OutSpecs.Add(MoveTemp(Spec));
        }

        if (!OutById.Contains(OutFirstNode))
        {
            OutError = FString::Printf(TEXT("first_node '%s' does not exist."), *OutFirstNode);
            return false;
        }

        TMap<FString, int32> ParentCounts;
        for (const FSoundNodeSpec& Spec : OutSpecs)
        {
            for (const FString& Child : Spec.Children)
            {
                if (!OutById.Contains(Child))
                {
                    OutError = FString::Printf(
                        TEXT("Node '%s' references missing child '%s'."), *Spec.Id, *Child);
                    return false;
                }
                ParentCounts.FindOrAdd(Child)++;
            }
        }
        for (const FSoundNodeSpec& Spec : OutSpecs)
        {
            const int32 ParentCount = ParentCounts.FindRef(Spec.Id);
            const int32 Expected = Spec.Id == OutFirstNode ? 0 : 1;
            if (ParentCount != Expected)
            {
                OutError = FString::Printf(
                    TEXT("Node '%s' has %d parents; a Sound Cue tree requires %d."),
                    *Spec.Id, ParentCount, Expected);
                return false;
            }
        }

        TSet<FString> Reached;
        TArray<FString> Pending = { OutFirstNode };
        while (Pending.Num() > 0)
        {
            const FString Id = Pending.Pop(false);
            if (Reached.Contains(Id))
            {
                OutError = FString::Printf(TEXT("The Sound Cue graph contains a cycle at '%s'."), *Id);
                return false;
            }
            Reached.Add(Id);
            Pending.Append(OutSpecs[OutById[Id]].Children);
        }
        if (Reached.Num() != OutSpecs.Num())
        {
            OutError = TEXT("Every sound node must be reachable from first_node.");
            return false;
        }
        return true;
    }

    bool PrepareNode(
        USoundNode* Node,
        const FSoundNodeSpec& Spec,
        FString& OutError)
    {
        Node->CreateStartingConnectors();
        while (Node->ChildNodes.Num() < Spec.Children.Num())
        {
            const int32 Before = Node->ChildNodes.Num();
            Node->InsertChildNode(Before);
            if (Node->ChildNodes.Num() == Before)
            {
                OutError = FString::Printf(
                    TEXT("Node '%s' refused child connector %d."), *Spec.Id, Before);
                return false;
            }
        }
        while (Node->ChildNodes.Num() > Spec.Children.Num())
        {
            const int32 Before = Node->ChildNodes.Num();
            Node->RemoveChildNode(Before - 1);
            if (Node->ChildNodes.Num() == Before)
            {
                OutError = FString::Printf(
                    TEXT("Node '%s' refused to remove child connector %d."), *Spec.Id, Before - 1);
                return false;
            }
        }
        return ApplyEditableProperties(
            Node, Spec.Properties, FString::Printf(TEXT("node '%s'"), *Spec.Id), OutError);
    }

    bool JsonEqual(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
    {
        return A.IsValid() && B.IsValid() && FJsonValue::CompareEqual(*A, *B);
    }

    bool InspectionMatches(
        const TSharedPtr<FJsonObject>& Inspection,
        const TArray<FSoundNodeSpec>& Specs,
        const FString& FirstNode,
        const TSharedPtr<FJsonObject>& CueProperties,
        FString& OutMismatch)
    {
        const TSharedPtr<FJsonObject>* Audio = nullptr;
        if (!Inspection.IsValid() || !Inspection->TryGetObjectField(TEXT("audio"), Audio)
            || Audio == nullptr || !Audio->IsValid())
        {
            OutMismatch = TEXT("audio_inspect returned no audio snapshot.");
            return false;
        }
        if (CueProperties.IsValid())
        {
            const TSharedPtr<FJsonObject>* ActualProperties = nullptr;
            if (!(*Audio)->TryGetObjectField(TEXT("properties"), ActualProperties)
                || ActualProperties == nullptr)
            {
                OutMismatch = TEXT("The Sound Cue has no property snapshot.");
                return false;
            }
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : CueProperties->Values)
            {
                const TSharedPtr<FJsonValue>* ActualValue = (*ActualProperties)->Values.Find(Field.Key);
                if (ActualValue == nullptr || !JsonEqual(*ActualValue, Field.Value))
                {
                    OutMismatch = FString::Printf(
                        TEXT("Sound Cue property '%s' did not read back as requested."), *Field.Key);
                    return false;
                }
            }
        }
        FString ActualFirst;
        (*Audio)->TryGetStringField(TEXT("first_node"), ActualFirst);
        if (ActualFirst != FirstNode)
        {
            OutMismatch = FString::Printf(
                TEXT("first_node read back as '%s', expected '%s'."), *ActualFirst, *FirstNode);
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (!(*Audio)->TryGetArrayField(TEXT("sound_nodes"), Nodes) || Nodes == nullptr
            || Nodes->Num() != Specs.Num())
        {
            OutMismatch = TEXT("The sound node count did not match the request.");
            return false;
        }

        TMap<FString, TSharedPtr<FJsonObject>> ActualById;
        for (const TSharedPtr<FJsonValue>& Value : *Nodes)
        {
            const TSharedPtr<FJsonObject> Node = Value->AsObject();
            FString Id;
            if (Node.IsValid() && Node->TryGetStringField(TEXT("id"), Id))
            {
                ActualById.Add(Id, Node);
            }
        }
        for (const FSoundNodeSpec& Spec : Specs)
        {
            const TSharedPtr<FJsonObject>* ActualPtr = ActualById.Find(Spec.Id);
            if (ActualPtr == nullptr || !ActualPtr->IsValid())
            {
                OutMismatch = FString::Printf(TEXT("Node '%s' was not read back."), *Spec.Id);
                return false;
            }
            const TSharedPtr<FJsonObject>& Actual = *ActualPtr;
            FString ClassPath;
            Actual->TryGetStringField(TEXT("class_path"), ClassPath);
            if (ClassPath != SoundNodeClass(Spec.Type)->GetPathName())
            {
                OutMismatch = FString::Printf(TEXT("Node '%s' has the wrong class."), *Spec.Id);
                return false;
            }
            if (Spec.Type == TEXT("wave_player"))
            {
                FString ActualWave;
                Actual->TryGetStringField(TEXT("sound_wave"), ActualWave);
                if (ActualWave != Spec.SoundWave)
                {
                    OutMismatch = FString::Printf(
                        TEXT("Node '%s' references '%s', expected '%s'."),
                        *Spec.Id, *ActualWave, *Spec.SoundWave);
                    return false;
                }
            }
            const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
            if (!Actual->TryGetArrayField(TEXT("children"), Children) || Children == nullptr
                || Children->Num() != Spec.Children.Num())
            {
                OutMismatch = FString::Printf(TEXT("Node '%s' has the wrong child count."), *Spec.Id);
                return false;
            }
            for (int32 Index = 0; Index < Spec.Children.Num(); ++Index)
            {
                if ((*Children)[Index]->AsString() != Spec.Children[Index])
                {
                    OutMismatch = FString::Printf(TEXT("Node '%s' has the wrong child order."), *Spec.Id);
                    return false;
                }
            }
            const TSharedPtr<FJsonObject>* ActualProperties = nullptr;
            if (Spec.Properties.IsValid()
                && (!Actual->TryGetObjectField(TEXT("properties"), ActualProperties)
                    || ActualProperties == nullptr))
            {
                OutMismatch = FString::Printf(TEXT("Node '%s' has no property snapshot."), *Spec.Id);
                return false;
            }
            if (Spec.Properties.IsValid())
            {
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Spec.Properties->Values)
                {
                    const TSharedPtr<FJsonValue>* ActualValue = (*ActualProperties)->Values.Find(Field.Key);
                    if (ActualValue == nullptr || !JsonEqual(*ActualValue, Field.Value))
                    {
                        OutMismatch = FString::Printf(
                            TEXT("Node '%s' property '%s' did not read back as requested."),
                            *Spec.Id, *Field.Key);
                        return false;
                    }
                }
            }
        }
        return true;
    }
}

bool UMCPPuerTSBridgeService::BuildSoundCueJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError)
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("audio_build request must be a JSON object.");
        return false;
    }
    const TSet<FString> AllowedRequest = {
        TEXT("asset_path"), TEXT("first_node"), TEXT("nodes"), TEXT("properties"),
        TEXT("plan_only"), TEXT("save") };
    if (!HasOnlyFields(Request, AllowedRequest, TEXT("request"), OutError)) { return false; }

    FString RequestedPath;
    Request->TryGetStringField(TEXT("asset_path"), RequestedPath);
    FString PackagePath, ObjectPath, AssetName;
    if (!ResolveBuildPath(RequestedPath, PackagePath, ObjectPath, AssetName, OutError)) { return false; }

    TArray<FSoundNodeSpec> Specs;
    TMap<FString, int32> ById;
    FString FirstNode;
    if (!ParseNodeSpecs(Request, Specs, ById, FirstNode, OutError)) { return false; }

    const TSharedPtr<FJsonObject>* CuePropertiesPtr = nullptr;
    TSharedPtr<FJsonObject> CueProperties;
    if (Request->TryGetObjectField(TEXT("properties"), CuePropertiesPtr) && CuePropertiesPtr != nullptr)
    {
        CueProperties = *CuePropertiesPtr;
    }

    USoundCue* ScratchCue = NewObject<USoundCue>(GetTransientPackage());
    if (!ApplyEditableProperties(ScratchCue, CueProperties, TEXT("asset properties"), OutError))
    {
        return false;
    }
    for (const FSoundNodeSpec& Spec : Specs)
    {
        USoundNode* Scratch = NewObject<USoundNode>(GetTransientPackage(), SoundNodeClass(Spec.Type));
        if (!ApplyEditableProperties(
            Scratch, Spec.Properties, FString::Printf(TEXT("node '%s'"), *Spec.Id), OutError))
        {
            return false;
        }
        if (Spec.Type == TEXT("wave_player"))
        {
            USoundWave* Wave = LoadObject<USoundWave>(nullptr, *Spec.SoundWave);
            if (Wave == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("No Sound Wave was found at '%s' for node '%s'."),
                    *Spec.SoundWave, *Spec.Id);
                return false;
            }
        }
    }

    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData ExistingData = AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath));
    USoundCue* Cue = ExistingData.IsValid() ? Cast<USoundCue>(ExistingData.GetAsset()) : nullptr;
    if (ExistingData.IsValid() && Cue == nullptr)
    {
        OutError = FString::Printf(TEXT("The asset at '%s' is not a Sound Cue."), *ObjectPath);
        return false;
    }
    const bool bCreating = Cue == nullptr;

    FString BeforeHash;
    bool bAlreadyMatches = false;
    if (!bCreating)
    {
        TSharedPtr<FJsonObject> InspectRequest = MakeShared<FJsonObject>();
        InspectRequest->SetStringField(TEXT("asset_path"), PackagePath);
        FString InspectJson, InspectError;
        TSharedPtr<FJsonObject> Inspection;
        if (InspectAudioAssetJson(SerializeJson(InspectRequest), InspectJson, InspectError))
        {
            TSharedRef<TJsonReader<>> InspectReader = TJsonReaderFactory<>::Create(InspectJson);
            FJsonSerializer::Deserialize(InspectReader, Inspection);
            if (Inspection.IsValid())
            {
                Inspection->TryGetStringField(TEXT("structure_hash_sha1"), BeforeHash);
                FString Mismatch;
                bAlreadyMatches = InspectionMatches(Inspection, Specs, FirstNode, CueProperties, Mismatch);
            }
        }
        else
        {
            OutError = FString::Printf(
                TEXT("The existing cue could not be inspected before editing: %s"), *InspectError);
            return false;
        }
    }

    const bool bPlanOnly = Request->HasField(TEXT("plan_only"))
        && Request->GetBoolField(TEXT("plan_only"));
    if (bPlanOnly || bAlreadyMatches)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), PackagePath);
        Result->SetStringField(TEXT("object_path"), ObjectPath);
        Result->SetBoolField(TEXT("plan_only"), bPlanOnly);
        Result->SetBoolField(TEXT("would_create"), bCreating);
        Result->SetNumberField(TEXT("expected_change_count"), bAlreadyMatches ? 0 : 1);
        Result->SetBoolField(TEXT("unchanged"), bAlreadyMatches);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetStringField(TEXT("structure_hash_sha1"), BeforeHash);
        OutResultJson = SerializeJson(Result);
        return true;
    }
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("audio_build requires an active transaction.");
        return false;
    }

    FBridgeAssetRollback CreationRollback;
    CreationRollback.Snapshot(PackagePath);
    const TArray<FString> DirtyBefore = CreationRollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = CreationRollback.SourceControlState();
    FBridgeContentSnapshot UpdateSnapshot;
    if (!bCreating)
    {
        FString SnapshotRefusal;
        if (!UpdateSnapshot.Capture(PackagePath, SnapshotRefusal))
        {
            OutError = SnapshotRefusal;
            return false;
        }
    }

    auto Fail = [&](const FString& Error) -> bool
    {
        ActiveTransaction->Cancel();
        bool bRollbackSucceeded = false;
        TSharedPtr<FJsonObject> Evidence;
        if (bCreating)
        {
            CreationRollback.Rollback();
            bRollbackSucceeded = !AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath)).IsValid();
            Evidence = CreationRollback.BuildEvidence(DirtyBefore, SourceControlBefore);
        }
        else
        {
            FString RestoreError;
            const bool bRestored = UpdateSnapshot.Restore(RestoreError);
            FString HashAfter;
            TSharedPtr<FJsonObject> InspectRequest = MakeShared<FJsonObject>();
            InspectRequest->SetStringField(TEXT("asset_path"), PackagePath);
            FString InspectJson, InspectError;
            if (InspectAudioAssetJson(SerializeJson(InspectRequest), InspectJson, InspectError))
            {
                TSharedPtr<FJsonObject> Inspection;
                TSharedRef<TJsonReader<>> InspectReader = TJsonReaderFactory<>::Create(InspectJson);
                if (FJsonSerializer::Deserialize(InspectReader, Inspection) && Inspection.IsValid())
                {
                    Inspection->TryGetStringField(TEXT("structure_hash_sha1"), HashAfter);
                }
            }
            bRollbackSucceeded = bRestored && !BeforeHash.IsEmpty() && HashAfter == BeforeHash;
            Evidence = UpdateSnapshot.BuildEvidence();
        }
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), PackagePath);
        Result->SetBoolField(TEXT("built"), false);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetBoolField(TEXT("rollback_succeeded"), bRollbackSucceeded);
        Result->SetStringField(TEXT("rollback_verified_by"), TEXT("audio_inspect"));
        Result->SetStringField(TEXT("error"), Error);
        Result->SetObjectField(TEXT("rollback"), Evidence);
        OutResultJson = SerializeJson(Result);
        OutError = Error;
        return false;
    };

    if (bCreating)
    {
        UPackage* Package = CreatePackage(*PackagePath);
        Cue = Package != nullptr ? NewObject<USoundCue>(
            Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional) : nullptr;
        if (Cue == nullptr) { return Fail(TEXT("Unreal could not create the Sound Cue.")); }
        Cue->CreateGraph();
        CreationRollback.TrackCreated(Cue);
        AssetRegistry.Get().AssetCreated(Cue);
    }

    if (!Cue->Modify())
    {
        return Fail(TEXT("Sound Cue Modify() returned false; no authoring writes were attempted."));
    }
    TArray<USoundNode*> OldNodes = Cue->AllNodes;
    for (USoundNode* OldNode : OldNodes)
    {
        if (OldNode != nullptr && !OldNode->Modify())
        {
            return Fail(FString::Printf(
                TEXT("Sound node '%s' Modify() returned false; no graph replacement was attempted."),
                *OldNode->GetName()));
        }
    }
    Cue->ResetGraph();
    for (USoundNode* OldNode : OldNodes)
    {
        if (OldNode != nullptr)
        {
            OldNode->Rename(nullptr, GetTransientPackage(),
                REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
        }
    }
    if (!ApplyEditableProperties(Cue, CueProperties, TEXT("asset properties"), OutError))
    {
        return Fail(OutError);
    }

    TMap<FString, USoundNode*> Built;
    for (const FSoundNodeSpec& Spec : Specs)
    {
        USoundNode* Node = NewObject<USoundNode>(
            Cue, SoundNodeClass(Spec.Type), FName(*Spec.Id), RF_Transactional);
        if (Node == nullptr) { return Fail(FString::Printf(TEXT("Could not create node '%s'."), *Spec.Id)); }
        Cue->AllNodes.Add(Node);
        Cue->SetupSoundNode(Node, false);
        if (!PrepareNode(Node, Spec, OutError)) { return Fail(OutError); }
        if (Spec.Type == TEXT("wave_player"))
        {
            USoundWave* Wave = LoadObject<USoundWave>(nullptr, *Spec.SoundWave);
            CastChecked<USoundNodeWavePlayer>(Node)->SetSoundWave(Wave);
        }
        Built.Add(Spec.Id, Node);
    }
    for (const FSoundNodeSpec& Spec : Specs)
    {
        USoundNode* Node = Built[Spec.Id];
        for (int32 Index = 0; Index < Spec.Children.Num(); ++Index)
        {
            Node->ChildNodes[Index] = Built[Spec.Children[Index]];
        }
    }
    Cue->FirstNode = Built[FirstNode];
    Cue->LinkGraphNodesFromSoundNodes();
    Cue->PostEditChange();
    Cue->MarkPackageDirty();

    TSharedPtr<FJsonObject> InspectRequest = MakeShared<FJsonObject>();
    InspectRequest->SetStringField(TEXT("asset_path"), PackagePath);
    FString InspectJson, InspectError;
    if (!InspectAudioAssetJson(SerializeJson(InspectRequest), InspectJson, InspectError))
    {
        return Fail(FString::Printf(TEXT("audio_inspect failed after the build: %s"), *InspectError));
    }
    TSharedPtr<FJsonObject> Inspection;
    TSharedRef<TJsonReader<>> InspectReader = TJsonReaderFactory<>::Create(InspectJson);
    if (!FJsonSerializer::Deserialize(InspectReader, Inspection) || !Inspection.IsValid())
    {
        return Fail(TEXT("audio_inspect returned invalid JSON after the build."));
    }
    FString Mismatch;
    if (!InspectionMatches(Inspection, Specs, FirstNode, CueProperties, Mismatch))
    {
        return Fail(FString::Printf(TEXT("Sound Cue read-back mismatch: %s"), *Mismatch));
    }

    FString StructureHash;
    Inspection->TryGetStringField(TEXT("structure_hash_sha1"), StructureHash);
    const bool bSave = !Request->HasField(TEXT("save")) || Request->GetBoolField(TEXT("save"));
    bool bSaved = false;
    FString SaveError;
    if (bSave)
    {
        bSaved = SaveProjectAsset(ObjectPath, SaveError);
    }
    CreationRollback.Commit();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), ObjectPath);
    Result->SetBoolField(TEXT("built"), true);
    Result->SetBoolField(TEXT("created"), bCreating);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetBoolField(TEXT("verified"), true);
    Result->SetStringField(TEXT("verified_by"), TEXT("audio_inspect"));
    Result->SetStringField(TEXT("structure_hash_sha1_before"), BeforeHash);
    Result->SetStringField(TEXT("structure_hash_sha1"), StructureHash);
    Result->SetNumberField(TEXT("sound_node_count"), Specs.Num());
    if (!SaveError.IsEmpty()) { Result->SetStringField(TEXT("save_warning"), SaveError); }
    OutResultJson = SerializeJson(Result);
    return true;
}