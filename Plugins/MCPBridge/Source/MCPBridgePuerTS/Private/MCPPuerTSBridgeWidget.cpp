// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Json.h"
#include "MCPBridgeAssetRollback.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintBuilderLibrary.h"

namespace
{
    /** The compile status the caller sees, in the same vocabulary
        blueprint_build reports so one client can read both. */
    FString WidgetCompileStatusText(const UWidgetBlueprint* WidgetBlueprint)
    {
        if (WidgetBlueprint == nullptr)
        {
            return TEXT("Unknown");
        }
        switch (WidgetBlueprint->Status)
        {
        case BS_UpToDate:              return TEXT("UpToDate");
        case BS_UpToDateWithWarnings:  return TEXT("UpToDateWithWarnings");
        case BS_Error:                 return TEXT("Error");
        case BS_Dirty:                 return TEXT("Dirty");
        default:                       return TEXT("Unknown");
        }
    }

    /** Read one widget and its children back out of the built tree.

        The tree is walked from the live UWidgetTree rather than echoed from
        the request, so the response is evidence that the hierarchy exists in
        the asset rather than a restatement of what was asked for. A canvas
        slot reports its position and size for the same reason: slot layout is
        applied by a different code path than widget construction, and a tree
        that built with every slot silently at the origin would otherwise read
        as a success. */
    TSharedPtr<FJsonObject> DescribeWidget(const UWidget* Widget, int32& InOutCount)
    {
        TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
        if (Widget == nullptr)
        {
            return Node;
        }
        InOutCount++;
        Node->SetStringField(TEXT("name"), Widget->GetName());
        Node->SetStringField(TEXT("class"), Widget->GetClass()->GetName());

        if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
        {
            const FAnchorData& Layout = CanvasSlot->LayoutData;
            TSharedPtr<FJsonObject> SlotJson = MakeShared<FJsonObject>();
            SlotJson->SetStringField(TEXT("class"), TEXT("CanvasPanelSlot"));
            TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
            Position->SetNumberField(TEXT("x"), Layout.Offsets.Left);
            Position->SetNumberField(TEXT("y"), Layout.Offsets.Top);
            SlotJson->SetObjectField(TEXT("position"), Position);
            TSharedPtr<FJsonObject> Size = MakeShared<FJsonObject>();
            Size->SetNumberField(TEXT("x"), Layout.Offsets.Right);
            Size->SetNumberField(TEXT("y"), Layout.Offsets.Bottom);
            SlotJson->SetObjectField(TEXT("size"), Size);
            SlotJson->SetNumberField(TEXT("z_order"), CanvasSlot->GetZOrder());
            Node->SetObjectField(TEXT("slot"), SlotJson);
        }
        else if (Widget->Slot != nullptr)
        {
            TSharedPtr<FJsonObject> SlotJson = MakeShared<FJsonObject>();
            SlotJson->SetStringField(TEXT("class"), Widget->Slot->GetClass()->GetName());
            Node->SetObjectField(TEXT("slot"), SlotJson);
        }

        if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
        {
            TArray<TSharedPtr<FJsonValue>> Children;
            for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
            {
                Children.Add(MakeShared<FJsonValueObject>(
                    DescribeWidget(Panel->GetChildAt(Index), InOutCount)));
            }
            Node->SetArrayField(TEXT("children"), Children);
        }
        return Node;
    }
}

bool UMCPPuerTSBridgeService::BuildWidgetJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Widget build requires an active transaction.");
        return false;
    }

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Widget spec must be a JSON object.");
        return false;
    }

    // --- Validation. Nothing below this block touches an asset. ---

    FString AssetPath;
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Widget Blueprints are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    const TSharedPtr<FJsonObject>* TreeObject = nullptr;
    if (!Spec->TryGetObjectField(TEXT("tree"), TreeObject))
    {
        OutError = TEXT("Widget spec needs a tree object holding the root widget.");
        return false;
    }

    // The builder library owns the tree grammar: widget types, categories,
    // child-count rules, property names and their JSON types. Handing it the
    // spec in validate-only mode first is what makes a rejected request leave
    // no asset behind, the same contract blueprint_build has.
    FString TreeJson;
    TSharedRef<TJsonWriter<>> TreeWriter = TJsonWriterFactory<>::Create(&TreeJson);
    FJsonSerializer::Serialize(TreeObject->ToSharedRef(), TreeWriter);

    const FString ValidationError = UWidgetBlueprintBuilderLibrary::ValidateWidgetJSON(TreeJson);
    if (!ValidationError.IsEmpty())
    {
        OutError = ValidationError;
        return false;
    }

    bool bSave = true;
    Spec->TryGetBoolField(TEXT("save"), bSave);

    FString PackagePath;
    FString AssetName;
    AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (PackagePath.IsEmpty() || AssetName.IsEmpty())
    {
        OutError = FString::Printf(TEXT("Widget asset path has no asset name: %s"), *AssetPath);
        return false;
    }
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);

    // --- Mutation starts here. ---
    //
    // Same rollback boundary as the Behavior Tree and Blueprint builders
    // (MCPBridgeAssetRollback.h). Widget has one difference that makes it
    // matter more, not less: BuildWidgetFromJSON creates, compiles AND SAVES
    // inside the library, so on the create path the .uasset is already on disk
    // before this command can judge whether it compiled. A failure after that
    // point leaves a saved file, not just a dirty package, which is why the
    // boundary's file deletion is load-bearing here rather than defensive.
    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
    const bool bCreated = WidgetBlueprint == nullptr;

    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;

    // Adopt whatever the library left at ObjectPath before rolling back. On the
    // create path the command has no pointer to the asset until it loads it
    // back, so a library failure partway through would otherwise leave an
    // untracked, registered asset behind.
    auto TrackIfOurs = [&]()
    {
        if (!bCreated) { return; }
        UObject* Orphan = FindObject<UObject>(nullptr, *ObjectPath);
        if (Orphan != nullptr) { Rollback.TrackCreated(Orphan); }
    };

    auto FailWithRollback = [&](const FString& Error) -> bool
    {
        TrackIfOurs();
        if (ActiveTransaction != nullptr)
        {
            ActiveTransaction->Cancel();
        }
        Rollback.Rollback();
        OutError = Error;
        return false;
    };

    if (bCreated)
    {
        // BuildWidgetFromJSON creates, compiles and saves. It refuses a path
        // that already holds an asset, which is why the existing-asset case is
        // routed to Rebuild instead of retried here.
        const FString BuildError = UWidgetBlueprintBuilderLibrary::BuildWidgetFromJSON(
            PackagePath, AssetName, TreeJson);
        if (!BuildError.IsEmpty())
        {
            return FailWithRollback(BuildError);
        }
        WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
        if (WidgetBlueprint == nullptr)
        {
            return FailWithRollback(FString::Printf(
                TEXT("The widget Blueprint reported success but could not be loaded back from %s."),
                *ObjectPath));
        }
        Rollback.TrackCreated(WidgetBlueprint);
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Widget Blueprint asset creation is not undoable: undo restores level and actor state, not the new package.")));
    }
    else
    {
        // Rerunning a spec converges by replacing the tree of the asset that is
        // already there, rather than by creating a second asset or by merging
        // into a tree the caller did not describe. A widget tree has no stable
        // per-widget identity to converge against the way a component or a
        // variable does: the spec is the whole tree.
        const FString RebuildError = UWidgetBlueprintBuilderLibrary::RebuildWidgetFromJSON(
            WidgetBlueprint, TreeJson);
        if (!RebuildError.IsEmpty())
        {
            return FailWithRollback(RebuildError);
        }
    }

    const FString CompileStatus = WidgetCompileStatusText(WidgetBlueprint);
    const bool bCompiled = WidgetBlueprint->Status == BS_UpToDate
        || WidgetBlueprint->Status == BS_UpToDateWithWarnings;
    if (!bCompiled)
    {
        Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
            TEXT("The widget Blueprint compiled to status %s."), *CompileStatus)));
    }

    // Rebuild finalizes without saving so the caller decides when to write;
    // creation already saved inside the library. Only one of the two paths
    // needs a save here, and neither saves a tree that did not compile.
    // The failure exit. On the create path the library has already written the
    // asset to disk, so this is what removes the file as well as the
    // registration.
    auto FailRolledBack = [&]() -> bool
    {
        if (ActiveTransaction != nullptr)
        {
            ActiveTransaction->Cancel();
        }
        Rollback.Rollback();

        TSharedPtr<FJsonObject> Failed = MakeShared<FJsonObject>();
        Failed->SetStringField(TEXT("asset_path"), AssetPath);
        Failed->SetBoolField(TEXT("created"), false);
        Failed->SetStringField(TEXT("compile_status"), TEXT("RolledBack"));
        Failed->SetBoolField(TEXT("saved"), false);
        Failed->SetNumberField(TEXT("widget_count"), 0);
        Failed->SetArrayField(TEXT("errors"), Errors);
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("The build failed and was rolled back: no asset, package, or file remains.")));
        Failed->SetArrayField(TEXT("warnings"), Warnings);
        Failed->SetObjectField(TEXT("cleanup"),
            Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
        OutResultJson = SerializeJson(Failed);
        return true;
    };

    if (Errors.Num() > 0)
    {
        return FailRolledBack();
    }

    bool bSaved = bCreated;
    if (bSave && !bCreated)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(ObjectPath, SaveError);
        if (!bSaved)
        {
            // A half-written asset is the same hazard as a half-built one.
            Errors.Add(MakeShared<FJsonValueString>(
                FString::Printf(TEXT("Asset save failed: %s"), *SaveError)));
            return FailRolledBack();
        }
    }
    else if (!bSave)
    {
        bSaved = false;
    }

    int32 WidgetCount = 0;
    TSharedPtr<FJsonObject> Root;
    if (WidgetBlueprint->WidgetTree != nullptr)
    {
        Root = DescribeWidget(WidgetBlueprint->WidgetTree->RootWidget, WidgetCount);
    }
    else
    {
        Errors.Add(MakeShared<FJsonValueString>(
            TEXT("The widget Blueprint has no WidgetTree after the build.")));
        return FailRolledBack();
    }

    // Past every failure exit: the request succeeded, so keep what it made.
    Rollback.Commit();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("object_path"), WidgetBlueprint->GetPathName());
    Result->SetStringField(TEXT("generated_class_path"),
        WidgetBlueprint->GeneratedClass != nullptr
            ? WidgetBlueprint->GeneratedClass->GetPathName()
            : FString());
    Result->SetBoolField(TEXT("created"), bCreated);
    Result->SetNumberField(TEXT("widget_count"), WidgetCount);
    Result->SetObjectField(TEXT("tree"), Root);
    Result->SetBoolField(TEXT("compiled"), bCompiled);
    Result->SetStringField(TEXT("compile_status"), CompileStatus);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetArrayField(TEXT("errors"), Errors);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    // Always present, so a caller never has to distinguish "clean" from
    // "this build predates the rollback boundary".
    Result->SetObjectField(TEXT("cleanup"),
        Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
    OutResultJson = SerializeJson(Result);
    return true;
}
