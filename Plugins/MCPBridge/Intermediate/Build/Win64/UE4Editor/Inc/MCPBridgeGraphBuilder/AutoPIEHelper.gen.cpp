// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/AutoPIEHelper.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeAutoPIEHelper() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UAutoPIEHelper_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_UAutoPIEHelper();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
	COREUOBJECT_API UClass* Z_Construct_UClass_UObject_NoRegister();
// End Cross Module References
	DEFINE_FUNCTION(UAutoPIEHelper::execPlayCameraShakeByPath)
	{
		P_GET_OBJECT(UObject,Z_Param_WorldContextObject);
		P_GET_PROPERTY(FStrProperty,Z_Param_ShakeClassPath);
		P_GET_PROPERTY(FFloatProperty,Z_Param_Scale);
		P_FINISH;
		P_NATIVE_BEGIN;
		UAutoPIEHelper::PlayCameraShakeByPath(Z_Param_WorldContextObject,Z_Param_ShakeClassPath,Z_Param_Scale);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UAutoPIEHelper::execPlayCameraShakeOnPlayer)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_ShakeClassPath);
		P_GET_PROPERTY(FFloatProperty,Z_Param_Scale);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UAutoPIEHelper::PlayCameraShakeOnPlayer(Z_Param_ShakeClassPath,Z_Param_Scale);
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UAutoPIEHelper::execIsPIERunning)
	{
		P_FINISH;
		P_NATIVE_BEGIN;
		*(bool*)Z_Param__Result=UAutoPIEHelper::IsPIERunning();
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UAutoPIEHelper::execStopPIE)
	{
		P_FINISH;
		P_NATIVE_BEGIN;
		UAutoPIEHelper::StopPIE();
		P_NATIVE_END;
	}
	DEFINE_FUNCTION(UAutoPIEHelper::execStartPIE)
	{
		P_FINISH;
		P_NATIVE_BEGIN;
		UAutoPIEHelper::StartPIE();
		P_NATIVE_END;
	}
	void UAutoPIEHelper::StaticRegisterNativesUAutoPIEHelper()
	{
		UClass* Class = UAutoPIEHelper::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "IsPIERunning", &UAutoPIEHelper::execIsPIERunning },
			{ "PlayCameraShakeByPath", &UAutoPIEHelper::execPlayCameraShakeByPath },
			{ "PlayCameraShakeOnPlayer", &UAutoPIEHelper::execPlayCameraShakeOnPlayer },
			{ "StartPIE", &UAutoPIEHelper::execStartPIE },
			{ "StopPIE", &UAutoPIEHelper::execStopPIE },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics
	{
		struct AutoPIEHelper_eventIsPIERunning_Parms
		{
			bool ReturnValue;
		};
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	void Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((AutoPIEHelper_eventIsPIERunning_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(AutoPIEHelper_eventIsPIERunning_Parms), &Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "Automation" },
		{ "Comment", "/** Returns true if a PIE session is currently active. */" },
		{ "ModuleRelativePath", "Public/AutoPIEHelper.h" },
		{ "ToolTip", "Returns true if a PIE session is currently active." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UAutoPIEHelper, nullptr, "IsPIERunning", nullptr, nullptr, sizeof(AutoPIEHelper_eventIsPIERunning_Parms), Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics
	{
		struct AutoPIEHelper_eventPlayCameraShakeByPath_Parms
		{
			UObject* WorldContextObject;
			FString ShakeClassPath;
			float Scale;
		};
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_WorldContextObject;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ShakeClassPath_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ShakeClassPath;
		static const UE4CodeGen_Private::FFloatPropertyParams NewProp_Scale;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::NewProp_WorldContextObject = { "WorldContextObject", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AutoPIEHelper_eventPlayCameraShakeByPath_Parms, WorldContextObject), Z_Construct_UClass_UObject_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::NewProp_ShakeClassPath_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::NewProp_ShakeClassPath = { "ShakeClassPath", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AutoPIEHelper_eventPlayCameraShakeByPath_Parms, ShakeClassPath), METADATA_PARAMS(Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::NewProp_ShakeClassPath_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::NewProp_ShakeClassPath_MetaData)) };
	const UE4CodeGen_Private::FFloatPropertyParams Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::NewProp_Scale = { "Scale", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AutoPIEHelper_eventPlayCameraShakeByPath_Parms, Scale), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::NewProp_WorldContextObject,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::NewProp_ShakeClassPath,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::NewProp_Scale,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::Function_MetaDataParams[] = {
		{ "Category", "Automation" },
		{ "Comment", "/** Play a camera shake by asset path -- callable from Blueprint graphs at runtime.\n\x09 *  Full chain: WorldContext -> GetPlayerController(0) -> PlayerCameraManager -> PlayCameraShake.\n\x09 *  Use this in event graphs to avoid needing Cast/GetPC/GetCamMgr node chains.\n\x09 *\n\x09 *  @param WorldContextObject  Automatic world context (hidden in Blueprint)\n\x09 *  @param ShakeClassPath      Asset path (e.g. \"/Game/CS_Earthquake.CS_Earthquake_C\")\n\x09 *  @param Scale               Shake intensity multiplier\n\x09 */" },
		{ "CPP_Default_Scale", "1.000000" },
		{ "ModuleRelativePath", "Public/AutoPIEHelper.h" },
		{ "ToolTip", "Play a camera shake by asset path -- callable from Blueprint graphs at runtime.\nFull chain: WorldContext -> GetPlayerController(0) -> PlayerCameraManager -> PlayCameraShake.\nUse this in event graphs to avoid needing Cast/GetPC/GetCamMgr node chains.\n\n@param WorldContextObject  Automatic world context (hidden in Blueprint)\n@param ShakeClassPath      Asset path (e.g. \"/Game/CS_Earthquake.CS_Earthquake_C\")\n@param Scale               Shake intensity multiplier" },
		{ "WorldContext", "WorldContextObject" },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UAutoPIEHelper, nullptr, "PlayCameraShakeByPath", nullptr, nullptr, sizeof(AutoPIEHelper_eventPlayCameraShakeByPath_Parms), Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics
	{
		struct AutoPIEHelper_eventPlayCameraShakeOnPlayer_Parms
		{
			FString ShakeClassPath;
			float Scale;
			bool ReturnValue;
		};
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ShakeClassPath_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ShakeClassPath;
		static const UE4CodeGen_Private::FFloatPropertyParams NewProp_Scale;
		static void NewProp_ReturnValue_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_ShakeClassPath_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_ShakeClassPath = { "ShakeClassPath", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AutoPIEHelper_eventPlayCameraShakeOnPlayer_Parms, ShakeClassPath), METADATA_PARAMS(Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_ShakeClassPath_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_ShakeClassPath_MetaData)) };
	const UE4CodeGen_Private::FFloatPropertyParams Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_Scale = { "Scale", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AutoPIEHelper_eventPlayCameraShakeOnPlayer_Parms, Scale), METADATA_PARAMS(nullptr, 0) };
	void Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_ReturnValue_SetBit(void* Obj)
	{
		((AutoPIEHelper_eventPlayCameraShakeOnPlayer_Parms*)Obj)->ReturnValue = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(AutoPIEHelper_eventPlayCameraShakeOnPlayer_Parms), &Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_ShakeClassPath,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_Scale,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "Automation" },
		{ "Comment", "/** Play a camera shake on the first player's camera (editor/Python use).\n\x09 *  Handles the full chain: PIE World -> PlayerController(0) -> PlayerCameraManager -> PlayCameraShake.\n\x09 *  Call from Python during PIE to trigger shakes without needing Cast/Get nodes in the graph.\n\x09 *\n\x09 *  @param ShakeClassPath  Asset path to the CameraShake Blueprint (e.g. \"/Game/CS_Earthquake.CS_Earthquake_C\")\n\x09 *  @param Scale           Shake intensity multiplier (default 1.0)\n\x09 *  @return True if the shake was played successfully\n\x09 */" },
		{ "CPP_Default_Scale", "1.000000" },
		{ "ModuleRelativePath", "Public/AutoPIEHelper.h" },
		{ "ToolTip", "Play a camera shake on the first player's camera (editor/Python use).\nHandles the full chain: PIE World -> PlayerController(0) -> PlayerCameraManager -> PlayCameraShake.\nCall from Python during PIE to trigger shakes without needing Cast/Get nodes in the graph.\n\n@param ShakeClassPath  Asset path to the CameraShake Blueprint (e.g. \"/Game/CS_Earthquake.CS_Earthquake_C\")\n@param Scale           Shake intensity multiplier (default 1.0)\n@return True if the shake was played successfully" },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UAutoPIEHelper, nullptr, "PlayCameraShakeOnPlayer", nullptr, nullptr, sizeof(AutoPIEHelper_eventPlayCameraShakeOnPlayer_Parms), Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UAutoPIEHelper_StartPIE_Statics
	{
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAutoPIEHelper_StartPIE_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "Automation" },
		{ "Comment", "/** Start a real PIE session on the next engine tick.\n\x09 *  Safe to call from Python -- defers so the handler can finish first. */" },
		{ "ModuleRelativePath", "Public/AutoPIEHelper.h" },
		{ "ToolTip", "Start a real PIE session on the next engine tick.\nSafe to call from Python -- defers so the handler can finish first." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UAutoPIEHelper_StartPIE_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UAutoPIEHelper, nullptr, "StartPIE", nullptr, nullptr, 0, nullptr, 0, RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UAutoPIEHelper_StartPIE_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UAutoPIEHelper_StartPIE_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UAutoPIEHelper_StartPIE()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UAutoPIEHelper_StartPIE_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	struct Z_Construct_UFunction_UAutoPIEHelper_StopPIE_Statics
	{
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UAutoPIEHelper_StopPIE_Statics::Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "Automation" },
		{ "Comment", "/** Stop the current PIE session on the next engine tick. */" },
		{ "ModuleRelativePath", "Public/AutoPIEHelper.h" },
		{ "ToolTip", "Stop the current PIE session on the next engine tick." },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UAutoPIEHelper_StopPIE_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UAutoPIEHelper, nullptr, "StopPIE", nullptr, nullptr, 0, nullptr, 0, RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UAutoPIEHelper_StopPIE_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UAutoPIEHelper_StopPIE_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UAutoPIEHelper_StopPIE()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UAutoPIEHelper_StopPIE_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UAutoPIEHelper_NoRegister()
	{
		return UAutoPIEHelper::StaticClass();
	}
	struct Z_Construct_UClass_UAutoPIEHelper_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UAutoPIEHelper_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UAutoPIEHelper_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UAutoPIEHelper_IsPIERunning, "IsPIERunning" }, // 616868428
		{ &Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeByPath, "PlayCameraShakeByPath" }, // 3561346070
		{ &Z_Construct_UFunction_UAutoPIEHelper_PlayCameraShakeOnPlayer, "PlayCameraShakeOnPlayer" }, // 3674876172
		{ &Z_Construct_UFunction_UAutoPIEHelper_StartPIE, "StartPIE" }, // 3181103901
		{ &Z_Construct_UFunction_UAutoPIEHelper_StopPIE, "StopPIE" }, // 4106155821
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UAutoPIEHelper_Statics::Class_MetaDataParams[] = {
		{ "Comment", "/**\n * Editor automation helpers exposed to Python/Blueprint.\n * Defers PIE start to the next engine tick so the calling thread\n * (Python game-thread handler) can return first.\n */" },
		{ "IncludePath", "AutoPIEHelper.h" },
		{ "ModuleRelativePath", "Public/AutoPIEHelper.h" },
		{ "ToolTip", "Editor automation helpers exposed to Python/Blueprint.\nDefers PIE start to the next engine tick so the calling thread\n(Python game-thread handler) can return first." },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UAutoPIEHelper_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UAutoPIEHelper>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UAutoPIEHelper_Statics::ClassParams = {
		&UAutoPIEHelper::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UAutoPIEHelper_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UAutoPIEHelper_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UAutoPIEHelper()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UAutoPIEHelper_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UAutoPIEHelper, 3851509035);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<UAutoPIEHelper>()
	{
		return UAutoPIEHelper::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UAutoPIEHelper(Z_Construct_UClass_UAutoPIEHelper, &UAutoPIEHelper::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("UAutoPIEHelper"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UAutoPIEHelper);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
