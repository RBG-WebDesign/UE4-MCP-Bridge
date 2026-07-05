// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MCPBridgeEditorPanel/Public/MCPBridgeProfilerLibrary.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeMCPBridgeProfilerLibrary() {}
// Cross Module References
	MCPBRIDGEEDITORPANEL_API UClass* Z_Construct_UClass_UMCPBridgeProfilerLibrary_NoRegister();
	MCPBRIDGEEDITORPANEL_API UClass* Z_Construct_UClass_UMCPBridgeProfilerLibrary();
	ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
	UPackage* Z_Construct_UPackage__Script_MCPBridgeEditorPanel();
// End Cross Module References
	DEFINE_FUNCTION(UMCPBridgeProfilerLibrary::execSummarizeTraceFile)
	{
		P_GET_PROPERTY(FStrProperty,Z_Param_TracePath);
		P_FINISH;
		P_NATIVE_BEGIN;
		*(FString*)Z_Param__Result=UMCPBridgeProfilerLibrary::SummarizeTraceFile(Z_Param_TracePath);
		P_NATIVE_END;
	}
	void UMCPBridgeProfilerLibrary::StaticRegisterNativesUMCPBridgeProfilerLibrary()
	{
		UClass* Class = UMCPBridgeProfilerLibrary::StaticClass();
		static const FNameNativePtrPair Funcs[] = {
			{ "SummarizeTraceFile", &UMCPBridgeProfilerLibrary::execSummarizeTraceFile },
		};
		FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
	}
	struct Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics
	{
		struct MCPBridgeProfilerLibrary_eventSummarizeTraceFile_Parms
		{
			FString TracePath;
			FString ReturnValue;
		};
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam NewProp_TracePath_MetaData[];
#endif
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_TracePath;
		static const UE4CodeGen_Private::FStrPropertyParams NewProp_ReturnValue;
		static const UE4CodeGen_Private::FPropertyParamsBase* const PropPointers[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Function_MetaDataParams[];
#endif
		static const UE4CodeGen_Private::FFunctionParams FuncParams;
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::NewProp_TracePath_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::NewProp_TracePath = { "TracePath", nullptr, (EPropertyFlags)0x0010000000000080, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeProfilerLibrary_eventSummarizeTraceFile_Parms, TracePath), METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::NewProp_TracePath_MetaData, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::NewProp_TracePath_MetaData)) };
	const UE4CodeGen_Private::FStrPropertyParams Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UE4CodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, 1, STRUCT_OFFSET(MCPBridgeProfilerLibrary_eventSummarizeTraceFile_Parms, ReturnValue), METADATA_PARAMS(nullptr, 0) };
	const UE4CodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::PropPointers[] = {
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::NewProp_TracePath,
		(const UE4CodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::NewProp_ReturnValue,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::Function_MetaDataParams[] = {
		{ "Category", "MCP Bridge|Profiler" },
		{ "ModuleRelativePath", "Public/MCPBridgeProfilerLibrary.h" },
	};
#endif
	const UE4CodeGen_Private::FFunctionParams Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_UMCPBridgeProfilerLibrary, nullptr, "SummarizeTraceFile", nullptr, nullptr, sizeof(MCPBridgeProfilerLibrary_eventSummarizeTraceFile_Parms), Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::PropPointers), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::Function_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::Function_MetaDataParams)) };
	UFunction* Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile()
	{
		static UFunction* ReturnFunction = nullptr;
		if (!ReturnFunction)
		{
			UE4CodeGen_Private::ConstructUFunction(ReturnFunction, Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile_Statics::FuncParams);
		}
		return ReturnFunction;
	}
	UClass* Z_Construct_UClass_UMCPBridgeProfilerLibrary_NoRegister()
	{
		return UMCPBridgeProfilerLibrary::StaticClass();
	}
	struct Z_Construct_UClass_UMCPBridgeProfilerLibrary_Statics
	{
		static UObject* (*const DependentSingletons[])();
		static const FClassFunctionLinkInfo FuncInfo[];
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_UMCPBridgeProfilerLibrary_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
		(UObject* (*)())Z_Construct_UPackage__Script_MCPBridgeEditorPanel,
	};
	const FClassFunctionLinkInfo Z_Construct_UClass_UMCPBridgeProfilerLibrary_Statics::FuncInfo[] = {
		{ &Z_Construct_UFunction_UMCPBridgeProfilerLibrary_SummarizeTraceFile, "SummarizeTraceFile" }, // 2839375465
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_UMCPBridgeProfilerLibrary_Statics::Class_MetaDataParams[] = {
		{ "IncludePath", "MCPBridgeProfilerLibrary.h" },
		{ "ModuleRelativePath", "Public/MCPBridgeProfilerLibrary.h" },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_UMCPBridgeProfilerLibrary_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UMCPBridgeProfilerLibrary>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_UMCPBridgeProfilerLibrary_Statics::ClassParams = {
		&UMCPBridgeProfilerLibrary::StaticClass,
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
		METADATA_PARAMS(Z_Construct_UClass_UMCPBridgeProfilerLibrary_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_UMCPBridgeProfilerLibrary_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_UMCPBridgeProfilerLibrary()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_UMCPBridgeProfilerLibrary_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(UMCPBridgeProfilerLibrary, 1004442765);
	template<> MCPBRIDGEEDITORPANEL_API UClass* StaticClass<UMCPBridgeProfilerLibrary>()
	{
		return UMCPBridgeProfilerLibrary::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_UMCPBridgeProfilerLibrary(Z_Construct_UClass_UMCPBridgeProfilerLibrary, &UMCPBridgeProfilerLibrary::StaticClass, TEXT("/Script/MCPBridgeEditorPanel"), TEXT("UMCPBridgeProfilerLibrary"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(UMCPBridgeProfilerLibrary);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
