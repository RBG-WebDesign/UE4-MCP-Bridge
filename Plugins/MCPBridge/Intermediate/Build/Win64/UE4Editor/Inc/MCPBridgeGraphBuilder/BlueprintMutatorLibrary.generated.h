// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
class UBlueprint;
class UObject;
struct FVector2D;
#ifdef MCPBRIDGEGRAPHBUILDER_BlueprintMutatorLibrary_generated_h
#error "BlueprintMutatorLibrary.generated.h already included, missing '#pragma once' in BlueprintMutatorLibrary.h"
#endif
#define MCPBRIDGEGRAPHBUILDER_BlueprintMutatorLibrary_generated_h

#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_SPARSE_DATA
#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_RPC_WRAPPERS \
 \
	DECLARE_FUNCTION(execRemoveEventDispatcher); \
	DECLARE_FUNCTION(execAddEventDispatcher); \
	DECLARE_FUNCTION(execRemoveFunction); \
	DECLARE_FUNCTION(execAddFunction); \
	DECLARE_FUNCTION(execSetCallFunctionTarget); \
	DECLARE_FUNCTION(execConnectPins); \
	DECLARE_FUNCTION(execMoveNode); \
	DECLARE_FUNCTION(execDeleteNode); \
	DECLARE_FUNCTION(execAddNode); \
	DECLARE_FUNCTION(execRenameSCSNode); \
	DECLARE_FUNCTION(execRemoveSCSNode); \
	DECLARE_FUNCTION(execRemoveInterfaceImplementation); \
	DECLARE_FUNCTION(execAddInterfaceImplementation); \
	DECLARE_FUNCTION(execSetVariableDefault); \
	DECLARE_FUNCTION(execRemoveVariable); \
	DECLARE_FUNCTION(execAddVariable); \
	DECLARE_FUNCTION(execBreakPinLinks); \
	DECLARE_FUNCTION(execSetNodeEnabled);


#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_RPC_WRAPPERS_NO_PURE_DECLS \
 \
	DECLARE_FUNCTION(execRemoveEventDispatcher); \
	DECLARE_FUNCTION(execAddEventDispatcher); \
	DECLARE_FUNCTION(execRemoveFunction); \
	DECLARE_FUNCTION(execAddFunction); \
	DECLARE_FUNCTION(execSetCallFunctionTarget); \
	DECLARE_FUNCTION(execConnectPins); \
	DECLARE_FUNCTION(execMoveNode); \
	DECLARE_FUNCTION(execDeleteNode); \
	DECLARE_FUNCTION(execAddNode); \
	DECLARE_FUNCTION(execRenameSCSNode); \
	DECLARE_FUNCTION(execRemoveSCSNode); \
	DECLARE_FUNCTION(execRemoveInterfaceImplementation); \
	DECLARE_FUNCTION(execAddInterfaceImplementation); \
	DECLARE_FUNCTION(execSetVariableDefault); \
	DECLARE_FUNCTION(execRemoveVariable); \
	DECLARE_FUNCTION(execAddVariable); \
	DECLARE_FUNCTION(execBreakPinLinks); \
	DECLARE_FUNCTION(execSetNodeEnabled);


#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesUBlueprintMutatorLibrary(); \
	friend struct Z_Construct_UClass_UBlueprintMutatorLibrary_Statics; \
public: \
	DECLARE_CLASS(UBlueprintMutatorLibrary, UBlueprintFunctionLibrary, COMPILED_IN_FLAGS(0), CASTCLASS_None, TEXT("/Script/MCPBridgeGraphBuilder"), NO_API) \
	DECLARE_SERIALIZER(UBlueprintMutatorLibrary)


#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_INCLASS \
private: \
	static void StaticRegisterNativesUBlueprintMutatorLibrary(); \
	friend struct Z_Construct_UClass_UBlueprintMutatorLibrary_Statics; \
public: \
	DECLARE_CLASS(UBlueprintMutatorLibrary, UBlueprintFunctionLibrary, COMPILED_IN_FLAGS(0), CASTCLASS_None, TEXT("/Script/MCPBridgeGraphBuilder"), NO_API) \
	DECLARE_SERIALIZER(UBlueprintMutatorLibrary)


#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_STANDARD_CONSTRUCTORS \
	/** Standard constructor, called after all reflected properties have been initialized */ \
	NO_API UBlueprintMutatorLibrary(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get()); \
	DEFINE_DEFAULT_OBJECT_INITIALIZER_CONSTRUCTOR_CALL(UBlueprintMutatorLibrary) \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, UBlueprintMutatorLibrary); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(UBlueprintMutatorLibrary); \
private: \
	/** Private move- and copy-constructors, should never be used */ \
	NO_API UBlueprintMutatorLibrary(UBlueprintMutatorLibrary&&); \
	NO_API UBlueprintMutatorLibrary(const UBlueprintMutatorLibrary&); \
public:


#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_ENHANCED_CONSTRUCTORS \
	/** Standard constructor, called after all reflected properties have been initialized */ \
	NO_API UBlueprintMutatorLibrary(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get()) : Super(ObjectInitializer) { }; \
private: \
	/** Private move- and copy-constructors, should never be used */ \
	NO_API UBlueprintMutatorLibrary(UBlueprintMutatorLibrary&&); \
	NO_API UBlueprintMutatorLibrary(const UBlueprintMutatorLibrary&); \
public: \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, UBlueprintMutatorLibrary); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(UBlueprintMutatorLibrary); \
	DEFINE_DEFAULT_OBJECT_INITIALIZER_CONSTRUCTOR_CALL(UBlueprintMutatorLibrary)


#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_PRIVATE_PROPERTY_OFFSET
#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_10_PROLOG
#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_GENERATED_BODY_LEGACY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_PRIVATE_PROPERTY_OFFSET \
	Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_SPARSE_DATA \
	Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_RPC_WRAPPERS \
	Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_INCLASS \
	Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_STANDARD_CONSTRUCTORS \
public: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


#define Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_PRIVATE_PROPERTY_OFFSET \
	Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_SPARSE_DATA \
	Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_RPC_WRAPPERS_NO_PURE_DECLS \
	Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_INCLASS_NO_PURE_DECLS \
	Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h_13_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


template<> MCPBRIDGEGRAPHBUILDER_API UClass* StaticClass<class UBlueprintMutatorLibrary>();

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID Sinfeld_240301_Plugins_MCPBridge_Source_MCPBridgeGraphBuilder_Public_BlueprintMutatorLibrary_h


PRAGMA_ENABLE_DEPRECATION_WARNINGS
