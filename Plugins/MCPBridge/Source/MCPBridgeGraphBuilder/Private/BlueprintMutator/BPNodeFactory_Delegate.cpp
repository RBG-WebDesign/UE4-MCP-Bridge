#include "BPNodeFactory.h"
#include "BPNodeFactory_Internals.h"
#include "BPGLogCategories.h"
#include "EdGraph/EdGraph.h"

#include "K2Node_BaseMCDelegate.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_AssignDelegate.h"
#include "K2Node_CreateDelegate.h"

// =========================================================================================
// Phase 4.5 Batch 3 — Delegates (6 factories).
//
// All bodies use the BPNodeFactoryInternal::ParseJsonConfig + PositionAndFinalize helpers
// (declared in BPNodeFactory_Internals.h). No `using namespace` — explicit qualification.
//
// Five of the six nodes (AddDelegate/RemoveDelegate/CallDelegate/ClearDelegate/AssignDelegate)
// derive from UK2Node_BaseMCDelegate and share an FMemberReference DelegateReference member
// (K2Node_BaseMCDelegate.h line 22). We populate it via the base class helper
// SetFromProperty(FProperty*, bool, UClass*) which is BLUEPRINTGRAPH_API (line 46) and
// internally calls FMemberReference::SetFromField<FProperty>(Property, bSelfContext, OwnerClass)
// (declared in MemberReference.h line 107). This is the canonical path used by the editor
// context menu when a user right-clicks a delegate in MyBlueprint and picks Add/Remove/etc.
//
// CreateDelegate is structurally different — it binds a function signature to a delegate value
// at runtime and uses `FName SelectedFunctionName` (K2Node_CreateDelegate.h line 21) plus the
// BLUEPRINTGRAPH_API void SetFunction(FName) helper (line 50).
//
// No Build.cs deps added: BlueprintGraph is already a dep (used throughout existing factories).
// =========================================================================================

namespace
{
    // Resolve the delegate reference from ConfigJson. Handles two shapes:
    //   1. {self_var_name: "OnDisp"} -> SetSelfMember(FName(OnDisp))
    //   2. {delegate_owner_class: "/Path/Class_C", delegate_name: "OnDisp"}
    //                                 -> SetFromProperty(FMulticastDelegateProperty, false, OwnerClass)
    // Returns true on success; logs + returns false on any failure mode.
    bool ResolveDelegateRef(const TSharedPtr<FJsonObject>& Cfg,
                            UK2Node_BaseMCDelegate* Node,
                            const TCHAR* Ctx)
    {
        if (!Cfg.IsValid() || !Node) return false;

        FString SelfVarName;
        if (Cfg->TryGetStringField(TEXT("self_var_name"), SelfVarName) && !SelfVarName.IsEmpty())
        {
            // Self-context: FMemberReference::SetSelfMember handles bSelfContext=true and
            // clears MemberParent (MemberReference.h line 188). Resolution at pin-allocation
            // time will use the owning Blueprint's generated class as the scope.
            Node->DelegateReference.SetSelfMember(FName(*SelfVarName));
            return true;
        }

        FString OwnerPath, DelegateName;
        Cfg->TryGetStringField(TEXT("delegate_owner_class"), OwnerPath);
        Cfg->TryGetStringField(TEXT("delegate_name"), DelegateName);
        if (OwnerPath.IsEmpty() || DelegateName.IsEmpty())
        {
            UE_LOG(LogBlueprintMutator, Warning,
                TEXT("%s: need (self_var_name) or (delegate_owner_class + delegate_name)"), Ctx);
            return false;
        }

        UClass* Owner = LoadObject<UClass>(nullptr, *OwnerPath);
        if (!Owner)
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: owner class not found '%s'"), Ctx, *OwnerPath);
            return false;
        }

        // Resolve the multicast-delegate FProperty by name on the owner class (+ supers).
        FMulticastDelegateProperty* DelegateProp =
            FindFProperty<FMulticastDelegateProperty>(Owner, FName(*DelegateName));
        if (!DelegateProp)
        {
            UE_LOG(LogBlueprintMutator, Warning,
                TEXT("%s: delegate '%s' not found on class '%s'"), Ctx, *DelegateName, *OwnerPath);
            return false;
        }

        // SetFromProperty is the BLUEPRINTGRAPH_API helper on UK2Node_BaseMCDelegate
        // (K2Node_BaseMCDelegate.h line 46). It forwards to FMemberReference::SetFromField
        // which populates MemberParent + MemberName + MemberGuid correctly.
        Node->SetFromProperty(DelegateProp, /*bSelfContext=*/false, Owner);
        return true;
    }
}

//------------------------------------------------------------------------------
// Factory 3.1 — AddDelegate → UK2Node_AddDelegate
//------------------------------------------------------------------------------
UEdGraphNode* FBPNodeFactory::CreateAddDelegate(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FGraphNodeCreator<UK2Node_AddDelegate> Creator(*Graph);
    UK2Node_AddDelegate* Node = Creator.CreateNode();
    if (!ResolveDelegateRef(Cfg, Node, TEXT("CreateAddDelegate")))
    {
        Node->DestroyNode();
        return nullptr;
    }
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 3.2 — RemoveDelegate → UK2Node_RemoveDelegate
//------------------------------------------------------------------------------
UEdGraphNode* FBPNodeFactory::CreateRemoveDelegate(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FGraphNodeCreator<UK2Node_RemoveDelegate> Creator(*Graph);
    UK2Node_RemoveDelegate* Node = Creator.CreateNode();
    if (!ResolveDelegateRef(Cfg, Node, TEXT("CreateRemoveDelegate")))
    {
        Node->DestroyNode();
        return nullptr;
    }
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 3.3 — CallDelegate → UK2Node_CallDelegate
//------------------------------------------------------------------------------
UEdGraphNode* FBPNodeFactory::CreateCallDelegate(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FGraphNodeCreator<UK2Node_CallDelegate> Creator(*Graph);
    UK2Node_CallDelegate* Node = Creator.CreateNode();
    if (!ResolveDelegateRef(Cfg, Node, TEXT("CreateCallDelegate")))
    {
        Node->DestroyNode();
        return nullptr;
    }
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 3.4 — ClearDelegate → UK2Node_ClearDelegate
//------------------------------------------------------------------------------
UEdGraphNode* FBPNodeFactory::CreateClearDelegate(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FGraphNodeCreator<UK2Node_ClearDelegate> Creator(*Graph);
    UK2Node_ClearDelegate* Node = Creator.CreateNode();
    if (!ResolveDelegateRef(Cfg, Node, TEXT("CreateClearDelegate")))
    {
        Node->DestroyNode();
        return nullptr;
    }
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 3.5 — AssignDelegate → UK2Node_AssignDelegate
//------------------------------------------------------------------------------
// NOTE: UK2Node_AssignDelegate inherits from UK2Node_AddDelegate (K2Node_AssignDelegate.h
// line 18) and spawns an attached CustomEvent in PostPlacedNewNode — bound to the delegate.
// No secondary bIsSet property exists in 4.27 source; the plan's `is_set` hint is obsolete.
// We accept (and ignore) it for forward compat rather than returning an error.
UEdGraphNode* FBPNodeFactory::CreateAssignDelegate(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FGraphNodeCreator<UK2Node_AssignDelegate> Creator(*Graph);
    UK2Node_AssignDelegate* Node = Creator.CreateNode();
    if (!ResolveDelegateRef(Cfg, Node, TEXT("CreateAssignDelegate")))
    {
        Node->DestroyNode();
        return nullptr;
    }
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 3.6 — CreateDelegate → UK2Node_CreateDelegate
//------------------------------------------------------------------------------
// NOTE: UK2Node_CreateDelegate stores its binding in `FName SelectedFunctionName`
// (K2Node_CreateDelegate.h line 21). The BLUEPRINTGRAPH_API helper `SetFunction(FName)`
// (line 50) is the public setter — plan's TBD resolved. SelectedFunctionName is set
// POST-Finalize because the pin shape derives from wherever the delegate is wired
// downstream; standalone creation with no wiring yields a "pin type wildcard" node and
// that is expected (see plan risks §).
UEdGraphNode* FBPNodeFactory::CreateCreateDelegate(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    // Accept two keys for convenience — plan uses `selected_function_name`, controller
    // summary uses `function_name`. Either works.
    FString FuncName;
    Cfg->TryGetStringField(TEXT("selected_function_name"), FuncName);
    if (FuncName.IsEmpty())
    {
        Cfg->TryGetStringField(TEXT("function_name"), FuncName);
    }

    FGraphNodeCreator<UK2Node_CreateDelegate> Creator(*Graph);
    UK2Node_CreateDelegate* Node = Creator.CreateNode();
    BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);

    if (!FuncName.IsEmpty())
    {
        // Negative-test hook: reject names that clearly can't be a valid UFunction
        // identifier (e.g. contains spaces, slashes, or dots). The Kismet editor would
        // surface an IsValid() error later but we want the MCP to fail fast.
        const FString& N = FuncName;
        if (N.Contains(TEXT(" ")) || N.Contains(TEXT("/")) || N.Contains(TEXT(".")))
        {
            UE_LOG(LogBlueprintMutator, Warning,
                TEXT("CreateCreateDelegate: invalid function_name '%s'"), *FuncName);
            Node->DestroyNode();
            return nullptr;
        }
        Node->SetFunction(FName(*FuncName));
    }
    return Node;
}
