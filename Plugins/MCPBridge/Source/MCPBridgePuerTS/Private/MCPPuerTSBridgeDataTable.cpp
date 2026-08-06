// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "MCPBridgeAssetRollback.h"
#include "MCPBridgeContentSnapshot.h"
#include "MCPBridgeDataLibrary.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace
{
    FString SerializeDataTableJson(const TSharedPtr<FJsonObject>& Object)
    {
        FString Json;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Json;
    }

    bool ParseFillResult(const FString& Json, FString& OutError)
    {
        TSharedPtr<FJsonObject> Result;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Result) || !Result.IsValid())
        {
            OutError = TEXT("DataTable importer returned invalid JSON.");
            return false;
        }
        bool bSuccess = false;
        Result->TryGetBoolField(TEXT("success"), bSuccess);
        if (!bSuccess)
        {
            const TArray<TSharedPtr<FJsonValue>>* Problems = nullptr;
            TArray<FString> Messages;
            if (Result->TryGetArrayField(TEXT("problems"), Problems) && Problems != nullptr)
            {
                for (const TSharedPtr<FJsonValue>& Value : *Problems)
                {
                    FString Message;
                    if (Value.IsValid() && Value->TryGetString(Message)) { Messages.Add(Message); }
                }
            }
            OutError = Messages.Num() > 0
                ? TEXT("DataTable JSON import failed: ") + FString::Join(Messages, TEXT("; "))
                : TEXT("DataTable JSON import failed without a reported problem.");
        }
        return bSuccess;
    }

    TArray<TSharedPtr<FJsonValue>> RowNames(const UDataTable* Table)
    {
        TArray<FName> Names = Table != nullptr ? Table->GetRowNames() : TArray<FName>();
        Names.Sort(FNameLexicalLess());
        TArray<TSharedPtr<FJsonValue>> Result;
        for (const FName& Name : Names)
        {
            Result.Add(MakeShared<FJsonValueString>(Name.ToString()));
        }
        return Result;
    }

    bool OnlyDataTableFields(const TSharedPtr<FJsonObject>& Request, FString& OutError)
    {
        const TSet<FString> Allowed = {
            TEXT("asset_path"), TEXT("row_struct"), TEXT("rows_json"),
            TEXT("plan_only"), TEXT("save") };
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Request->Values)
        {
            if (!Allowed.Contains(Field.Key))
            {
                OutError = FString::Printf(TEXT("Unsupported field '%s' at request."), *Field.Key);
                return false;
            }
        }
        return true;
    }

    bool NormalizeRows(const TSharedPtr<FJsonObject>& Request, FString& OutRowsJson, FString& OutError)
    {
        const TSharedPtr<FJsonValue>* Value = Request->Values.Find(TEXT("rows_json"));
        if (Value == nullptr || !Value->IsValid())
        {
            OutRowsJson = TEXT("[]");
            return true;
        }
        if ((*Value)->Type == EJson::String)
        {
            if (!(*Value)->TryGetString(OutRowsJson) || OutRowsJson.TrimStartAndEnd().IsEmpty())
            {
                OutError = TEXT("rows_json string must contain a JSON array.");
                return false;
            }
            return true;
        }
        if ((*Value)->Type == EJson::Array)
        {
            const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutRowsJson);
            FJsonSerializer::Serialize((*Value)->AsArray(), Writer);
            return true;
        }
        OutError = TEXT("rows_json must be a JSON array or JSON text.");
        return false;
    }

    UDataTable* MakeScratchTable(
        UScriptStruct* RowStruct,
        const FString& RowsJson,
        FString& OutCanonicalRows,
        FString& OutError)
    {
        const FName ScratchName = MakeUniqueObjectName(
            GetTransientPackage(), UDataTable::StaticClass(), TEXT("MCPDataTableValidation"));
        FString CreateError;
        UDataTable* Scratch = UMCPBridgeDataLibrary::CreateDataTable(
            GetTransientPackage(), ScratchName, RowStruct, CreateError);
        if (Scratch == nullptr)
        {
            OutError = TEXT("Could not create DataTable validation copy: ") + CreateError;
            return nullptr;
        }
        Scratch->ClearFlags(RF_Public | RF_Standalone);
        Scratch->SetFlags(RF_Transient);
        const FString FillResult = UMCPBridgeDataLibrary::FillDataTableFromJSON(Scratch, RowsJson);
        if (!ParseFillResult(FillResult, OutError)) { return nullptr; }
        OutCanonicalRows = Scratch->GetTableAsJSON();
        return Scratch;
    }
}

bool UMCPPuerTSBridgeService::BuildDataTableJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError)
{
    TSharedPtr<FJsonObject> Request;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("data_table_build request must be a JSON object.");
        return false;
    }
    if (!OnlyDataTableFields(Request, OutError)) { return false; }
    if ((Request->HasField(TEXT("plan_only"))
            && !Request->HasTypedField<EJson::Boolean>(TEXT("plan_only")))
        || (Request->HasField(TEXT("save"))
            && !Request->HasTypedField<EJson::Boolean>(TEXT("save"))))
    {
        OutError = TEXT("plan_only and save must be boolean.");
        return false;
    }

    FString AssetPath;
    Request->TryGetStringField(TEXT("asset_path"), AssetPath);
    AssetPath = AssetPath.TrimStartAndEnd();
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("DataTables are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }
    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = AssetPath + TEXT(".") + AssetName;
    UObject* ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
    UDataTable* Table = Cast<UDataTable>(ExistingObject);
    if (ExistingObject != nullptr && Table == nullptr)
    {
        OutError = FString::Printf(TEXT("The asset at '%s' is not a DataTable."), *ObjectPath);
        return false;
    }
    const bool bCreating = Table == nullptr;

    FString RowStructPath;
    Request->TryGetStringField(TEXT("row_struct"), RowStructPath);
    RowStructPath = RowStructPath.TrimStartAndEnd();
    UScriptStruct* RequestedStruct = RowStructPath.IsEmpty()
        ? nullptr : LoadObject<UScriptStruct>(nullptr, *RowStructPath);
    if (!RowStructPath.IsEmpty() && RequestedStruct == nullptr)
    {
        OutError = FString::Printf(TEXT("No ScriptStruct was found at '%s'."), *RowStructPath);
        return false;
    }
    if (bCreating && RequestedStruct == nullptr)
    {
        OutError = TEXT("row_struct is required when creating a DataTable.");
        return false;
    }
    UScriptStruct* EffectiveStruct = bCreating
        ? RequestedStruct : const_cast<UScriptStruct*>(Table->GetRowStruct());
    if (EffectiveStruct == nullptr)
    {
        OutError = TEXT("The existing DataTable has no row struct.");
        return false;
    }
    if (!bCreating && RequestedStruct != nullptr && RequestedStruct != EffectiveStruct)
    {
        OutError = FString::Printf(
            TEXT("row_struct '%s' does not match the existing DataTable row struct '%s'."),
            *RequestedStruct->GetPathName(), *EffectiveStruct->GetPathName());
        return false;
    }

    FString RowsJson;
    if (!NormalizeRows(Request, RowsJson, OutError)) { return false; }
    FString DesiredRowsJson;
    UDataTable* Scratch = MakeScratchTable(EffectiveStruct, RowsJson, DesiredRowsJson, OutError);
    if (Scratch == nullptr) { return false; }
    const FString BeforeRowsJson = bCreating ? FString() : Table->GetTableAsJSON();
    const bool bUnchanged = !bCreating && BeforeRowsJson == DesiredRowsJson;
    const bool bPlanOnly = Request->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Request->GetBoolField(TEXT("plan_only"));
    const bool bSave = !Request->HasField(TEXT("save")) || Request->GetBoolField(TEXT("save"));

    auto BuildResult = [&](UDataTable* ResultTable) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), AssetPath);
        Result->SetStringField(TEXT("object_path"), ObjectPath);
        Result->SetStringField(TEXT("row_struct"), EffectiveStruct->GetPathName());
        Result->SetArrayField(TEXT("row_names"), RowNames(ResultTable));
        Result->SetNumberField(TEXT("row_count"), ResultTable != nullptr ? ResultTable->GetRowNames().Num() : 0);
        Result->SetStringField(TEXT("rows_json"), ResultTable != nullptr
            ? ResultTable->GetTableAsJSON() : DesiredRowsJson);
        return Result;
    };
    if (bPlanOnly)
    {
        TSharedPtr<FJsonObject> Result = BuildResult(Scratch);
        Result->SetBoolField(TEXT("plan_only"), true);
        Result->SetBoolField(TEXT("would_create"), bCreating);
        Result->SetBoolField(TEXT("would_change"), !bUnchanged);
        Result->SetBoolField(TEXT("unchanged"), bUnchanged);
        Result->SetBoolField(TEXT("saved"), false);
        OutResultJson = SerializeDataTableJson(Result);
        return true;
    }
    if (bUnchanged)
    {
        TSharedPtr<FJsonObject> Result = BuildResult(Table);
        Result->SetBoolField(TEXT("created"), false);
        Result->SetBoolField(TEXT("unchanged"), true);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetBoolField(TEXT("verified"), true);
        Result->SetStringField(TEXT("verified_by"), TEXT("UDataTable::GetTableAsJSON"));
        OutResultJson = SerializeDataTableJson(Result);
        return true;
    }
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("data_table_build requires an active transaction.");
        return false;
    }

    FAssetRegistryModule& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    FBridgeAssetRollback CreationRollback;
    CreationRollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = CreationRollback.DirtyPackages();
    const TArray<FString> SourceBefore = CreationRollback.SourceControlState();
    FBridgeContentSnapshot UpdateSnapshot;
    if (!bCreating)
    {
        FString Refusal;
        if (!UpdateSnapshot.Capture(AssetPath, Refusal)) { OutError = Refusal; return false; }
    }
    auto Fail = [&](const FString& Error) -> bool
    {
        ActiveTransaction->Cancel();
        bool bRestored = false;
        TSharedPtr<FJsonObject> Evidence;
        if (bCreating)
        {
            CreationRollback.Rollback();
            bRestored = !Registry.Get().GetAssetByObjectPath(FName(*ObjectPath)).IsValid();
            Evidence = CreationRollback.BuildEvidence(DirtyBefore, SourceBefore);
        }
        else
        {
            FString RestoreError;
            const bool bMechanism = UpdateSnapshot.Restore(RestoreError);
            UDataTable* Restored = LoadObject<UDataTable>(nullptr, *ObjectPath);
            bRestored = bMechanism && Restored != nullptr
                && Restored->GetTableAsJSON() == BeforeRowsJson;
            Evidence = UpdateSnapshot.BuildEvidence();
        }
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), AssetPath);
        Result->SetBoolField(TEXT("built"), false);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetBoolField(TEXT("rollback_succeeded"), bRestored);
        Result->SetStringField(TEXT("rollback_verified_by"), TEXT("UDataTable::GetTableAsJSON"));
        Result->SetStringField(TEXT("error"), Error);
        Result->SetObjectField(TEXT("rollback"), Evidence);
        OutResultJson = SerializeDataTableJson(Result);
        OutError = Error;
        return false;
    };

    if (bCreating)
    {
        UPackage* Package = CreatePackage(nullptr, *AssetPath);
        FString CreateError;
        Table = UMCPBridgeDataLibrary::CreateDataTable(
            Package, FName(*AssetName), EffectiveStruct, CreateError);
        if (Table == nullptr) { return Fail(CreateError); }
        CreationRollback.TrackCreated(Table);
        Registry.Get().AssetCreated(Table);
    }
    if (!Table->Modify())
    {
        return Fail(TEXT("DataTable Modify() returned false; rows were not changed."));
    }
    const FString FillResult = UMCPBridgeDataLibrary::FillDataTableFromJSON(Table, DesiredRowsJson);
    FString FillError;
    if (!ParseFillResult(FillResult, FillError)) { return Fail(FillError); }
    Table->PostEditChange();
    Table->MarkPackageDirty();
    if (Table->GetTableAsJSON() != DesiredRowsJson)
    {
        return Fail(TEXT("DataTable read-back did not match the desired row JSON."));
    }
    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(ObjectPath, SaveError);
        if (!bSaved) { return Fail(TEXT("DataTable save failed: ") + SaveError); }
    }
    CreationRollback.Commit();
    TSharedPtr<FJsonObject> Result = BuildResult(Table);
    Result->SetBoolField(TEXT("created"), bCreating);
    Result->SetBoolField(TEXT("unchanged"), false);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetBoolField(TEXT("verified"), true);
    Result->SetStringField(TEXT("verified_by"), TEXT("UDataTable::GetTableAsJSON"));
    OutResultJson = SerializeDataTableJson(Result);
    return true;
}
