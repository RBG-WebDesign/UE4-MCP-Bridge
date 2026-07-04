#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "Templates/Function.h"

class UBlueprint;

class FBPMutatorHelpers
{
public:
    /** Wrap a mutation body with validation -> FScopedTransaction -> BP->Modify() -> body ->
     *  MarkBlueprintAsStructurallyModified -> CompileBlueprint. Returns true iff body returned true. */
    static bool RunMutation(UBlueprint* Blueprint, const FText& TransactionName, TFunctionRef<bool()> Body);

    /** Resolve an interface UClass by path (e.g. /Script/Engine.MyInterface). Null on not-found. */
    static UClass* FindInterfaceClassByPath(const FString& Path);
};
