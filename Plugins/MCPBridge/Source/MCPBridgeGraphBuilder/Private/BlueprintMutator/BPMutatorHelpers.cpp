// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "BPMutatorHelpers"

bool FBPMutatorHelpers::RunMutation(UBlueprint* Blueprint, const FText& TransactionName, TFunctionRef<bool()> Body)
{
    if (!Blueprint)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("RunMutation: null blueprint (transaction '%s')"),
            *TransactionName.ToString());
        return false;
    }

    UE_LOG(LogBlueprintMutator, Log,
        TEXT("RunMutation begin: '%s' on %s"),
        *TransactionName.ToString(), *Blueprint->GetName());

    const FScopedTransaction Transaction(TransactionName);
    Blueprint->Modify();

    const bool bBodyOk = Body();
    if (!bBodyOk)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("RunMutation body returned false: '%s' on %s"),
            *TransactionName.ToString(), *Blueprint->GetName());
        return false;
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    UE_LOG(LogBlueprintMutator, Log,
        TEXT("RunMutation end: '%s' on %s"),
        *TransactionName.ToString(), *Blueprint->GetName());
    return true;
}

UClass* FBPMutatorHelpers::FindInterfaceClassByPath(const FString& Path)
{
    if (Path.IsEmpty()) return nullptr;
    UClass* Cls = LoadObject<UClass>(nullptr, *Path);
    if (!Cls)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("FindInterfaceClassByPath: not found '%s'"), *Path);
    }
    return Cls;
}

#undef LOCTEXT_NAMESPACE
