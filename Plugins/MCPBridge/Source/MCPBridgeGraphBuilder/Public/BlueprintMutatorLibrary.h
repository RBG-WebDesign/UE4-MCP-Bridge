// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintMutatorLibrary.generated.h"

class FProperty;
class FScopedTransaction;
class UBlueprint;
class UClass;

UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UBlueprintMutatorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Revert every object recorded in the currently open transaction, then
     * discard the transaction.
     *
     * UE4.27's `FScopedTransaction::Cancel()` does NOT restore state. It calls
     * `UTransBuffer::Cancel`, which pops the transaction off the undo buffer and
     * returns (EditorTransaction.cpp:1280-1331). Whatever the body already wrote
     * stays written; all that is lost is the ability to undo it. Finding 0g
     * measured the same thing from the other end.
     *
     * The restore is `ITransaction::Apply()` on the still-open transaction:
     * the records were begun with bFlip, so Apply saves the current state and
     * loads the recorded one, in reverse record order (Inc = -1). The engine
     * documents this exact use at EditorTransaction.cpp:895 and does it at
     * `FLevelEditorViewportClient::AbortTracking`, LevelEditorViewport.cpp:3199.
     *
     * Reverts and cancels the WHOLE transaction, not one scope of it: UE4.27
     * cannot cancel part of a transaction and says so
     * (EditorTransaction.cpp:1287, "Canceling transaction partially is
     * unsupported. Canceling %s entirely."). Only call this from the scope that
     * opened the outermost transaction. Calling it from a nested scope discards
     * the enclosing caller's work as well.
     */
    static void RevertAndCancelTransaction(FScopedTransaction& Transaction);

    /**
     * Convert a JSON scalar (json.dumps output) to FProperty::ImportText form.
     *
     * Public because two callers need the SAME answer: the mutation that writes
     * a default, and any caller deciding whether the default is already the one
     * being asked for. A second copy of this rule would let those two disagree,
     * and the visible failure is a converged patch that writes anyway.
     *
     * ImportText on a numeric, bool or enum property parses a leading quote as
     * garbage (Atof("\"42.5\"") is 0.0) while still reporting success, so JSON
     * quoting is stripped unless the property genuinely holds text.
     */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static FString JsonDefaultToImportText(const FString& DefaultValueJson, bool bTextLike);

    /**
     * Import a default value into ValueAddress and report whether the WHOLE
     * value was read. Public for the same reason JsonDefaultToImportText is:
     * the validator that decides whether a type can hold a value and the
     * mutator that writes it must apply one rule, or the validator passes what
     * the mutator then mangles.
     *
     * A non-null return from FProperty::ImportText is not a parse success.
     * FNumericProperty's floating point branch
     * (PropertyNumeric.cpp:113-123) advances over `[+-.0-9]` then `f`, calls
     * SetNumericPropertyValueFromString on whatever it has and returns the
     * buffer pointer, WITHOUT the `Start == Buffer` guard its integer branch
     * has fourteen lines above. So "not-a-number" on a float consumes nothing,
     * imports as 0.0, and reports success. FIntProperty rejects the same string
     * (UEnum::ParseEnum returns INDEX_NONE), which is why int looked correct
     * while float silently wrote a zero, verified it against itself and saved it.
     *
     * The only thing that separates a parse from a shrug is whether the buffer
     * was consumed, so that is what is checked. Trailing whitespace is skipped
     * because struct and array importers legitimately leave it.
     *
     * On false, ValueAddress may already have been written: ImportText has no
     * dry-run mode. Import into an FDefaultConstructedPropertyElement first
     * when the destination is real state.
     */
    static bool ImportDefaultValue(const FProperty* Property, const FString& ImportText,
        void* ValueAddress, UObject* Owner);

    // --- Tier 2: simple mutations (Phase 2) ---

    /** Enable or disable a node (by guid) within a named graph. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool SetNodeEnabled(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid, bool bEnabled);

    /** Break all links on a single pin of a node. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool BreakPinLinks(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid, const FString& PinName);

    /** Add a member variable. TypeJson matches the Type Descriptor schema. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool AddVariable(UBlueprint* Blueprint, const FString& VarName, const FString& TypeJson, const FString& DefaultValueJson, const FString& Category);

    /** Remove a member variable by name. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool RemoveVariable(UBlueprint* Blueprint, const FString& VarName);

    /** Update a member variable's default value (string form). */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool SetVariableDefault(UBlueprint* Blueprint, const FString& VarName, const FString& DefaultValueJson);

    /** Implement an interface on this Blueprint. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool AddInterfaceImplementation(UBlueprint* Blueprint, UClass* InterfaceClass);

    /** Remove an interface implementation. Function graphs produced by the interface are deleted. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool RemoveInterfaceImplementation(UBlueprint* Blueprint, UClass* InterfaceClass);

    /** Remove an SCS (component) node by name. Children are re-parented to the removed node's parent. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool RemoveSCSNode(UBlueprint* Blueprint, const FString& ComponentName);

    /** Rename an SCS (component) node. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool RenameSCSNode(UBlueprint* Blueprint, const FString& OldName, const FString& NewName);

    // --- Tier 3: node authoring (Phase 3) ---

    /** Add a node to a named graph. Returns the new node's GUID as a string on success, empty on failure.
     *  ConfigJson shape depends on NodeType (see spec). */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static FString AddNode(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeType, FVector2D Position, const FString& ConfigJson);

    /** Delete a node by guid. All pin links are broken first. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool DeleteNode(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid);

    /** Move a node to a new position (Slate coords). */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool MoveNode(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid, FVector2D NewPosition);

    /** Connect two pins. Type compat validated via UEdGraphSchema_K2::CanCreateConnection. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool ConnectPins(UBlueprint* Blueprint, const FString& GraphName,
        const FString& SrcNodeGuid, const FString& SrcPinName,
        const FString& DstNodeGuid, const FString& DstPinName);

    /** Redirect a K2Node_CallFunction's target to a new class + function. Calls ReconstructNode. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool SetCallFunctionTarget(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeGuid, UClass* TargetClass, const FString& FunctionName);

    // --- Tier 4: structural (Phase 4) ---

    /**
     * Create a new user function graph. InputsJson / OutputsJson are arrays of
     * {name, type: TypeDescriptor} objects — empty arrays create a parameter-less function.
     * Returns the new graph's name on success (same as FunctionName if unique), empty string on failure.
     */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static FString AddFunction(UBlueprint* Blueprint, const FString& FunctionName, const FString& InputsJson, const FString& OutputsJson);

    /** Remove a user function graph by name. Returns true on success. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool RemoveFunction(UBlueprint* Blueprint, const FString& FunctionName);

    /**
     * Create a new event dispatcher (delegate signature graph). SignatureJson is
     * {parameters: [{name, type: TypeDescriptor}]}. Returns the dispatcher's name on success.
     */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static FString AddEventDispatcher(UBlueprint* Blueprint, const FString& DispatcherName, const FString& SignatureJson);

    /** Remove an event dispatcher by name. Returns true on success. */
    UFUNCTION(BlueprintCallable, Category="BlueprintMutator")
    static bool RemoveEventDispatcher(UBlueprint* Blueprint, const FString& DispatcherName);
};
