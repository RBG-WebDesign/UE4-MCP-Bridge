// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/BehaviorTreeBuilderLibrary.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeBehaviorTreeBuilderLibrary() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UBehaviorTreeBuilderLibrary_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UBehaviorTreeBuilderLibrary();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
	AIMODULE_API UClass* Z_Construct_UClass_UBehaviorTree_NoRegister();
// End Cross Module References
	DEFINE_FUNCTION(UBehaviorTreeBuilderLibrary::execBuildBehaviorTreeFromJSON)
	{
		P_GET_OBJECT(UBehaviorTree,Z_Param_BehaviorTree);
		P_GET_PROPERTY(FStrProperty,Z_Param_JsonString);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBehaviorTreeBuilderLibrary::BuildBehaviorTreeFromJSON(Z_Param_BehaviorTree,Z_Param_JsonString);
		P_NATIVE_END;
	}
	void UBehaviorTreeBuilderLibrary::StaticRegisterNativesUBehaviorTreeBuilderLibrary()
	{
		UClass* Class = UBehaviorTreeBuilderLibrary::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "BuildBehaviorTreeFromJSON", &UBehaviorTreeBuilderLibrary::execBuildBehaviorTreeFromJSON },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics
	{
		struct BehaviorTreeBuilderLibrary_eventBuildBehaviorTreeFromJSON_Parms
		{
			UBehaviorTree* BehaviorTree;
			FString JsonString;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_BehaviorTree;
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
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::NewProp_BehaviorTree = { "BehaviorTree", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BehaviorTreeBuilderLibrary_eventBuildBehaviorTreeFromJSON_Parms, BehaviorTree), Z_Construct_UClass_UBehaviorTree_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::NewProp_JsonString_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::NewProp_JsonString = { "JsonString", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BehaviorTreeBuilderLibrary_eventBuildBehaviorTreeFromJSON_Parms, JsonString), METADATA_PARAMS(Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::NewProp_JsonString_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::NewProp_JsonString_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BehaviorTreeBuilderLibrary_eventBuildBehaviorTreeFromJSON_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::NewProp_BehaviorTree,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::NewProp_JsonString,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/**\n\x09 * Build a BehaviorTree node graph from JSON.\n\x09 * The BehaviorTree must already exist and have its BlackboardAsset assigned.\n\x09 * Returns empty string on success, error message on failure.\n\x09 */" },
		{ "ModuleRelativePath", "Public/BehaviorTreeBuilderLibrary.h" },
		{ "ToolTip", "Build a BehaviorTree node graph from JSON.\nThe BehaviorTree must already exist and have its BlackboardAsset assigned.\nReturns empty string on success, error message on failure." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBehaviorTreeBuilderLibrary, nullptr, "BuildBehaviorTreeFromJSON", nullptr, nullptr, sizeof(BehaviorTreeBuilderLibrary_eventBuildBehaviorTreeFromJSON_Parms), Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UBehaviorTreeBuilderLibrary_NoRegister()
	{
		return UBehaviorTreeBuilderLibrary::StaticClass();
	}
	struct Z_Construct_UClass_UBehaviorTreeBuilderLibrary_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UBehaviorTreeBuilderLibrary_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UBehaviorTreeBuilderLibrary_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UBehaviorTreeBuilderLibrary_BuildBehaviorTreeFromJSON, "BuildBehaviorTreeFromJSON" }, // 3615680373
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UBehaviorTreeBuilderLibrary_Statics::Class_MetaDataParams[] = {
		{ "IncludePath", "BehaviorTreeBuilderLibrary.h" },
		{ "ModuleRelativePath", "Public/BehaviorTreeBuilderLibrary.h" },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UBehaviorTreeBuilderLibrary_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UBehaviorTreeBuilderLibrary>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UBehaviorTreeBuilderLibrary_Statics::ClassParams = {
		&UBehaviorTreeBuilderLibrary::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UBehaviorTreeBuilderLibrary_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UBehaviorTreeBuilderLibrary_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UBehaviorTreeBuilderLibrary()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UBehaviorTreeBuilderLibrary_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UBehaviorTreeBuilderLibrary, 486814500);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<UBehaviorTreeBuilderLibrary>()
	{
		return UBehaviorTreeBuilderLibrary::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UBehaviorTreeBuilderLibrary(Z_Construct_UClass_UBehaviorTreeBuilderLibrary, &UBehaviorTreeBuilderLibrary::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("UBehaviorTreeBuilderLibrary"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UBehaviorTreeBuilderLibrary);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
