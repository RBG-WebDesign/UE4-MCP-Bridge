// Copyright 2026 RareBird Games. All Rights Reserved.
//
// widget_bind: the write half of the two things widget_inspect could already
// READ and nothing could author.
//
// A UMG widget built by widget_build is static and unreachable. Static because
// no property is bound to anything, so a ProgressBar shows the constant its
// spec gave it forever. Unreachable because bIsVariable is false, so the
// generated class publishes no member for it and a graph cannot address it by
// name. Both are one flag and one array on the UWidgetBlueprint, and neither
// had a write path.
//
// UE4.27 API, all confirmed in
// Engine/Source/Editor/UMGEditor/{Public/WidgetBlueprint.h,Private/WidgetBlueprint.cpp}:
//
//   FDelegateEditorBinding { ObjectName, PropertyName, FunctionName,
//                            SourceProperty, SourcePath, MemberGuid, Kind }
//
//   The delegate a binding actually drives is NOT PropertyName. UE4.27 looks up
//   FindFProperty<FDelegateProperty>(WidgetClass, PropertyName + "Delegate")
//   for a property binding and falls back to PropertyName itself for an event
//   (FDelegateEditorBinding::IsBindingValid, WidgetBlueprint.cpp:440). So the
//   caller writes "Percent" and the engine binds "PercentDelegate". Getting
//   that wrong produces a binding the compiler silently ignores, which is why
//   this command resolves the delegate up front and refuses when neither name
//   exists.
//
//   A property-kind binding needs a non-empty SourcePath or it validates
//   against a FunctionName that is not there. FEditorPropertyPath's public
//   constructor from a TArray<FFieldVariant> is what the UMG details panel
//   itself uses (UMGDetailCustomizations.cpp:313), and its Validate() is the
//   engine's own check, so it is what refuses here rather than a private guess
//   at the same rules.
//
// Failure atomicity: everything is validated before anything is written, and
// the pre-state of Bindings and of every bIsVariable flag is captured so a
// compile that comes back Error is restored and recompiled rather than left
// half-applied. The transaction is cancelled on that path too, but it is not
// the thing relied on: a Blueprint compile is not undo.
//
// Convergence: a rerun of an identical spec applies nothing, compiles nothing
// and saves nothing, and says so with converged = true. Satisfied-ness is
// evaluated immediately before each mutation rather than once at plan time,
// because an earlier entry in the same batch can be what satisfies a later one.

#include "MCPPuerTSBridgeService.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/Blueprint.h"
#include "Json.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
    /** One binding the caller asked for, resolved against the asset. */
    struct FDesiredWidgetBinding
    {
        FDelegateEditorBinding Binding;
        /** The delegate property the engine will actually drive. Reported so a
            caller can see that "Percent" became "PercentDelegate" rather than
            wonder why a binding compiled and did nothing. */
        FString DelegateName;
        FString SourceKind;
        FString SourceName;
    };

    /** Canonical text for one binding, used for equality.

        FDelegateEditorBinding::operator== deliberately compares only ObjectName
        and PropertyName, because a property may be bound once. That is the
        right identity for "which entry is this" and the wrong one for "is this
        entry already what was asked for": under operator==, rebinding Percent
        from GetChargeA to GetChargeB reads as no change. This includes
        everything that decides behaviour, so an unchanged report means
        unchanged. */
    FString BindingLine(const FDelegateEditorBinding& Binding)
    {
        return FString::Printf(TEXT("%s|%s|%s|%s|%s|%s"),
            *Binding.ObjectName,
            *Binding.PropertyName.ToString(),
            *Binding.FunctionName.ToString(),
            *Binding.SourceProperty.ToString(),
            Binding.Kind == EBindingKind::Function ? TEXT("function") : TEXT("property"),
            *Binding.SourcePath.GetDisplayText().ToString());
    }

    TSharedPtr<FJsonObject> BindingToJson(const FDelegateEditorBinding& Binding)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("widget_name"), Binding.ObjectName);
        Out->SetStringField(TEXT("property_name"), Binding.PropertyName.ToString());
        Out->SetStringField(TEXT("function_name"), Binding.FunctionName.ToString());
        Out->SetStringField(TEXT("source_property"), Binding.SourceProperty.ToString());
        Out->SetStringField(TEXT("kind"),
            Binding.Kind == EBindingKind::Function ? TEXT("function") : TEXT("property"));
        Out->SetStringField(TEXT("source_path"), Binding.SourcePath.GetDisplayText().ToString());
        return Out;
    }

    TArray<TSharedPtr<FJsonValue>> StringsToJsonArray(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Values.Num());
        for (const FString& Value : Values) { Out.Add(MakeShared<FJsonValueString>(Value)); }
        return Out;
    }

    /** Every property name on a widget class that a property binding can
        target: the "Delegate" suffix stripped back off, because that suffix is
        an implementation detail the caller never writes. Used only to make a
        refusal name the alternatives instead of just saying no. */
    TArray<FString> BindablePropertyNames(const UClass* WidgetClass)
    {
        TArray<FString> Names;
        if (WidgetClass == nullptr) { return Names; }
        for (TFieldIterator<FDelegateProperty> It(WidgetClass); It; ++It)
        {
            FString Name = It->GetName();
            if (Name.EndsWith(TEXT("Delegate")))
            {
                Name.LeftChopInline(8, false);
            }
            if (!Name.IsEmpty()) { Names.AddUnique(Name); }
        }
        Names.Sort();
        return Names;
    }

    /** Names of the widgets in the tree, for the same reason. */
    TArray<FString> WidgetNames(const UWidgetBlueprint* Blueprint)
    {
        TArray<FString> Names;
        if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr) { return Names; }
        TArray<UWidget*> All;
        Blueprint->WidgetTree->GetAllWidgets(All);
        for (const UWidget* Widget : All)
        {
            if (Widget != nullptr) { Names.Add(Widget->GetName()); }
        }
        Names.Sort();
        return Names;
    }

    /** The class a source function or variable is looked up on. The skeleton
        class is the one the UMG details panel uses, because it carries members
        that were added since the last full compile; the generated class is the
        fallback for an asset whose skeleton has not been built. */
    UClass* SourceLookupClass(const UWidgetBlueprint* Blueprint)
    {
        if (Blueprint == nullptr) { return nullptr; }
        if (Blueprint->SkeletonGeneratedClass != nullptr) { return Blueprint->SkeletonGeneratedClass; }
        return Blueprint->GeneratedClass;
    }

    FString CompileStatusText(const UWidgetBlueprint* Blueprint)
    {
        if (Blueprint == nullptr) { return TEXT("Unknown"); }
        switch (Blueprint->Status)
        {
        case BS_UpToDate:             return TEXT("UpToDate");
        case BS_UpToDateWithWarnings: return TEXT("UpToDateWithWarnings");
        case BS_Error:                return TEXT("Error");
        case BS_Dirty:                return TEXT("Dirty");
        default:                      return TEXT("Unknown");
        }
    }
}

bool UMCPPuerTSBridgeService::BindWidgetJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Widget bind requires an active transaction.");
        return false;
    }

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Widget bind spec must be a JSON object.");
        return false;
    }

    // --- Validation. Nothing below this block writes anything. ---

    FString AssetPath;
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    AssetPath = AssetPath.TrimStartAndEnd();
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Widget binding is limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    FString PackagePath;
    FString AssetName;
    AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (AssetName.IsEmpty())
    {
        OutError = FString::Printf(TEXT("Widget asset path has no asset name: %s"), *AssetPath);
        return false;
    }
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);

    UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
    if (Blueprint == nullptr)
    {
        OutError = FString::Printf(
            TEXT("No Widget Blueprint exists at '%s'. widget_bind binds an existing widget; "
                 "build it with widget_build first."),
            *ObjectPath);
        return false;
    }
    if (Blueprint->WidgetTree == nullptr)
    {
        OutError = FString::Printf(TEXT("The Widget Blueprint at '%s' has no widget tree."), *ObjectPath);
        return false;
    }

    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    bool bRemoveUnlisted = false;
    Spec->TryGetBoolField(TEXT("remove_unlisted"), bRemoveUnlisted);
    bool bSave = true;
    Spec->TryGetBoolField(TEXT("save"), bSave);

    const TArray<TSharedPtr<FJsonValue>>* RawBindings = nullptr;
    const bool bHasBindings = Spec->TryGetArrayField(TEXT("bindings"), RawBindings);
    const TArray<TSharedPtr<FJsonValue>>* RawExpose = nullptr;
    const bool bHasExpose = Spec->TryGetArrayField(TEXT("expose_as_variable"), RawExpose);

    if (!bHasBindings && !bHasExpose)
    {
        OutError = TEXT(
            "widget_bind needs 'bindings', 'expose_as_variable', or both. A request that states "
            "neither describes no desired state.");
        return false;
    }
    // remove_unlisted against a section the caller never stated would delete
    // work from a request that named none of it. Downward convergence applies
    // only inside the halves the spec actually described.
    if (bRemoveUnlisted && !bHasBindings && !bHasExpose)
    {
        OutError = TEXT("remove_unlisted needs a desired set to be unlisted against.");
        return false;
    }

    UClass* const LookupClass = SourceLookupClass(Blueprint);
    if (bHasBindings && RawBindings->Num() > 0 && LookupClass == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The Widget Blueprint at '%s' has neither a skeleton nor a generated class, so no "
                 "binding source can be resolved. Compile it first."),
            *ObjectPath);
        return false;
    }

    TArray<FDesiredWidgetBinding> Desired;
    TSet<FString> SeenKeys;
    if (bHasBindings)
    {
        for (const TSharedPtr<FJsonValue>& Value : *RawBindings)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value->TryGetObject(Entry))
            {
                OutError = TEXT("Every bindings entry must be a JSON object.");
                return false;
            }

            FString WidgetName;
            FString PropertyName;
            (*Entry)->TryGetStringField(TEXT("widget"), WidgetName);
            (*Entry)->TryGetStringField(TEXT("property"), PropertyName);
            WidgetName = WidgetName.TrimStartAndEnd();
            PropertyName = PropertyName.TrimStartAndEnd();
            if (WidgetName.IsEmpty() || PropertyName.IsEmpty())
            {
                OutError = TEXT("Every bindings entry needs a non-empty 'widget' and 'property'.");
                return false;
            }

            const FString Key = WidgetName + TEXT(".") + PropertyName;
            if (SeenKeys.Contains(Key))
            {
                OutError = FString::Printf(
                    TEXT("bindings lists '%s' twice. UE4.27 allows one binding per widget property, "
                         "so two entries for the same pair have no defined winner."),
                    *Key);
                return false;
            }
            SeenKeys.Add(Key);

            UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(*WidgetName));
            if (Widget == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("No widget named '%s' exists in '%s'. Widgets in this tree: %s"),
                    *WidgetName, *AssetName, *FString::Join(WidgetNames(Blueprint), TEXT(", ")));
                return false;
            }

            // The delegate the engine will drive: "<Property>Delegate" for a
            // property binding, the bare name for an event binding.
            FDelegateProperty* DelegateProperty = FindFProperty<FDelegateProperty>(
                Widget->GetClass(), FName(*(PropertyName + TEXT("Delegate"))));
            const bool bNeedsPureFunction = DelegateProperty != nullptr;
            if (DelegateProperty == nullptr)
            {
                DelegateProperty = FindFProperty<FDelegateProperty>(Widget->GetClass(), FName(*PropertyName));
            }
            if (DelegateProperty == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("'%s' on widget '%s' (%s) is not bindable: UE4.27 has neither a '%sDelegate' "
                         "nor a '%s' delegate property on that class. Bindable properties here: %s"),
                    *PropertyName, *WidgetName, *Widget->GetClass()->GetName(),
                    *PropertyName, *PropertyName,
                    *FString::Join(BindablePropertyNames(Widget->GetClass()), TEXT(", ")));
                return false;
            }

            FString SourceKind;
            FString SourceName;
            const TSharedPtr<FJsonObject>* Source = nullptr;
            if (!(*Entry)->TryGetObjectField(TEXT("source"), Source))
            {
                OutError = FString::Printf(
                    TEXT("bindings entry '%s' needs a 'source' object of "
                         "{kind: \"function\"|\"variable\", name: string}."), *Key);
                return false;
            }
            (*Source)->TryGetStringField(TEXT("kind"), SourceKind);
            (*Source)->TryGetStringField(TEXT("name"), SourceName);
            SourceKind = SourceKind.TrimStartAndEnd().ToLower();
            SourceName = SourceName.TrimStartAndEnd();
            if (SourceName.IsEmpty())
            {
                OutError = FString::Printf(TEXT("bindings entry '%s' needs a non-empty source.name."), *Key);
                return false;
            }
            if (SourceKind != TEXT("function") && SourceKind != TEXT("variable"))
            {
                OutError = FString::Printf(
                    TEXT("bindings entry '%s' has source.kind '%s'. It must be \"function\" or \"variable\"."),
                    *Key, *SourceKind);
                return false;
            }

            FDesiredWidgetBinding Wanted;
            Wanted.DelegateName = DelegateProperty->GetName();
            Wanted.SourceKind = SourceKind;
            Wanted.SourceName = SourceName;
            Wanted.Binding.ObjectName = WidgetName;
            Wanted.Binding.PropertyName = FName(*PropertyName);

            TArray<FFieldVariant> FieldChain;
            if (SourceKind == TEXT("function"))
            {
                UFunction* Function = LookupClass->FindFunctionByName(FName(*SourceName));
                if (Function == nullptr)
                {
                    OutError = FString::Printf(
                        TEXT("No function named '%s' exists on '%s'. Author it with "
                             "blueprint_graph_patch before binding to it."),
                        *SourceName, *LookupClass->GetName());
                    return false;
                }
                // UE4.27 refuses a non-pure function on a property binding at
                // compile time (WidgetBlueprint.cpp:493). Refusing here instead
                // keeps the asset out of the failure.
                if (bNeedsPureFunction
                    && !Function->HasAnyFunctionFlags(FUNC_Const | FUNC_BlueprintPure))
                {
                    OutError = FString::Printf(
                        TEXT("Function '%s' is not pure, and UE4.27 only binds pure functions to the "
                             "property delegate '%s'. Mark it BlueprintPure or const."),
                        *SourceName, *Wanted.DelegateName);
                    return false;
                }
                Wanted.Binding.FunctionName = Function->GetFName();
                UBlueprint::GetGuidFromClassByFieldName<UFunction>(
                    Function->GetOwnerClass(), Function->GetFName(), Wanted.Binding.MemberGuid);
                Wanted.Binding.Kind = EBindingKind::Function;
                FieldChain.Add(FFieldVariant(Function));
            }
            else
            {
                FProperty* Property = FindFProperty<FProperty>(LookupClass, FName(*SourceName));
                if (Property == nullptr)
                {
                    OutError = FString::Printf(
                        TEXT("No variable named '%s' exists on '%s'. Author it with "
                             "blueprint_member_patch before binding to it."),
                        *SourceName, *LookupClass->GetName());
                    return false;
                }
                Wanted.Binding.SourceProperty = Property->GetFName();
                UBlueprint::GetGuidFromClassByFieldName<FProperty>(
                    LookupClass, Property->GetFName(), Wanted.Binding.MemberGuid);
                Wanted.Binding.Kind = EBindingKind::Property;
                FieldChain.Add(FFieldVariant(Property));
            }

            // The same path the UMG details panel writes, and the same
            // validation the compiler runs. Whether the source type can drive
            // this delegate at all is the engine's question, so it answers it.
            Wanted.Binding.SourcePath = FEditorPropertyPath(FieldChain);
            FText PathError;
            if (!Wanted.Binding.SourcePath.Validate(DelegateProperty, PathError))
            {
                OutError = FString::Printf(
                    TEXT("Binding '%s' to %s '%s' is not valid: %s"),
                    *Key, *SourceKind, *SourceName, *PathError.ToString());
                return false;
            }

            Desired.Add(Wanted);
        }
    }

    TArray<FString> DesiredVariables;
    if (bHasExpose)
    {
        for (const TSharedPtr<FJsonValue>& Value : *RawExpose)
        {
            FString WidgetName;
            if (!Value->TryGetString(WidgetName) || WidgetName.TrimStartAndEnd().IsEmpty())
            {
                OutError = TEXT("Every expose_as_variable entry must be a non-empty widget name.");
                return false;
            }
            WidgetName = WidgetName.TrimStartAndEnd();
            if (Blueprint->WidgetTree->FindWidget(FName(*WidgetName)) == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("No widget named '%s' exists in '%s'. Widgets in this tree: %s"),
                    *WidgetName, *AssetName, *FString::Join(WidgetNames(Blueprint), TEXT(", ")));
                return false;
            }
            DesiredVariables.AddUnique(WidgetName);
        }
    }

    // A binding is dead unless its widget is a variable on the generated class:
    // the runtime resolves ObjectName against the class members. Refusing here
    // would make the common case a two-call dance, so the widget is exposed
    // instead, and the response says which ones this rule added.
    TArray<FString> ImpliedVariables;
    for (const FDesiredWidgetBinding& Wanted : Desired)
    {
        if (!DesiredVariables.Contains(Wanted.Binding.ObjectName))
        {
            DesiredVariables.Add(Wanted.Binding.ObjectName);
            ImpliedVariables.AddUnique(Wanted.Binding.ObjectName);
        }
    }

    // --- Plan. A report, not the authority: every mutation below re-checks. ---

    TArray<FString> PlannedAdds;
    TArray<FString> PlannedRemovals;
    TArray<FString> PlannedUnchanged;
    for (const FDesiredWidgetBinding& Wanted : Desired)
    {
        const FDelegateEditorBinding* Existing = Blueprint->Bindings.FindByPredicate(
            [&Wanted](const FDelegateEditorBinding& Candidate)
            {
                return Candidate.ObjectName == Wanted.Binding.ObjectName
                    && Candidate.PropertyName == Wanted.Binding.PropertyName;
            });
        if (Existing != nullptr && BindingLine(*Existing) == BindingLine(Wanted.Binding))
        {
            PlannedUnchanged.Add(BindingLine(Wanted.Binding));
        }
        else
        {
            PlannedAdds.Add(BindingLine(Wanted.Binding));
        }
    }
    if (bRemoveUnlisted && bHasBindings)
    {
        for (const FDelegateEditorBinding& Existing : Blueprint->Bindings)
        {
            const bool bWanted = Desired.ContainsByPredicate(
                [&Existing](const FDesiredWidgetBinding& Wanted)
                {
                    return Wanted.Binding.ObjectName == Existing.ObjectName
                        && Wanted.Binding.PropertyName == Existing.PropertyName;
                });
            if (!bWanted) { PlannedRemovals.Add(BindingLine(Existing)); }
        }
    }

    TArray<UWidget*> AllWidgets;
    Blueprint->WidgetTree->GetAllWidgets(AllWidgets);
    TArray<FString> PlannedExposures;
    TArray<FString> PlannedHides;
    for (UWidget* Widget : AllWidgets)
    {
        if (Widget == nullptr) { continue; }
        const FString Name = Widget->GetName();
        const bool bWanted = DesiredVariables.Contains(Name);
        if (bWanted && !Widget->bIsVariable) { PlannedExposures.Add(Name); }
        // Unexposing is opt-in twice over: remove_unlisted, and only when the
        // caller stated the variable half. Clearing bIsVariable breaks every
        // graph reference to that widget, so it is never a default.
        else if (!bWanted && Widget->bIsVariable && bRemoveUnlisted && bHasExpose)
        {
            PlannedHides.Add(Name);
        }
    }
    PlannedExposures.Sort();
    PlannedHides.Sort();
    PlannedAdds.Sort();
    PlannedRemovals.Sort();
    PlannedUnchanged.Sort();

    const int32 ExpectedChangeCount =
        PlannedAdds.Num() + PlannedRemovals.Num() + PlannedExposures.Num() + PlannedHides.Num();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("object_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("generated_class_path"),
        Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetPathName() : FString());
    Result->SetBoolField(TEXT("plan_only"), bPlanOnly);
    Result->SetBoolField(TEXT("remove_unlisted"), bRemoveUnlisted);
    Result->SetArrayField(TEXT("bindings_to_add"), StringsToJsonArray(PlannedAdds));
    Result->SetArrayField(TEXT("bindings_to_remove"), StringsToJsonArray(PlannedRemovals));
    Result->SetArrayField(TEXT("bindings_unchanged"), StringsToJsonArray(PlannedUnchanged));
    Result->SetArrayField(TEXT("variables_to_expose"), StringsToJsonArray(PlannedExposures));
    Result->SetArrayField(TEXT("variables_to_hide"), StringsToJsonArray(PlannedHides));
    Result->SetArrayField(TEXT("variables_implied_by_bindings"), StringsToJsonArray(ImpliedVariables));
    Result->SetNumberField(TEXT("expected_change_count"), ExpectedChangeCount);

    TArray<TSharedPtr<FJsonValue>> Warnings;
    TArray<TSharedPtr<FJsonValue>> Errors;

    if (bPlanOnly)
    {
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }
        Result->SetNumberField(TEXT("applied_change_count"), 0);
        Result->SetBoolField(TEXT("converged"), ExpectedChangeCount == 0);
        Result->SetBoolField(TEXT("compiled"), false);
        Result->SetStringField(TEXT("compile_status"), CompileStatusText(Blueprint));
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetArrayField(TEXT("warnings"), Warnings);
        Result->SetArrayField(TEXT("errors"), Errors);
        OutResultJson = SerializeJson(Result);
        return true;
    }

    // --- Apply. ---
    //
    // The pre-state is captured by value first. A Blueprint compile is not an
    // undo record, so the transaction alone cannot restore this if the compile
    // comes back Error; these two snapshots are what can.
    const TArray<FDelegateEditorBinding> BindingsBefore = Blueprint->Bindings;
    TMap<FName, bool> VariableFlagsBefore;
    for (const UWidget* Widget : AllWidgets)
    {
        if (Widget != nullptr) { VariableFlagsBefore.Add(Widget->GetFName(), Widget->bIsVariable != 0); }
    }

    int32 Applied = 0;
    // Modify() dirties the package, so a converged rerun must not call it. It
    // is taken on the first real write instead of up front, which is what lets
    // "nothing changed" also mean "nothing was dirtied".
    bool bBlueprintModified = false;
    auto MarkBlueprintModified = [&]()
    {
        if (!bBlueprintModified)
        {
            Blueprint->Modify();
            bBlueprintModified = true;
        }
    };

    for (const FDesiredWidgetBinding& Wanted : Desired)
    {
        // Re-read the array here, not at plan time. An earlier entry in this
        // same batch can already have written the state being judged, and a
        // no-op decided against pre-batch state is how a live mutation gets
        // reported as converged.
        const int32 Index = Blueprint->Bindings.IndexOfByPredicate(
            [&Wanted](const FDelegateEditorBinding& Candidate)
            {
                return Candidate.ObjectName == Wanted.Binding.ObjectName
                    && Candidate.PropertyName == Wanted.Binding.PropertyName;
            });
        if (Index != INDEX_NONE
            && BindingLine(Blueprint->Bindings[Index]) == BindingLine(Wanted.Binding))
        {
            continue;
        }
        MarkBlueprintModified();
        if (Index != INDEX_NONE)
        {
            Blueprint->Bindings.RemoveAt(Index);
        }
        Blueprint->Bindings.Add(Wanted.Binding);
        Applied++;
    }

    if (bRemoveUnlisted && bHasBindings)
    {
        for (int32 Index = Blueprint->Bindings.Num() - 1; Index >= 0; --Index)
        {
            const FDelegateEditorBinding& Existing = Blueprint->Bindings[Index];
            const bool bWanted = Desired.ContainsByPredicate(
                [&Existing](const FDesiredWidgetBinding& Wanted)
                {
                    return Wanted.Binding.ObjectName == Existing.ObjectName
                        && Wanted.Binding.PropertyName == Existing.PropertyName;
                });
            if (!bWanted)
            {
                MarkBlueprintModified();
                Blueprint->Bindings.RemoveAt(Index);
                Applied++;
            }
        }
    }

    for (UWidget* Widget : AllWidgets)
    {
        if (Widget == nullptr) { continue; }
        const bool bWanted = DesiredVariables.Contains(Widget->GetName());
        const bool bShouldHide = !bWanted && bRemoveUnlisted && bHasExpose;
        const bool bCurrent = Widget->bIsVariable != 0;
        const bool bTarget = bWanted ? true : (bShouldHide ? false : bCurrent);
        // Same rule: the flag is read immediately before it is written.
        if (bCurrent == bTarget) { continue; }
        Widget->Modify();
        Widget->bIsVariable = bTarget;
        Applied++;
    }

    bool bCompiled = false;
    bool bSaved = false;
    if (Applied > 0)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        bCompiled = Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings;

        if (!bCompiled)
        {
            // Restore by value and recompile, then cancel the transaction. The
            // order matters: the transaction cancel replays undo records against
            // objects that must still be whole, and a Blueprint left compiled
            // against rejected bindings is a broken asset either way.
            Blueprint->Bindings = BindingsBefore;
            for (UWidget* Widget : AllWidgets)
            {
                if (Widget == nullptr) { continue; }
                const bool* Before = VariableFlagsBefore.Find(Widget->GetFName());
                if (Before != nullptr && (Widget->bIsVariable != 0) != *Before)
                {
                    Widget->bIsVariable = *Before;
                }
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            FKismetEditorUtilities::CompileBlueprint(Blueprint);
            if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }

            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("The Widget Blueprint compiled to status %s with the requested bindings, so they "
                     "were reverted and the asset recompiled."),
                *CompileStatusText(Blueprint))));
            Result->SetNumberField(TEXT("applied_change_count"), 0);
            Result->SetBoolField(TEXT("converged"), false);
            Result->SetBoolField(TEXT("compiled"), false);
            Result->SetStringField(TEXT("compile_status"), TEXT("RolledBack"));
            Result->SetStringField(TEXT("compile_status_after_rollback"), CompileStatusText(Blueprint));
            Result->SetBoolField(TEXT("saved"), false);
            Result->SetArrayField(TEXT("warnings"), Warnings);
            Result->SetArrayField(TEXT("errors"), Errors);
            OutResultJson = SerializeJson(Result);
            return true;
        }

        if (bSave)
        {
            FString SaveError;
            bSaved = SaveProjectAsset(ObjectPath, SaveError);
            if (!bSaved)
            {
                Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("The bindings applied and compiled, but the asset could not be saved: %s"),
                    *SaveError)));
            }
        }
    }
    else
    {
        bCompiled = Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings;
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Nothing changed: the asset already held every binding and variable flag this spec "
                 "asked for. Nothing was compiled and nothing was saved.")));
    }

    // Read the result back off the asset rather than echoing the request, so
    // the response is evidence and not a restatement.
    TArray<TSharedPtr<FJsonValue>> BindingsAfter;
    for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
    {
        BindingsAfter.Add(MakeShared<FJsonValueObject>(BindingToJson(Binding).ToSharedRef()));
    }
    TArray<FString> VariablesAfter;
    for (const UWidget* Widget : AllWidgets)
    {
        if (Widget != nullptr && Widget->bIsVariable) { VariablesAfter.Add(Widget->GetName()); }
    }
    VariablesAfter.Sort();

    TArray<TSharedPtr<FJsonValue>> DelegateNames;
    for (const FDesiredWidgetBinding& Wanted : Desired)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("widget_name"), Wanted.Binding.ObjectName);
        Entry->SetStringField(TEXT("property_name"), Wanted.Binding.PropertyName.ToString());
        Entry->SetStringField(TEXT("delegate_property"), Wanted.DelegateName);
        Entry->SetStringField(TEXT("source_kind"), Wanted.SourceKind);
        Entry->SetStringField(TEXT("source_name"), Wanted.SourceName);
        DelegateNames.Add(MakeShared<FJsonValueObject>(Entry.ToSharedRef()));
    }

    Result->SetNumberField(TEXT("applied_change_count"), Applied);
    Result->SetBoolField(TEXT("converged"), Applied == 0);
    Result->SetBoolField(TEXT("compiled"), bCompiled);
    Result->SetStringField(TEXT("compile_status"), CompileStatusText(Blueprint));
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetArrayField(TEXT("bindings"), BindingsAfter);
    Result->SetArrayField(TEXT("variables"), StringsToJsonArray(VariablesAfter));
    Result->SetArrayField(TEXT("resolved_delegates"), DelegateNames);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    Result->SetArrayField(TEXT("errors"), Errors);
    OutResultJson = SerializeJson(Result);
    return true;
}
