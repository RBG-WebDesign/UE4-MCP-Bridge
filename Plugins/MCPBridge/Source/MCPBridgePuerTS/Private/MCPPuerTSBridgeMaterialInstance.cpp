// Copyright 2026 RareBird Games. All Rights Reserved.
//
// Create or update a UMaterialInstanceConstant and set its scalar, vector,
// texture and static switch parameters, in one transaction.
//
// Why instances and not master material graphs: a material instance is a set of
// named parameter overrides on a parent, so the operation is desired-state by
// construction - the request IS the end state, a rerun converges, and every
// value written can be read back through the same reader the inspector uses.
// A master material graph is none of those things in UE4.27; see the note at
// the bottom of this file and lane I's report.
//
// The boundary this command owns, which UMaterialEditingLibrary does not have:
// the /Game/MCPGenerated/ limit, whole-request validation before any asset is
// created or mutated, Modify() so the writes are actually in the undo record,
// a compile whose result is reported rather than assumed, an independent
// read-back that decides whether the writes landed, and a save that runs only
// after that read-back passes.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Texture.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Json.h"
#include "MCPBridgeAssetRollback.h"
#include "MCPBridgeMaterialSnapshot.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "RHI.h"
#include "ScopedTransaction.h"
#include "StaticParameterSet.h"
#include "UObject/Package.h"

using MCPBridgeMaterialSnapshot::EParameterKind;

namespace
{
    /** One requested parameter write, resolved and validated before anything is
        touched. Only the member matching Kind is meaningful. */
    struct FParameterRequest
    {
        EParameterKind Kind = EParameterKind::Scalar;
        FName Name;
        float Scalar = 0.0f;
        FLinearColor Vector = FLinearColor::Black;
        UTexture* Texture = nullptr;
        FString TexturePath;
        bool bSwitch = false;

        FString Id() const
        {
            return FString::Printf(TEXT("%s:%s"),
                MCPBridgeMaterialSnapshot::ParameterKindName(Kind), *Name.ToString());
        }
    };

    void SetRequestedValueField(const TSharedPtr<FJsonObject>& Out, const FParameterRequest& Request)
    {
        switch (Request.Kind)
        {
            case EParameterKind::Scalar:
                Out->SetNumberField(TEXT("requested_value"), Request.Scalar);
                return;
            case EParameterKind::Vector:
            {
                TSharedPtr<FJsonObject> Value = MakeShared<FJsonObject>();
                Value->SetNumberField(TEXT("r"), Request.Vector.R);
                Value->SetNumberField(TEXT("g"), Request.Vector.G);
                Value->SetNumberField(TEXT("b"), Request.Vector.B);
                Value->SetNumberField(TEXT("a"), Request.Vector.A);
                Out->SetObjectField(TEXT("requested_value"), Value);
                return;
            }
            case EParameterKind::Texture:
                Out->SetStringField(TEXT("requested_value"), Request.TexturePath);
                return;
            case EParameterKind::StaticSwitch:
                Out->SetBoolField(TEXT("requested_value"), Request.bSwitch);
                return;
        }
    }

    /** Is the instance already at the requested value AND already overriding it?
        Both halves matter: a value that merely matches the parent's default is
        not an override, so writing it is a real change and skipping it would
        make the command silently not do what it was asked. */
    bool MatchesRequest(UMaterialInstanceConstant* Instance, const FParameterRequest& Request)
    {
        if (Instance == nullptr) { return false; }
        if (!MCPBridgeMaterialSnapshot::IsOverridden(Instance, Request.Kind, Request.Name))
        {
            return false;
        }
        switch (Request.Kind)
        {
            case EParameterKind::Scalar:
                return FMath::IsNearlyEqual(
                    UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(Instance, Request.Name),
                    Request.Scalar, KINDA_SMALL_NUMBER);
            case EParameterKind::Vector:
                return UMaterialEditingLibrary::GetMaterialInstanceVectorParameterValue(Instance, Request.Name)
                    .Equals(Request.Vector, KINDA_SMALL_NUMBER);
            case EParameterKind::Texture:
                return UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(Instance, Request.Name)
                    == Request.Texture;
            case EParameterKind::StaticSwitch:
                return UMaterialEditingLibrary::GetMaterialInstanceStaticSwitchParameterValue(Instance, Request.Name)
                    == Request.bSwitch;
            default:
                return false;
        }
    }

    /** Read one requested parameter's map out of the spec into typed requests.
        Every failure is collected rather than thrown, so a caller gets the whole
        list of what is wrong with a request instead of the first item. */
    void ParseParameterMap(
        const TSharedPtr<FJsonObject>& Spec,
        const TCHAR* Field,
        EParameterKind Kind,
        TArray<FParameterRequest>& OutRequests,
        TArray<FString>& OutErrors)
    {
        const TSharedPtr<FJsonObject>* Map = nullptr;
        if (!Spec->HasField(Field)) { return; }
        if (!Spec->TryGetObjectField(Field, Map) || Map == nullptr)
        {
            OutErrors.Add(FString::Printf(
                TEXT("%s must be a JSON object of parameter name to value."), Field));
            return;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Map)->Values)
        {
            FParameterRequest Request;
            Request.Kind = Kind;
            Request.Name = FName(*Pair.Key);
            if (Pair.Key.TrimStartAndEnd().IsEmpty())
            {
                OutErrors.Add(FString::Printf(TEXT("%s contains an empty parameter name."), Field));
                continue;
            }
            if (!Pair.Value.IsValid())
            {
                OutErrors.Add(FString::Printf(TEXT("%s.%s has no value."), Field, *Pair.Key));
                continue;
            }

            switch (Kind)
            {
                case EParameterKind::Scalar:
                {
                    double Number = 0.0;
                    if (!Pair.Value->TryGetNumber(Number))
                    {
                        OutErrors.Add(FString::Printf(
                            TEXT("%s.%s must be a number."), Field, *Pair.Key));
                        continue;
                    }
                    Request.Scalar = static_cast<float>(Number);
                    break;
                }
                case EParameterKind::Vector:
                {
                    const TSharedPtr<FJsonObject>* Color = nullptr;
                    if (!Pair.Value->TryGetObject(Color) || Color == nullptr)
                    {
                        OutErrors.Add(FString::Printf(
                            TEXT("%s.%s must be an object with r, g, b and optional a."),
                            Field, *Pair.Key));
                        continue;
                    }
                    double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
                    (*Color)->TryGetNumberField(TEXT("r"), R);
                    (*Color)->TryGetNumberField(TEXT("g"), G);
                    (*Color)->TryGetNumberField(TEXT("b"), B);
                    (*Color)->TryGetNumberField(TEXT("a"), A);
                    Request.Vector = FLinearColor(
                        static_cast<float>(R), static_cast<float>(G),
                        static_cast<float>(B), static_cast<float>(A));
                    break;
                }
                case EParameterKind::Texture:
                {
                    FString Path;
                    if (!Pair.Value->TryGetString(Path) || Path.TrimStartAndEnd().IsEmpty())
                    {
                        OutErrors.Add(FString::Printf(
                            TEXT("%s.%s must be a texture asset path."), Field, *Pair.Key));
                        continue;
                    }
                    Request.TexturePath = Path.TrimStartAndEnd();
                    break;
                }
                case EParameterKind::StaticSwitch:
                {
                    bool bValue = false;
                    if (!Pair.Value->TryGetBool(bValue))
                    {
                        OutErrors.Add(FString::Printf(
                            TEXT("%s.%s must be true or false."), Field, *Pair.Key));
                        continue;
                    }
                    Request.bSwitch = bValue;
                    break;
                }
            }
            OutRequests.Add(Request);
        }
    }
}

bool UMCPPuerTSBridgeService::BuildMaterialInstanceJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Material instance spec must be a JSON object.");
        return false;
    }

    FString AssetPath;
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    AssetPath = AssetPath.TrimStartAndEnd();
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Material instances are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    const bool bClearUnlisted = Spec->HasTypedField<EJson::Boolean>(TEXT("clear_unlisted"))
        && Spec->GetBoolField(TEXT("clear_unlisted"));
    const bool bSave = !Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        || Spec->GetBoolField(TEXT("save"));

    // --- Resolve the existing asset, if any. ---
    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = AssetPath + TEXT(".") + AssetName;
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData ExistingData =
        AssetRegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath));
    UMaterialInstanceConstant* Instance = nullptr;
    if (ExistingData.IsValid())
    {
        Instance = Cast<UMaterialInstanceConstant>(ExistingData.GetAsset());
        if (Instance == nullptr)
        {
            OutError = FString::Printf(
                TEXT("The asset at '%s' is a %s, not a MaterialInstanceConstant. "
                     "Choose a different asset_path rather than overwriting it."),
                *ObjectPath, *ExistingData.AssetClass.ToString());
            return false;
        }
    }
    const bool bCreating = Instance == nullptr;

    // --- Resolve the parent. Everything else is validated against it. ---
    FString ParentPath;
    Spec->TryGetStringField(TEXT("parent_path"), ParentPath);
    ParentPath = ParentPath.TrimStartAndEnd();
    UMaterialInterface* Parent = nullptr;
    if (!ParentPath.IsEmpty())
    {
        if (!ParentPath.StartsWith(TEXT("/Game/")) && !ParentPath.StartsWith(TEXT("/Engine/")))
        {
            OutError = TEXT("parent_path is limited to /Game and /Engine.");
            return false;
        }
        Parent = LoadObject<UMaterialInterface>(nullptr, *ParentPath);
        if (Parent == nullptr)
        {
            OutError = FString::Printf(
                TEXT("No material or material instance could be loaded from parent_path '%s'."),
                *ParentPath);
            return false;
        }
        if (Parent == Instance)
        {
            OutError = TEXT("A material instance cannot be its own parent.");
            return false;
        }
    }
    else if (Instance != nullptr)
    {
        Parent = Instance->Parent;
    }
    if (Parent == nullptr)
    {
        OutError = bCreating
            ? TEXT("parent_path is required when the material instance does not exist yet.")
            : TEXT("This material instance has no parent and parent_path was not supplied, "
                   "so no parameter can be resolved.");
        return false;
    }

    // --- Parse every requested parameter, then validate the whole set. ---
    TArray<FParameterRequest> Requests;
    TArray<FString> Errors;
    ParseParameterMap(Spec, TEXT("scalars"),  EParameterKind::Scalar,       Requests, Errors);
    ParseParameterMap(Spec, TEXT("vectors"),  EParameterKind::Vector,       Requests, Errors);
    ParseParameterMap(Spec, TEXT("textures"), EParameterKind::Texture,      Requests, Errors);
    ParseParameterMap(Spec, TEXT("switches"), EParameterKind::StaticSwitch, Requests, Errors);

    for (FParameterRequest& Request : Requests)
    {
        if (!MCPBridgeMaterialSnapshot::HasParameter(Parent, Request.Kind, Request.Name))
        {
            // Name the matches, not just the miss.
            const TArray<FString> Closest = MCPBridgeMaterialSnapshot::ClosestParameterNames(
                Parent, Request.Kind, Request.Name);
            Errors.Add(FString::Printf(
                TEXT("'%s' does not publish a %s parameter named '%s'.%s"),
                *Parent->GetPathName(),
                MCPBridgeMaterialSnapshot::ParameterKindName(Request.Kind),
                *Request.Name.ToString(),
                Closest.Num() > 0
                    ? *FString::Printf(TEXT(" Closest %s parameters: %s."),
                        MCPBridgeMaterialSnapshot::ParameterKindName(Request.Kind),
                        *FString::Join(Closest, TEXT(", ")))
                    : TEXT(" The parent publishes no parameters of that kind.")));
            continue;
        }
        if (Request.Kind == EParameterKind::Texture)
        {
            Request.Texture = LoadObject<UTexture>(nullptr, *Request.TexturePath);
            if (Request.Texture == nullptr)
            {
                Errors.Add(FString::Printf(
                    TEXT("textures.%s: no texture could be loaded from '%s'."),
                    *Request.Name.ToString(), *Request.TexturePath));
            }
        }
    }

    // Duplicate names within one kind would make the last write win silently.
    {
        TSet<FString> Seen;
        for (const FParameterRequest& Request : Requests)
        {
            const FString Id = Request.Id();
            if (Seen.Contains(Id))
            {
                Errors.Add(FString::Printf(TEXT("'%s' is requested more than once."), *Id));
            }
            Seen.Add(Id);
        }
    }

    // --- The plan. Answered before any transaction is required, so a caller
    //     can check a request without the editor opening one. ---
    TArray<TSharedPtr<FJsonValue>> Plan;
    int32 PlannedWrites = 0;
    for (const FParameterRequest& Request : Requests)
    {
        const bool bAlreadyCorrect = MatchesRequest(Instance, Request);
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("id"), Request.Id());
        Entry->SetStringField(TEXT("name"), Request.Name.ToString());
        Entry->SetStringField(TEXT("type"),
            MCPBridgeMaterialSnapshot::ParameterKindName(Request.Kind));
        Entry->SetBoolField(TEXT("unchanged"), bAlreadyCorrect);
        SetRequestedValueField(Entry, Request);
        Plan.Add(MakeShared<FJsonValueObject>(Entry));
        if (!bAlreadyCorrect) { PlannedWrites++; }
    }
    MCPBridgeBlueprintMembers::SortByStringField(Plan, TEXT("id"));

    // Overrides this instance carries that the request does not mention. Only
    // meaningful with clear_unlisted; reported either way so a caller can see
    // what a desired-state run would remove before asking for it.
    TArray<TSharedPtr<FJsonValue>> Unlisted;
    if (Instance != nullptr)
    {
        TSet<FString> Requested;
        for (const FParameterRequest& Request : Requests) { Requested.Add(Request.Id()); }
        for (const EParameterKind Kind : MCPBridgeMaterialSnapshot::AllParameterKinds())
        {
            for (const FName& Name : MCPBridgeMaterialSnapshot::ParameterNames(Instance, Kind))
            {
                if (!MCPBridgeMaterialSnapshot::IsOverridden(Instance, Kind, Name)) { continue; }
                const FString Id = FString::Printf(TEXT("%s:%s"),
                    MCPBridgeMaterialSnapshot::ParameterKindName(Kind), *Name.ToString());
                if (Requested.Contains(Id)) { continue; }
                Unlisted.Add(MakeShared<FJsonValueString>(Id));
            }
        }
    }
    const bool bNeedsClear = bClearUnlisted && Unlisted.Num() > 0;

    auto BuildBaseResult = [&]() -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), AssetPath);
        Result->SetStringField(TEXT("parent_path"), Parent->GetPathName());
        Result->SetBoolField(TEXT("created"), false);
        Result->SetArrayField(TEXT("plan"), Plan);
        Result->SetArrayField(TEXT("unlisted_overrides"), Unlisted);
        Result->SetBoolField(TEXT("clear_unlisted"), bClearUnlisted);
        return Result;
    };

    if (Errors.Num() > 0)
    {
        OutError = FString::Join(Errors, TEXT(" "));
        return false;
    }

    if (bPlanOnly)
    {
        TSharedPtr<FJsonObject> Result = BuildBaseResult();
        Result->SetBoolField(TEXT("plan_only"), true);
        Result->SetBoolField(TEXT("would_create"), bCreating);
        Result->SetNumberField(TEXT("planned_writes"), bNeedsClear ? Requests.Num() : PlannedWrites);
        Result->SetBoolField(TEXT("unchanged"), !bCreating && !bNeedsClear && PlannedWrites == 0);
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        OutResultJson = SerializeJson(Result);
        return true;
    }

    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Material instance authoring requires an active transaction.");
        return false;
    }

    // --- One rollback boundary around the whole request. ---
    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    /** The effective value and override state of every parameter this request
        touches, as one comparable string. This is what decides whether a
        rollback worked: the structure hash deliberately excludes values, and
        the undo record's own word that it replayed is not evidence that the
        values came back. */
    auto ReadRequestedValues = [&]() -> FString
    {
        if (Instance == nullptr) { return FString(TEXT("<absent>")); }
        TArray<FString> Lines;
        for (const FParameterRequest& Request : Requests)
        {
            TSharedPtr<FJsonObject> Value = MakeShared<FJsonObject>();
            MCPBridgeMaterialSnapshot::SetParameterValueField(
                Value, Instance, Request.Kind, Request.Name);
            FString Text;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
            FJsonSerializer::Serialize(Value.ToSharedRef(), Writer);
            Lines.Add(Request.Id() + TEXT("=") + Text + (MCPBridgeMaterialSnapshot::IsOverridden(
                Instance, Request.Kind, Request.Name) ? TEXT("|override") : TEXT("|inherit")));
        }
        Lines.Sort();
        return FString::Join(Lines, TEXT("\n"));
    };
    const FString PreValues = ReadRequestedValues();

    bool bRollbackSucceeded = false;
    auto FailWithRollback = [&](const FString& Error) -> bool
    {
        // Cancel first, then roll back: the transaction's undo records point at
        // objects the rollback is about to destroy.
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }
        Rollback.Rollback();

        if (bCreating)
        {
            // The asset this command created must be gone, and the Asset
            // Registry is what decides that, not the pointer we still hold.
            bRollbackSucceeded =
                !AssetRegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath)).IsValid();
            Instance = nullptr;
        }
        else
        {
            bRollbackSucceeded = ReadRequestedValues() == PreValues;
        }

        OutError = bRollbackSucceeded
            ? Error
            : FString::Printf(
                TEXT("%s The rollback did NOT restore the material instance: re-reading the "
                     "parameters this request touched does not match what they were. "
                     "Treat '%s' as damaged."),
                *Error, *AssetPath);
        return false;
    };

    if (bCreating)
    {
        UPackage* Package = CreatePackage(*AssetPath);
        if (Package == nullptr)
        {
            return FailWithRollback(FString::Printf(
                TEXT("Unreal could not create the package '%s'."), *AssetPath));
        }
        UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
        Factory->InitialParent = Parent;
        Instance = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(
            UMaterialInstanceConstant::StaticClass(),
            Package,
            FName(*AssetName),
            RF_Public | RF_Standalone | RF_Transactional,
            nullptr,
            GWarn));
        if (Instance == nullptr)
        {
            return FailWithRollback(TEXT("Unreal could not create the material instance."));
        }
        Rollback.TrackCreated(Instance);
        AssetRegistryModule.Get().AssetCreated(Instance);
    }

    // Modify() before anything is written, and this is not decoration.
    // UMaterialEditingLibrary's setters call SetScalarParameterValueEditorOnly
    // and friends without ever calling Modify, so the parameter arrays would be
    // changed outside the undo record and a cancelled transaction would leave
    // the writes in place. One call here puts every array this command touches
    // into the transaction, which is what makes the failure path below atomic.
    Instance->Modify();

    const bool bParentChanged = Instance->Parent != Parent;
    if (bParentChanged)
    {
        UMaterialEditingLibrary::SetMaterialInstanceParent(Instance, Parent);
    }

    if (bNeedsClear)
    {
        // Desired state: drop the overrides the request did not mention. The
        // arrays are emptied directly rather than through
        // ClearAllMaterialInstanceParameters so that static switches, which
        // live in a separate set, are covered by the same decision. Every
        // listed parameter is then written unconditionally below.
        Instance->ScalarParameterValues.Empty();
        Instance->VectorParameterValues.Empty();
        Instance->TextureParameterValues.Empty();
    }

    // --- Apply. ---
    TArray<TSharedPtr<FJsonValue>> Applied;
    int32 WriteCount = 0;
    FStaticParameterSet StaticSet = Instance->GetStaticParameters();
    bool bStaticSetChanged = false;
    if (bNeedsClear && StaticSet.StaticSwitchParameters.Num() > 0)
    {
        StaticSet.StaticSwitchParameters.Empty();
        bStaticSetChanged = true;
    }

    for (const FParameterRequest& Request : Requests)
    {
        const bool bWrite = bNeedsClear || !MatchesRequest(Instance, Request);
        if (bWrite)
        {
            switch (Request.Kind)
            {
                case EParameterKind::Scalar:
                    // The library's setters return a hardcoded false in 4.27
                    // (MaterialEditingLibrary.cpp:966, :987, :1008 never assign
                    // bResult), so the return value is deliberately ignored.
                    // Whether the write landed is decided by the read-back.
                    UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
                        Instance, Request.Name, Request.Scalar);
                    break;
                case EParameterKind::Vector:
                    UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(
                        Instance, Request.Name, Request.Vector);
                    break;
                case EParameterKind::Texture:
                    UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(
                        Instance, Request.Name, Request.Texture);
                    break;
                case EParameterKind::StaticSwitch:
                {
                    // 4.27's UMaterialEditingLibrary has a getter for static
                    // switches and no setter, so the override set is edited
                    // directly and handed to UpdateStaticPermutation below.
                    FGuid ExpressionGuid;
                    MCPBridgeMaterialSnapshot::ParameterGuid(
                        Parent, EParameterKind::StaticSwitch, Request.Name, ExpressionGuid);
                    bool bFound = false;
                    for (FStaticSwitchParameter& Existing : StaticSet.StaticSwitchParameters)
                    {
                        if (Existing.ParameterInfo.Name != Request.Name) { continue; }
                        Existing.Value = Request.bSwitch;
                        Existing.bOverride = true;
                        Existing.ExpressionGUID = ExpressionGuid;
                        bFound = true;
                        break;
                    }
                    if (!bFound)
                    {
                        StaticSet.StaticSwitchParameters.Add(FStaticSwitchParameter(
                            FMaterialParameterInfo(Request.Name), Request.bSwitch, true, ExpressionGuid));
                    }
                    bStaticSetChanged = true;
                    break;
                }
            }
            WriteCount++;
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("id"), Request.Id());
        Entry->SetStringField(TEXT("name"), Request.Name.ToString());
        Entry->SetStringField(TEXT("type"),
            MCPBridgeMaterialSnapshot::ParameterKindName(Request.Kind));
        Entry->SetBoolField(TEXT("written"), bWrite);
        SetRequestedValueField(Entry, Request);
        Applied.Add(MakeShared<FJsonValueObject>(Entry));
    }
    MCPBridgeBlueprintMembers::SortByStringField(Applied, TEXT("id"));

    if (bStaticSetChanged)
    {
        // A static switch change recompiles the instance's shader permutation,
        // which is why it is applied once for the whole batch rather than per
        // parameter.
        Instance->UpdateStaticPermutation(StaticSet);
    }

    const bool bChanged = bCreating || bParentChanged || bNeedsClear || WriteCount > 0;
    if (bChanged)
    {
        UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
    }

    // --- Compile, and report the result rather than assume it. ---
    TArray<TSharedPtr<FJsonValue>> Warnings;
    TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> CompileErrors;
    bool bCompileFinished = false;
    FMaterialResource* Resource = Instance->GetMaterialResource(GMaxRHIFeatureLevel);
    // An instance with no static permutation of its own answers with its
    // PARENT's resource (MaterialInstance.cpp:1990), so "the compile failed" can
    // mean "the parent was already broken". Which one answered decides whether a
    // compile error is this request's fault, and the caller is told either way.
    const bool bOwnResource =
        Resource != nullptr && Resource != Parent->GetMaterialResource(GMaxRHIFeatureLevel);
    if (Resource != nullptr)
    {
        // Blocking on purpose: an asynchronous compile that has not finished
        // cannot tell a caller whether the material is valid, and answering
        // "requested" instead of "compiled" is the assumption this command
        // exists to remove.
        Resource->FinishCompilation();
        bCompileFinished = Resource->IsCompilationFinished();
        for (const FString& Error : Resource->GetCompileErrors())
        {
            CompileErrors.Add(MakeShared<FJsonValueString>(Error));
        }
    }
    Compile->SetBoolField(TEXT("resource_found"), Resource != nullptr);
    Compile->SetBoolField(TEXT("instance_own_resource"), bOwnResource);
    Compile->SetBoolField(TEXT("finished"), bCompileFinished);
    Compile->SetBoolField(TEXT("succeeded"), Resource != nullptr && bCompileFinished && CompileErrors.Num() == 0);
    Compile->SetArrayField(TEXT("errors"), CompileErrors);

    if (CompileErrors.Num() > 0)
    {
        const FString Joined = FString::Join(Resource->GetCompileErrors(), TEXT(" "));
        if (bOwnResource)
        {
            return FailWithRollback(FString::Printf(
                TEXT("The material instance did not compile cleanly: %s"), *Joined));
        }
        // Not ours to fail on, and not ours to hide either.
        Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
            TEXT("The parent material '%s' has compile errors, which this instance inherits "
                 "and did not cause: %s"),
            *Parent->GetPathName(), *Joined)));
    }

    // --- Independent read-back. The writes are believed only after the same
    //     reader material_inspect uses agrees with them. ---
    TArray<FString> Mismatches;
    for (const FParameterRequest& Request : Requests)
    {
        if (!MatchesRequest(Instance, Request))
        {
            Mismatches.Add(FString::Printf(
                TEXT("%s did not take the requested value."), *Request.Id()));
        }
    }
    if (Mismatches.Num() > 0)
    {
        return FailWithRollback(FString::Printf(
            TEXT("The material instance was written but the read-back disagrees: %s "
                 "Nothing was saved and the transaction was cancelled."),
            *FString::Join(Mismatches, TEXT(" "))));
    }

    // --- Save, only now. ---
    bool bSaved = false;
    if (bSave && bChanged)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(AssetPath, SaveError);
        if (!bSaved)
        {
            return FailWithRollback(FString::Printf(
                TEXT("The material instance was built and verified but could not be saved: %s"),
                *SaveError));
        }
    }

    Rollback.Commit();

    TSharedPtr<FJsonObject> Result = BuildBaseResult();
    Result->SetBoolField(TEXT("created"), bCreating);
    Result->SetBoolField(TEXT("parent_changed"), bParentChanged);
    Result->SetStringField(TEXT("object_path"), Instance->GetPathName());
    Result->SetArrayField(TEXT("parameters_applied"), Applied);
    Result->SetNumberField(TEXT("write_count"), WriteCount);
    Result->SetBoolField(TEXT("unchanged"), !bChanged);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetObjectField(TEXT("compile"), Compile);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    Result->SetStringField(TEXT("structure_hash_sha1"),
        MCPBridgeMaterialSnapshot::StructureHash(Instance));
    Result->SetObjectField(TEXT("rollback"),
        Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
    Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
    OutResultJson = SerializeJson(Result);
    return true;
}

// This note used to say there could be no material_build, because
// UMaterialEditingLibrary::CreateMaterialExpressionEx
// (MaterialEditingLibrary.cpp:469) adds to UMaterial::Expressions without
// calling Material->Modify(), and ConnectMaterialExpressions (:631) writes
// through FExpressionInput::Connect the same way.
//
// Both of those readings are correct and the conclusion drawn from them was
// not: the engine's own material editor has the same problem and owns Modify()
// at the call site (FMaterialEditor::CreateNewMaterialExpression,
// MaterialEditor.cpp:4344). One checked Modify() on the UMaterial, plus full
// graph replacement so no pre-existing expression is ever rewritten, is enough
// to make a multi-node build failure-atomic. See
// MCPPuerTSBridgeMaterialBuild.cpp and finding 0s in
// docs/CAPABILITY_FINDINGS.md.
//
// What remains true is the split between the two commands. A material instance
// is a set of named parameter overrides on a parent, which is desired-state by
// construction and per-parameter recoverable; a master material graph is a
// whole-asset replacement. They are different operations and stay in different
// files.
