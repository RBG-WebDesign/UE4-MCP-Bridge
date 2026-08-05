// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPBridgeDataLibrary.generated.h"

class UDataTable;
class UScriptStruct;

/**
 * DataTable row filling for the MCP bridge.
 *
 * unreal.DataTableFunctionLibrary.fill_data_table_from_json_string shows a
 * modal summary dialog on the game thread, which hangs the bridge. This
 * wraps UDataTable::CreateTableFromJSONString, the raw importer that returns
 * problems as strings and shows no dialog.
 */
UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UMCPBridgeDataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create an editor-owned DataTable with a validated row struct.
	 *
	 * Asset callers pass the destination package as Outer, then fill the
	 * returned table through FillDataTableFromJSON and save the package in the
	 * same transaction. Keeping construction here gives the native command one
	 * row-struct check and one object-flag policy to reuse.
	 *
	 * OutError is empty on success. The table is not returned when validation
	 * fails.
	 */
	static UDataTable* CreateDataTable(
		UObject* Outer,
		FName ObjectName,
		UScriptStruct* RowStruct,
		FString& OutError);

	/**
	 * Replace a DataTable's rows from a DataTable JSON array, with no dialog.
	 * Returns a JSON object: {"success": bool, "problems": [..], "rows": [..]}.
	 * success is true when the importer reported no problems.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MCPBridgeData")
	static FString FillDataTableFromJSON(UDataTable* DataTable, const FString& JsonString);
};
