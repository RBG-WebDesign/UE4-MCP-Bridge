// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/BlueprintInspectorLibrary.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeBlueprintInspectorLibrary() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UBlueprintInspectorLibrary_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UBlueprintInspectorLibrary();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprint_NoRegister();
// End Cross Module References
	DEFINE_FUNCTION(UBlueprintInspectorLibrary::execFindNodes)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_QueryJson);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintInspectorLibrary::FindNodes(Z_Param_Blueprint,Z_Param_QueryJson);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintInspectorLibrary::execGetNodeDetail)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_GraphName);
		P_GET_PROPERTY(FStrProperty,Z_Param_NodeGuid);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintInspectorLibrary::GetNodeDetail(Z_Param_Blueprint,Z_Param_GraphName,Z_Param_NodeGuid);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintInspectorLibrary::execListNodesInGraph)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_GraphName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintInspectorLibrary::ListNodesInGraph(Z_Param_Blueprint,Z_Param_GraphName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintInspectorLibrary::execListSCSNodes)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintInspectorLibrary::ListSCSNodes(Z_Param_Blueprint);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintInspectorLibrary::execListEventDispatchers)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintInspectorLibrary::ListEventDispatchers(Z_Param_Blueprint);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintInspectorLibrary::execListInterfaces)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintInspectorLibrary::ListInterfaces(Z_Param_Blueprint);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintInspectorLibrary::execListMacros)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintInspectorLibrary::ListMacros(Z_Param_Blueprint);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintInspectorLibrary::execListVariables)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintInspectorLibrary::ListVariables(Z_Param_Blueprint);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintInspectorLibrary::execListFunctions)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintInspectorLibrary::ListFunctions(Z_Param_Blueprint);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintInspectorLibrary::execListGraphs)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintInspectorLibrary::ListGraphs(Z_Param_Blueprint);
		P_NATIVE_END;
	}
	void UBlueprintInspectorLibrary::StaticRegisterNativesUBlueprintInspectorLibrary()
	{
		UClass* Class = UBlueprintInspectorLibrary::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "FindNodes", &UBlueprintInspectorLibrary::execFindNodes },
			{ "GetNodeDetail", &UBlueprintInspectorLibrary::execGetNodeDetail },
			{ "ListEventDispatchers", &UBlueprintInspectorLibrary::execListEventDispatchers },
			{ "ListFunctions", &UBlueprintInspectorLibrary::execListFunctions },
			{ "ListGraphs", &UBlueprintInspectorLibrary::execListGraphs },
			{ "ListInterfaces", &UBlueprintInspectorLibrary::execListInterfaces },
			{ "ListMacros", &UBlueprintInspectorLibrary::execListMacros },
			{ "ListNodesInGraph", &UBlueprintInspectorLibrary::execListNodesInGraph },
			{ "ListSCSNodes", &UBlueprintInspectorLibrary::execListSCSNodes },
			{ "ListVariables", &UBlueprintInspectorLibrary::execListVariables },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics
	{
		struct BlueprintInspectorLibrary_eventFindNodes_Parms
		{
			UBlueprint* Blueprint;
			FString QueryJson;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_QueryJson_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_QueryJson;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventFindNodes_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::NewProp_QueryJson_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::NewProp_QueryJson = { "QueryJson", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventFindNodes_Parms, QueryJson), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::NewProp_QueryJson_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::NewProp_QueryJson_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventFindNodes_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::NewProp_QueryJson,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintInspector" },
		{ "Comment", "/** Search for nodes matching filters. Query JSON: {node_class?, calls_function?, references_variable?, references_asset?, graph_name?} */" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
		{ "ToolTip", "Search for nodes matching filters. Query JSON: {node_class?, calls_function?, references_variable?, references_asset?, graph_name?}" },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintInspectorLibrary, nullptr, "FindNodes", nullptr, nullptr, sizeof(BlueprintInspectorLibrary_eventFindNodes_Parms), Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics
	{
		struct BlueprintInspectorLibrary_eventGetNodeDetail_Parms
		{
			UBlueprint* Blueprint;
			FString GraphName;
			FString NodeGuid;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_GraphName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_GraphName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_NodeGuid_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_NodeGuid;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventGetNodeDetail_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_GraphName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_GraphName = { "GraphName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventGetNodeDetail_Parms, GraphName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_GraphName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_GraphName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_NodeGuid_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_NodeGuid = { "NodeGuid", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventGetNodeDetail_Parms, NodeGuid), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_NodeGuid_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_NodeGuid_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventGetNodeDetail_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_GraphName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_NodeGuid,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintInspector" },
		{ "Comment", "/** Fetch a single node by GUID in a graph with full detail. */" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
		{ "ToolTip", "Fetch a single node by GUID in a graph with full detail." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintInspectorLibrary, nullptr, "GetNodeDetail", nullptr, nullptr, sizeof(BlueprintInspectorLibrary_eventGetNodeDetail_Parms), Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics
	{
		struct BlueprintInspectorLibrary_eventListEventDispatchers_Parms
		{
			UBlueprint* Blueprint;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListEventDispatchers_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListEventDispatchers_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintInspector" },
		{ "Comment", "/** List event dispatchers (multicast delegates) declared on the blueprint. */" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
		{ "ToolTip", "List event dispatchers (multicast delegates) declared on the blueprint." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintInspectorLibrary, nullptr, "ListEventDispatchers", nullptr, nullptr, sizeof(BlueprintInspectorLibrary_eventListEventDispatchers_Parms), Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics
	{
		struct BlueprintInspectorLibrary_eventListFunctions_Parms
		{
			UBlueprint* Blueprint;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListFunctions_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListFunctions_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintInspector" },
		{ "Comment", "/** List all user functions on the blueprint. */" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
		{ "ToolTip", "List all user functions on the blueprint." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintInspectorLibrary, nullptr, "ListFunctions", nullptr, nullptr, sizeof(BlueprintInspectorLibrary_eventListFunctions_Parms), Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics
	{
		struct BlueprintInspectorLibrary_eventListGraphs_Parms
		{
			UBlueprint* Blueprint;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListGraphs_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListGraphs_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintInspector" },
		{ "Comment", "/** List all graphs on the blueprint (EventGraph, function graphs, macro graphs). */" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
		{ "ToolTip", "List all graphs on the blueprint (EventGraph, function graphs, macro graphs)." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintInspectorLibrary, nullptr, "ListGraphs", nullptr, nullptr, sizeof(BlueprintInspectorLibrary_eventListGraphs_Parms), Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics
	{
		struct BlueprintInspectorLibrary_eventListInterfaces_Parms
		{
			UBlueprint* Blueprint;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListInterfaces_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListInterfaces_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintInspector" },
		{ "Comment", "/** List interfaces implemented by the blueprint. */" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
		{ "ToolTip", "List interfaces implemented by the blueprint." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintInspectorLibrary, nullptr, "ListInterfaces", nullptr, nullptr, sizeof(BlueprintInspectorLibrary_eventListInterfaces_Parms), Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics
	{
		struct BlueprintInspectorLibrary_eventListMacros_Parms
		{
			UBlueprint* Blueprint;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListMacros_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListMacros_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintInspector" },
		{ "Comment", "/** List all user macros on the blueprint. */" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
		{ "ToolTip", "List all user macros on the blueprint." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintInspectorLibrary, nullptr, "ListMacros", nullptr, nullptr, sizeof(BlueprintInspectorLibrary_eventListMacros_Parms), Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics
	{
		struct BlueprintInspectorLibrary_eventListNodesInGraph_Parms
		{
			UBlueprint* Blueprint;
			FString GraphName;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_GraphName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_GraphName;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListNodesInGraph_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::NewProp_GraphName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::NewProp_GraphName = { "GraphName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListNodesInGraph_Parms, GraphName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::NewProp_GraphName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::NewProp_GraphName_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListNodesInGraph_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::NewProp_GraphName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintInspector" },
		{ "Comment", "/** List all nodes in a named graph with pins and link info. */" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
		{ "ToolTip", "List all nodes in a named graph with pins and link info." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintInspectorLibrary, nullptr, "ListNodesInGraph", nullptr, nullptr, sizeof(BlueprintInspectorLibrary_eventListNodesInGraph_Parms), Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics
	{
		struct BlueprintInspectorLibrary_eventListSCSNodes_Parms
		{
			UBlueprint* Blueprint;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListSCSNodes_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListSCSNodes_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintInspector" },
		{ "Comment", "/** List simple-construction-script nodes (components) with hierarchy. */" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
		{ "ToolTip", "List simple-construction-script nodes (components) with hierarchy." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintInspectorLibrary, nullptr, "ListSCSNodes", nullptr, nullptr, sizeof(BlueprintInspectorLibrary_eventListSCSNodes_Parms), Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics
	{
		struct BlueprintInspectorLibrary_eventListVariables_Parms
		{
			UBlueprint* Blueprint;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListVariables_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintInspectorLibrary_eventListVariables_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintInspector" },
		{ "Comment", "/** List all user variables on the blueprint. */" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
		{ "ToolTip", "List all user variables on the blueprint." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintInspectorLibrary, nullptr, "ListVariables", nullptr, nullptr, sizeof(BlueprintInspectorLibrary_eventListVariables_Parms), Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UBlueprintInspectorLibrary_NoRegister()
	{
		return UBlueprintInspectorLibrary::StaticClass();
	}
	struct Z_Construct_UClass_UBlueprintInspectorLibrary_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UBlueprintInspectorLibrary_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UBlueprintInspectorLibrary_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UBlueprintInspectorLibrary_FindNodes, "FindNodes" }, // 3327087664
		{ &Z_Construct_UFunction_UBlueprintInspectorLibrary_GetNodeDetail, "GetNodeDetail" }, // 1971434343
		{ &Z_Construct_UFunction_UBlueprintInspectorLibrary_ListEventDispatchers, "ListEventDispatchers" }, // 2306234937
		{ &Z_Construct_UFunction_UBlueprintInspectorLibrary_ListFunctions, "ListFunctions" }, // 3820921049
		{ &Z_Construct_UFunction_UBlueprintInspectorLibrary_ListGraphs, "ListGraphs" }, // 2171272724
		{ &Z_Construct_UFunction_UBlueprintInspectorLibrary_ListInterfaces, "ListInterfaces" }, // 2251998511
		{ &Z_Construct_UFunction_UBlueprintInspectorLibrary_ListMacros, "ListMacros" }, // 3792556560
		{ &Z_Construct_UFunction_UBlueprintInspectorLibrary_ListNodesInGraph, "ListNodesInGraph" }, // 2162959404
		{ &Z_Construct_UFunction_UBlueprintInspectorLibrary_ListSCSNodes, "ListSCSNodes" }, // 4253563671
		{ &Z_Construct_UFunction_UBlueprintInspectorLibrary_ListVariables, "ListVariables" }, // 2065535015
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UBlueprintInspectorLibrary_Statics::Class_MetaDataParams[] = {
		{ "IncludePath", "BlueprintInspectorLibrary.h" },
		{ "ModuleRelativePath", "Public/BlueprintInspectorLibrary.h" },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UBlueprintInspectorLibrary_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UBlueprintInspectorLibrary>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UBlueprintInspectorLibrary_Statics::ClassParams = {
		&UBlueprintInspectorLibrary::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UBlueprintInspectorLibrary_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UBlueprintInspectorLibrary_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UBlueprintInspectorLibrary()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UBlueprintInspectorLibrary_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UBlueprintInspectorLibrary, 4243607253);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<UBlueprintInspectorLibrary>()
	{
		return UBlueprintInspectorLibrary::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UBlueprintInspectorLibrary(Z_Construct_UClass_UBlueprintInspectorLibrary, &UBlueprintInspectorLibrary::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("UBlueprintInspectorLibrary"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UBlueprintInspectorLibrary);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
