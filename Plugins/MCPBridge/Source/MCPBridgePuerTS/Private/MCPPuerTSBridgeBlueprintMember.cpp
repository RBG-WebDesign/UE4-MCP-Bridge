// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "BlueprintGraphBuilderLibrary.h"
#include "BlueprintMutatorLibrary.h"
#include "MCPBridgeAssetRollback.h"
#include "MCPBridgeBlueprintMembers.h"

#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "EdGraph/EdGraph.h"
#include "Json.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

namespace
{
    /** Every op this command understands, in one place, so an unsupported op is
        answered with the vocabulary rather than with a shrug. */
    const TCHAR* const SupportedOps[] = {
        TEXT("add_variable"), TEXT("remove_variable"), TEXT("set_variable_default"),
        TEXT("add_function"), TEXT("remove_function"),
        TEXT("add_interface"), TEXT("remove_interface"),
        TEXT("add_event_dispatcher"), TEXT("remove_event_dispatcher"),
        TEXT("remove_component"), TEXT("rename_component"),
    };

    FString SupportedOpList()
    {
        TArray<FString> Names;
        for (const TCHAR* Name : SupportedOps) { Names.Add(Name); }
        return FString::Join(Names, TEXT(", "));
    }

    bool IsSupportedOp(const FString& Op)
    {
        for (const TCHAR* Name : SupportedOps) { if (Op == Name) { return true; } }
        return false;
    }

    /**
     * Does every field the caller SPECIFIED appear, with the same value, in the
     * value actually on the asset?
     *
     * Subset rather than equality on purpose. A type descriptor the inspector
     * writes carries every field; a caller writes the two or three that matter.
     * Full equality would refuse {"category":"float"} against a variable that is
     * exactly a float, which is a false refusal, and treating any difference as
     * a match would be the opposite lie.
     */
    bool JsonSubsetMatches(const TSharedPtr<FJsonValue>& Desired, const TSharedPtr<FJsonValue>& Actual)
    {
        if (!Desired.IsValid() || !Actual.IsValid()) { return false; }
        if (Desired->Type != Actual->Type) { return false; }

        const TSharedPtr<FJsonObject>* DesiredObject = nullptr;
        const TSharedPtr<FJsonObject>* ActualObject = nullptr;
        if (Desired->TryGetObject(DesiredObject) && Actual->TryGetObject(ActualObject))
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*DesiredObject)->Values)
            {
                const TSharedPtr<FJsonValue> ActualField = (*ActualObject)->TryGetField(Pair.Key);
                if (!JsonSubsetMatches(Pair.Value, ActualField)) { return false; }
            }
            return true;
        }

        const TArray<TSharedPtr<FJsonValue>>* DesiredArray = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* ActualArray = nullptr;
        if (Desired->TryGetArray(DesiredArray) && Actual->TryGetArray(ActualArray))
        {
            if (DesiredArray->Num() != ActualArray->Num()) { return false; }
            for (int32 Index = 0; Index < DesiredArray->Num(); ++Index)
            {
                if (!JsonSubsetMatches((*DesiredArray)[Index], (*ActualArray)[Index])) { return false; }
            }
            return true;
        }

        FString DesiredText;
        FString ActualText;
        {
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&DesiredText);
            FJsonSerializer::Serialize(Desired, TEXT(""), Writer);
        }
        {
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ActualText);
            FJsonSerializer::Serialize(Actual, TEXT(""), Writer);
        }
        return DesiredText == ActualText;
    }

    /** Serialize any JSON value to the compact text the mutator entry points take.
     *
     *  CONDENSED, not the default writer. TJsonWriter<>'s default print policy is
     *  the pretty one, and serializing a bare value under an empty identifier
     *  emits a separator and a newline first, so 0.75 left here as ",\r\n0.75".
     *  ImportText then stopped at the comma, consumed nothing, and reported
     *  success while writing 0.0. That is why a valid float was rejected the
     *  moment the type check started requiring the whole buffer to be consumed:
     *  the value had been mangled all along and nothing looked. */
    FString ValueToJsonText(const TSharedPtr<FJsonValue>& Value)
    {
        if (!Value.IsValid()) { return FString(); }

        // Serialize the value as the single field of a throwaway object, then
        // drop the wrapper. FJsonSerializer has no bare-value entry point: the
        // identifier overload goes through the writer's named-value path, which
        // writes a separator prefix, so 0.75 came out as ",0.75" (and as
        // ",\r\n0.75" under the default pretty policy).
        //
        // Wrapping is the lazy correct fix rather than a hand-written switch on
        // EJson: escaping, number formatting and nested objects and arrays all
        // stay the serializer's job, which is where they belong.
        TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
        Wrapper->SetField(TEXT("v"), Value);

        FString Text;
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
        if (!FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer)) { return FString(); }

        // Condensed output is exactly {"v":<value>}. Refuse rather than guess if
        // it is not, so a print-policy change surfaces here instead of silently
        // corrupting every default value again.
        const FString Prefix = TEXT("{\"v\":");
        if (!Text.StartsWith(Prefix) || !Text.EndsWith(TEXT("}"))) { return FString(); }
        return Text.Mid(Prefix.Len(), Text.Len() - Prefix.Len() - 1);
    }

    const FBPVariableDescription* FindVariable(const UBlueprint* Blueprint, const FString& Name)
    {
        const FName VarName(*Name);
        return Blueprint->NewVariables.FindByPredicate(
            [&](const FBPVariableDescription& Description) { return Description.VarName == VarName; });
    }

    bool HasGraphNamed(const TArray<UEdGraph*>& Graphs, const FString& Name)
    {
        const FName GraphName(*Name);
        for (const UEdGraph* Graph : Graphs)
        {
            if (Graph != nullptr && Graph->GetFName() == GraphName) { return true; }
        }
        return false;
    }

    bool HasComponentNamed(const UBlueprint* Blueprint, const FString& Name)
    {
        const USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
        if (SCS == nullptr) { return false; }
        const FName ComponentName(*Name);
        for (const USCS_Node* Node : SCS->GetAllNodes())
        {
            if (Node != nullptr && Node->GetVariableName() == ComponentName) { return true; }
        }
        return false;
    }

    bool ImplementsInterface(const UBlueprint* Blueprint, const UClass* InterfaceClass)
    {
        return Blueprint->ImplementedInterfaces.ContainsByPredicate(
            [&](const FBPInterfaceDescription& Description) { return Description.Interface == InterfaceClass; });
    }

    /** One operation after the request has been parsed and its references
        resolved. The request object itself is kept rather than copied out, so
        the state predicate below reads the same fields the applier writes. */
    struct FResolvedOp
    {
        FString Op;
        int32 Index = 0;
        const TSharedPtr<FJsonObject>* Spec = nullptr;
        FString Label;
        FString MemberKey;
        bool bSatisfied = false;
        UClass* InterfaceClass = nullptr;
    };

    /** The type descriptor the INSPECTOR reports for a variable, or an invalid
        value when there is no such variable. Read through the shared snapshot on
        purpose: a caller compares against what graph_inspect answers, so the
        type this command matches against has to be the same rendering. */
    TSharedPtr<FJsonValue> ActualVariableType(UBlueprint* Blueprint, const FString& Name)
    {
        const TSharedPtr<FJsonObject> Snapshot = MCPBridgeBlueprintMembers::BuildSnapshot(Blueprint);
        const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
        if (!Snapshot->TryGetArrayField(TEXT("variables"), Variables) || Variables == nullptr)
        {
            return nullptr;
        }
        for (const TSharedPtr<FJsonValue>& Value : *Variables)
        {
            const TSharedPtr<FJsonObject>* VariableObject = nullptr;
            FString VariableName;
            if (Value->TryGetObject(VariableObject)
                && (*VariableObject)->TryGetStringField(TEXT("name"), VariableName)
                && VariableName == Name)
            {
                return (*VariableObject)->TryGetField(TEXT("type"));
            }
        }
        return nullptr;
    }

    /**
     * Is a member variable's default ALREADY the requested value?
     *
     * Answered by the property's own codec rather than by comparing strings: the
     * caller sends 1 and Unreal exports 1.000000, and a string comparison would
     * call an unchanged default a change and write it, dirtying a package on a
     * converged rerun. The desired text is imported into a scratch value of the
     * property's type and compared with FProperty::Identical.
     *
     * Doubles as the validation pass: a value that will not import is refused
     * here, before anything is mutated, instead of failing halfway through a
     * batch.
     */
    enum class EDefaultCheck : uint8 { Equal, Different, Unavailable };

    EDefaultCheck CompareVariableDefault(
        UBlueprint* Blueprint,
        const FString& VarName,
        const TSharedPtr<FJsonValue>& DesiredValue,
        FString& OutError)
    {
        UClass* GeneratedClass = Blueprint->GeneratedClass;
        UObject* DefaultObject = GeneratedClass != nullptr
            ? GeneratedClass->GetDefaultObject(/*bCreateIfNeeded=*/false) : nullptr;
        FProperty* Property = GeneratedClass != nullptr
            ? FindFProperty<FProperty>(GeneratedClass, FName(*VarName)) : nullptr;
        if (DefaultObject == nullptr || Property == nullptr)
        {
            OutError = FString::Printf(
                TEXT("'%s' has no compiled property on this Blueprint yet, so its default cannot be "
                     "read or compared. Compile the Blueprint first."), *VarName);
            return EDefaultCheck::Unavailable;
        }

        const bool bTextLike = Property->IsA<FStrProperty>()
            || Property->IsA<FNameProperty>() || Property->IsA<FTextProperty>();
        const FString ImportText = UBlueprintMutatorLibrary::JsonDefaultToImportText(
            ValueToJsonText(DesiredValue), bTextLike);

        FDefaultConstructedPropertyElement Scratch(Property);
        // Not `ImportText(...) != nullptr`. That returned non-null for a float
        // handed "not-a-number", which then compared Different against the real
        // default, applied as 0.0, verified against itself and saved.
        const bool bImported = UBlueprintMutatorLibrary::ImportDefaultValue(
            Property, ImportText, Scratch.GetObjAddress(), DefaultObject);
        bool bEqual = false;
        if (bImported)
        {
            const void* Current = Property->ContainerPtrToValuePtr<void>(DefaultObject);
            bEqual = Property->Identical(Scratch.GetObjAddress(), Current);
        }

        if (!bImported)
        {
            OutError = FString::Printf(
                TEXT("'%s' is not a value %s can hold (variable '%s')."),
                *ImportText, *Property->GetClass()->GetName(), *VarName);
            return EDefaultCheck::Unavailable;
        }
        return bEqual ? EDefaultCheck::Equal : EDefaultCheck::Different;
    }

    /**
     * Is this operation's requested end state ALREADY true of the Blueprint as
     * it stands right now?
     *
     * One definition, asked three times: against the pre-batch state to build
     * the plan, again immediately before each mutation to decide whether it is
     * still needed, and a third time after the batch to verify.
     *
     * The middle call is the one lane A paid for. A batch is ordered, so an
     * operation's precondition is whatever the operations before it left, not
     * what the asset looked like when the request was parsed. A skip decided
     * against the pre-batch state skips work that has since become necessary,
     * and applying against it does work that has since become wrong. Nothing
     * here reads a cached answer; every call re-reads the asset.
     *
     * A condition that cannot be evaluated (a default that will not import, a
     * variable with no compiled property) counts as NOT satisfied. Refusing is
     * pass 1's job, and it does so before anything is mutated; if such an
     * operation reaches the applier anyway, letting the mutator refuse it is a
     * failure that rolls back, which beats a silent skip.
     */
    bool IsOpSatisfied(UBlueprint* Blueprint, const FResolvedOp& R)
    {
        FString Name;
        (*R.Spec)->TryGetStringField(TEXT("name"), Name);

        if (R.Op == TEXT("add_variable"))
        {
            if (FindVariable(Blueprint, Name) == nullptr) { return false; }
            const TSharedPtr<FJsonObject>* TypeObject = nullptr;
            if ((*R.Spec)->TryGetObjectField(TEXT("type"), TypeObject)
                && !JsonSubsetMatches(
                    MakeShared<FJsonValueObject>(*TypeObject), ActualVariableType(Blueprint, Name)))
            {
                return false;
            }
            const TSharedPtr<FJsonValue> Desired = (*R.Spec)->TryGetField(TEXT("default"));
            if (!Desired.IsValid()) { return true; }
            FString CheckError;
            return CompareVariableDefault(Blueprint, Name, Desired, CheckError) == EDefaultCheck::Equal;
        }
        if (R.Op == TEXT("remove_variable"))
        {
            return FindVariable(Blueprint, Name) == nullptr;
        }
        if (R.Op == TEXT("set_variable_default"))
        {
            FString CheckError;
            return CompareVariableDefault(
                Blueprint, Name, (*R.Spec)->TryGetField(TEXT("default")), CheckError) == EDefaultCheck::Equal;
        }
        if (R.Op == TEXT("add_function")) { return HasGraphNamed(Blueprint->FunctionGraphs, Name); }
        if (R.Op == TEXT("remove_function")) { return !HasGraphNamed(Blueprint->FunctionGraphs, Name); }
        if (R.Op == TEXT("add_event_dispatcher"))
        {
            return HasGraphNamed(Blueprint->DelegateSignatureGraphs, Name);
        }
        if (R.Op == TEXT("remove_event_dispatcher"))
        {
            return !HasGraphNamed(Blueprint->DelegateSignatureGraphs, Name);
        }
        if (R.Op == TEXT("add_interface")) { return ImplementsInterface(Blueprint, R.InterfaceClass); }
        if (R.Op == TEXT("remove_interface")) { return !ImplementsInterface(Blueprint, R.InterfaceClass); }
        if (R.Op == TEXT("remove_component")) { return !HasComponentNamed(Blueprint, Name); }
        if (R.Op == TEXT("rename_component"))
        {
            FString From;
            FString To;
            (*R.Spec)->TryGetStringField(TEXT("from"), From);
            (*R.Spec)->TryGetStringField(TEXT("to"), To);
            // Both halves, not just the absence of the old name. The old name
            // can be gone because an earlier operation removed the component
            // outright, and calling that a satisfied rename would report a
            // component the caller asked for and does not have.
            return !HasComponentNamed(Blueprint, From) && HasComponentNamed(Blueprint, To);
        }
        return false;
    }
}

bool UMCPPuerTSBridgeService::PatchBlueprintMembersJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Blueprint member patch spec must be a JSON object.");
        return false;
    }

    FString AssetPath;
    if (!Spec->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutError = TEXT("asset_path is required.");
        return false;
    }
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/")) || AssetPath.Contains(TEXT("..")))
    {
        OutError = TEXT("Blueprint assets are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    const bool bCompile = !Spec->HasTypedField<EJson::Boolean>(TEXT("compile"))
        || Spec->GetBoolField(TEXT("compile"));
    const bool bSave = !Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        || Spec->GetBoolField(TEXT("save"));
    const bool bVerify = !Spec->HasTypedField<EJson::Boolean>(TEXT("verify"))
        || Spec->GetBoolField(TEXT("verify"));

    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Spec->TryGetArrayField(TEXT("operations"), Operations) || Operations == nullptr)
    {
        OutError = TEXT("operations must be an array.");
        return false;
    }
    if (Operations->Num() == 0)
    {
        OutError = TEXT("operations must be a non-empty array; a patch with no operations is not a request.");
        return false;
    }

    // Patching never creates an asset. A patch against something that is not
    // there is a mistake in the request, and creating an empty Blueprint to
    // receive it would turn that mistake into an asset nobody asked for.
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *FPackageName::GetShortName(AssetPath));
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
    if (Blueprint == nullptr)
    {
        OutError = FString::Printf(
            TEXT("No Blueprint at %s. Patching changes an existing Blueprint and never creates one; "
                 "use blueprint_build to author the asset first."), *AssetPath);
        return false;
    }

    auto ReadMemberHash = [&]() -> FString
    {
        return MCPBridgeBlueprintMembers::StructureHash(
            MCPBridgeBlueprintMembers::BuildSnapshot(Blueprint));
    };
    const FString PreHash = ReadMemberHash();

    // --- Pass 1: resolve, validate and REFUSE, mutating nothing.
    //
    // Every operation that cannot legally run is caught here. A batch that
    // half-applies and then meets an illegal operation has already done the
    // damage the rollback then has to undo, and the mutator's own entry points
    // compile on every call, so that damage is expensive.
    //
    // The satisfied/not-satisfied split this pass also produces is a PLAN, not
    // a decision. Whether an operation actually runs is settled at apply time
    // against the state the operations before it left; see IsOpSatisfied.
    TArray<FResolvedOp> Resolved;
    TArray<FString> Refusals;
    TSet<FString> SeenMemberKeys;

    for (int32 Index = 0; Index < Operations->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* OpObject = nullptr;
        if (!(*Operations)[Index]->TryGetObject(OpObject))
        {
            Refusals.Add(FString::Printf(TEXT("operation %d is not an object."), Index));
            continue;
        }

        FResolvedOp R;
        R.Index = Index;
        R.Spec = OpObject;
        (*OpObject)->TryGetStringField(TEXT("op"), R.Op);
        if (!IsSupportedOp(R.Op))
        {
            Refusals.Add(FString::Printf(
                TEXT("operation %d names an unsupported op '%s'. Supported: %s. Graph node and pin "
                     "changes belong to blueprint_graph_patch."),
                Index, *R.Op, *SupportedOpList()));
            continue;
        }

        auto RequireString = [&](const TCHAR* Field, FString& Out) -> bool
        {
            if ((*OpObject)->TryGetStringField(Field, Out) && !Out.IsEmpty()) { return true; }
            Refusals.Add(FString::Printf(
                TEXT("operation %d (%s) requires a non-empty '%s'."), Index, *R.Op, Field));
            return false;
        };

        if (R.Op == TEXT("add_variable") || R.Op == TEXT("remove_variable")
            || R.Op == TEXT("set_variable_default"))
        {
            FString Name;
            if (!RequireString(TEXT("name"), Name)) { continue; }
            R.Label = FString::Printf(TEXT("%s %s"), *R.Op, *Name);
            R.MemberKey = FString::Printf(TEXT("variable:%s"), *Name);
            const FBPVariableDescription* Existing = FindVariable(Blueprint, Name);

            if (R.Op == TEXT("add_variable"))
            {
                const TSharedPtr<FJsonObject>* TypeObject = nullptr;
                if (!(*OpObject)->TryGetObjectField(TEXT("type"), TypeObject))
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (add_variable %s) requires a 'type' descriptor object, "
                             "for example {\"category\":\"float\"}."), Index, *Name));
                    continue;
                }
                if (Existing != nullptr)
                {
                    // Compared against the INSPECTOR's rendering of the existing
                    // type, so the type this refuses on is the one a caller can
                    // read back for themselves.
                    const TSharedPtr<FJsonValue> ActualType = ActualVariableType(Blueprint, Name);
                    const TSharedPtr<FJsonValue> DesiredType =
                        MakeShared<FJsonValueObject>(*TypeObject);
                    if (!JsonSubsetMatches(DesiredType, ActualType))
                    {
                        Refusals.Add(FString::Printf(
                            TEXT("operation %d (add_variable %s): a variable of that name already "
                                 "exists with type %s, which does not match the requested %s. There "
                                 "is no change-variable-type primitive; remove_variable then "
                                 "add_variable if the retype is intended."),
                            Index, *Name, *ValueToJsonText(ActualType), *ValueToJsonText(DesiredType)));
                        continue;
                    }
                    // Same name, same type. A default given alongside it still
                    // has to be reachable, so refuse one that will not import
                    // here rather than halfway through the batch.
                    const TSharedPtr<FJsonValue> Desired = (*OpObject)->TryGetField(TEXT("default"));
                    FString CheckError;
                    if (Desired.IsValid()
                        && CompareVariableDefault(Blueprint, Name, Desired, CheckError)
                            == EDefaultCheck::Unavailable)
                    {
                        Refusals.Add(FString::Printf(
                            TEXT("operation %d (add_variable %s): %s"), Index, *Name, *CheckError));
                        continue;
                    }
                }
            }
            else if (R.Op == TEXT("remove_variable"))
            {
                if (Existing == nullptr && Blueprint->GeneratedClass != nullptr
                    && FindFProperty<FProperty>(Blueprint->GeneratedClass, FName(*Name)) != nullptr)
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (remove_variable %s): that property exists but was not "
                             "declared on this Blueprint. Inherited and native properties are not "
                             "this command's to remove."), Index, *Name));
                    continue;
                }
            }
            else
            {
                if (Existing == nullptr)
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (set_variable_default %s): no variable of that name is "
                             "declared on this Blueprint."), Index, *Name));
                    continue;
                }
                const TSharedPtr<FJsonValue> Desired = (*OpObject)->TryGetField(TEXT("default"));
                if (!Desired.IsValid())
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (set_variable_default %s) requires a 'default' value."),
                        Index, *Name));
                    continue;
                }
                FString CheckError;
                if (CompareVariableDefault(Blueprint, Name, Desired, CheckError)
                    == EDefaultCheck::Unavailable)
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (set_variable_default %s): %s"), Index, *Name, *CheckError));
                    continue;
                }
            }
        }
        else if (R.Op == TEXT("add_function") || R.Op == TEXT("remove_function"))
        {
            FString Name;
            if (!RequireString(TEXT("name"), Name)) { continue; }
            R.Label = FString::Printf(TEXT("%s %s"), *R.Op, *Name);
            // Functions and event dispatchers share one key namespace because
            // they share one namespace in the Blueprint: both are UEdGraphs, and
            // a batch naming the same graph as each is a batch with no single
            // desired state.
            R.MemberKey = FString::Printf(TEXT("graph:%s"), *Name);
        }
        else if (R.Op == TEXT("add_event_dispatcher") || R.Op == TEXT("remove_event_dispatcher"))
        {
            FString Name;
            if (!RequireString(TEXT("name"), Name)) { continue; }
            R.Label = FString::Printf(TEXT("%s %s"), *R.Op, *Name);
            R.MemberKey = FString::Printf(TEXT("graph:%s"), *Name);
            if (R.Op == TEXT("add_event_dispatcher")
                && !HasGraphNamed(Blueprint->DelegateSignatureGraphs, Name)
                && HasGraphNamed(Blueprint->FunctionGraphs, Name))
            {
                Refusals.Add(FString::Printf(
                    TEXT("operation %d (add_event_dispatcher %s): a function graph already has that "
                         "name."), Index, *Name));
                continue;
            }
        }
        else if (R.Op == TEXT("add_interface") || R.Op == TEXT("remove_interface"))
        {
            FString Path;
            if (!RequireString(TEXT("path"), Path)) { continue; }
            R.Label = FString::Printf(TEXT("%s %s"), *R.Op, *Path);
            R.MemberKey = FString::Printf(TEXT("interface:%s"), *Path);
            R.InterfaceClass = LoadObject<UClass>(nullptr, *Path);
            if (R.InterfaceClass == nullptr)
            {
                Refusals.Add(FString::Printf(
                    TEXT("operation %d (%s): no class loaded from '%s'."), Index, *R.Op, *Path));
                continue;
            }
            if (!R.InterfaceClass->HasAnyClassFlags(CLASS_Interface))
            {
                Refusals.Add(FString::Printf(
                    TEXT("operation %d (%s): '%s' is a class but not an interface."),
                    Index, *R.Op, *Path));
                continue;
            }
        }
        else if (R.Op == TEXT("remove_component") || R.Op == TEXT("rename_component"))
        {
            if (Blueprint->SimpleConstructionScript == nullptr)
            {
                Refusals.Add(FString::Printf(
                    TEXT("operation %d (%s): this Blueprint has no SimpleConstructionScript, so it "
                         "has no components. Only an Actor Blueprint does."), Index, *R.Op));
                continue;
            }
            if (R.Op == TEXT("remove_component"))
            {
                FString Name;
                if (!RequireString(TEXT("name"), Name)) { continue; }
                R.Label = FString::Printf(TEXT("remove_component %s"), *Name);
                R.MemberKey = FString::Printf(TEXT("component:%s"), *Name);
            }
            else
            {
                FString From;
                FString To;
                if (!RequireString(TEXT("from"), From)) { continue; }
                if (!RequireString(TEXT("to"), To)) { continue; }
                R.Label = FString::Printf(TEXT("rename_component %s -> %s"), *From, *To);
                R.MemberKey = FString::Printf(TEXT("component:%s"), *From);
                const bool bFromPresent = HasComponentNamed(Blueprint, From);
                const bool bToPresent = HasComponentNamed(Blueprint, To);
                if (!bFromPresent && !bToPresent)
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (rename_component %s -> %s): neither name is a component "
                             "on this Blueprint, so there is nothing to rename and nothing to "
                             "converge on."), Index, *From, *To));
                    continue;
                }
                if (bFromPresent && bToPresent)
                {
                    Refusals.Add(FString::Printf(
                        TEXT("operation %d (rename_component %s -> %s): '%s' is already taken by a "
                             "different component."), Index, *From, *To, *To));
                    continue;
                }
            }
        }

        // The plan's satisfied/not-satisfied split, from the one predicate the
        // applier and the verifier also use. Computed here rather than inside
        // each validation branch above so the three answers cannot drift.
        R.bSatisfied = IsOpSatisfied(Blueprint, R);

        if (SeenMemberKeys.Contains(R.MemberKey))
        {
            Refusals.Add(FString::Printf(
                TEXT("operation %d (%s) is the second operation in this batch on %s. A batch that "
                     "names one member twice has no single desired state to converge on."),
                Index, *R.Label, *R.MemberKey));
            continue;
        }
        SeenMemberKeys.Add(R.MemberKey);
        Resolved.Add(R);
    }

    TArray<TSharedPtr<FJsonValue>> ToApply;
    TArray<TSharedPtr<FJsonValue>> Unchanged;
    for (const FResolvedOp& R : Resolved)
    {
        (R.bSatisfied ? Unchanged : ToApply).Add(MakeShared<FJsonValueString>(R.Label));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetNumberField(TEXT("requested_operation_count"), Operations->Num());
    Result->SetArrayField(TEXT("operations_to_apply"), ToApply);
    Result->SetArrayField(TEXT("unchanged_operations"), Unchanged);
    Result->SetNumberField(TEXT("expected_change_count"), ToApply.Num());
    Result->SetStringField(TEXT("pre_member_hash"), PreHash);

    if (Refusals.Num() > 0)
    {
        OutError = FString::Join(Refusals, TEXT(" "));
        return false;
    }

    // --- Plan mode returns here, having changed nothing. ---
    if (bPlanOnly)
    {
        Result->SetBoolField(TEXT("plan_only"), true);
        // A prediction is offered only where it is deterministic. A batch that
        // changes nothing leaves the members it found, so the hash is known
        // exactly; anything else depends on what the engine materialises when a
        // graph or variable is created, and a guessed hash a caller then
        // compares against is worse than admitting there is none.
        if (ToApply.Num() == 0)
        {
            Result->SetStringField(TEXT("predicted_member_hash"), PreHash);
        }
        else
        {
            Result->SetStringField(TEXT("prediction_unavailable_reason"),
                TEXT("the batch changes the Blueprint's members, and a created graph or variable "
                     "carries engine-generated detail that only exists once it is made. A "
                     "predicted hash is offered only for a no-op batch, where it is the current "
                     "hash by definition."));
        }
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        return true;
    }

    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Blueprint member patching requires an active transaction.");
        return false;
    }

    // --- One rollback boundary around the whole batch. ---
    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    bool bRollbackAttempted = false;
    bool bRollbackSucceeded = false;
    auto FailWithRollback = [&](const FString& Error) -> bool
    {
        bRollbackAttempted = true;
        // This is the outermost transaction (AcceptCommand opened it), so this
        // is the scope allowed to revert. Cancel() alone never restored
        // anything - it pops the record and leaves the writes, which is the
        // whole of finding 0g - so the revert has to run before it.
        // FBPMutatorHelpers::RunMutation deliberately does NOT roll back its own
        // failures, because every mutator scope is nested inside this one.
        if (ActiveTransaction != nullptr)
        {
            UBlueprintMutatorLibrary::RevertAndCancelTransaction(*ActiveTransaction);
        }
        Rollback.Rollback();
        // Trust the read-back, not the undo. Findings 0g: a cancelled
        // transaction looked transactional and was not, and the only thing that
        // settles whether the members came back is reading them again.
        const FString Recovered = ReadMemberHash();
        bRollbackSucceeded = (Recovered == PreHash);
        OutError = bRollbackSucceeded
            ? Error
            : FString::Printf(
                TEXT("%s The rollback did NOT restore the original members: the member hash is %s, "
                     "expected %s. Treat this asset as damaged."),
                *Error, *Recovered, *PreHash);
        return false;
    };

    // --- Apply. Every entry point below is UBlueprintMutatorLibrary's; this
    //     command owns the boundary, not the mutation. ---
    TArray<TSharedPtr<FJsonValue>> Applied;
    TArray<TSharedPtr<FJsonValue>> Skipped;
    for (const FResolvedOp& R : Resolved)
    {
        // Asked here, not read from the plan. The batch is ordered, so this
        // operation's precondition is whatever the operations before it left.
        // R.bSatisfied was answered against the pre-batch state and is only a
        // prediction by the time execution reaches this operation; deciding a
        // skip with it is the lane A defect, one member collection over.
        if (IsOpSatisfied(Blueprint, R))
        {
            Skipped.Add(MakeShared<FJsonValueString>(R.Label));
            continue;
        }

        bool bOk = false;
        FString Name;
        (*R.Spec)->TryGetStringField(TEXT("name"), Name);

        if (R.Op == TEXT("add_variable"))
        {
            const TSharedPtr<FJsonObject>* TypeObject = nullptr;
            (*R.Spec)->TryGetObjectField(TEXT("type"), TypeObject);
            const FString TypeJson = ValueToJsonText(MakeShared<FJsonValueObject>(*TypeObject));
            const TSharedPtr<FJsonValue> Desired = (*R.Spec)->TryGetField(TEXT("default"));
            const FString DefaultJson = Desired.IsValid() ? ValueToJsonText(Desired) : FString();
            FString Category;
            (*R.Spec)->TryGetStringField(TEXT("category"), Category);

            if (FindVariable(Blueprint, Name) != nullptr)
            {
                // The variable is here and the predicate above said the request
                // is not met, so either the default differs or the type does.
                // Pass 1 ruled out the type against the PRE-batch state, which
                // an earlier operation may since have moved, so it is checked
                // again rather than assumed: writing a default into a variable
                // of the wrong type is the silent wrong answer.
                const TSharedPtr<FJsonValue> ActualType = ActualVariableType(Blueprint, Name);
                const TSharedPtr<FJsonValue> DesiredType = MakeShared<FJsonValueObject>(*TypeObject);
                if (!JsonSubsetMatches(DesiredType, ActualType))
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("operation %d (add_variable %s): by the time this operation ran, a "
                             "variable of that name existed with type %s rather than the requested "
                             "%s. There is no change-variable-type primitive."),
                        R.Index, *Name, *ValueToJsonText(ActualType), *ValueToJsonText(DesiredType)));
                }
                bOk = UBlueprintMutatorLibrary::SetVariableDefault(Blueprint, Name, DefaultJson);
            }
            else
            {
                bOk = UBlueprintMutatorLibrary::AddVariable(Blueprint, Name, TypeJson, DefaultJson, Category);
            }
        }
        else if (R.Op == TEXT("remove_variable"))
        {
            bOk = UBlueprintMutatorLibrary::RemoveVariable(Blueprint, Name);
        }
        else if (R.Op == TEXT("set_variable_default"))
        {
            bOk = UBlueprintMutatorLibrary::SetVariableDefault(
                Blueprint, Name, ValueToJsonText((*R.Spec)->TryGetField(TEXT("default"))));
        }
        else if (R.Op == TEXT("add_function"))
        {
            const TSharedPtr<FJsonValue> Inputs = (*R.Spec)->TryGetField(TEXT("inputs"));
            const TSharedPtr<FJsonValue> Outputs = (*R.Spec)->TryGetField(TEXT("outputs"));
            bOk = !UBlueprintMutatorLibrary::AddFunction(
                Blueprint, Name,
                Inputs.IsValid() ? ValueToJsonText(Inputs) : TEXT("[]"),
                Outputs.IsValid() ? ValueToJsonText(Outputs) : TEXT("[]")).IsEmpty();
        }
        else if (R.Op == TEXT("remove_function"))
        {
            bOk = UBlueprintMutatorLibrary::RemoveFunction(Blueprint, Name);
        }
        else if (R.Op == TEXT("add_event_dispatcher"))
        {
            TSharedPtr<FJsonObject> Signature = MakeShared<FJsonObject>();
            const TArray<TSharedPtr<FJsonValue>>* Parameters = nullptr;
            if ((*R.Spec)->TryGetArrayField(TEXT("parameters"), Parameters) && Parameters != nullptr)
            {
                Signature->SetArrayField(TEXT("parameters"), *Parameters);
            }
            bOk = !UBlueprintMutatorLibrary::AddEventDispatcher(
                Blueprint, Name, ValueToJsonText(MakeShared<FJsonValueObject>(Signature))).IsEmpty();
        }
        else if (R.Op == TEXT("remove_event_dispatcher"))
        {
            bOk = UBlueprintMutatorLibrary::RemoveEventDispatcher(Blueprint, Name);
        }
        else if (R.Op == TEXT("add_interface"))
        {
            bOk = UBlueprintMutatorLibrary::AddInterfaceImplementation(Blueprint, R.InterfaceClass);
        }
        else if (R.Op == TEXT("remove_interface"))
        {
            bOk = UBlueprintMutatorLibrary::RemoveInterfaceImplementation(Blueprint, R.InterfaceClass);
        }
        else if (R.Op == TEXT("remove_component"))
        {
            bOk = UBlueprintMutatorLibrary::RemoveSCSNode(Blueprint, Name);
        }
        else if (R.Op == TEXT("rename_component"))
        {
            FString From;
            FString To;
            (*R.Spec)->TryGetStringField(TEXT("from"), From);
            (*R.Spec)->TryGetStringField(TEXT("to"), To);
            bOk = UBlueprintMutatorLibrary::RenameSCSNode(Blueprint, From, To);
        }

        if (!bOk)
        {
            return FailWithRollback(FString::Printf(
                TEXT("operation %d (%s) failed inside the mutator; see the LogBlueprintMutator "
                     "output for the reason. The whole batch is rolled back rather than left "
                     "half-applied."), R.Index, *R.Label));
        }
        Applied.Add(MakeShared<FJsonValueString>(R.Label));
    }

    // --- Compile. The mutator compiles on every entry point already; this is the
    //     one that produces a status a caller can read and refuse on. ---
    FString CompileStatus = TEXT("Skipped");
    if (bCompile)
    {
        TSharedPtr<FJsonObject> CompileReport;
        TSharedRef<TJsonReader<>> CompileReader =
            TJsonReaderFactory<>::Create(UBlueprintGraphBuilderLibrary::CompileAndReport(Blueprint));
        bool bCompileSucceeded = false;
        if (FJsonSerializer::Deserialize(CompileReader, CompileReport) && CompileReport.IsValid())
        {
            CompileReport->TryGetBoolField(TEXT("success"), bCompileSucceeded);
            CompileReport->TryGetStringField(TEXT("status"), CompileStatus);
        }
        if (!bCompileSucceeded)
        {
            return FailWithRollback(FString::Printf(
                TEXT("The patched Blueprint did not compile (status %s). The patch is rolled back "
                     "rather than saved: a Blueprint that will not compile is not the one the "
                     "caller asked for."), *CompileStatus));
        }
    }
    Result->SetStringField(TEXT("compile_status"), CompileStatus);

    // --- Read the members back independently and verify. ---
    const FString PostHash = ReadMemberHash();
    Result->SetStringField(TEXT("post_member_hash"), PostHash);
    Result->SetArrayField(TEXT("applied_operations"), Applied);
    Result->SetNumberField(TEXT("applied_operation_count"), Applied.Num());
    // Overwrites the plan's list with what the applier actually found when it
    // reached each operation. operations_to_apply and expected_change_count are
    // deliberately left as the plan, so a caller who compares them against
    // applied_operation_count can see where the two disagreed and why.
    Result->SetArrayField(TEXT("unchanged_operations"), Skipped);
    const bool bConverged = (Applied.Num() == 0) && (PostHash == PreHash);
    Result->SetBoolField(TEXT("converged"), bConverged);

    TArray<TSharedPtr<FJsonValue>> VerificationMismatches;
    if (bVerify)
    {
        // The independent check is the point. The loop above reports what the
        // mutator told it; this asks the Blueprint. Every operation is re-run
        // through the SAME satisfied-predicate pass 1 used, against freshly read
        // state, so an operation that reported success and did not land is
        // caught here rather than saved.
        for (const FResolvedOp& R : Resolved)
        {
            if (!IsOpSatisfied(Blueprint, R))
            {
                VerificationMismatches.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (%s) is not present in the Blueprint after the patch."),
                    R.Index, *R.Label)));
            }
        }
        if (Applied.Num() > 0 && PostHash == PreHash)
        {
            VerificationMismatches.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("%d operation(s) reported as applied, but the member hash is unchanged at %s. "
                     "Either nothing actually changed or the change is invisible to the member "
                     "snapshot; neither is a result to save."), Applied.Num(), *PostHash)));
        }
        if (Applied.Num() == 0 && PostHash != PreHash)
        {
            VerificationMismatches.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("no operation was applied, but the member hash moved from %s to %s."),
                *PreHash, *PostHash)));
        }
    }
    Result->SetArrayField(TEXT("verification_mismatches"), VerificationMismatches);

    if (VerificationMismatches.Num() > 0)
    {
        return FailWithRollback(FString::Printf(
            TEXT("%d verification mismatch(es) after patching members, so nothing was saved."),
            VerificationMismatches.Num()));
    }

    // --- Save, and only now. ---
    bool bSaved = false;
    if (bSave && Applied.Num() > 0)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(ObjectPath, SaveError);
        if (!bSaved)
        {
            return FailWithRollback(FString::Printf(
                TEXT("The member patch verified but could not be saved: %s"), *SaveError));
        }
    }
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetBoolField(TEXT("rollback_attempted"), bRollbackAttempted);
    Result->SetBoolField(TEXT("rollback_succeeded"), bRollbackSucceeded);
    Result->SetObjectField(TEXT("members"), MCPBridgeBlueprintMembers::BuildSnapshot(Blueprint));
    Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

    // The same cleanup evidence shape the builder and the graph patch return,
    // from the same rollback object, so a caller reads one contract across all
    // three commands.
    Result->SetObjectField(TEXT("cleanup"), Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
    FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
    return true;
}
