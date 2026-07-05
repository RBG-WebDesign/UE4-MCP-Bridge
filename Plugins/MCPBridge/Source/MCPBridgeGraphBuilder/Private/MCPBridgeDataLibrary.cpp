#include "MCPBridgeDataLibrary.h"

#include "Engine/DataTable.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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
