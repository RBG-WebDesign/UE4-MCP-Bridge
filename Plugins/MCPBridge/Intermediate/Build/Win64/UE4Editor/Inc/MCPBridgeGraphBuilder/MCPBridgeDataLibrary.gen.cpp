// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/MCPBridgeDataLibrary.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeMCPBridgeDataLibrary() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UMCPBridgeDataLibrary_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UMCPBridgeDataLibrary();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
	ENGINE_API UClass* Z_Construct_UClass_UDataTable_NoRegister();
// End Cross Module References
	DEFINE_FUNCTION(UMCPBridgeDataLibrary::execFillDataTableFromJSON)
	{
		P_GET_OBJECT(UDataTable,Z_Param_DataTable);
		P_GET_PROPERTY(FStrProperty,Z_Param_JsonString);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UMCPBridgeDataLibrary::FillDataTableFromJSON(Z_Param_DataTable,Z_Param_JsonString);
		P_NATIVE_END;
	}
	void UMCPBridgeDataLibrary::StaticRegisterNativesUMCPBridgeDataLibrary()
	{
		UClass* Class = UMCPBridgeDataLibrary::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "FillDataTableFromJSON", &UMCPBridgeDataLibrary::execFillDataTableFromJSON },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics
	{
		struct MCPBridgeDataLibrary_eventFillDataTableFromJSON_Parms
		{
			UDataTable* DataTable;
			FString JsonString;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_DataTable;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_JsonString_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_JsonString;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::NewProp_DataTable = { "DataTable", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeDataLibrary_eventFillDataTableFromJSON_Parms, DataTable), Z_Construct_UClass_UDataTable_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::NewProp_JsonString_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::NewProp_JsonString = { "JsonString", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeDataLibrary_eventFillDataTableFromJSON_Parms, JsonString), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::NewProp_JsonString_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::NewProp_JsonString_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeDataLibrary_eventFillDataTableFromJSON_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::NewProp_DataTable,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::NewProp_JsonString,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "MCPBridgeData" },
		{ "Comment", "/**\n\x09 * Replace a DataTable's rows from a DataTable JSON array, with no dialog.\n\x09 * Returns a JSON object: {\"success\": bool, \"problems\": [..], \"rows\": [..]}.\n\x09 * success is true when the importer reported no problems.\n\x09 */" },
		{ "ModuleRelativePath", "Public/MCPBridgeDataLibrary.h" },
		{ "ToolTip", "Replace a DataTable's rows from a DataTable JSON array, with no dialog.\nReturns a JSON object: {\"success\": bool, \"problems\": [..], \"rows\": [..]}.\nsuccess is true when the importer reported no problems." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UMCPBridgeDataLibrary, nullptr, "FillDataTableFromJSON", nullptr, nullptr, sizeof(MCPBridgeDataLibrary_eventFillDataTableFromJSON_Parms), Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UMCPBridgeDataLibrary_NoRegister()
	{
		return UMCPBridgeDataLibrary::StaticClass();
	}
	struct Z_Construct_UClass_UMCPBridgeDataLibrary_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UMCPBridgeDataLibrary_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UMCPBridgeDataLibrary_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UMCPBridgeDataLibrary_FillDataTableFromJSON, "FillDataTableFromJSON" }, // 2604849125
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UMCPBridgeDataLibrary_Statics::Class_MetaDataParams[] = {
		{ "Comment", "/**\n * DataTable row filling for the MCP bridge.\n *\n * unreal.DataTableFunctionLibrary.fill_data_table_from_json_string shows a\n * modal summary dialog on the game thread, which hangs the bridge. This\n * wraps UDataTable::CreateTableFromJSONString, the raw importer that returns\n * problems as strings and shows no dialog.\n */" },
		{ "IncludePath", "MCPBridgeDataLibrary.h" },
		{ "ModuleRelativePath", "Public/MCPBridgeDataLibrary.h" },
		{ "ToolTip", "DataTable row filling for the MCP bridge.\n\nunreal.DataTableFunctionLibrary.fill_data_table_from_json_string shows a\nmodal summary dialog on the game thread, which hangs the bridge. This\nwraps UDataTable::CreateTableFromJSONString, the raw importer that returns\nproblems as strings and shows no dialog." },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UMCPBridgeDataLibrary_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UMCPBridgeDataLibrary>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UMCPBridgeDataLibrary_Statics::ClassParams = {
		&UMCPBridgeDataLibrary::StaticClass,
		nullptr,
		&StaticCppClassTypeInfo,
		DependentSingletons,
		FuncInfo,
		nullptr,
		nullptr,
		UE_ARRAY_COUNT(DependentSingletons),
		UE_ARRAY_COUNT(FuncInfo),
		0,
		0,
		0x001000A0u,
		METADATA_PARAMS(Z_Construct_UClass_UMCPBridgeDataLibrary_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UMCPBridgeDataLibrary_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UMCPBridgeDataLibrary()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UMCPBridgeDataLibrary_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UMCPBridgeDataLibrary, 3088323649);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<UMCPBridgeDataLibrary>()
	{
		return UMCPBridgeDataLibrary::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UMCPBridgeDataLibrary(Z_Construct_UClass_UMCPBridgeDataLibrary, &UMCPBridgeDataLibrary::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("UMCPBridgeDataLibrary"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UMCPBridgeDataLibrary);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
