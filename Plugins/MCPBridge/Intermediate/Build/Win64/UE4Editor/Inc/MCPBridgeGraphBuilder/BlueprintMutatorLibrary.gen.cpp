// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/BlueprintMutatorLibrary.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeBlueprintMutatorLibrary() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UBlueprintMutatorLibrary_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UBlueprintMutatorLibrary();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprint_NoRegister();
	COREUOBJECT_API UClass* Z_Construct_UClass_UClass();
	COREUOBJECT_API UClass* Z_Construct_UClass_UObject_NoRegister();
	COREUOBJECT_API UScriptStruct* Z_Construct_UScriptStruct_FVector2D();
// End Cross Module References
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execRemoveEventDispatcher)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_DispatcherName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::RemoveEventDispatcher(Z_Param_Blueprint,Z_Param_DispatcherName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execAddEventDispatcher)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_DispatcherName);
		P_GET_PROPERTY(FStrProperty,Z_Param_SignatureJson);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintMutatorLibrary::AddEventDispatcher(Z_Param_Blueprint,Z_Param_DispatcherName,Z_Param_SignatureJson);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execRemoveFunction)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_FunctionName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::RemoveFunction(Z_Param_Blueprint,Z_Param_FunctionName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execAddFunction)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_FunctionName);
		P_GET_PROPERTY(FStrProperty,Z_Param_InputsJson);
		P_GET_PROPERTY(FStrProperty,Z_Param_OutputsJson);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintMutatorLibrary::AddFunction(Z_Param_Blueprint,Z_Param_FunctionName,Z_Param_InputsJson,Z_Param_OutputsJson);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execSetCallFunctionTarget)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_GraphName);
		P_GET_PROPERTY(FStrProperty,Z_Param_NodeGuid);
		P_GET_OBJECT(UClass,Z_Param_TargetClass);
		P_GET_PROPERTY(FStrProperty,Z_Param_FunctionName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::SetCallFunctionTarget(Z_Param_Blueprint,Z_Param_GraphName,Z_Param_NodeGuid,Z_Param_TargetClass,Z_Param_FunctionName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execConnectPins)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_GraphName);
		P_GET_PROPERTY(FStrProperty,Z_Param_SrcNodeGuid);
		P_GET_PROPERTY(FStrProperty,Z_Param_SrcPinName);
		P_GET_PROPERTY(FStrProperty,Z_Param_DstNodeGuid);
		P_GET_PROPERTY(FStrProperty,Z_Param_DstPinName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::ConnectPins(Z_Param_Blueprint,Z_Param_GraphName,Z_Param_SrcNodeGuid,Z_Param_SrcPinName,Z_Param_DstNodeGuid,Z_Param_DstPinName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execMoveNode)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_GraphName);
		P_GET_PROPERTY(FStrProperty,Z_Param_NodeGuid);
		P_GET_STRUCT(FVector2D,Z_Param_NewPosition);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::MoveNode(Z_Param_Blueprint,Z_Param_GraphName,Z_Param_NodeGuid,Z_Param_NewPosition);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execDeleteNode)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_GraphName);
		P_GET_PROPERTY(FStrProperty,Z_Param_NodeGuid);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::DeleteNode(Z_Param_Blueprint,Z_Param_GraphName,Z_Param_NodeGuid);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execAddNode)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_GraphName);
		P_GET_PROPERTY(FStrProperty,Z_Param_NodeType);
		P_GET_STRUCT(FVector2D,Z_Param_Position);
		P_GET_PROPERTY(FStrProperty,Z_Param_ConfigJson);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UBlueprintMutatorLibrary::AddNode(Z_Param_Blueprint,Z_Param_GraphName,Z_Param_NodeType,Z_Param_Position,Z_Param_ConfigJson);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execRenameSCSNode)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_OldName);
		P_GET_PROPERTY(FStrProperty,Z_Param_NewName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::RenameSCSNode(Z_Param_Blueprint,Z_Param_OldName,Z_Param_NewName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execRemoveSCSNode)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_ComponentName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::RemoveSCSNode(Z_Param_Blueprint,Z_Param_ComponentName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execRemoveInterfaceImplementation)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_OBJECT(UClass,Z_Param_InterfaceClass);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::RemoveInterfaceImplementation(Z_Param_Blueprint,Z_Param_InterfaceClass);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execAddInterfaceImplementation)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_OBJECT(UClass,Z_Param_InterfaceClass);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::AddInterfaceImplementation(Z_Param_Blueprint,Z_Param_InterfaceClass);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execSetVariableDefault)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_VarName);
		P_GET_PROPERTY(FStrProperty,Z_Param_DefaultValueJson);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::SetVariableDefault(Z_Param_Blueprint,Z_Param_VarName,Z_Param_DefaultValueJson);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execRemoveVariable)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_VarName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::RemoveVariable(Z_Param_Blueprint,Z_Param_VarName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execAddVariable)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_VarName);
		P_GET_PROPERTY(FStrProperty,Z_Param_TypeJson);
		P_GET_PROPERTY(FStrProperty,Z_Param_DefaultValueJson);
		P_GET_PROPERTY(FStrProperty,Z_Param_Category);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::AddVariable(Z_Param_Blueprint,Z_Param_VarName,Z_Param_TypeJson,Z_Param_DefaultValueJson,Z_Param_Category);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execBreakPinLinks)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_GraphName);
		P_GET_PROPERTY(FStrProperty,Z_Param_NodeGuid);
		P_GET_PROPERTY(FStrProperty,Z_Param_PinName);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::BreakPinLinks(Z_Param_Blueprint,Z_Param_GraphName,Z_Param_NodeGuid,Z_Param_PinName);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UBlueprintMutatorLibrary::execSetNodeEnabled)
	{
		P_GET_OBJECT(UBlueprint,Z_Param_Blueprint);
		P_GET_PROPERTY(FStrProperty,Z_Param_GraphName);
		P_GET_PROPERTY(FStrProperty,Z_Param_NodeGuid);
		P_GET_UBOOL(Z_Param_bEnabled);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UBlueprintMutatorLibrary::SetNodeEnabled(Z_Param_Blueprint,Z_Param_GraphName,Z_Param_NodeGuid,Z_Param_bEnabled);
		P_NATIVE_END;
	}
	void UBlueprintMutatorLibrary::StaticRegisterNativesUBlueprintMutatorLibrary()
	{
		UClass* Class = UBlueprintMutatorLibrary::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "AddEventDispatcher", &UBlueprintMutatorLibrary::execAddEventDispatcher },
			{ "AddFunction", &UBlueprintMutatorLibrary::execAddFunction },
			{ "AddInterfaceImplementation", &UBlueprintMutatorLibrary::execAddInterfaceImplementation },
			{ "AddNode", &UBlueprintMutatorLibrary::execAddNode },
			{ "AddVariable", &UBlueprintMutatorLibrary::execAddVariable },
			{ "BreakPinLinks", &UBlueprintMutatorLibrary::execBreakPinLinks },
			{ "ConnectPins", &UBlueprintMutatorLibrary::execConnectPins },
			{ "DeleteNode", &UBlueprintMutatorLibrary::execDeleteNode },
			{ "MoveNode", &UBlueprintMutatorLibrary::execMoveNode },
			{ "RemoveEventDispatcher", &UBlueprintMutatorLibrary::execRemoveEventDispatcher },
			{ "RemoveFunction", &UBlueprintMutatorLibrary::execRemoveFunction },
			{ "RemoveInterfaceImplementation", &UBlueprintMutatorLibrary::execRemoveInterfaceImplementation },
			{ "RemoveSCSNode", &UBlueprintMutatorLibrary::execRemoveSCSNode },
			{ "RemoveVariable", &UBlueprintMutatorLibrary::execRemoveVariable },
			{ "RenameSCSNode", &UBlueprintMutatorLibrary::execRenameSCSNode },
			{ "SetCallFunctionTarget", &UBlueprintMutatorLibrary::execSetCallFunctionTarget },
			{ "SetNodeEnabled", &UBlueprintMutatorLibrary::execSetNodeEnabled },
			{ "SetVariableDefault", &UBlueprintMutatorLibrary::execSetVariableDefault },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics
	{
		struct BlueprintMutatorLibrary_eventAddEventDispatcher_Parms
		{
			UBlueprint* Blueprint;
			FString DispatcherName;
			FString SignatureJson;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_DispatcherName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_DispatcherName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_SignatureJson_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_SignatureJson;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddEventDispatcher_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_DispatcherName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_DispatcherName = { "DispatcherName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddEventDispatcher_Parms, DispatcherName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_DispatcherName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_DispatcherName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_SignatureJson_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_SignatureJson = { "SignatureJson", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddEventDispatcher_Parms, SignatureJson), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_SignatureJson_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_SignatureJson_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddEventDispatcher_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_DispatcherName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_SignatureJson,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/**\n     * Create a new event dispatcher (delegate signature graph). SignatureJson is\n     * {parameters: [{name, type: TypeDescriptor}]}. Returns the dispatcher's name on success.\n     */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Create a new event dispatcher (delegate signature graph). SignatureJson is\n{parameters: [{name, type: TypeDescriptor}]}. Returns the dispatcher's name on success." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "AddEventDispatcher", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventAddEventDispatcher_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics
	{
		struct BlueprintMutatorLibrary_eventAddFunction_Parms
		{
			UBlueprint* Blueprint;
			FString FunctionName;
			FString InputsJson;
			FString OutputsJson;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_FunctionName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_FunctionName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_InputsJson_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_InputsJson;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_OutputsJson_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_OutputsJson;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddFunction_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_FunctionName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_FunctionName = { "FunctionName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddFunction_Parms, FunctionName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_FunctionName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_FunctionName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_InputsJson_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_InputsJson = { "InputsJson", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddFunction_Parms, InputsJson), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_InputsJson_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_InputsJson_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_OutputsJson_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_OutputsJson = { "OutputsJson", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddFunction_Parms, OutputsJson), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_OutputsJson_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_OutputsJson_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddFunction_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_FunctionName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_InputsJson,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_OutputsJson,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/**\n     * Create a new user function graph. InputsJson / OutputsJson are arrays of\n     * {name, type: TypeDescriptor} objects \xe2\x80\x94 empty arrays create a parameter-less function.\n     * Returns the new graph's name on success (same as FunctionName if unique), empty string on failure.\n     */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Create a new user function graph. InputsJson / OutputsJson are arrays of\n{name, type: TypeDescriptor} objects \xe2\x80\x94 empty arrays create a parameter-less function.\nReturns the new graph's name on success (same as FunctionName if unique), empty string on failure." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "AddFunction", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventAddFunction_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics
	{
		struct BlueprintMutatorLibrary_eventAddInterfaceImplementation_Parms
		{
			UBlueprint* Blueprint;
			UClass* InterfaceClass;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
		static const UE4CodeGen_Private::FClassPropertyParams NewProp_InterfaceClass;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddInterfaceImplementation_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FClassPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::NewProp_InterfaceClass = { "InterfaceClass", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Class, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddInterfaceImplementation_Parms, InterfaceClass), Z_Construct_UClass_UObject_NoRegister, Z_Construct_UClass_UClass, METADATA_PARAMS(nullptr, 0) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventAddInterfaceImplementation_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventAddInterfaceImplementation_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::NewProp_InterfaceClass,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Implement an interface on this Blueprint. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Implement an interface on this Blueprint." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "AddInterfaceImplementation", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventAddInterfaceImplementation_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics
	{
		struct BlueprintMutatorLibrary_eventAddNode_Parms
		{
			UBlueprint* Blueprint;
			FString GraphName;
			FString NodeType;
			FVector2D Position;
			FString ConfigJson;
			FString ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_GraphName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_GraphName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_NodeType_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_NodeType;
		static const UE4CodeGen_Private::FStructPropertyParams NewProp_Position;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ConfigJson_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ConfigJson;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddNode_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_GraphName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_GraphName = { "GraphName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddNode_Parms, GraphName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_GraphName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_GraphName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_NodeType_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_NodeType = { "NodeType", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddNode_Parms, NodeType), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_NodeType_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_NodeType_MetaData)) };
	const UE4CodeGen_Private::FStructPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_Position = { "Position", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddNode_Parms, Position), Z_Construct_UScriptStruct_FVector2D, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_ConfigJson_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_ConfigJson = { "ConfigJson", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddNode_Parms, ConfigJson), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_ConfigJson_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_ConfigJson_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddNode_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_GraphName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_NodeType,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_Position,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_ConfigJson,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Add a node to a named graph. Returns the new node's GUID as a string on success, empty on failure.\n     *  ConfigJson shape depends on NodeType (see spec). */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Add a node to a named graph. Returns the new node's GUID as a string on success, empty on failure.\nConfigJson shape depends on NodeType (see spec)." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "AddNode", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventAddNode_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04822401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics
	{
		struct BlueprintMutatorLibrary_eventAddVariable_Parms
		{
			UBlueprint* Blueprint;
			FString VarName;
			FString TypeJson;
			FString DefaultValueJson;
			FString Category;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_VarName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_VarName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_TypeJson_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_TypeJson;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_DefaultValueJson_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_DefaultValueJson;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Category_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_Category;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddVariable_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_VarName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_VarName = { "VarName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddVariable_Parms, VarName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_VarName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_VarName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_TypeJson_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_TypeJson = { "TypeJson", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddVariable_Parms, TypeJson), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_TypeJson_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_TypeJson_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_DefaultValueJson_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_DefaultValueJson = { "DefaultValueJson", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddVariable_Parms, DefaultValueJson), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_DefaultValueJson_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_DefaultValueJson_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_Category_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_Category = { "Category", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventAddVariable_Parms, Category), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_Category_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_Category_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventAddVariable_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventAddVariable_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_VarName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_TypeJson,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_DefaultValueJson,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_Category,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Add a member variable. TypeJson matches the Type Descriptor schema. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Add a member variable. TypeJson matches the Type Descriptor schema." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "AddVariable", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventAddVariable_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics
	{
		struct BlueprintMutatorLibrary_eventBreakPinLinks_Parms
		{
			UBlueprint* Blueprint;
			FString GraphName;
			FString NodeGuid;
			FString PinName;
			bool ReturnValue;
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
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_PinName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_PinName;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventBreakPinLinks_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_GraphName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_GraphName = { "GraphName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventBreakPinLinks_Parms, GraphName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_GraphName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_GraphName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_NodeGuid_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_NodeGuid = { "NodeGuid", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventBreakPinLinks_Parms, NodeGuid), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_NodeGuid_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_NodeGuid_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_PinName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_PinName = { "PinName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventBreakPinLinks_Parms, PinName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_PinName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_PinName_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventBreakPinLinks_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventBreakPinLinks_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_GraphName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_NodeGuid,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_PinName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Break all links on a single pin of a node. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Break all links on a single pin of a node." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "BreakPinLinks", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventBreakPinLinks_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics
	{
		struct BlueprintMutatorLibrary_eventConnectPins_Parms
		{
			UBlueprint* Blueprint;
			FString GraphName;
			FString SrcNodeGuid;
			FString SrcPinName;
			FString DstNodeGuid;
			FString DstPinName;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_GraphName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_GraphName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_SrcNodeGuid_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_SrcNodeGuid;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_SrcPinName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_SrcPinName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_DstNodeGuid_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_DstNodeGuid;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_DstPinName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_DstPinName;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventConnectPins_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_GraphName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_GraphName = { "GraphName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventConnectPins_Parms, GraphName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_GraphName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_GraphName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_SrcNodeGuid_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_SrcNodeGuid = { "SrcNodeGuid", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventConnectPins_Parms, SrcNodeGuid), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_SrcNodeGuid_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_SrcNodeGuid_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_SrcPinName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_SrcPinName = { "SrcPinName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventConnectPins_Parms, SrcPinName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_SrcPinName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_SrcPinName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_DstNodeGuid_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_DstNodeGuid = { "DstNodeGuid", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventConnectPins_Parms, DstNodeGuid), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_DstNodeGuid_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_DstNodeGuid_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_DstPinName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_DstPinName = { "DstPinName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventConnectPins_Parms, DstPinName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_DstPinName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_DstPinName_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventConnectPins_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventConnectPins_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_GraphName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_SrcNodeGuid,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_SrcPinName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_DstNodeGuid,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_DstPinName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Connect two pins. Type compat validated via UEdGraphSchema_K2::CanCreateConnection. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Connect two pins. Type compat validated via UEdGraphSchema_K2::CanCreateConnection." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "ConnectPins", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventConnectPins_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics
	{
		struct BlueprintMutatorLibrary_eventDeleteNode_Parms
		{
			UBlueprint* Blueprint;
			FString GraphName;
			FString NodeGuid;
			bool ReturnValue;
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
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventDeleteNode_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_GraphName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_GraphName = { "GraphName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventDeleteNode_Parms, GraphName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_GraphName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_GraphName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_NodeGuid_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_NodeGuid = { "NodeGuid", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventDeleteNode_Parms, NodeGuid), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_NodeGuid_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_NodeGuid_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventDeleteNode_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventDeleteNode_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_GraphName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_NodeGuid,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Delete a node by guid. All pin links are broken first. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Delete a node by guid. All pin links are broken first." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "DeleteNode", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventDeleteNode_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics
	{
		struct BlueprintMutatorLibrary_eventMoveNode_Parms
		{
			UBlueprint* Blueprint;
			FString GraphName;
			FString NodeGuid;
			FVector2D NewPosition;
			bool ReturnValue;
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
		static const UE4CodeGen_Private::FStructPropertyParams NewProp_NewPosition;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventMoveNode_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_GraphName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_GraphName = { "GraphName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventMoveNode_Parms, GraphName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_GraphName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_GraphName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_NodeGuid_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_NodeGuid = { "NodeGuid", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventMoveNode_Parms, NodeGuid), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_NodeGuid_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_NodeGuid_MetaData)) };
	const UE4CodeGen_Private::FStructPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_NewPosition = { "NewPosition", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventMoveNode_Parms, NewPosition), Z_Construct_UScriptStruct_FVector2D, METADATA_PARAMS(nullptr, 0) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventMoveNode_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventMoveNode_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_GraphName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_NodeGuid,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_NewPosition,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Move a node to a new position (Slate coords). */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Move a node to a new position (Slate coords)." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "MoveNode", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventMoveNode_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04822401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics
	{
		struct BlueprintMutatorLibrary_eventRemoveEventDispatcher_Parms
		{
			UBlueprint* Blueprint;
			FString DispatcherName;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_DispatcherName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_DispatcherName;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRemoveEventDispatcher_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_DispatcherName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_DispatcherName = { "DispatcherName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRemoveEventDispatcher_Parms, DispatcherName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_DispatcherName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_DispatcherName_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventRemoveEventDispatcher_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventRemoveEventDispatcher_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_DispatcherName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Remove an event dispatcher by name. Returns true on success. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Remove an event dispatcher by name. Returns true on success." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "RemoveEventDispatcher", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventRemoveEventDispatcher_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics
	{
		struct BlueprintMutatorLibrary_eventRemoveFunction_Parms
		{
			UBlueprint* Blueprint;
			FString FunctionName;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_FunctionName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_FunctionName;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRemoveFunction_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_FunctionName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_FunctionName = { "FunctionName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRemoveFunction_Parms, FunctionName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_FunctionName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_FunctionName_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventRemoveFunction_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventRemoveFunction_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_FunctionName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Remove a user function graph by name. Returns true on success. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Remove a user function graph by name. Returns true on success." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "RemoveFunction", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventRemoveFunction_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics
	{
		struct BlueprintMutatorLibrary_eventRemoveInterfaceImplementation_Parms
		{
			UBlueprint* Blueprint;
			UClass* InterfaceClass;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
		static const UE4CodeGen_Private::FClassPropertyParams NewProp_InterfaceClass;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRemoveInterfaceImplementation_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FClassPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::NewProp_InterfaceClass = { "InterfaceClass", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Class, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRemoveInterfaceImplementation_Parms, InterfaceClass), Z_Construct_UClass_UObject_NoRegister, Z_Construct_UClass_UClass, METADATA_PARAMS(nullptr, 0) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventRemoveInterfaceImplementation_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventRemoveInterfaceImplementation_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::NewProp_InterfaceClass,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Remove an interface implementation. Function graphs produced by the interface are deleted. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Remove an interface implementation. Function graphs produced by the interface are deleted." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "RemoveInterfaceImplementation", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventRemoveInterfaceImplementation_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics
	{
		struct BlueprintMutatorLibrary_eventRemoveSCSNode_Parms
		{
			UBlueprint* Blueprint;
			FString ComponentName;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ComponentName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ComponentName;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRemoveSCSNode_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_ComponentName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_ComponentName = { "ComponentName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRemoveSCSNode_Parms, ComponentName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_ComponentName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_ComponentName_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventRemoveSCSNode_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventRemoveSCSNode_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_ComponentName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Remove an SCS (component) node by name. Children are re-parented to the removed node's parent. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Remove an SCS (component) node by name. Children are re-parented to the removed node's parent." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "RemoveSCSNode", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventRemoveSCSNode_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics
	{
		struct BlueprintMutatorLibrary_eventRemoveVariable_Parms
		{
			UBlueprint* Blueprint;
			FString VarName;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_VarName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_VarName;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRemoveVariable_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_VarName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_VarName = { "VarName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRemoveVariable_Parms, VarName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_VarName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_VarName_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventRemoveVariable_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventRemoveVariable_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_VarName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Remove a member variable by name. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Remove a member variable by name." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "RemoveVariable", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventRemoveVariable_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics
	{
		struct BlueprintMutatorLibrary_eventRenameSCSNode_Parms
		{
			UBlueprint* Blueprint;
			FString OldName;
			FString NewName;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_OldName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_OldName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_NewName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_NewName;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRenameSCSNode_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_OldName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_OldName = { "OldName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRenameSCSNode_Parms, OldName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_OldName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_OldName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_NewName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_NewName = { "NewName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventRenameSCSNode_Parms, NewName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_NewName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_NewName_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventRenameSCSNode_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventRenameSCSNode_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_OldName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_NewName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Rename an SCS (component) node. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Rename an SCS (component) node." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "RenameSCSNode", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventRenameSCSNode_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics
	{
		struct BlueprintMutatorLibrary_eventSetCallFunctionTarget_Parms
		{
			UBlueprint* Blueprint;
			FString GraphName;
			FString NodeGuid;
			UClass* TargetClass;
			FString FunctionName;
			bool ReturnValue;
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
		static const UE4CodeGen_Private::FClassPropertyParams NewProp_TargetClass;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_FunctionName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_FunctionName;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetCallFunctionTarget_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_GraphName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_GraphName = { "GraphName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetCallFunctionTarget_Parms, GraphName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_GraphName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_GraphName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_NodeGuid_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_NodeGuid = { "NodeGuid", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetCallFunctionTarget_Parms, NodeGuid), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_NodeGuid_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_NodeGuid_MetaData)) };
	const UE4CodeGen_Private::FClassPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_TargetClass = { "TargetClass", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Class, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetCallFunctionTarget_Parms, TargetClass), Z_Construct_UClass_UObject_NoRegister, Z_Construct_UClass_UClass, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_FunctionName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_FunctionName = { "FunctionName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetCallFunctionTarget_Parms, FunctionName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_FunctionName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_FunctionName_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventSetCallFunctionTarget_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventSetCallFunctionTarget_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_GraphName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_NodeGuid,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_TargetClass,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_FunctionName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Redirect a K2Node_CallFunction's target to a new class + function. Calls ReconstructNode. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Redirect a K2Node_CallFunction's target to a new class + function. Calls ReconstructNode." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "SetCallFunctionTarget", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventSetCallFunctionTarget_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics
	{
		struct BlueprintMutatorLibrary_eventSetNodeEnabled_Parms
		{
			UBlueprint* Blueprint;
			FString GraphName;
			FString NodeGuid;
			bool bEnabled;
			bool ReturnValue;
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
		static void NewProp_bEnabled_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_bEnabled;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetNodeEnabled_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_GraphName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_GraphName = { "GraphName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetNodeEnabled_Parms, GraphName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_GraphName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_GraphName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_NodeGuid_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_NodeGuid = { "NodeGuid", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetNodeEnabled_Parms, NodeGuid), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_NodeGuid_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_NodeGuid_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_bEnabled_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventSetNodeEnabled_Parms*)Obj)->bEnabled = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_bEnabled = { "bEnabled", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventSetNodeEnabled_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_bEnabled_SetBit, METADATA_PARAMS(nullptr, 0) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventSetNodeEnabled_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventSetNodeEnabled_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_GraphName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_NodeGuid,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_bEnabled,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Enable or disable a node (by guid) within a named graph. */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Enable or disable a node (by guid) within a named graph." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "SetNodeEnabled", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventSetNodeEnabled_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics
	{
		struct BlueprintMutatorLibrary_eventSetVariableDefault_Parms
		{
			UBlueprint* Blueprint;
			FString VarName;
			FString DefaultValueJson;
			bool ReturnValue;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Blueprint;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_VarName_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_VarName;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_DefaultValueJson_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_DefaultValueJson;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_Blueprint = { "Blueprint", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetVariableDefault_Parms, Blueprint), Z_Construct_UClass_UBlueprint_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_VarName_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_VarName = { "VarName", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetVariableDefault_Parms, VarName), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_VarName_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_VarName_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_DefaultValueJson_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_DefaultValueJson = { "DefaultValueJson", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(BlueprintMutatorLibrary_eventSetVariableDefault_Parms, DefaultValueJson), METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_DefaultValueJson_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_DefaultValueJson_MetaData)) };
	void Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((BlueprintMutatorLibrary_eventSetVariableDefault_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(BlueprintMutatorLibrary_eventSetVariableDefault_Parms), &Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_Blueprint,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_VarName,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_DefaultValueJson,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::Function_MetaDataParams[] = {
		{ "Category", "BlueprintMutator" },
		{ "Comment", "/** Update a member variable's default value (string form). */" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
		{ "ToolTip", "Update a member variable's default value (string form)." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UBlueprintMutatorLibrary, nullptr, "SetVariableDefault", nullptr, nullptr, sizeof(BlueprintMutatorLibrary_eventSetVariableDefault_Parms), Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UBlueprintMutatorLibrary_NoRegister()
	{
		return UBlueprintMutatorLibrary::StaticClass();
	}
	struct Z_Construct_UClass_UBlueprintMutatorLibrary_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UBlueprintMutatorLibrary_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UBlueprintMutatorLibrary_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_AddEventDispatcher, "AddEventDispatcher" }, // 196001703
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_AddFunction, "AddFunction" }, // 3838039551
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_AddInterfaceImplementation, "AddInterfaceImplementation" }, // 3458603828
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_AddNode, "AddNode" }, // 2676586500
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_AddVariable, "AddVariable" }, // 3992706833
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_BreakPinLinks, "BreakPinLinks" }, // 454294265
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_ConnectPins, "ConnectPins" }, // 2705365303
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_DeleteNode, "DeleteNode" }, // 2065700211
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_MoveNode, "MoveNode" }, // 446721633
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveEventDispatcher, "RemoveEventDispatcher" }, // 2442655080
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveFunction, "RemoveFunction" }, // 2673002492
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveInterfaceImplementation, "RemoveInterfaceImplementation" }, // 1320653919
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveSCSNode, "RemoveSCSNode" }, // 2029623416
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_RemoveVariable, "RemoveVariable" }, // 2418978503
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_RenameSCSNode, "RenameSCSNode" }, // 160451111
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_SetCallFunctionTarget, "SetCallFunctionTarget" }, // 4076057986
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_SetNodeEnabled, "SetNodeEnabled" }, // 2633763912
		{ &Z_Construct_UFunction_UBlueprintMutatorLibrary_SetVariableDefault, "SetVariableDefault" }, // 2836972792
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UBlueprintMutatorLibrary_Statics::Class_MetaDataParams[] = {
		{ "IncludePath", "BlueprintMutatorLibrary.h" },
		{ "ModuleRelativePath", "Public/BlueprintMutatorLibrary.h" },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UBlueprintMutatorLibrary_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UBlueprintMutatorLibrary>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UBlueprintMutatorLibrary_Statics::ClassParams = {
		&UBlueprintMutatorLibrary::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UBlueprintMutatorLibrary_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UBlueprintMutatorLibrary_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UBlueprintMutatorLibrary()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UBlueprintMutatorLibrary_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UBlueprintMutatorLibrary, 3555901618);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<UBlueprintMutatorLibrary>()
	{
		return UBlueprintMutatorLibrary::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UBlueprintMutatorLibrary(Z_Construct_UClass_UBlueprintMutatorLibrary, &UBlueprintMutatorLibrary::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("UBlueprintMutatorLibrary"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UBlueprintMutatorLibrary);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
