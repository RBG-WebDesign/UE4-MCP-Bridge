// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/WidgetBlueprintBuilderLibrary.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeWidgetBlueprintBuilderLibrary() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UWidgetBlueprintBuilderLibrary();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
	UMGEDITOR_API UClass* Z_Construct_UClass_UWidgetBlueprint_NoRegister();
// End Cross Module References
	DEFINE_FUNCTION(UWidgetBlueprintBuilderLibrary::execValidateWidgetJSON)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_JsonString);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UWidgetBlueprintBuilderLibrary::ValidateWidgetJSON(Z_Param_JsonString);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UWidgetBlueprintBuilderLibrary::execRebuildWidgetFromJSON)
	{
		P_GET_OBJECT(UWidgetBlueprint,Z_Param_WidgetBlueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_JsonString);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UWidgetBlueprintBuilderLibrary::RebuildWidgetFromJSON(Z_Param_WidgetBlueprint,Z_Param_JsonString);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UWidgetBlueprintBuilderLibrary::execBuildWidgetFromJSON)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_PackagePath);
		P_GET_PROPERTY(FStrProperty,Z_Param_AssetName);
		P_GET_PROPERTY(FStrProperty,Z_Param_JsonString);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UWidgetBlueprintBuilderLibrary::BuildWidgetFromJSON(Z_Param_PackagePath,Z_Param_AssetName,Z_Param_JsonString);
		P_NATIVE_END;
	}
	void UWidgetBlueprintBuilderLibrary::StaticRegisterNativesUWidgetBlueprintBuilderLibrary()
	{
		UClass* Class = UWidgetBlueprintBuilderLibrary::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "BuildWidgetFromJSON", &UWidgetBlueprintBuilderLibrary::execBuildWidgetFromJSON },
			{ "RebuildWidgetFromJSON", &UWidgetBlueprintBuilderLibrary::execRebuildWidgetFromJSON },
			{ "ValidateWidgetJSON", &UWidgetBlueprintBuilderLibrary::execValidateWidgetJSON },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics
	{
		struct WidgetBlueprintBuilderLibrary_eventBuildWidgetFromJSON_Parms
		{
			FString PackagePath;
			FString AssetName;
			FString JsonString;
			FString ReturnValue;
		};
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_PackagePath_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_PackagePath;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_AssetName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_AssetName;
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
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_PackagePath_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_PackagePath = { "PackagePath", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(WidgetBlueprintBuilderLibrary_eventBuildWidgetFromJSON_Parms, PackagePath), METADATA_PARAMS(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_PackagePath_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_PackagePath_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_AssetName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_AssetName = { "AssetName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(WidgetBlueprintBuilderLibrary_eventBuildWidgetFromJSON_Parms, AssetName), METADATA_PARAMS(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_AssetName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_AssetName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_JsonString_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_JsonString = { "JsonString", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(WidgetBlueprintBuilderLibrary_eventBuildWidgetFromJSON_Parms, JsonString), METADATA_PARAMS(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_JsonString_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_JsonString_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(WidgetBlueprintBuilderLibrary_eventBuildWidgetFromJSON_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_PackagePath,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_AssetName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_JsonString,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/**\n\x09 * Create a new UWidgetBlueprint from JSON.\n\x09 * Returns empty string on success, error message on failure.\n\x09 */" },
		{ "ModuleRelativePath", "Public/WidgetBlueprintBuilderLibrary.h" },
		{ "ToolTip", "Create a new UWidgetBlueprint from JSON.\nReturns empty string on success, error message on failure." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UWidgetBlueprintBuilderLibrary, nullptr, "BuildWidgetFromJSON", nullptr, nullptr, sizeof(WidgetBlueprintBuilderLibrary_eventBuildWidgetFromJSON_Parms), Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics
	{
		struct WidgetBlueprintBuilderLibrary_eventRebuildWidgetFromJSON_Parms
		{
			UWidgetBlueprint* WidgetBlueprint;
			FString JsonString;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_WidgetBlueprint;
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
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::NewProp_WidgetBlueprint = { "WidgetBlueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(WidgetBlueprintBuilderLibrary_eventRebuildWidgetFromJSON_Parms, WidgetBlueprint), Z_Construct_UClass_UWidgetBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::NewProp_JsonString_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::NewProp_JsonString = { "JsonString", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(WidgetBlueprintBuilderLibrary_eventRebuildWidgetFromJSON_Parms, JsonString), METADATA_PARAMS(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::NewProp_JsonString_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::NewProp_JsonString_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(WidgetBlueprintBuilderLibrary_eventRebuildWidgetFromJSON_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::NewProp_WidgetBlueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::NewProp_JsonString,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/**\n\x09 * Replace the widget tree of an existing UWidgetBlueprint from JSON.\n\x09 * Returns empty string on success, error message on failure.\n\x09 */" },
		{ "ModuleRelativePath", "Public/WidgetBlueprintBuilderLibrary.h" },
		{ "ToolTip", "Replace the widget tree of an existing UWidgetBlueprint from JSON.\nReturns empty string on success, error message on failure." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UWidgetBlueprintBuilderLibrary, nullptr, "RebuildWidgetFromJSON", nullptr, nullptr, sizeof(WidgetBlueprintBuilderLibrary_eventRebuildWidgetFromJSON_Parms), Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics
	{
		struct WidgetBlueprintBuilderLibrary_eventValidateWidgetJSON_Parms
		{
			FString JsonString;
			FString ReturnValue;
		};
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
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::NewProp_JsonString_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::NewProp_JsonString = { "JsonString", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(WidgetBlueprintBuilderLibrary_eventValidateWidgetJSON_Parms, JsonString), METADATA_PARAMS(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::NewProp_JsonString_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::NewProp_JsonString_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(WidgetBlueprintBuilderLibrary_eventValidateWidgetJSON_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::NewProp_JsonString,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/**\n\x09 * Validate JSON without creating an asset.\n\x09 * Returns empty string if valid, error message if invalid.\n\x09 */" },
		{ "ModuleRelativePath", "Public/WidgetBlueprintBuilderLibrary.h" },
		{ "ToolTip", "Validate JSON without creating an asset.\nReturns empty string if valid, error message if invalid." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UWidgetBlueprintBuilderLibrary, nullptr, "ValidateWidgetJSON", nullptr, nullptr, sizeof(WidgetBlueprintBuilderLibrary_eventValidateWidgetJSON_Parms), Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_NoRegister()
	{
		return UWidgetBlueprintBuilderLibrary::StaticClass();
	}
	struct Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_BuildWidgetFromJSON, "BuildWidgetFromJSON" }, // 1245467827
		{ &Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_RebuildWidgetFromJSON, "RebuildWidgetFromJSON" }, // 2265008160
		{ &Z_Construct_UFunction_UWidgetBlueprintBuilderLibrary_ValidateWidgetJSON, "ValidateWidgetJSON" }, // 1388835420
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_Statics::Class_MetaDataParams[] = {
		{ "IncludePath", "WidgetBlueprintBuilderLibrary.h" },
		{ "ModuleRelativePath", "Public/WidgetBlueprintBuilderLibrary.h" },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UWidgetBlueprintBuilderLibrary>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_Statics::ClassParams = {
		&UWidgetBlueprintBuilderLibrary::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UWidgetBlueprintBuilderLibrary()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UWidgetBlueprintBuilderLibrary_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UWidgetBlueprintBuilderLibrary, 1646115220);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<UWidgetBlueprintBuilderLibrary>()
	{
		return UWidgetBlueprintBuilderLibrary::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UWidgetBlueprintBuilderLibrary(Z_Construct_UClass_UWidgetBlueprintBuilderLibrary, &UWidgetBlueprintBuilderLibrary::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("UWidgetBlueprintBuilderLibrary"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UWidgetBlueprintBuilderLibrary);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
