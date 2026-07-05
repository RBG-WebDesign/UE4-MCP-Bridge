#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPBridgeDataLibrary.generated.h"

class UDataTable;

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
	 * Replace a DataTable's rows from a DataTable JSON array, with no dialog.
	 * Returns a JSON object: {"success": bool, "problems": [..], "rows": [..]}.
	 * success is true when the importer reported no problems.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MCPBridgeData")
	static FString FillDataTableFromJSON(UDataTable* DataTable, const FString& JsonString);
};
