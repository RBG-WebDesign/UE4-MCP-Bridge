// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/MCPBridgeInputLibrary.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeMCPBridgeInputLibrary() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UMCPBridgeInputLibrary_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UMCPBridgeInputLibrary();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
// End Cross Module References
	DEFINE_FUNCTION(UMCPBridgeInputLibrary::execIsValidKeyName)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_KeyName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UMCPBridgeInputLibrary::IsValidKeyName(Z_Param_KeyName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UMCPBridgeInputLibrary::execRemoveAxisMapping)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_AxisName);
		P_GET_PROPERTY(FStrProperty,Z_Param_KeyName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UMCPBridgeInputLibrary::RemoveAxisMapping(Z_Param_AxisName,Z_Param_KeyName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UMCPBridgeInputLibrary::execAddAxisMapping)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_AxisName);
		P_GET_PROPERTY(FStrProperty,Z_Param_KeyName);
		P_GET_PROPERTY(FFloatProperty,Z_Param_Scale);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UMCPBridgeInputLibrary::AddAxisMapping(Z_Param_AxisName,Z_Param_KeyName,Z_Param_Scale);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UMCPBridgeInputLibrary::execRemoveActionMapping)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_ActionName);
		P_GET_PROPERTY(FStrProperty,Z_Param_KeyName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UMCPBridgeInputLibrary::RemoveActionMapping(Z_Param_ActionName,Z_Param_KeyName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UMCPBridgeInputLibrary::execAddActionMapping)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_ActionName);
		P_GET_PROPERTY(FStrProperty,Z_Param_KeyName);
		P_GET_UBOOL(Z_Param_bShift);
		P_GET_UBOOL(Z_Param_bCtrl);
		P_GET_UBOOL(Z_Param_bAlt);
		P_GET_UBOOL(Z_Param_bCmd);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UMCPBridgeInputLibrary::AddActionMapping(Z_Param_ActionName,Z_Param_KeyName,Z_Param_bShift,Z_Param_bCtrl,Z_Param_bAlt,Z_Param_bCmd);
		P_NATIVE_END;
	}
	void UMCPBridgeInputLibrary::StaticRegisterNativesUMCPBridgeInputLibrary()
	{
		UClass* Class = UMCPBridgeInputLibrary::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "AddActionMapping", &UMCPBridgeInputLibrary::execAddActionMapping },
			{ "AddAxisMapping", &UMCPBridgeInputLibrary::execAddAxisMapping },
			{ "IsValidKeyName", &UMCPBridgeInputLibrary::execIsValidKeyName },
			{ "RemoveActionMapping", &UMCPBridgeInputLibrary::execRemoveActionMapping },
			{ "RemoveAxisMapping", &UMCPBridgeInputLibrary::execRemoveAxisMapping },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics
	{
		struct MCPBridgeInputLibrary_eventAddActionMapping_Parms
		{
			FString ActionName;
			FString KeyName;
			bool bShift;
			bool bCtrl;
			bool bAlt;
			bool bCmd;
			FString ReturnValue;
		};
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ActionName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ActionName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_KeyName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_KeyName;
		static void NewProp_bShift_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_bShift;
		static void NewProp_bCtrl_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_bCtrl;
		static void NewProp_bAlt_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_bAlt;
		static void NewProp_bCmd_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_bCmd;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_ActionName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_ActionName = { "ActionName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventAddActionMapping_Parms, ActionName), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_ActionName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_ActionName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_KeyName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_KeyName = { "KeyName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventAddActionMapping_Parms, KeyName), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_KeyName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_KeyName_MetaData)) };
	void Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bShift_SetBit(void* Obj)
	{
		((MCPBridgeInputLibrary_eventAddActionMapping_Parms*)Obj)->bShift = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bShift = { "bShift", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(MCPBridgeInputLibrary_eventAddActionMapping_Parms), &Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bShift_SetBit, METADATA_PARAMS(nullptr, 0) };
	void Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bCtrl_SetBit(void* Obj)
	{
		((MCPBridgeInputLibrary_eventAddActionMapping_Parms*)Obj)->bCtrl = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bCtrl = { "bCtrl", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(MCPBridgeInputLibrary_eventAddActionMapping_Parms), &Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bCtrl_SetBit, METADATA_PARAMS(nullptr, 0) };
	void Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bAlt_SetBit(void* Obj)
	{
		((MCPBridgeInputLibrary_eventAddActionMapping_Parms*)Obj)->bAlt = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bAlt = { "bAlt", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(MCPBridgeInputLibrary_eventAddActionMapping_Parms), &Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bAlt_SetBit, METADATA_PARAMS(nullptr, 0) };
	void Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bCmd_SetBit(void* Obj)
	{
		((MCPBridgeInputLibrary_eventAddActionMapping_Parms*)Obj)->bCmd = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bCmd = { "bCmd", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(MCPBridgeInputLibrary_eventAddActionMapping_Parms), &Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bCmd_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventAddActionMapping_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_ActionName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_KeyName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bShift,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bCtrl,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bAlt,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_bCmd,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "MCPBridgeInput" },
		{ "Comment", "/** Add an action mapping. Returns empty string on success, error message on failure. */" },
		{ "CPP_Default_bAlt", "false" },
		{ "CPP_Default_bCmd", "false" },
		{ "CPP_Default_bCtrl", "false" },
		{ "CPP_Default_bShift", "false" },
		{ "ModuleRelativePath", "Public/MCPBridgeInputLibrary.h" },
		{ "ToolTip", "Add an action mapping. Returns empty string on success, error message on failure." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UMCPBridgeInputLibrary, nullptr, "AddActionMapping", nullptr, nullptr, sizeof(MCPBridgeInputLibrary_eventAddActionMapping_Parms), Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics
	{
		struct MCPBridgeInputLibrary_eventAddAxisMapping_Parms
		{
			FString AxisName;
			FString KeyName;
			float Scale;
			FString ReturnValue;
		};
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_AxisName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_AxisName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_KeyName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_KeyName;
		static const UE4CodeGen_Private::FFloatPropertyParams NewProp_Scale;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_AxisName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_AxisName = { "AxisName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventAddAxisMapping_Parms, AxisName), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_AxisName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_AxisName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_KeyName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_KeyName = { "KeyName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventAddAxisMapping_Parms, KeyName), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_KeyName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_KeyName_MetaData)) };
	const UE4CodeGen_Private::FFloatPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_Scale = { "Scale", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventAddAxisMapping_Parms, Scale), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventAddAxisMapping_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_AxisName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_KeyName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_Scale,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "MCPBridgeInput" },
		{ "Comment", "/** Add an axis mapping with scale. Returns empty string on success. */" },
		{ "CPP_Default_Scale", "1.000000" },
		{ "ModuleRelativePath", "Public/MCPBridgeInputLibrary.h" },
		{ "ToolTip", "Add an axis mapping with scale. Returns empty string on success." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UMCPBridgeInputLibrary, nullptr, "AddAxisMapping", nullptr, nullptr, sizeof(MCPBridgeInputLibrary_eventAddAxisMapping_Parms), Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics
	{
		struct MCPBridgeInputLibrary_eventIsValidKeyName_Parms
		{
			FString KeyName;
			bool ReturnValue;
		};
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_KeyName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_KeyName;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::NewProp_KeyName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::NewProp_KeyName = { "KeyName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventIsValidKeyName_Parms, KeyName), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::NewProp_KeyName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::NewProp_KeyName_MetaData)) };
	void Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((MCPBridgeInputLibrary_eventIsValidKeyName_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(MCPBridgeInputLibrary_eventIsValidKeyName_Parms), &Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::NewProp_KeyName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "MCPBridgeInput" },
		{ "Comment", "/** Validate that a key name resolves to a real FKey. */" },
		{ "ModuleRelativePath", "Public/MCPBridgeInputLibrary.h" },
		{ "ToolTip", "Validate that a key name resolves to a real FKey." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UMCPBridgeInputLibrary, nullptr, "IsValidKeyName", nullptr, nullptr, sizeof(MCPBridgeInputLibrary_eventIsValidKeyName_Parms), Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics
	{
		struct MCPBridgeInputLibrary_eventRemoveActionMapping_Parms
		{
			FString ActionName;
			FString KeyName;
			FString ReturnValue;
		};
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ActionName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ActionName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_KeyName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_KeyName;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_ActionName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_ActionName = { "ActionName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventRemoveActionMapping_Parms, ActionName), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_ActionName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_ActionName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_KeyName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_KeyName = { "KeyName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventRemoveActionMapping_Parms, KeyName), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_KeyName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_KeyName_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventRemoveActionMapping_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_ActionName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_KeyName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "MCPBridgeInput" },
		{ "Comment", "/** Remove action mappings by name. Empty KeyName removes all keys for the action. */" },
		{ "ModuleRelativePath", "Public/MCPBridgeInputLibrary.h" },
		{ "ToolTip", "Remove action mappings by name. Empty KeyName removes all keys for the action." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UMCPBridgeInputLibrary, nullptr, "RemoveActionMapping", nullptr, nullptr, sizeof(MCPBridgeInputLibrary_eventRemoveActionMapping_Parms), Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics
	{
		struct MCPBridgeInputLibrary_eventRemoveAxisMapping_Parms
		{
			FString AxisName;
			FString KeyName;
			FString ReturnValue;
		};
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_AxisName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_AxisName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_KeyName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_KeyName;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_AxisName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_AxisName = { "AxisName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventRemoveAxisMapping_Parms, AxisName), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_AxisName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_AxisName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_KeyName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_KeyName = { "KeyName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventRemoveAxisMapping_Parms, KeyName), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_KeyName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_KeyName_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeInputLibrary_eventRemoveAxisMapping_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_AxisName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_KeyName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "MCPBridgeInput" },
		{ "Comment", "/** Remove axis mappings by name. Empty KeyName removes all keys for the axis. */" },
		{ "ModuleRelativePath", "Public/MCPBridgeInputLibrary.h" },
		{ "ToolTip", "Remove axis mappings by name. Empty KeyName removes all keys for the axis." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UMCPBridgeInputLibrary, nullptr, "RemoveAxisMapping", nullptr, nullptr, sizeof(MCPBridgeInputLibrary_eventRemoveAxisMapping_Parms), Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UMCPBridgeInputLibrary_NoRegister()
	{
		return UMCPBridgeInputLibrary::StaticClass();
	}
	struct Z_Construct_UClass_UMCPBridgeInputLibrary_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UMCPBridgeInputLibrary_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UMCPBridgeInputLibrary_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UMCPBridgeInputLibrary_AddActionMapping, "AddActionMapping" }, // 2445371814
		{ &Z_Construct_UFunction_UMCPBridgeInputLibrary_AddAxisMapping, "AddAxisMapping" }, // 820745219
		{ &Z_Construct_UFunction_UMCPBridgeInputLibrary_IsValidKeyName, "IsValidKeyName" }, // 3646825952
		{ &Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveActionMapping, "RemoveActionMapping" }, // 372865830
		{ &Z_Construct_UFunction_UMCPBridgeInputLibrary_RemoveAxisMapping, "RemoveAxisMapping" }, // 1313691647
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UMCPBridgeInputLibrary_Statics::Class_MetaDataParams[] = {
		{ "Comment", "/**\n * Input mapping helpers for the MCP bridge.\n *\n * UE4.27 Python cannot construct FKey (the struct exposes no init\n * parameters to the reflection layer), so action/axis mapping mutation\n * has to happen in C++. Key names use FKey string form, e.g. \"SpaceBar\",\n * \"W\", \"LeftMouseButton\", \"Gamepad_LeftX\", \"MouseX\".\n */" },
		{ "IncludePath", "MCPBridgeInputLibrary.h" },
		{ "ModuleRelativePath", "Public/MCPBridgeInputLibrary.h" },
		{ "ToolTip", "Input mapping helpers for the MCP bridge.\n\nUE4.27 Python cannot construct FKey (the struct exposes no init\nparameters to the reflection layer), so action/axis mapping mutation\nhas to happen in C++. Key names use FKey string form, e.g. \"SpaceBar\",\n\"W\", \"LeftMouseButton\", \"Gamepad_LeftX\", \"MouseX\"." },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UMCPBridgeInputLibrary_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UMCPBridgeInputLibrary>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UMCPBridgeInputLibrary_Statics::ClassParams = {
		&UMCPBridgeInputLibrary::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UMCPBridgeInputLibrary_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UMCPBridgeInputLibrary_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UMCPBridgeInputLibrary()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UMCPBridgeInputLibrary_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UMCPBridgeInputLibrary, 1101216802);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<UMCPBridgeInputLibrary>()
	{
		return UMCPBridgeInputLibrary::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UMCPBridgeInputLibrary(Z_Construct_UClass_UMCPBridgeInputLibrary, &UMCPBridgeInputLibrary::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("UMCPBridgeInputLibrary"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UMCPBridgeInputLibrary);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
