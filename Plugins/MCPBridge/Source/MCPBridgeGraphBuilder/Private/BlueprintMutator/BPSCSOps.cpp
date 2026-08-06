// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPSCSOps.h"
#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/SceneComponent.h"

#define LOCTEXT_NAMESPACE "BPSCSOps"

namespace
{
    USCS_Node* FindSCSNodeByName(USimpleConstructionScript* SCS, const FString& Name)
    {
        if (!SCS) return nullptr;
        const FName NameF(*Name);
        for (USCS_Node* N : SCS->GetAllNodes())
        {
            if (N && N->GetVariableName() == NameF) return N;
        }
        return nullptr;
    }

    USCS_Node* FindParentOf(USimpleConstructionScript* SCS, USCS_Node* Target)
    {
        if (!SCS || !Target) return nullptr;
        for (USCS_Node* Candidate : SCS->GetAllNodes())
        {
            if (!Candidate) continue;
            for (USCS_Node* Child : Candidate->GetChildNodes())
            {
                if (Child == Target) return Candidate;
            }
        }
        return nullptr;
    }
}

bool FBPSCSOps::RemoveSCSNode(UBlueprint* Blueprint, const FString& ComponentName)
{
    if (!Blueprint) { UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveSCSNode: null BP")); return false; }
    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (!SCS) { UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveSCSNode: BP has no SCS")); return false; }

    USCS_Node* Target = FindSCSNodeByName(SCS, ComponentName);
    if (!Target)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveSCSNode: '%s' not found"), *ComponentName);
        return false;
    }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("RemoveSCSNode", "Soul Juice: Remove SCS Node"),
        [&]() -> bool
        {
            USCS_Node* Parent = FindParentOf(SCS, Target);
            const bool bTargetIsRoot = (Parent == nullptr);

            TArray<USCS_Node*> Children = Target->GetChildNodes();

            for (USCS_Node* Child : Children)
            {
                if (!Child) continue;
                Target->Modify();
                Child->Modify();
                Target->RemoveChildNode(Child, /*bRemoveFromAllNodes=*/false);
            }

            for (USCS_Node* Child : Children)
            {
                if (!Child) continue;
                if (bTargetIsRoot)
                {
                    SCS->Modify();
                    SCS->AddNode(Child);
                }
                else
                {
                    Parent->Modify();
                    Parent->AddChildNode(Child, /*bAddToAllNodes=*/false);
                }
            }

            SCS->Modify();
            SCS->RemoveNode(Target, /*bValidateSceneRootNodes=*/true);
            return true;
        });
}

bool FBPSCSOps::RenameSCSNode(UBlueprint* Blueprint, const FString& OldName, const FString& NewName)
{
    if (!Blueprint) return false;
    if (OldName.IsEmpty() || NewName.IsEmpty()) return false;
    if (OldName == NewName) return true;
    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (!SCS) return false;

    const FName NewF(*NewName);

    USCS_Node* Target = FindSCSNodeByName(SCS, OldName);
    if (!Target)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("RenameSCSNode: '%s' not found"), *OldName);
        return false;
    }
    for (USCS_Node* N : SCS->GetAllNodes())
    {
        if (N && N != Target && N->GetVariableName() == NewF)
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("RenameSCSNode: '%s' collides"), *NewName);
            return false;
        }
    }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("RenameSCSNode", "Soul Juice: Rename SCS Node"),
        [&]() -> bool
        {
            // RenameComponentMemberVariable is the SCS-aware variant — it updates the
            // VariableName on the SCS_Node AND rewrites all VariableGet/Set refs.
            FBlueprintEditorUtils::RenameComponentMemberVariable(Blueprint, Target, NewF);
            return true;
        });
}

bool FBPSCSOps::ReparentSCSNode(UBlueprint* Blueprint, const FString& ComponentName,
    const FString& ParentName)
{
    if (!Blueprint || !Blueprint->SimpleConstructionScript || ComponentName.IsEmpty()) return false;
    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    USCS_Node* Target = FindSCSNodeByName(SCS, ComponentName);
    USCS_Node* NewParent = ParentName.IsEmpty() ? nullptr : FindSCSNodeByName(SCS, ParentName);
    if (!Target || (!ParentName.IsEmpty() && !NewParent) || Target == NewParent) return false;
    if (NewParent && (!Target->ComponentTemplate || !NewParent->ComponentTemplate
        || !Target->ComponentTemplate->IsA<USceneComponent>()
        || !NewParent->ComponentTemplate->IsA<USceneComponent>())) return false;

    TArray<USCS_Node*> Stack = Target->GetChildNodes();
    while (Stack.Num() > 0)
    {
        USCS_Node* Descendant = Stack.Pop();
        if (Descendant == NewParent) return false;
        if (Descendant) Stack.Append(Descendant->GetChildNodes());
    }
    USCS_Node* OldParent = FindParentOf(SCS, Target);
    if (OldParent == NewParent) return true;

    return FBPMutatorHelpers::RunMutation(Blueprint,
        LOCTEXT("ReparentSCSNode", "MCP Bridge: Reparent Component"), [&]()
        {
            Target->Modify();
            SCS->Modify();
            if (OldParent)
            {
                OldParent->Modify();
                OldParent->RemoveChildNode(Target, /*bRemoveFromAllNodes=*/true);
            }
            else
            {
                SCS->RemoveNode(Target, /*bValidateSceneRootNodes=*/false);
            }
            if (NewParent)
            {
                NewParent->Modify();
                NewParent->AddChildNode(Target, /*bAddToAllNodes=*/true);
            }
            else
            {
                SCS->AddNode(Target);
            }
            return FindParentOf(SCS, Target) == NewParent;
        });
}

#undef LOCTEXT_NAMESPACE
