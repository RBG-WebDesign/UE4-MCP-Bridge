// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPBridgeDataLibrary.h"

#include "Engine/DataTable.h"
#include "Factories/DataTableFactory.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UDataTable* UMCPBridgeDataLibrary::CreateDataTable(
	UObject* Outer,
	FName ObjectName,
	UScriptStruct* RowStruct,
	FString& OutError)
{
	OutError.Reset();
	if (!Outer)
	{
		OutError = TEXT("Outer is null");
		return nullptr;
	}
	if (ObjectName.IsNone())
	{
		OutError = TEXT("ObjectName is empty");
		return nullptr;
	}
	if (!RowStruct)
	{
		OutError = TEXT("RowStruct is null");
		return nullptr;
	}
	if (!RowStruct->IsChildOf(FTableRowBase::StaticStruct()))
	{
		OutError = FString::Printf(
			TEXT("RowStruct '%s' must derive from FTableRowBase"),
			*RowStruct->GetPathName());
		return nullptr;
	}
	if (FindObject<UObject>(Outer, *ObjectName.ToString()))
	{
		OutError = FString::Printf(
			TEXT("A DataTable named '%s' already exists in '%s'"),
			*ObjectName.ToString(), *Outer->GetPathName());
		return nullptr;
	}

	UDataTableFactory* Factory = NewObject<UDataTableFactory>();
	Factory->Struct = RowStruct;
	UDataTable* Table = Cast<UDataTable>(Factory->FactoryCreateNew(
		UDataTable::StaticClass(), Outer, ObjectName,
		RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (!Table)
	{
		OutError = FString::Printf(
			TEXT("Failed to create DataTable '%s' in '%s'"),
			*ObjectName.ToString(), *Outer->GetPathName());
		return nullptr;
	}
	Table->MarkPackageDirty();
	return Table;
}

FString UMCPBridgeDataLibrary::FillDataTableFromJSON(UDataTable* DataTable, const FString& JsonString)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!DataTable)
	{
		Result->SetBoolField(TEXT("success"), false);
		TArray<TSharedPtr<FJsonValue>> Problems;
		Problems.Add(MakeShared<FJsonValueString>(TEXT("DataTable is null")));
		Result->SetArrayField(TEXT("problems"), Problems);
	}
	else
	{
		// Raw importer: returns problem strings, shows no modal dialog.
		const TArray<FString> ProblemList = DataTable->CreateTableFromJSONString(JsonString);

		TArray<TSharedPtr<FJsonValue>> Problems;
		for (const FString& Problem : ProblemList)
		{
			Problems.Add(MakeShared<FJsonValueString>(Problem));
		}
		Result->SetArrayField(TEXT("problems"), Problems);
		Result->SetBoolField(TEXT("success"), ProblemList.Num() == 0);

		TArray<TSharedPtr<FJsonValue>> RowNames;
		for (const FName& RowName : DataTable->GetRowNames())
		{
			RowNames.Add(MakeShared<FJsonValueString>(RowName.ToString()));
		}
		Result->SetArrayField(TEXT("rows"), RowNames);

		if (ProblemList.Num() == 0)
		{
			DataTable->MarkPackageDirty();
		}
	}

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Out;
}
