// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "BlueprintMutatorLibrary.h"
#include "Editor.h"
#include "Editor/Transactor.h"
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

    // Asked BEFORE the scope opens, because after it opens every scope looks the
    // same from the inside. UE4.27 has one transaction with a nesting count, not
    // nested transactions: a scope opened while another is active shares the
    // outer transaction's records, and cancelling it cancels the outer one
    // entirely (EditorTransaction.cpp:1287). Whether this scope may cancel is
    // therefore decided by whether anything was already open.
    const bool bOwnsTransaction = !(GEditor != nullptr && GEditor->Trans != nullptr && GEditor->Trans->IsActive());

    FScopedTransaction Transaction(TransactionName);
    Blueprint->Modify();

    const bool bBodyOk = Body();
    if (!bBodyOk)
    {
        if (bOwnsTransaction)
        {
            // The failure path used to fall through to the scope's destructor,
            // which ends the transaction normally and COMMITS whatever the body
            // wrote before it gave up. Every entry point was transactional and
            // none was failure-atomic, from this one site.
            UBlueprintMutatorLibrary::RevertAndCancelTransaction(Transaction);
            UE_LOG(LogBlueprintMutator, Warning,
                TEXT("RunMutation body returned false: '%s' on %s. The transaction was reverted "
                     "and cancelled; the Blueprint is as the mutation found it."),
                *TransactionName.ToString(), *Blueprint->GetName());
        }
        else
        {
            // Nested. Reverting here would revert the caller's transaction too,
            // and cancelling it would discard the caller's work, so neither is
            // this scope's call to make. Say what was left rather than imply a
            // rollback that did not happen: the enclosing scope owns it, and in
            // the live path that scope is the command boundary opened by
            // UMCPPuerTSBridgeService::AcceptCommand.
            UE_LOG(LogBlueprintMutator, Warning,
                TEXT("RunMutation body returned false: '%s' on %s. This scope is nested inside an "
                     "already-open transaction, so it did NOT roll back: UE4.27 cannot cancel part "
                     "of a transaction. Any partial write is still present and the enclosing "
                     "transaction owns undoing it."),
                *TransactionName.ToString(), *Blueprint->GetName());
        }
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
