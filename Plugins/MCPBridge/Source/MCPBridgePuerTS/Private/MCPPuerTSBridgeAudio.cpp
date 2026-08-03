// Copyright 2026 RareBird Games. All Rights Reserved.
//
// audio_inspect: the first native command in the audio domain, which had
// nothing at all. The legacy catalog's only audio entry was
// audio_component_add, a Blueprint component write with no way to read what it
// pointed at.
//
// Read half first, as every domain in this program has done. The write half is
// not here, and unlike Environment Queries that is a scheduling decision rather
// than an impossibility: see the note above InspectAudioAssetJson for what a
// cue builder would need and what the engine already gives it.
//
// Every API used here was confirmed against the installed UE4.27 source at
// D:/UE/UE_4.27 before it was written.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "MCPBridgeBlueprintMembers.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
    /** How many sound nodes one cue may carry before this refuses to walk it.
        A cue that large is a data problem rather than a request, and a runaway
        traversal on the game thread is the failure mode that is hardest to
        diagnose from the other end of a pipe. */
    constexpr int32 MaximumSoundNodes = 500;

    bool ResolveAudioAssetPaths(
        const FString& AssetPath,
        FString& OutObjectPath,
        FString& OutPackagePath,
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
            OutError = TEXT("Audio inspection is limited to /Game and /Engine.");
            return false;
        }
        if (Trimmed.Contains(TEXT(".")))
        {
            OutObjectPath = Trimmed;
            OutPackagePath = FPackageName::ObjectPathToPackageName(Trimmed);
            return true;
        }
        if (!FPackageName::IsValidLongPackageName(Trimmed))
        {
            OutError = FString::Printf(TEXT("'%s' is not a valid package path."), *Trimmed);
            return false;
        }
        OutPackagePath = Trimmed;
        OutObjectPath = Trimmed + TEXT(".") + FPackageName::GetLongPackageAssetName(Trimmed);
        return true;
    }

    /**
     * Every EditAnywhere property on an object, by reflected name.
     *
     * By reflection rather than a hand-written field list because the two sound
     * asset types and roughly thirty USoundNode subclasses do not share a
     * useful base beyond USoundBase, and a list would have to be extended for
     * every node class the project uses. A property that cannot be converted is
     * named in OutUnsupported instead of being dropped, so a caller can tell a
     * field this cannot read from a field the asset does not have.
     */
    TSharedPtr<FJsonObject> EditablePropertiesToJson(
        const UObject* Object,
        const FString& Prefix,
        TArray<TSharedPtr<FJsonValue>>& OutUnsupported)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        if (Object == nullptr) { return Out; }
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        {
            FProperty* Property = *It;
            if (Property == nullptr || !Property->HasAnyPropertyFlags(CPF_Edit)) { continue; }
            const TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(
                Property, Property->ContainerPtrToValuePtr<void>(Object), 0, 0);
            if (Value.IsValid())
            {
                Out->SetField(Property->GetName(), Value);
            }
            else
            {
                OutUnsupported.Add(MakeShared<FJsonValueString>(
                    Prefix + TEXT(".") + Property->GetName()));
            }
        }
        return Out;
    }

    /** Depth-first walk from a cue's FirstNode, collecting every reachable node
        once. Returns false when the cue is larger than MaximumSoundNodes. */
    bool CollectSoundNodes(USoundNode* Root, TArray<USoundNode*>& OutNodes)
    {
        if (Root == nullptr) { return true; }
        TArray<USoundNode*> Pending;
        Pending.Add(Root);
        while (Pending.Num() > 0)
        {
            USoundNode* Node = Pending.Pop(false);
            if (Node == nullptr || OutNodes.Contains(Node)) { continue; }
            if (OutNodes.Num() >= MaximumSoundNodes) { return false; }
            OutNodes.Add(Node);
            for (USoundNode* Child : Node->ChildNodes)
            {
                if (Child != nullptr) { Pending.Add(Child); }
            }
        }
        return true;
    }
}

// ---------------------------------------------------------------------------
// audio_inspect
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::InspectAudioAssetJson(
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
    FString ObjectPath, PackagePath;
    if (!ResolveAudioAssetPaths(AssetPath, ObjectPath, PackagePath, OutError)) { return false; }

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData AssetData =
        AssetRegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath));
    if (!AssetData.IsValid())
    {
        OutError = FString::Printf(TEXT("No asset was found at '%s'."), *ObjectPath);
        return false;
    }
    USoundBase* Sound = Cast<USoundBase>(AssetData.GetAsset());
    if (Sound == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The asset at '%s' is not a USoundBase. This reads Sound Cues and Sound Waves; "
                 "a Sound Class, Sound Mix or Attenuation asset is a different type and is not "
                 "covered."),
            *ObjectPath);
        return false;
    }

    const UPackage* Package = Sound->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    TArray<TSharedPtr<FJsonValue>> Warnings;
    TArray<TSharedPtr<FJsonValue>> Unsupported;

    // The canonical snapshot: everything the hash covers, and nothing that
    // describes the request or the read rather than the asset.
    TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
    Snapshot->SetStringField(TEXT("asset_path"), ObjectPath);
    Snapshot->SetStringField(TEXT("class_path"), Sound->GetClass()->GetPathName());
    Snapshot->SetNumberField(TEXT("duration_seconds"), Sound->Duration);
    Snapshot->SetStringField(TEXT("sound_class"),
        Sound->SoundClassObject != nullptr ? Sound->SoundClassObject->GetPathName() : FString());
    Snapshot->SetStringField(TEXT("attenuation_settings"),
        Sound->AttenuationSettings != nullptr ? Sound->AttenuationSettings->GetPathName() : FString());
    Snapshot->SetObjectField(TEXT("properties"),
        EditablePropertiesToJson(Sound, TEXT("asset"), Unsupported));

    // A cue's node graph. FirstNode and ChildNodes are the source of truth and
    // SoundCueGraph is derived from them, which is the opposite of an
    // Environment Query: USoundCue::LinkGraphNodesFromSoundNodes builds the
    // editor graph FROM the nodes, and the engine's own factories call it after
    // constructing nodes programmatically (Editor/AudioEditor/Private/Factories/
    // SoundCueFactoryNew.cpp:42). So the graph reported here is the graph that
    // plays, not a compiled artifact that a human opening the asset would
    // overwrite.
    TArray<TSharedPtr<FJsonValue>> NodeEntries;
    USoundCue* Cue = Cast<USoundCue>(Sound);
    if (Cue != nullptr)
    {
        TArray<USoundNode*> Nodes;
        if (!CollectSoundNodes(Cue->FirstNode, Nodes))
        {
            OutError = FString::Printf(
                TEXT("This Sound Cue has more than %d reachable sound nodes, which this refuses "
                     "to walk rather than occupying the game thread indefinitely."),
                MaximumSoundNodes);
            return false;
        }

        for (const USoundNode* Node : Nodes)
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            // The node's object name. Stable within the asset and chosen by the
            // engine, so it is an observed identity rather than a derived one.
            Entry->SetStringField(TEXT("id"), Node->GetName());
            Entry->SetStringField(TEXT("class_path"), Node->GetClass()->GetPathName());
            Entry->SetStringField(TEXT("title"), Node->GetTitle().ToString());
            Entry->SetBoolField(TEXT("is_first_node"), Node == Cue->FirstNode);

            // Child order is NOT sorted, unlike the node list. For a Mixer, a
            // Random or a Concatenator the index IS the meaning, so sorting
            // children would canonicalise away the thing the graph encodes.
            TArray<TSharedPtr<FJsonValue>> Children;
            for (const USoundNode* Child : Node->ChildNodes)
            {
                Children.Add(MakeShared<FJsonValueString>(
                    Child != nullptr ? Child->GetName() : FString()));
            }
            Entry->SetArrayField(TEXT("children"), Children);
            Entry->SetObjectField(TEXT("properties"),
                EditablePropertiesToJson(Node, Node->GetName(), Unsupported));
            NodeEntries.Add(MakeShared<FJsonValueObject>(Entry));
        }

        // Canonical order, with the shared sort, for the reason
        // MCPBridgeBlueprintMembers exists: the engine's stored order is the
        // order the nodes happen to be in, so a hash over it would move without
        // the cue changing.
        MCPBridgeBlueprintMembers::SortByStringField(NodeEntries, TEXT("id"));
        Snapshot->SetStringField(TEXT("first_node"),
            Cue->FirstNode != nullptr ? Cue->FirstNode->GetName() : FString());
        Snapshot->SetArrayField(TEXT("sound_nodes"), NodeEntries);

        if (Cue->FirstNode == nullptr)
        {
            Warnings.Add(MakeShared<FJsonValueString>(
                TEXT("This Sound Cue has no FirstNode, so it plays nothing. That is an authoring "
                     "gap in the asset, not a read failure.")));
        }
#if WITH_EDITORONLY_DATA
        // AllNodes is the editor's bookkeeping list and can hold nodes that no
        // longer hang off FirstNode. Reporting the difference rather than
        // either list alone, because an orphan is exactly the thing a caller
        // cannot see from the graph and cannot hear from the cue. It is
        // WITH_EDITORONLY_DATA (Sound/SoundCue.h:110-116), so the guard is not
        // decoration even though this module is editor-only.
        const int32 Orphans = Cue->AllNodes.Num() - NodeEntries.Num();
        if (Orphans > 0)
        {
            Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("%d node(s) in AllNodes are not reachable from FirstNode. They are stored in "
                     "the asset, do not play, and are not listed above."),
                Orphans)));
        }
#endif
    }
    else if (Cast<USoundWave>(Sound) == nullptr)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This is a USoundBase that is neither a Sound Cue nor a Sound Wave. The common "
                 "fields and every EditAnywhere property are reported; there is no type-specific "
                 "section for it.")));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("audio"), Snapshot);
    Result->SetStringField(TEXT("asset_path"), ObjectPath);
    Result->SetStringField(TEXT("package_path"), PackagePath);
    Result->SetNumberField(TEXT("sound_node_count"), NodeEntries.Num());
    Result->SetBoolField(TEXT("is_sound_cue"), Cue != nullptr);
    Result->SetBoolField(TEXT("is_sound_wave"), Cast<USoundWave>(Sound) != nullptr);
    Result->SetStringField(TEXT("structure_hash_sha1"),
        MCPBridgeBlueprintMembers::StructureHash(Snapshot));
    Result->SetStringField(TEXT("build_unsupported_reason"),
        TEXT("There is no audio_build yet, and unlike eqs_inspect that is scheduling rather than "
             "an engine limit. USoundCue::FirstNode and USoundNode::ChildNodes are the source of "
             "truth and USoundCue::LinkGraphNodesFromSoundNodes derives the editor graph from "
             "them, so a builder could author nodes and then call it, which is what the engine's "
             "own SoundCueFactoryNew does. What was missing was failure atomicity: replacing an "
             "existing cue's node graph needs a content snapshot the rollback boundary can "
             "restore. That gap is now closed generically - FBridgeContentSnapshot in this "
             "module snapshots any saved asset by its file on disk and restores it with "
             "UPackageTools::ReloadPackages, which is what unblocked anim_blueprint_patch - so "
             "what remains here is scheduling rather than an engine limit."));
    Result->SetArrayField(TEXT("unsupported_fields"), Unsupported);
    Result->SetArrayField(TEXT("warnings"), Warnings);

    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this asset dirtied its package, which a read must not do. Report it.")));
        Result->SetArrayField(TEXT("warnings"), Warnings);
    }

    OutResultJson = SerializeJson(Result);
    return true;
}
