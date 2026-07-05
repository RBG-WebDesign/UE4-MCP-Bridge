// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/MCPBridgeAILibrary.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeMCPBridgeAILibrary() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UMCPBridgeAILibrary_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UMCPBridgeAILibrary();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
	AIMODULE_API UClass* Z_Construct_UClass_UBlackboardData_NoRegister();
	AIMODULE_API UClass* Z_Construct_UClass_UBehaviorTree_NoRegister();
// End Cross Module References
	DEFINE_FUNCTION(UMCPBridgeAILibrary::execListBlackboardKeys)
	{
		P_GET_OBJECT(UBlackboardData,Z_Param_Blackboard);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UMCPBridgeAILibrary::ListBlackboardKeys(Z_Param_Blackboard);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UMCPBridgeAILibrary::execSetBlackboardAsset)
	{
		P_GET_OBJECT(UBehaviorTree,Z_Param_BehaviorTree);
		P_GET_OBJECT(UBlackboardData,Z_Param_Blackboard);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UMCPBridgeAILibrary::SetBlackboardAsset(Z_Param_BehaviorTree,Z_Param_Blackboard);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UMCPBridgeAILibrary::execAddBlackboardKeys)
	{
		P_GET_OBJECT(UBlackboardData,Z_Param_Blackboard);
		P_GET_PROPERTY(FStrProperty,Z_Param_KeysJson);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UMCPBridgeAILibrary::AddBlackboardKeys(Z_Param_Blackboard,Z_Param_KeysJson);
		P_NATIVE_END;
	}
	void UMCPBridgeAILibrary::StaticRegisterNativesUMCPBridgeAILibrary()
	{
		UClass* Class = UMCPBridgeAILibrary::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "AddBlackboardKeys", &UMCPBridgeAILibrary::execAddBlackboardKeys },
			{ "ListBlackboardKeys", &UMCPBridgeAILibrary::execListBlackboardKeys },
			{ "SetBlackboardAsset", &UMCPBridgeAILibrary::execSetBlackboardAsset },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics
	{
		struct MCPBridgeAILibrary_eventAddBlackboardKeys_Parms
		{
			UBlackboardData* Blackboard;
			FString KeysJson;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blackboard;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_KeysJson_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_KeysJson;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::NewProp_Blackboard = { "Blackboard", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeAILibrary_eventAddBlackboardKeys_Parms, Blackboard), Z_Construct_UClass_UBlackboardData_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::NewProp_KeysJson_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::NewProp_KeysJson = { "KeysJson", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeAILibrary_eventAddBlackboardKeys_Parms, KeysJson), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::NewProp_KeysJson_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::NewProp_KeysJson_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeAILibrary_eventAddBlackboardKeys_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::NewProp_Blackboard,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::NewProp_KeysJson,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "MCPBridgeAI" },
		{ "Comment", "/**\n\x09 * Add keys to a BlackboardData asset. KeysJson is an array:\n\x09 * [{\"name\": \"TargetActor\", \"type\": \"Object\", \"base_class\": \"/Script/Engine.Actor\"}, ...]\n\x09 * Existing keys with the same name are skipped (idempotent).\n\x09 * Returns empty string on success, error message on failure.\n\x09 */" },
		{ "ModuleRelativePath", "Public/MCPBridgeAILibrary.h" },
		{ "ToolTip", "Add keys to a BlackboardData asset. KeysJson is an array:\n[{\"name\": \"TargetActor\", \"type\": \"Object\", \"base_class\": \"/Script/Engine.Actor\"}, ...]\nExisting keys with the same name are skipped (idempotent).\nReturns empty string on success, error message on failure." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UMCPBridgeAILibrary, nullptr, "AddBlackboardKeys", nullptr, nullptr, sizeof(MCPBridgeAILibrary_eventAddBlackboardKeys_Parms), Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics
	{
		struct MCPBridgeAILibrary_eventListBlackboardKeys_Parms
		{
			UBlackboardData* Blackboard;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blackboard;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::NewProp_Blackboard = { "Blackboard", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeAILibrary_eventListBlackboardKeys_Parms, Blackboard), Z_Construct_UClass_UBlackboardData_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeAILibrary_eventListBlackboardKeys_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::NewProp_Blackboard,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "MCPBridgeAI" },
		{ "Comment", "/** List key names and types on a BlackboardData asset as JSON. */" },
		{ "ModuleRelativePath", "Public/MCPBridgeAILibrary.h" },
		{ "ToolTip", "List key names and types on a BlackboardData asset as JSON." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UMCPBridgeAILibrary, nullptr, "ListBlackboardKeys", nullptr, nullptr, sizeof(MCPBridgeAILibrary_eventListBlackboardKeys_Parms), Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics
	{
		struct MCPBridgeAILibrary_eventSetBlackboardAsset_Parms
		{
			UBehaviorTree* BehaviorTree;
			UBlackboardData* Blackboard;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_BehaviorTree;
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blackboard;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::NewProp_BehaviorTree = { "BehaviorTree", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeAILibrary_eventSetBlackboardAsset_Parms, BehaviorTree), Z_Construct_UClass_UBehaviorTree_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::NewProp_Blackboard = { "Blackboard", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeAILibrary_eventSetBlackboardAsset_Parms, Blackboard), Z_Construct_UClass_UBlackboardData_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeAILibrary_eventSetBlackboardAsset_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::NewProp_BehaviorTree,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::NewProp_Blackboard,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "MCPBridgeAI" },
		{ "Comment", "/** Assign the blackboard asset on a BehaviorTree. Returns empty string on success. */" },
		{ "ModuleRelativePath", "Public/MCPBridgeAILibrary.h" },
		{ "ToolTip", "Assign the blackboard asset on a BehaviorTree. Returns empty string on success." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UMCPBridgeAILibrary, nullptr, "SetBlackboardAsset", nullptr, nullptr, sizeof(MCPBridgeAILibrary_eventSetBlackboardAsset_Parms), Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UMCPBridgeAILibrary_NoRegister()
	{
		return UMCPBridgeAILibrary::StaticClass();
	}
	struct Z_Construct_UClass_UMCPBridgeAILibrary_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UMCPBridgeAILibrary_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UMCPBridgeAILibrary_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UMCPBridgeAILibrary_AddBlackboardKeys, "AddBlackboardKeys" }, // 2418654678
		{ &Z_Construct_UFunction_UMCPBridgeAILibrary_ListBlackboardKeys, "ListBlackboardKeys" }, // 2034976023
		{ &Z_Construct_UFunction_UMCPBridgeAILibrary_SetBlackboardAsset, "SetBlackboardAsset" }, // 2375810420
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UMCPBridgeAILibrary_Statics::Class_MetaDataParams[] = {
		{ "Comment", "/**\n * AI asset helpers for the MCP bridge.\n *\n * UE4.27 protects UBehaviorTree::BlackboardAsset and FBlackboardEntry from\n * Python reflection, so blackboard wiring happens here. Key types: Bool,\n * Int, Float, String, Name, Vector, Rotator, Object, Class.\n */" },
		{ "IncludePath", "MCPBridgeAILibrary.h" },
		{ "ModuleRelativePath", "Public/MCPBridgeAILibrary.h" },
		{ "ToolTip", "AI asset helpers for the MCP bridge.\n\nUE4.27 protects UBehaviorTree::BlackboardAsset and FBlackboardEntry from\nPython reflection, so blackboard wiring happens here. Key types: Bool,\nInt, Float, String, Name, Vector, Rotator, Object, Class." },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UMCPBridgeAILibrary_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UMCPBridgeAILibrary>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UMCPBridgeAILibrary_Statics::ClassParams = {
		&UMCPBridgeAILibrary::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UMCPBridgeAILibrary_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UMCPBridgeAILibrary_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UMCPBridgeAILibrary()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UMCPBridgeAILibrary_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UMCPBridgeAILibrary, 2838917680);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<UMCPBridgeAILibrary>()
	{
		return UMCPBridgeAILibrary::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UMCPBridgeAILibrary(Z_Construct_UClass_UMCPBridgeAILibrary, &UMCPBridgeAILibrary::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("UMCPBridgeAILibrary"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UMCPBridgeAILibrary);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
