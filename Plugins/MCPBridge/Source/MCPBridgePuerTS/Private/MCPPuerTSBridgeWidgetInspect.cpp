// Copyright 2026 RareBird Games. All Rights Reserved.
//
// Read-only Widget Blueprint inspection: the independent read half of
// widget_build, in the same shape graph_inspect and behavior_tree_inspect use.
//
// Why it exists: widget_build's response was previously the only evidence that
// a widget tree landed, so "the build reported four widgets" and "the asset
// contains four widgets" were the same claim. This walks the saved UWidgetTree
// instead, so a desired spec can be compared against actual saved state without
// trusting the builder that wrote it.
//
// UE4.27 UMG widgets carry no GUID, so node identity here is DERIVED from the
// traversal path (parent id / child index : class : name), labeled
// identity_kind = "derived", exactly as the Behavior Tree inspector does. A
// renamed or reordered widget is a different identity on purpose: the identity
// IS the structure.
//
// READ ONLY, and that is the contract rather than a hope: widget_inspect is
// absent from IsToolMutating so no transaction opens, nothing here calls
// Modify or MarkPackageDirty, and the package dirty flag is read before and
// after the walk and reported both ways.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
// MovieScene is a public dependency of UMG (UWidgetAnimation derives from
// UMovieSceneSequence), so these need no Build.cs change. Read only: the track
// list is counted and named, never opened.
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneTrack.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
    struct FWidgetWalkContext
    {
        TArray<TSharedPtr<FJsonValue>> UnsupportedFields;
        FString HashLines;
        int32 WidgetCount = 0;
        int32 PanelCount = 0;
        int32 NamedSlotCount = 0;
        int32 Depth = 0;
    };

    /** Structural fields are the tree itself and are reported as hierarchy
        rather than as properties; serializing them here would duplicate the
        walk and, for Slot, recurse back up into the parent. */
    bool IsStructuralWidgetField(const FName& Name)
    {
        static const TSet<FName> Structural = {
            FName(TEXT("Slot")), FName(TEXT("Children")), FName(TEXT("RootWidget")),
            FName(TEXT("WidgetTree")), FName(TEXT("Content")),
        };
        return Structural.Contains(Name);
    }

    /** Every editable property of an object, through FJsonObjectConverter.
        A property the converter cannot express is recorded in
        unsupported_fields rather than silently dropped, so an unreadable field
        stays visible in structured output instead of looking like an absent
        one. */
    TSharedPtr<FJsonObject> DescribeProperties(
        const UObject* Object,
        const FString& OwnerId,
        FWidgetWalkContext& Context)
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
                || IsStructuralWidgetField(Property->GetFName()))
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

    /** The slot a widget occupies in its parent. CanvasPanelSlot is broken out
        because its anchors, offsets, alignment and z-order are applied by a
        different code path than widget construction: a tree that built with
        every slot silently at the origin would otherwise read as a success. */
    TSharedPtr<FJsonObject> DescribeSlot(
        const UPanelSlot* Slot,
        const FString& OwnerId,
        FWidgetWalkContext& Context)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        if (Slot == nullptr)
        {
            Out->SetStringField(TEXT("class"), FString());
            return Out;
        }
        Out->SetStringField(TEXT("class"), Slot->GetClass()->GetName());
        Out->SetStringField(TEXT("class_path"), Slot->GetClass()->GetPathName());

        if (const UCanvasPanelSlot* Canvas = Cast<UCanvasPanelSlot>(Slot))
        {
            const FAnchorData& Layout = Canvas->LayoutData;
            TSharedPtr<FJsonObject> Anchors = MakeShared<FJsonObject>();
            Anchors->SetNumberField(TEXT("minimum_x"), Layout.Anchors.Minimum.X);
            Anchors->SetNumberField(TEXT("minimum_y"), Layout.Anchors.Minimum.Y);
            Anchors->SetNumberField(TEXT("maximum_x"), Layout.Anchors.Maximum.X);
            Anchors->SetNumberField(TEXT("maximum_y"), Layout.Anchors.Maximum.Y);
            Out->SetObjectField(TEXT("anchors"), Anchors);

            TSharedPtr<FJsonObject> Offsets = MakeShared<FJsonObject>();
            Offsets->SetNumberField(TEXT("left"), Layout.Offsets.Left);
            Offsets->SetNumberField(TEXT("top"), Layout.Offsets.Top);
            Offsets->SetNumberField(TEXT("right"), Layout.Offsets.Right);
            Offsets->SetNumberField(TEXT("bottom"), Layout.Offsets.Bottom);
            Out->SetObjectField(TEXT("offsets"), Offsets);

            TSharedPtr<FJsonObject> Alignment = MakeShared<FJsonObject>();
            Alignment->SetNumberField(TEXT("x"), Layout.Alignment.X);
            Alignment->SetNumberField(TEXT("y"), Layout.Alignment.Y);
            Out->SetObjectField(TEXT("alignment"), Alignment);

            Out->SetNumberField(TEXT("z_order"), Canvas->GetZOrder());
            Out->SetBoolField(TEXT("auto_size"), Canvas->bAutoSize);

            // The position/size pair widget_build's own report uses, so the two
            // responses can be compared field for field without a conversion.
            TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
            Position->SetNumberField(TEXT("x"), Layout.Offsets.Left);
            Position->SetNumberField(TEXT("y"), Layout.Offsets.Top);
            Out->SetObjectField(TEXT("position"), Position);
            TSharedPtr<FJsonObject> Size = MakeShared<FJsonObject>();
            Size->SetNumberField(TEXT("x"), Layout.Offsets.Right);
            Size->SetNumberField(TEXT("y"), Layout.Offsets.Bottom);
            Out->SetObjectField(TEXT("size"), Size);
        }

        Out->SetObjectField(TEXT("properties"),
            DescribeProperties(Slot, OwnerId + TEXT(".slot"), Context));
        return Out;
    }

    TSharedPtr<FJsonObject> DescribeWidgetNode(
        UWidget* Widget,
        const FString& NodeId,
        FWidgetWalkContext& Context);

    /** Named slots (UNamedSlot and anything else implementing
        INamedSlotInterface) hold content that is NOT reachable through
        UPanelWidget::GetChildAt, so a walk that only followed panel children
        would silently omit it. */
    TArray<TSharedPtr<FJsonValue>> DescribeNamedSlots(
        UWidget* Widget,
        const FString& NodeId,
        FWidgetWalkContext& Context)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Widget);
        if (NamedSlotHost == nullptr)
        {
            return Out;
        }
        TArray<FName> SlotNames;
        NamedSlotHost->GetSlotNames(SlotNames);
        SlotNames.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
        for (const FName& SlotName : SlotNames)
        {
            Context.NamedSlotCount++;
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("slot_name"), SlotName.ToString());
            UWidget* Content = NamedSlotHost->GetContentForSlot(SlotName);
            Context.HashLines += NodeId + TEXT("|namedslot|") + SlotName.ToString() + TEXT("\n");
            if (Content != nullptr)
            {
                const FString ContentId = FString::Printf(TEXT("%s/slot:%s:%s:%s"),
                    *NodeId, *SlotName.ToString(),
                    *Content->GetClass()->GetName(), *Content->GetName());
                Entry->SetObjectField(TEXT("content"),
                    DescribeWidgetNode(Content, ContentId, Context));
            }
            else
            {
                Entry->SetField(TEXT("content"), MakeShared<FJsonValueNull>());
            }
            Out.Add(MakeShared<FJsonValueObject>(Entry));
        }
        return Out;
    }

    TSharedPtr<FJsonObject> DescribeWidgetNode(
        UWidget* Widget,
        const FString& NodeId,
        FWidgetWalkContext& Context)
    {
        TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
        if (Widget == nullptr)
        {
            return Node;
        }
        Context.WidgetCount++;
        Node->SetStringField(TEXT("id"), NodeId);
        Node->SetStringField(TEXT("identity"), TEXT("derived"));
        Node->SetStringField(TEXT("name"), Widget->GetName());
        Node->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
        Node->SetStringField(TEXT("class_path"), Widget->GetClass()->GetPathName());
        // bIsVariable is what makes a widget reachable as a member of the
        // generated class, which is the difference between a decorative element
        // and one a graph can drive.
        Node->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);
        Node->SetObjectField(TEXT("slot"), DescribeSlot(Widget->Slot, NodeId, Context));
        Node->SetObjectField(TEXT("properties"), DescribeProperties(Widget, NodeId, Context));

        // The canonical structure line: identity, class, name, variable flag,
        // in traversal order. Properties are deliberately not hashed - this is
        // a STRUCTURE hash, so a text change does not read as a reshape.
        Context.HashLines += NodeId + TEXT("|") + Widget->GetClass()->GetPathName()
            + TEXT("|") + Widget->GetName()
            + TEXT("|") + (Widget->bIsVariable ? TEXT("var") : TEXT("novar")) + TEXT("\n");

        const TArray<TSharedPtr<FJsonValue>> NamedSlots =
            DescribeNamedSlots(Widget, NodeId, Context);
        Node->SetArrayField(TEXT("named_slots"), NamedSlots);

        TArray<TSharedPtr<FJsonValue>> Children;
        if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
        {
            Context.PanelCount++;
            Node->SetBoolField(TEXT("is_panel"), true);
            if (Context.Depth >= 64)
            {
                Context.UnsupportedFields.Add(MakeShared<FJsonValueString>(
                    FString::Printf(TEXT("%s.children (depth limit 64 reached)"), *NodeId)));
            }
            else
            {
                Context.Depth++;
                for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
                {
                    UWidget* Child = Panel->GetChildAt(Index);
                    if (Child == nullptr) { continue; }
                    // Child order is part of the identity, so the index is in
                    // the id rather than only in the array position.
                    const FString ChildId = FString::Printf(TEXT("%s/%d:%s:%s"),
                        *NodeId, Index, *Child->GetClass()->GetName(), *Child->GetName());
                    TSharedPtr<FJsonObject> ChildJson =
                        DescribeWidgetNode(Child, ChildId, Context);
                    ChildJson->SetNumberField(TEXT("child_index"), Index);
                    Children.Add(MakeShared<FJsonValueObject>(ChildJson));
                }
                Context.Depth--;
            }
        }
        else
        {
            Node->SetBoolField(TEXT("is_panel"), false);
        }
        Node->SetArrayField(TEXT("children"), Children);
        return Node;
    }
}

bool UMCPPuerTSBridgeService::InspectWidgetJson(
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
    AssetPath = AssetPath.TrimStartAndEnd();
    if (AssetPath.IsEmpty())
    {
        OutError = TEXT("asset_path is required.");
        return false;
    }
    // Reading is allowed anywhere under the two content roots, like every other
    // read in this bridge; authoring stays limited to /Game/MCPGenerated/.
    if (!AssetPath.StartsWith(TEXT("/Game/")) && !AssetPath.StartsWith(TEXT("/Engine/")))
    {
        OutError = TEXT("Widget Blueprint inspection is limited to /Game and /Engine.");
        return false;
    }

    FString ObjectPath = AssetPath;
    FString PackagePath = AssetPath;
    if (!AssetPath.Contains(TEXT(".")))
    {
        if (!FPackageName::IsValidLongPackageName(AssetPath))
        {
            OutError = FString::Printf(TEXT("'%s' is not a valid package path."), *AssetPath);
            return false;
        }
        ObjectPath = AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
    }
    else
    {
        PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
    }

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData AssetData =
        AssetRegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath));
    if (!AssetData.IsValid())
    {
        OutError = FString::Printf(TEXT("No asset was found at '%s'."), *ObjectPath);
        return false;
    }
    UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(AssetData.GetAsset());
    if (WidgetBlueprint == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The asset at '%s' is a %s, not a WidgetBlueprint."),
            *ObjectPath, *AssetData.AssetClass.ToString());
        return false;
    }

    const UPackage* Package = WidgetBlueprint->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    TArray<TSharedPtr<FJsonValue>> Warnings;
    FWidgetWalkContext Context;

    TSharedPtr<FJsonValue> Root = MakeShared<FJsonValueNull>();
    if (WidgetBlueprint->WidgetTree != nullptr
        && WidgetBlueprint->WidgetTree->RootWidget != nullptr)
    {
        Root = MakeShared<FJsonValueObject>(DescribeWidgetNode(
            WidgetBlueprint->WidgetTree->RootWidget, TEXT("root"), Context));
    }
    else
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This Widget Blueprint has no root widget.")));
    }

    // Exposed variables: the widgets the generated class publishes as members,
    // read off the tree rather than off the spec.
    TArray<TSharedPtr<FJsonValue>> Variables;
    if (WidgetBlueprint->WidgetTree != nullptr)
    {
        TArray<UWidget*> AllWidgets;
        WidgetBlueprint->WidgetTree->GetAllWidgets(AllWidgets);
        AllWidgets.Sort([](const UWidget& A, const UWidget& B)
        {
            return A.GetName() < B.GetName();
        });
        for (const UWidget* Widget : AllWidgets)
        {
            if (Widget == nullptr || !Widget->bIsVariable) { continue; }
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Widget->GetName());
            Entry->SetStringField(TEXT("class_path"), Widget->GetClass()->GetPathName());
            Variables.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    // Property and event bindings, where UE4.27 exposes them on the Blueprint.
    TArray<TSharedPtr<FJsonValue>> Bindings;
    {
        // Two suffix fields were added when widget_bind gave this array a write
        // half, and they are deliberately outside the hashed prefix: the first
        // four fields are the hash basis this tool shipped with, so a widget
        // that predates the addition hashes to exactly what it hashed before.
        // (ObjectName, PropertyName) is unique per UE4.27's own binding rule, so
        // appending to the line cannot reorder the sort either.
        TArray<FString> BindingLines;
        for (const FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
        {
            BindingLines.Add(FString::Printf(TEXT("%s|%s|%s|%s|%s|%s"),
                *Binding.ObjectName,
                *Binding.PropertyName.ToString(),
                *Binding.FunctionName.ToString(),
                Binding.Kind == EBindingKind::Function ? TEXT("function") : TEXT("property"),
                *Binding.SourceProperty.ToString(),
                // FEditorPropertyPath has no serialized text form, but
                // GetDisplayText() joins its segment member names with dots and
                // is what the UMG details panel shows, so it is stable for the
                // same path rather than an invented spelling.
                *Binding.SourcePath.GetDisplayText().ToString()));
        }
        BindingLines.Sort();
        for (const FString& Line : BindingLines)
        {
            TArray<FString> Parts;
            Line.ParseIntoArray(Parts, TEXT("|"), false);
            Parts.SetNum(6, false);
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("widget_name"), Parts[0]);
            Entry->SetStringField(TEXT("property_name"), Parts[1]);
            Entry->SetStringField(TEXT("function_name"), Parts[2]);
            Entry->SetStringField(TEXT("kind"), Parts[3]);
            Entry->SetStringField(TEXT("source_property"), Parts[4]);
            Entry->SetStringField(TEXT("source_path"), Parts[5]);
            Bindings.Add(MakeShared<FJsonValueObject>(Entry));
            Context.HashLines += FString::Printf(TEXT("binding|%s|%s|%s|%s\n"),
                *Parts[0], *Parts[1], *Parts[2], *Parts[3]);
        }
    }

    // Animations, when the Blueprint carries any.
    //
    // Two shapes on purpose. "animations" is the string array this tool shipped
    // with and stays a string array, because it is what live evidence and the UI
    // slice already read. "animation_details" is the same set described, added
    // when widget_bind made the binding half authorable and the animation half
    // still is not: a caller that wants to know whether an animation has any
    // tracks at all should not have to open the editor.
    //
    // Read only, and shallow on purpose. A UWidgetAnimation wraps a UMovieScene,
    // so anything below the track list is Sequencer's data model and belongs to
    // whoever owns MovieScene track code, not here. Track counts and class names
    // are a read of the same array Sequencer displays; keyframes are not read.
    TArray<TSharedPtr<FJsonValue>> Animations;
    TArray<TSharedPtr<FJsonValue>> AnimationDetails;
    {
        TArray<const UWidgetAnimation*> SortedAnimations;
        for (const UWidgetAnimation* Animation : WidgetBlueprint->Animations)
        {
            if (Animation != nullptr) { SortedAnimations.Add(Animation); }
        }
        SortedAnimations.Sort([](const UWidgetAnimation& A, const UWidgetAnimation& B)
        {
            return A.GetName() < B.GetName();
        });
        for (const UWidgetAnimation* Animation : SortedAnimations)
        {
            const FString Name = Animation->GetName();
            Animations.Add(MakeShared<FJsonValueString>(Name));
            Context.HashLines += TEXT("animation|") + Name + TEXT("\n");

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Name);
            Entry->SetStringField(TEXT("display_label"), Animation->GetDisplayLabel());
            Entry->SetNumberField(TEXT("start_time"), Animation->GetStartTime());
            Entry->SetNumberField(TEXT("end_time"), Animation->GetEndTime());

            // Which widgets the animation possesses. A binding whose widget was
            // renamed or deleted still sits in this array, which is exactly the
            // case a read-back is for.
            TArray<TSharedPtr<FJsonValue>> AnimationBindings;
            for (const FWidgetAnimationBinding& Binding : Animation->GetBindings())
            {
                TSharedPtr<FJsonObject> BindingJson = MakeShared<FJsonObject>();
                BindingJson->SetStringField(TEXT("widget_name"), Binding.WidgetName.ToString());
                BindingJson->SetStringField(TEXT("slot_widget_name"), Binding.SlotWidgetName.ToString());
                BindingJson->SetStringField(TEXT("guid"), Binding.AnimationGuid.ToString());
                BindingJson->SetBoolField(TEXT("is_root_widget"), Binding.bIsRootWidget);
                AnimationBindings.Add(MakeShared<FJsonValueObject>(BindingJson));
            }
            Entry->SetArrayField(TEXT("animation_bindings"), AnimationBindings);

            int32 TrackCount = 0;
            TArray<TSharedPtr<FJsonValue>> TrackClasses;
            if (const UMovieScene* MovieScene = Animation->GetMovieScene())
            {
                for (const UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
                {
                    if (Track == nullptr) { continue; }
                    TrackCount++;
                    TrackClasses.Add(MakeShared<FJsonValueString>(Track->GetClass()->GetName()));
                }
                for (const FMovieSceneBinding& SceneBinding : MovieScene->GetBindings())
                {
                    for (const UMovieSceneTrack* Track : SceneBinding.GetTracks())
                    {
                        if (Track == nullptr) { continue; }
                        TrackCount++;
                        TrackClasses.Add(MakeShared<FJsonValueString>(Track->GetClass()->GetName()));
                    }
                }
            }
            else
            {
                Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("Widget animation '%s' has no MovieScene, so it can hold no tracks."), *Name)));
            }
            Entry->SetNumberField(TEXT("track_count"), TrackCount);
            Entry->SetArrayField(TEXT("track_classes"), TrackClasses);
            AnimationDetails.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    uint8 Sha1[FSHA1::DigestSize];
    const FTCHARToUTF8 HashUtf8(*Context.HashLines);
    FSHA1::HashBuffer(HashUtf8.Get(), HashUtf8.Length(), Sha1);

    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this Widget Blueprint dirtied its package. That is a defect in the "
                 "inspector, not a property of the asset: report it.")));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), WidgetBlueprint->GetPathName());
    Result->SetStringField(TEXT("widget_blueprint_class"),
        WidgetBlueprint->GetClass()->GetPathName());
    Result->SetStringField(TEXT("generated_class_path"),
        WidgetBlueprint->GeneratedClass != nullptr
            ? WidgetBlueprint->GeneratedClass->GetPathName()
            : FString());
    Result->SetStringField(TEXT("parent_class"),
        WidgetBlueprint->ParentClass != nullptr
            ? WidgetBlueprint->ParentClass->GetPathName()
            : FString());
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    Result->SetStringField(TEXT("identity_kind"), TEXT("derived"));
    Result->SetField(TEXT("root"), Root);
    Result->SetNumberField(TEXT("widget_count"), Context.WidgetCount);
    Result->SetNumberField(TEXT("panel_count"), Context.PanelCount);
    Result->SetNumberField(TEXT("named_slot_count"), Context.NamedSlotCount);
    Result->SetArrayField(TEXT("variables"), Variables);
    Result->SetArrayField(TEXT("bindings"), Bindings);
    Result->SetArrayField(TEXT("animations"), Animations);
    Result->SetArrayField(TEXT("animation_details"), AnimationDetails);
    Result->SetArrayField(TEXT("unsupported_fields"), Context.UnsupportedFields);
    Result->SetStringField(TEXT("structure_hash_sha1"), BytesToHex(Sha1, FSHA1::DigestSize));
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}
