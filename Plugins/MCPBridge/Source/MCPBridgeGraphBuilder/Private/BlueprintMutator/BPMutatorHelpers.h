// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "Templates/Function.h"

class UBlueprint;

class FBPMutatorHelpers
{
public:
    /** Wrap a mutation body with validation -> FScopedTransaction -> BP->Modify() -> body ->
     *  MarkBlueprintAsStructurallyModified -> CompileBlueprint. Returns true iff body returned true.
     *
     *  Failure path: if this call opened the outermost transaction, the transaction is
     *  reverted and cancelled, so a failed mutation leaves the Blueprint as it found it.
     *  If a transaction was already open, this scope shares it and cannot roll back
     *  without discarding the caller's work, so it does not: it logs what it left and the
     *  enclosing scope owns the rollback. See UBlueprintMutatorLibrary::RevertAndCancelTransaction. */
    static bool RunMutation(UBlueprint* Blueprint, const FText& TransactionName, TFunctionRef<bool()> Body);

    /** Resolve an interface UClass by path (e.g. /Script/Engine.MyInterface). Null on not-found. */
    static UClass* FindInterfaceClassByPath(const FString& Path);
};
