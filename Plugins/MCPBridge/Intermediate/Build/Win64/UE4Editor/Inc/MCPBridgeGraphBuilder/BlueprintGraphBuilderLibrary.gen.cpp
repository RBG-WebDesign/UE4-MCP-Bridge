// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/BlueprintGraphBuilderLibrary.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeBlueprintGraphBuilderLibrary() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UBlueprintGraphBuilderLibrary_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UBlueprintGraphBuilderLibrary();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprint_NoRegister();
	COREUOBJECT_API UClass* Z_Construct_UClass_UClass();
	ENGINE_API UClass* Z_Construct_UClass_UActorComponent_NoRegister();
	ENGINE_API UClass* Z_Construct_UClass_UUserDefinedEnum_NoRegister();
// End Cross Module References
	DEFINE_FUNCTION(UBlueprintGraphBuilderLibrary::execConfigureUserDefinedEnum)
	{
		P_GET_OBJECT(UUserDefinedEnum,Z_Param_Enum);
		P_GET_TARRAY_REF(FString,Z_Param_Out_DisplayNames);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintGraphBuilderLibrary::ConfigureUserDefinedEnum(Z_Param_Enum,Z_Param_Out_DisplayNames);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintGraphBuilderLibrary::execCompileAndReport)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintGraphBuilderLibrary::CompileAndReport(Z_Param_Blueprint);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintGraphBuilderLibrary::execSetComponentProperty)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_ComponentName);
		P_GET_PROPERTY(FStrProperty,Z_Param_PropertyName);
		P_GET_PROPERTY(FStrProperty,Z_Param_JsonValue);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintGraphBuilderLibrary::SetComponentProperty(Z_Param_Blueprint,Z_Param_ComponentName,Z_Param_PropertyName,Z_Param_JsonValue);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintGraphBuilderLibrary::execAddComponentToBlueprint)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_OBJECT(UClass,Z_Param_ComponentClass);
		P_GET_PROPERTY(FStrProperty,Z_Param_ComponentName);
		P_GET_PROPERTY(FStrProperty,Z_Param_AttachToName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintGraphBuilderLibrary::AddComponentToBlueprint(Z_Param_Blueprint,Z_Param_ComponentClass,Z_Param_ComponentName,Z_Param_AttachToName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintGraphBuilderLibrary::execBuildBlueprintFromJSON)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_JsonString);
		P_GET_UBOOL(Z_Param_bClearExistingGraph);
		P_FINISH;
		P_NATIVE_BEGIN;
		UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON(Z_Param_Blueprint,Z_Param_JsonString,Z_Param_bClearExistingGraph);
		P_NATIVE_END;
	}
	void UBlueprintGraphBuilderLibrary::StaticRegisterNativesUBlueprintGraphBuilderLibrary()
	{
		UClass* Class = UBlueprintGraphBuilderLibrary::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "AddComponentToBlueprint", &UBlueprintGraphBuilderLibrary::execAddComponentToBlueprint },
			{ "BuildBlueprintFromJSON", &UBlueprintGraphBuilderLibrary::execBuildBlueprintFromJSON },
			{ "CompileAndReport", &UBlueprintGraphBuilderLibrary::execCompileAndReport },
			{ "ConfigureUserDefinedEnum", &UBlueprintGraphBuilderLibrary::execConfigureUserDefinedEnum },
			{ "SetComponentProperty", &UBlueprintGraphBuilderLibrary::execSetComponentProperty },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics
	{
		struct BlueprintGraphBuilderLibrary_eventAddComponentToBlueprint_Parms
		{
			UBlueprint* Blueprint;
			TSubclassOf<UActorComponent>  ComponentClass;
			FString ComponentName;
			FString AttachToName;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
		static const UE4CodeGen_Private::FClassPropertyParams NewProp_ComponentClass;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ComponentName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ComponentName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_AttachToName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_AttachToName;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventAddComponentToBlueprint_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FClassPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ComponentClass = { "ComponentClass", nullptr, (EPropertyFlags)0x0014000000000080, UE4CodeGen_Private::EPropertyGenFlags::Class, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventAddComponentToBlueprint_Parms, ComponentClass), Z_Construct_UClass_UActorComponent_NoRegister, Z_Construct_UClass_UClass, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ComponentName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ComponentName = { "ComponentName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventAddComponentToBlueprint_Parms, ComponentName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ComponentName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ComponentName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_AttachToName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_AttachToName = { "AttachToName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventAddComponentToBlueprint_Parms, AttachToName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_AttachToName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_AttachToName_MetaData)) };
	void Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintGraphBuilderLibrary_eventAddComponentToBlueprint_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintGraphBuilderLibrary_eventAddComponentToBlueprint_Parms), &Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ComponentClass,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ComponentName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_AttachToName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/** Add a component to a Blueprint's SimpleConstructionScript.\n     *  This is the missing piece that lets Python build proper Blueprint actors\n     *  with components (BoxComponent, CameraComponent, etc.) instead of\n     *  falling back to spawning raw actors in the world.\n     *\n     *  @param Blueprint      Target Blueprint to add the component to\n     *  @param ComponentClass The component class (e.g. UBoxComponent, UCameraComponent)\n     *  @param ComponentName  Name for the new component\n     *  @param AttachToName   Name of parent component to attach to (empty = root)\n     *  @return True if the component was added successfully\n     */" },
		{ "CPP_Default_AttachToName", "" },
		{ "ModuleRelativePath", "Public/BlueprintGraphBuilderLibrary.h" },
		{ "ToolTip", "Add a component to a Blueprint's SimpleConstructionScript.\nThis is the missing piece that lets Python build proper Blueprint actors\nwith components (BoxComponent, CameraComponent, etc.) instead of\nfalling back to spawning raw actors in the world.\n\n@param Blueprint      Target Blueprint to add the component to\n@param ComponentClass The component class (e.g. UBoxComponent, UCameraComponent)\n@param ComponentName  Name for the new component\n@param AttachToName   Name of parent component to attach to (empty = root)\n@return True if the component was added successfully" },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintGraphBuilderLibrary, nullptr, "AddComponentToBlueprint", nullptr, nullptr, sizeof(BlueprintGraphBuilderLibrary_eventAddComponentToBlueprint_Parms), Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics
	{
		struct BlueprintGraphBuilderLibrary_eventBuildBlueprintFromJSON_Parms
		{
			UBlueprint* Blueprint;
			FString JsonString;
			bool bClearExistingGraph;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_JsonString_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_JsonString;
		static void NewProp_bClearExistingGraph_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_bClearExistingGraph;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventBuildBlueprintFromJSON_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_JsonString_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_JsonString = { "JsonString", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventBuildBlueprintFromJSON_Parms, JsonString), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_JsonString_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_JsonString_MetaData)) };
	void Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_bClearExistingGraph_SetBit(void* Obj)
	{
		((BlueprintGraphBuilderLibrary_eventBuildBlueprintFromJSON_Parms*)Obj)->bClearExistingGraph = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_bClearExistingGraph = { "bClearExistingGraph", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintGraphBuilderLibrary_eventBuildBlueprintFromJSON_Parms), &Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_bClearExistingGraph_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_JsonString,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::NewProp_bClearExistingGraph,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/**\n     * Builds a Blueprint event graph from a JSON description.\n     *\n     * JSON format:\n     * {\n     *   \"nodes\": [{\"id\": \"start\", \"type\": \"BeginPlay\"}, {\"id\": \"print\", \"type\": \"PrintString\"}],\n     *   \"connections\": [{\"from\": \"start.exec\", \"to\": \"print.exec\"}]\n     * }\n     *\n     * Supported types (Pass 1): BeginPlay, PrintString\n     * Connection pin roles: exec (output Then on source, input Execute on target)\n     */" },
		{ "CPP_Default_bClearExistingGraph", "true" },
		{ "ModuleRelativePath", "Public/BlueprintGraphBuilderLibrary.h" },
		{ "ToolTip", "Builds a Blueprint event graph from a JSON description.\n\nJSON format:\n{\n  \"nodes\": [{\"id\": \"start\", \"type\": \"BeginPlay\"}, {\"id\": \"print\", \"type\": \"PrintString\"}],\n  \"connections\": [{\"from\": \"start.exec\", \"to\": \"print.exec\"}]\n}\n\nSupported types (Pass 1): BeginPlay, PrintString\nConnection pin roles: exec (output Then on source, input Execute on target)" },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintGraphBuilderLibrary, nullptr, "BuildBlueprintFromJSON", nullptr, nullptr, sizeof(BlueprintGraphBuilderLibrary_eventBuildBlueprintFromJSON_Parms), Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics
	{
		struct BlueprintGraphBuilderLibrary_eventCompileAndReport_Parms
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
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventCompileAndReport_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventCompileAndReport_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/** Compile a Blueprint and return a JSON report with {success, status, errors[], warnings[]}.\n     *  status values: \"UpToDate\" | \"UpToDateWithWarnings\" | \"Error\" | \"Dirty\" | \"Unknown\" */" },
		{ "ModuleRelativePath", "Public/BlueprintGraphBuilderLibrary.h" },
		{ "ToolTip", "Compile a Blueprint and return a JSON report with {success, status, errors[], warnings[]}.\nstatus values: \"UpToDate\" | \"UpToDateWithWarnings\" | \"Error\" | \"Dirty\" | \"Unknown\"" },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintGraphBuilderLibrary, nullptr, "CompileAndReport", nullptr, nullptr, sizeof(BlueprintGraphBuilderLibrary_eventCompileAndReport_Parms), Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics
	{
		struct BlueprintGraphBuilderLibrary_eventConfigureUserDefinedEnum_Parms
		{
			UUserDefinedEnum* Enum;
			TArray<FString> DisplayNames;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Enum;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_DisplayNames_Inner;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_DisplayNames_MetaData[];
#endif
		static const UE4CodeGen_Private::FArrayPropertyParams NewProp_DisplayNames;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_Enum = { "Enum", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventConfigureUserDefinedEnum_Parms, Enum), Z_Construct_UClass_UUserDefinedEnum_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_DisplayNames_Inner = { "DisplayNames", nullptr, (EPropertyFlags)0x0000000000000000, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, 0, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_DisplayNames_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FArrayPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_DisplayNames = { "DisplayNames", nullptr, (EPropertyFlags)0x0010000008000182, UE4CodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventConfigureUserDefinedEnum_Parms, DisplayNames), EArrayPropertyFlags::None, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_DisplayNames_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_DisplayNames_MetaData)) };
	void Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintGraphBuilderLibrary_eventConfigureUserDefinedEnum_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintGraphBuilderLibrary_eventConfigureUserDefinedEnum_Parms), &Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_Enum,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_DisplayNames_Inner,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_DisplayNames,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/** Replace a UserDefinedEnum's entries with the supplied display names. */" },
		{ "ModuleRelativePath", "Public/BlueprintGraphBuilderLibrary.h" },
		{ "ToolTip", "Replace a UserDefinedEnum's entries with the supplied display names." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintGraphBuilderLibrary, nullptr, "ConfigureUserDefinedEnum", nullptr, nullptr, sizeof(BlueprintGraphBuilderLibrary_eventConfigureUserDefinedEnum_Parms), Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04422401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics
	{
		struct BlueprintGraphBuilderLibrary_eventSetComponentProperty_Parms
		{
			UBlueprint* Blueprint;
			FString ComponentName;
			FString PropertyName;
			FString JsonValue;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ComponentName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ComponentName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_PropertyName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_PropertyName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_JsonValue_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_JsonValue;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventSetComponentProperty_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_ComponentName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_ComponentName = { "ComponentName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventSetComponentProperty_Parms, ComponentName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_ComponentName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_ComponentName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_PropertyName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_PropertyName = { "PropertyName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventSetComponentProperty_Parms, PropertyName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_PropertyName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_PropertyName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_JsonValue_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_JsonValue = { "JsonValue", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintGraphBuilderLibrary_eventSetComponentProperty_Parms, JsonValue), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_JsonValue_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_JsonValue_MetaData)) };
	void Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintGraphBuilderLibrary_eventSetComponentProperty_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintGraphBuilderLibrary_eventSetComponentProperty_Parms), &Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_ComponentName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_PropertyName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_JsonValue,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintGraphBuilder" },
		{ "Comment", "/** Set a property on a Blueprint component template by name.\n     *  Works on components added via AddComponentToBlueprint.\n     *\n     *  @param Blueprint      Target Blueprint\n     *  @param ComponentName  Name of the component to modify\n     *  @param PropertyName   Property to set (e.g. \"BoxExtent\", \"CollisionProfileName\")\n     *  @param JsonValue      Value as JSON string (e.g. \"{\\\"X\\\":200,\\\"Y\\\":200,\\\"Z\\\":200}\")\n     *  @return True if the property was set successfully\n     */" },
		{ "ModuleRelativePath", "Public/BlueprintGraphBuilderLibrary.h" },
		{ "ToolTip", "Set a property on a Blueprint component template by name.\nWorks on components added via AddComponentToBlueprint.\n\n@param Blueprint      Target Blueprint\n@param ComponentName  Name of the component to modify\n@param PropertyName   Property to set (e.g. \"BoxExtent\", \"CollisionProfileName\")\n@param JsonValue      Value as JSON string (e.g. \"{\\\"X\\\":200,\\\"Y\\\":200,\\\"Z\\\":200}\")\n@return True if the property was set successfully" },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintGraphBuilderLibrary, nullptr, "SetComponentProperty", nullptr, nullptr, sizeof(BlueprintGraphBuilderLibrary_eventSetComponentProperty_Parms), Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UBlueprintGraphBuilderLibrary_NoRegister()
	{
		return UBlueprintGraphBuilderLibrary::StaticClass();
	}
	struct Z_Construct_UClass_UBlueprintGraphBuilderLibrary_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UBlueprintGraphBuilderLibrary_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UBlueprintGraphBuilderLibrary_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_AddComponentToBlueprint, "AddComponentToBlueprint" }, // 1501526734
		{ &Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_BuildBlueprintFromJSON, "BuildBlueprintFromJSON" }, // 2752534944
		{ &Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_CompileAndReport, "CompileAndReport" }, // 831410946
		{ &Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_ConfigureUserDefinedEnum, "ConfigureUserDefinedEnum" }, // 3275597851
		{ &Z_Construct_UFunction_UBlueprintGraphBuilderLibrary_SetComponentProperty, "SetComponentProperty" }, // 2913487666
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UBlueprintGraphBuilderLibrary_Statics::Class_MetaDataParams[] = {
		{ "IncludePath", "BlueprintGraphBuilderLibrary.h" },
		{ "ModuleRelativePath", "Public/BlueprintGraphBuilderLibrary.h" },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UBlueprintGraphBuilderLibrary_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UBlueprintGraphBuilderLibrary>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UBlueprintGraphBuilderLibrary_Statics::ClassParams = {
		&UBlueprintGraphBuilderLibrary::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UBlueprintGraphBuilderLibrary_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UBlueprintGraphBuilderLibrary_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UBlueprintGraphBuilderLibrary()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UBlueprintGraphBuilderLibrary_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UBlueprintGraphBuilderLibrary, 3277646650);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<UBlueprintGraphBuilderLibrary>()
	{
		return UBlueprintGraphBuilderLibrary::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UBlueprintGraphBuilderLibrary(Z_Construct_UClass_UBlueprintGraphBuilderLibrary, &UBlueprintGraphBuilderLibrary::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("UBlueprintGraphBuilderLibrary"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UBlueprintGraphBuilderLibrary);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
