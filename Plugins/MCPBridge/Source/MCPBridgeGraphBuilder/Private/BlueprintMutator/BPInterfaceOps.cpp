#include "BPInterfaceOps.h"
#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "BPInterfaceOps"

bool FBPInterfaceOps::AddInterfaceImplementation(UBlueprint* Blueprint, UClass* InterfaceClass)
{
    if (!Blueprint) { UE_LOG(LogBlueprintMutator, Warning, TEXT("AddInterface: null BP")); return false; }
    if (!InterfaceClass) { UE_LOG(LogBlueprintMutator, Warning, TEXT("AddInterface: null interface class")); return false; }
    if (!InterfaceClass->HasAnyClassFlags(CLASS_Interface))
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("AddInterface: '%s' is not an interface"), *InterfaceClass->GetName());
        return false;
    }
    const FName IfaceFName = InterfaceClass->GetFName();
    for (const FBPInterfaceDescription& D : Blueprint->ImplementedInterfaces)
    {
        if (D.Interface == InterfaceClass)
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("AddInterface: '%s' already implemented"), *IfaceFName.ToString());
            return false;
        }
    }
    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("AddInterface", "Soul Juice: Add Interface"),
        [&]() -> bool
        {
            return FBlueprintEditorUtils::ImplementNewInterface(Blueprint, IfaceFName);
        });
}

bool FBPInterfaceOps::RemoveInterfaceImplementation(UBlueprint* Blueprint, UClass* InterfaceClass)
{
    if (!Blueprint || !InterfaceClass) return false;
    const bool bHas = Blueprint->ImplementedInterfaces.ContainsByPredicate(
        [&](const FBPInterfaceDescription& D){ return D.Interface == InterfaceClass; });
    if (!bHas)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveInterface: '%s' not implemented"),
            *InterfaceClass->GetName());
        return false;
    }
    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("RemoveInterface", "Soul Juice: Remove Interface"),
        [&]() -> bool
        {
            FBlueprintEditorUtils::RemoveInterface(Blueprint, InterfaceClass->GetFName(), /*bPreserveFunctions=*/false);
            return true;
        });
}

#undef LOCTEXT_NAMESPACE
