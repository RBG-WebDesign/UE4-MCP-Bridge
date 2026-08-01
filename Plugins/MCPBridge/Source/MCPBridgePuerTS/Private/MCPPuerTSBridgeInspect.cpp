// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "BlueprintGraphBuilderLibrary.h"
#include "BlueprintInspectorLibrary.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Json.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace
{
    /** Parse one of the inspector library's JSON strings back into a value so
        it can be nested in this command's response. The readers answer with a
        bare array on success and an object holding "error" when they cannot
        do the job, so both shapes are accepted and an unparseable answer is
        reported rather than silently dropped. */
    TSharedPtr<FJsonValue> ParseReaderJson(const FString& Json, FString& OutError)
    {
        TSharedPtr<FJsonValue> Value;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Value) || !Value.IsValid())
        {
            OutError = TEXT("An inspector reader returned JSON that could not be parsed.");
            return MakeShared<FJsonValueNull>();
        }
        return Value;
    }

    /** Sort an array of objects by one of their string fields.

        Canonical order is the whole point of this command: Unreal's own array
        order for variables, components and graphs is the order they happen to
        be stored in, and a reader that passes it through would answer
        differently after an unrelated edit reordered them. Sorting by a field
        the caller can see makes two reads comparable by hash. */
    void SortByStringField(TArray<TSharedPtr<FJsonValue>>& Values, const TCHAR* Field)
    {
        Values.Sort([Field](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
        {
            const TSharedPtr<FJsonObject>* ObjectA = nullptr;
            const TSharedPtr<FJsonObject>* ObjectB = nullptr;
            const FString KeyA = A.IsValid() && A->TryGetObject(ObjectA)
                ? (*ObjectA)->GetStringField(Field) : FString();
            const FString KeyB = B.IsValid() && B->TryGetObject(ObjectB)
                ? (*ObjectB)->GetStringField(Field) : FString();
            return KeyA < KeyB;
        });
    }

    /** Read one of the array-returning readers, sorted. Anything that is not
        an array (an error object) is passed through untouched. */
    TSharedPtr<FJsonValue> SortedReaderArray(const FString& Json, const TCHAR* Field, FString& OutError)
    {
        TSharedPtr<FJsonValue> Value = ParseReaderJson(Json, OutError);
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Value.IsValid() && Value->TryGetArray(Array))
        {
            TArray<TSharedPtr<FJsonValue>> Sorted = *Array;
            SortByStringField(Sorted, Field);
            return MakeShared<FJsonValueArray>(Sorted);
        }
        return Value;
    }

    FString CompileStatusLabel(const UBlueprint* Blueprint)
    {
        switch (Blueprint->Status)
        {
            case BS_UpToDate:             return TEXT("UpToDate");
            case BS_UpToDateWithWarnings: return TEXT("UpToDateWithWarnings");
            case BS_Error:                return TEXT("Error");
            case BS_Dirty:                return TEXT("Dirty");
            case BS_BeingCreated:         return TEXT("BeingCreated");
            default:                      return TEXT("Unknown");
        }
    }
}

bool UMCPPuerTSBridgeService::InspectBlueprintJson(
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
    // Reading is not writing, so this is not limited to /Game/MCPGenerated/
    // the way authoring is: the point of an inspector is to read a Blueprint
    // somebody else wrote. It is still limited to the two roots every other
    // read in this bridge uses, so a request cannot reach outside content.
    if (!AssetPath.StartsWith(TEXT("/Game/")) && !AssetPath.StartsWith(TEXT("/Engine/")))
    {
        OutError = TEXT("Blueprint inspection is limited to /Game and /Engine.");
        return false;
    }

    // Both spellings work: a package path (/Game/X/BP_Y) and the object path
    // the other tools hand back (/Game/X/BP_Y.BP_Y).
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

    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData AssetData = AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath));
    if (!AssetData.IsValid())
    {
        OutError = FString::Printf(TEXT("No asset was found at '%s'."), *ObjectPath);
        return false;
    }
    UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
    if (Blueprint == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The asset at '%s' is a %s, not a Blueprint."),
            *ObjectPath, *AssetData.AssetClass.ToString());
        return false;
    }

    // The cleanliness contract, measured rather than asserted. Loading an
    // asset to read it must not dirty it, and nothing below this line calls
    // Modify, MarkPackageDirty or a compile. Both readings are reported so a
    // caller can see the answer instead of taking the annotation's word:
    // an inspector that quietly dirties a package would send the next save of
    // an unrelated change out with a Blueprint the caller never edited.
    const UPackage* Package = Blueprint->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    FString GraphName;
    Request->TryGetStringField(TEXT("graph_name"), GraphName);
    const bool bIncludePins = Request->HasTypedField<EJson::Boolean>(TEXT("include_pins"))
        && Request->GetBoolField(TEXT("include_pins"));

    TArray<TSharedPtr<FJsonValue>> Warnings;
    FString ReaderError;

    // The member readers are UBlueprintInspectorLibrary's, not this command's.
    // They were written, compiled into MCPBridgeGraphBuilder and left without
    // a caller, exactly as the widget builder was; this command is a front for
    // them plus the one thing they did not have, which is the mapping from a
    // spawned K2 node back to the word blueprint_build would write for it.
    TSharedPtr<FJsonValue> Components =
        SortedReaderArray(UBlueprintInspectorLibrary::ListSCSNodes(Blueprint), TEXT("name"), ReaderError);
    TSharedPtr<FJsonValue> Variables =
        SortedReaderArray(UBlueprintInspectorLibrary::ListVariables(Blueprint), TEXT("name"), ReaderError);
    TSharedPtr<FJsonValue> Graphs =
        SortedReaderArray(UBlueprintInspectorLibrary::ListGraphs(Blueprint), TEXT("name"), ReaderError);
    TSharedPtr<FJsonValue> Functions =
        SortedReaderArray(UBlueprintInspectorLibrary::ListFunctions(Blueprint), TEXT("name"), ReaderError);
    TSharedPtr<FJsonValue> Interfaces =
        SortedReaderArray(UBlueprintInspectorLibrary::ListInterfaces(Blueprint), TEXT("path"), ReaderError);

    TSharedPtr<FJsonValue> Graph = ParseReaderJson(
        UBlueprintGraphBuilderLibrary::DescribeBlueprintGraphJSON(Blueprint, GraphName, bIncludePins),
        ReaderError);
    if (!ReaderError.IsEmpty())
    {
        OutError = ReaderError;
        return false;
    }
    // A graph that could not be found is reported inside the graph object
    // rather than failing the whole read, so the members half still answers.
    const TSharedPtr<FJsonObject>* GraphObject = nullptr;
    if (Graph.IsValid() && Graph->TryGetObject(GraphObject))
    {
        FString GraphError;
        if ((*GraphObject)->TryGetStringField(TEXT("error"), GraphError))
        {
            Warnings.Add(MakeShared<FJsonValueString>(GraphError));
        }
    }

    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this Blueprint dirtied its package. That is a defect in the "
                 "inspector, not a property of the asset: report it.")));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("package_name"), Package != nullptr ? Package->GetName() : FString());
    Result->SetStringField(TEXT("generated_class_path"),
        Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetPathName() : FString());
    Result->SetStringField(TEXT("parent_class"),
        Blueprint->ParentClass != nullptr ? Blueprint->ParentClass->GetPathName() : FString());
    Result->SetBoolField(TEXT("is_actor"),
        Blueprint->ParentClass != nullptr && Blueprint->ParentClass->IsChildOf(AActor::StaticClass()));
    Result->SetStringField(TEXT("compile_status"), CompileStatusLabel(Blueprint));
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    Result->SetField(TEXT("components"), Components);
    Result->SetField(TEXT("variables"), Variables);
    Result->SetField(TEXT("interfaces"), Interfaces);
    Result->SetField(TEXT("functions"), Functions);
    Result->SetField(TEXT("graphs"), Graphs);
    Result->SetField(TEXT("graph"), Graph);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}
