// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/AnimBlueprintBuilderLibrary.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeAnimBlueprintBuilderLibrary() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UAnimBlueprintBuilderLibrary_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UAnimBlueprintBuilderLibrary();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
	ENGINE_API UClass* Z_Construct_UClass_UAnimBlueprint_NoRegister();
// End Cross Module References
	DEFINE_FUNCTION(UAnimBlueprintBuilderLibrary::execValidateAnimBlueprintJSON)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_JsonString);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UAnimBlueprintBuilderLibrary::ValidateAnimBlueprintJSON(Z_Param_JsonString);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UAnimBlueprintBuilderLibrary::execRebuildAnimBlueprintFromJSON)
	{
		P_GET_OBJECT(UAnimBlueprint,Z_Param_AnimBlueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_JsonString);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UAnimBlueprintBuilderLibrary::RebuildAnimBlueprintFromJSON(Z_Param_AnimBlueprint,Z_Param_JsonString);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UAnimBlueprintBuilderLibrary::execBuildAnimBlueprintFromJSON)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_PackagePath);
		P_GET_PROPERTY(FStrProperty,Z_Param_AssetName);
		P_GET_PROPERTY(FStrProperty,Z_Param_SkeletonPath);
		P_GET_PROPERTY(FStrProperty,Z_Param_JsonString);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UAnimBlueprintBuilderLibrary::BuildAnimBlueprintFromJSON(Z_Param_PackagePath,Z_Param_AssetName,Z_Param_SkeletonPath,Z_Param_JsonString);
		P_NATIVE_END;
	}
	void UAnimBlueprintBuilderLibrary::StaticRegisterNativesUAnimBlueprintBuilderLibrary()
	{
		UClass* Class = UAnimBlueprintBuilderLibrary::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "BuildAnimBlueprintFromJSON", &UAnimBlueprintBuilderLibrary::execBuildAnimBlueprintFromJSON },
			{ "RebuildAnimBlueprintFromJSON", &UAnimBlueprintBuilderLibrary::execRebuildAnimBlueprintFromJSON },
			{ "ValidateAnimBlueprintJSON", &UAnimBlueprintBuilderLibrary::execValidateAnimBlueprintJSON },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics
	{
		struct AnimBlueprintBuilderLibrary_eventBuildAnimBlueprintFromJSON_Parms
		{
			FString PackagePath;
			FString AssetName;
			FString SkeletonPath;
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
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_SkeletonPath_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_SkeletonPath;
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
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_PackagePath_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_PackagePath = { "PackagePath", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AnimBlueprintBuilderLibrary_eventBuildAnimBlueprintFromJSON_Parms, PackagePath), METADATA_PARAMS(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_PackagePath_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_PackagePath_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_AssetName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_AssetName = { "AssetName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AnimBlueprintBuilderLibrary_eventBuildAnimBlueprintFromJSON_Parms, AssetName), METADATA_PARAMS(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_AssetName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_AssetName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_SkeletonPath_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_SkeletonPath = { "SkeletonPath", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AnimBlueprintBuilderLibrary_eventBuildAnimBlueprintFromJSON_Parms, SkeletonPath), METADATA_PARAMS(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_SkeletonPath_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_SkeletonPath_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_JsonString_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_JsonString = { "JsonString", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AnimBlueprintBuilderLibrary_eventBuildAnimBlueprintFromJSON_Parms, JsonString), METADATA_PARAMS(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_JsonString_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_JsonString_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AnimBlueprintBuilderLibrary_eventBuildAnimBlueprintFromJSON_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_PackagePath,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_AssetName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_SkeletonPath,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_JsonString,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/**\n\x09 * Create a new AnimBlueprint from JSON.\n\x09 * Returns empty string on success, error message on failure.\n\x09 */" },
		{ "ModuleRelativePath", "Public/AnimBlueprintBuilderLibrary.h" },
		{ "ToolTip", "Create a new AnimBlueprint from JSON.\nReturns empty string on success, error message on failure." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UAnimBlueprintBuilderLibrary, nullptr, "BuildAnimBlueprintFromJSON", nullptr, nullptr, sizeof(AnimBlueprintBuilderLibrary_eventBuildAnimBlueprintFromJSON_Parms), Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics
	{
		struct AnimBlueprintBuilderLibrary_eventRebuildAnimBlueprintFromJSON_Parms
		{
			UAnimBlueprint* AnimBlueprint;
			FString JsonString;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_AnimBlueprint;
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
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::NewProp_AnimBlueprint = { "AnimBlueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AnimBlueprintBuilderLibrary_eventRebuildAnimBlueprintFromJSON_Parms, AnimBlueprint), Z_Construct_UClass_UAnimBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::NewProp_JsonString_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::NewProp_JsonString = { "JsonString", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AnimBlueprintBuilderLibrary_eventRebuildAnimBlueprintFromJSON_Parms, JsonString), METADATA_PARAMS(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::NewProp_JsonString_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::NewProp_JsonString_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AnimBlueprintBuilderLibrary_eventRebuildAnimBlueprintFromJSON_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::NewProp_AnimBlueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::NewProp_JsonString,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/**\n\x09 * Replace the contents of an existing AnimBlueprint from JSON.\n\x09 * Returns empty string on success, error message on failure.\n\x09 */" },
		{ "ModuleRelativePath", "Public/AnimBlueprintBuilderLibrary.h" },
		{ "ToolTip", "Replace the contents of an existing AnimBlueprint from JSON.\nReturns empty string on success, error message on failure." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UAnimBlueprintBuilderLibrary, nullptr, "RebuildAnimBlueprintFromJSON", nullptr, nullptr, sizeof(AnimBlueprintBuilderLibrary_eventRebuildAnimBlueprintFromJSON_Parms), Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics
	{
		struct AnimBlueprintBuilderLibrary_eventValidateAnimBlueprintJSON_Parms
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
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::NewProp_JsonString_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::NewProp_JsonString = { "JsonString", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AnimBlueprintBuilderLibrary_eventValidateAnimBlueprintJSON_Parms, JsonString), METADATA_PARAMS(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::NewProp_JsonString_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::NewProp_JsonString_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AnimBlueprintBuilderLibrary_eventValidateAnimBlueprintJSON_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::NewProp_JsonString,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/**\n\x09 * Validate JSON without creating an asset.\n\x09 * Returns empty string if valid, error message if invalid.\n\x09 */" },
		{ "ModuleRelativePath", "Public/AnimBlueprintBuilderLibrary.h" },
		{ "ToolTip", "Validate JSON without creating an asset.\nReturns empty string if valid, error message if invalid." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UAnimBlueprintBuilderLibrary, nullptr, "ValidateAnimBlueprintJSON", nullptr, nullptr, sizeof(AnimBlueprintBuilderLibrary_eventValidateAnimBlueprintJSON_Parms), Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UAnimBlueprintBuilderLibrary_NoRegister()
	{
		return UAnimBlueprintBuilderLibrary::StaticClass();
	}
	struct Z_Construct_UClass_UAnimBlueprintBuilderLibrary_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UAnimBlueprintBuilderLibrary_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UAnimBlueprintBuilderLibrary_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_BuildAnimBlueprintFromJSON, "BuildAnimBlueprintFromJSON" }, // 1710441181
		{ &Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_RebuildAnimBlueprintFromJSON, "RebuildAnimBlueprintFromJSON" }, // 365173920
		{ &Z_Construct_UFunction_UAnimBlueprintBuilderLibrary_ValidateAnimBlueprintJSON, "ValidateAnimBlueprintJSON" }, // 3974583979
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UAnimBlueprintBuilderLibrary_Statics::Class_MetaDataParams[] = {
		{ "IncludePath", "AnimBlueprintBuilderLibrary.h" },
		{ "ModuleRelativePath", "Public/AnimBlueprintBuilderLibrary.h" },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UAnimBlueprintBuilderLibrary_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UAnimBlueprintBuilderLibrary>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UAnimBlueprintBuilderLibrary_Statics::ClassParams = {
		&UAnimBlueprintBuilderLibrary::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UAnimBlueprintBuilderLibrary_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UAnimBlueprintBuilderLibrary_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UAnimBlueprintBuilderLibrary()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UAnimBlueprintBuilderLibrary_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UAnimBlueprintBuilderLibrary, 3881196016);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<UAnimBlueprintBuilderLibrary>()
	{
		return UAnimBlueprintBuilderLibrary::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UAnimBlueprintBuilderLibrary(Z_Construct_UClass_UAnimBlueprintBuilderLibrary, &UAnimBlueprintBuilderLibrary::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("UAnimBlueprintBuilderLibrary"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UAnimBlueprintBuilderLibrary);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
