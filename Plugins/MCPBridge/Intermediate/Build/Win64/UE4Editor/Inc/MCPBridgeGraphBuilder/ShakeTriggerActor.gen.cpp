// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeGraphBuilder/Public/ShakeTriggerActor.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeShakeTriggerActor() {}
// Cross Module References
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_AShakeTriggerActor_NoRegister();
	MCPBRIDGEGRAPHBUILDER_API UClass* Z_Construct_UClass_AShakeTriggerActor();
	ENGINE_API UClass* Z_Construct_UClass_AActor();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeGraphBuilder();
	ENGINE_API UClass* Z_Construct_UClass_UPrimitiveComponent_NoRegister();
	ENGINE_API UClass* Z_Construct_UClass_AActor_NoRegister();
	ENGINE_API UScriptStruct* Z_Construct_UScriptStruct_FHitResult();
	ENGINE_API UClass* Z_Construct_UClass_UBoxComponent_NoRegister();
	COREUOBJECT_API UClass* Z_Construct_UClass_UClass();
	ENGINE_API UClass* Z_Construct_UClass_UCameraShakeBase_NoRegister();
// End Cross Module References
	DEFINE_FUNCTION(AShakeTriggerActor::execOnOverlapBegin)
	{
		P_GET_OBJECT(UPrimitiveComponent,Z_Param_OverlappedComponent);
		P_GET_OBJECT(AActor,Z_Param_OtherActor);
		P_GET_OBJECT(UPrimitiveComponent,Z_Param_OtherComp);
		P_GET_PROPERTY(FIntProperty,Z_Param_OtherBodyIndex);
		P_GET_UBOOL(Z_Param_bFromSweep);
		P_GET_STRUCT_REF(FHitResult,Z_Param_Out_SweepResult);
		P_FINISH;
		P_NATIVE_BEGIN;
		P_THIS->OnOverlapBegin(Z_Param_OverlappedComponent,Z_Param_OtherActor,Z_Param_OtherComp,Z_Param_OtherBodyIndex,Z_Param_bFromSweep,Z_Param_Out_SweepResult);
		P_NATIVE_END;
	}
	void AShakeTriggerActor::StaticRegisterNativesAShakeTriggerActor()
	{
		UClass* Class = AShakeTriggerActor::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "OnOverlapBegin", &AShakeTriggerActor::execOnOverlapBegin },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics
	{
		struct ShakeTriggerActor_eventOnOverlapBegin_Parms
		{
			UPrimitiveComponent* OverlappedComponent;
			AActor* OtherActor;
			UPrimitiveComponent* OtherComp;
			int32 OtherBodyIndex;
			bool bFromSweep;
			FHitResult SweepResult;
		};
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_OverlappedComponent_MetaData[];
#endif
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_OverlappedComponent;
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_OtherActor;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_OtherComp_MetaData[];
#endif
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_OtherComp;
		static const UE4CodeGen_Private::FIntPropertyParams NewProp_OtherBodyIndex;
		static void NewProp_bFromSweep_SetBit(void* Obj);
		static const UE4CodeGen_Private::FBoolPropertyParams NewProp_bFromSweep;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_SweepResult_MetaData[];
#endif
		static const UE4CodeGen_Private::FStructPropertyParams NewProp_SweepResult;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OverlappedComponent_MetaData[] = {
		{ "EditInline", "true" },
	};
#endif
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OverlappedComponent = { "OverlappedComponent", nullptr, (EPropertyFlags)0x0010000000080080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(ShakeTriggerActor_eventOnOverlapBegin_Parms, OverlappedComponent), Z_Construct_UClass_UPrimitiveComponent_NoRegister, METADATA_PARAMS(Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OverlappedComponent_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OverlappedComponent_MetaData)) };
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OtherActor = { "OtherActor", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(ShakeTriggerActor_eventOnOverlapBegin_Parms, OtherActor), Z_Construct_UClass_AActor_NoRegister, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OtherComp_MetaData[] = {
		{ "EditInline", "true" },
	};
#endif
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OtherComp = { "OtherComp", nullptr, (EPropertyFlags)0x0010000000080080, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(ShakeTriggerActor_eventOnOverlapBegin_Parms, OtherComp), Z_Construct_UClass_UPrimitiveComponent_NoRegister, METADATA_PARAMS(Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OtherComp_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OtherComp_MetaData)) };
	const UE4CodeGen_Private::FIntPropertyParams Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OtherBodyIndex = { "OtherBodyIndex", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(ShakeTriggerActor_eventOnOverlapBegin_Parms, OtherBodyIndex), METADATA_PARAMS(nullptr, 0) };
	void Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_bFromSweep_SetBit(void* Obj)
	{
		((ShakeTriggerActor_eventOnOverlapBegin_Parms*)Obj)->bFromSweep = 1;
	}
	const UE4CodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_bFromSweep = { "bFromSweep", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Bool | UE4CodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, 1, sizeof(bool), sizeof(ShakeTriggerActor_eventOnOverlapBegin_Parms), &Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_bFromSweep_SetBit, METADATA_PARAMS(nullptr, 0) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_SweepResult_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStructPropertyParams Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_SweepResult = { "SweepResult", nullptr, (EPropertyFlags)0x0010008008000182, UE4CodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(ShakeTriggerActor_eventOnOverlapBegin_Parms, SweepResult), Z_Construct_UScriptStruct_FHitResult, METADATA_PARAMS(Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_SweepResult_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_SweepResult_MetaData)) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OverlappedComponent,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OtherActor,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OtherComp,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_OtherBodyIndex,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_bFromSweep,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::NewProp_SweepResult,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::Function_MetaDataParams[] = {
		{ "ModuleRelativePath", "Public/ShakeTriggerActor.h" },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_AShakeTriggerActor, nullptr, "OnOverlapBegin", nullptr, nullptr, sizeof(ShakeTriggerActor_eventOnOverlapBegin_Parms), Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x00420401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_AShakeTriggerActor_NoRegister()
	{
		return AShakeTriggerActor::StaticClass();
	}
	struct Z_Construct_UClass_AShakeTriggerActor_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_Trigger_MetaData[];
#endif
		static const UE4CodeGen_Private::FObjectPropertyParams NewProp_Trigger;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ShakeClass_MetaData[];
#endif
		static const UE4CodeGen_Private::FClassPropertyParams NewProp_ShakeClass;
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_ShakeScale_MetaData[];
#endif
		static const UE4CodeGen_Private::FFloatPropertyParams NewProp_ShakeScale;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_AShakeTriggerActor_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_AActor,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeGraphBuilder,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_AShakeTriggerActor_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_AShakeTriggerActor_OnOverlapBegin, "OnOverlapBegin" }, // 1137894593
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_AShakeTriggerActor_Statics::Class_MetaDataParams[] = {
		{ "Comment", "/**\n * Runtime actor that plays a camera shake when the player overlaps a trigger box.\n * Spawned and configured by Python (editor-side), runs in C++ (runtime-side).\n * No Blueprint graph wiring needed.\n */" },
		{ "IncludePath", "ShakeTriggerActor.h" },
		{ "ModuleRelativePath", "Public/ShakeTriggerActor.h" },
		{ "ToolTip", "Runtime actor that plays a camera shake when the player overlaps a trigger box.\nSpawned and configured by Python (editor-side), runs in C++ (runtime-side).\nNo Blueprint graph wiring needed." },
	};
#endif
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_Trigger_MetaData[] = {
		{ "Category", "Shake" },
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/ShakeTriggerActor.h" },
	};
#endif
	const UE4CodeGen_Private::FObjectPropertyParams Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_Trigger = { "Trigger", nullptr, (EPropertyFlags)0x00100000000a001d, UE4CodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AShakeTriggerActor, Trigger), Z_Construct_UClass_UBoxComponent_NoRegister, METADATA_PARAMS(Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_Trigger_MetaData, UE_ARRAY_COUNT(Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_Trigger_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_ShakeClass_MetaData[] = {
		{ "Category", "Shake" },
		{ "ModuleRelativePath", "Public/ShakeTriggerActor.h" },
	};
#endif
	const UE4CodeGen_Private::FClassPropertyParams Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_ShakeClass = { "ShakeClass", nullptr, (EPropertyFlags)0x0014000000000005, UE4CodeGen_Private::EPropertyGenFlags::Class, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AShakeTriggerActor, ShakeClass), Z_Construct_UClass_UCameraShakeBase_NoRegister, Z_Construct_UClass_UClass, METADATA_PARAMS(Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_ShakeClass_MetaData, UE_ARRAY_COUNT(Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_ShakeClass_MetaData)) };
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_ShakeScale_MetaData[] = {
		{ "Category", "Shake" },
		{ "ModuleRelativePath", "Public/ShakeTriggerActor.h" },
	};
#endif
	const UE4CodeGen_Private::FFloatPropertyParams Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_ShakeScale = { "ShakeScale", nullptr, (EPropertyFlags)0x0010000000000005, UE4CodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(AShakeTriggerActor, ShakeScale), METADATA_PARAMS(Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_ShakeScale_MetaData, UE_ARRAY_COUNT(Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_ShakeScale_MetaData)) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_AShakeTriggerActor_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_Trigger,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_ShakeClass,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AShakeTriggerActor_Statics::NewProp_ShakeScale,
	};
	const FCppClassTypeInfoStatic Z_Construct_UClass_AShakeTriggerActor_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<AShakeTriggerActor>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_AShakeTriggerActor_Statics::ClassParams = {
		&AShakeTriggerActor::StaticClass,
		"Engine",
		&StaticCppClassTypeInfo,
		DependentSingletons,
		FuncInfo,
		Z_Construct_UClass_AShakeTriggerActor_Statics::PropPointers,
		nullptr,
		UE_ARRAY_COUNT(DependentSingletons),
		UE_ARRAY_COUNT(FuncInfo),
		UE_ARRAY_COUNT(Z_Construct_UClass_AShakeTriggerActor_Statics::PropPointers),
		0,
		0x009000A4u,
		METADATA_PARAMS(Z_Construct_UClass_AShakeTriggerActor_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_AShakeTriggerActor_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_AShakeTriggerActor()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_AShakeTriggerActor_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(AShakeTriggerActor, 1008702903);
	template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<AShakeTriggerActor>()
	{
		return AShakeTriggerActor::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_AShakeTriggerActor(Z_Construct_UClass_AShakeTriggerActor, &AShakeTriggerActor::StaticClass, TEXT("/Script/MCPBridgeGraphBuilder"), TEXT("AShakeTriggerActor"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(AShakeTriggerActor);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
