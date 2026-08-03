// Copyright 2026 RareBird Games. All Rights Reserved.
//
// The AI and gameplay half of the native command surface: blackboards as
// assets in their own right, Environment Query inspection, navigation
// configuration and navigation queries, and AIController perception.
//
// Every API used here was confirmed against the installed UE4.27 source at
// D:/UE/UE_4.27 before it was written. Where a UE5 system would be the obvious
// answer (MassAI, SmartObjects, StateTree) it is not used and not reachable
// from 4.27 at all; the AI stack here is BehaviorTree plus AIController, as
// AGENTS.md requires.
//
// What is NOT here, deliberately: an Environment Query BUILDER. See
// InspectEnvQueryJson for the reason, which is a property of
// UEnvironmentQueryGraph::UpdateAsset rather than an opinion.

#include "MCPPuerTSBridgeService.h"

#include "AIController.h"
#include "AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BlueprintGraphBuilderLibrary.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MCPBridgeAssetRollback.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavModifierVolume.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense.h"
#include "Perception/AISenseConfig.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Prediction.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Team.h"
#include "Perception/AISenseConfig_Touch.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
    // ---------------------------------------------------------------------
    // Small shared helpers.
    // ---------------------------------------------------------------------

    TSharedPtr<FJsonObject> VectorToJson(const FVector& Value)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetNumberField(TEXT("x"), Value.X);
        Out->SetNumberField(TEXT("y"), Value.Y);
        Out->SetNumberField(TEXT("z"), Value.Z);
        return Out;
    }

    /** Read {x,y,z} from a JSON object. Absent components are zero, because a
        caller naming only the axes it cares about is a request, not an error. */
    FVector JsonToVector(const TSharedPtr<FJsonObject>& Object)
    {
        if (!Object.IsValid()) { return FVector::ZeroVector; }
        double X = 0.0, Y = 0.0, Z = 0.0;
        Object->TryGetNumberField(TEXT("x"), X);
        Object->TryGetNumberField(TEXT("y"), Y);
        Object->TryGetNumberField(TEXT("z"), Z);
        return FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
    }

    TArray<TSharedPtr<FJsonValue>> AIStringsToJson(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Values.Num());
        for (const FString& Value : Values) { Out.Add(MakeShared<FJsonValueString>(Value)); }
        return Out;
    }

    FString HashOf(const FString& Lines)
    {
        uint8 Digest[FSHA1::DigestSize];
        const FTCHARToUTF8 Utf8(*Lines);
        FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Digest);
        return BytesToHex(Digest, FSHA1::DigestSize);
    }

    /** Every editor-visible property of an object, as JSON, with anything
        reflection could not express named rather than silently dropped. The
        same shape MCPPuerTSBridgeBehaviorTree.cpp reports for a BT node, so an
        EQS generator, a sense config and a nav data actor all read alike. */
    TSharedPtr<FJsonObject> DescribeEditProperties(
        const UObject* Object,
        const FString& OwnerId,
        TArray<TSharedPtr<FJsonValue>>& OutUnsupported)
    {
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        if (Object == nullptr) { return Properties; }
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Edit)) { continue; }
            const TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(
                Property, Property->ContainerPtrToValuePtr<void>(Object), 0, 0);
            if (Value.IsValid())
            {
                Properties->SetField(Property->GetName(), Value);
            }
            else
            {
                OutUnsupported.Add(MakeShared<FJsonValueString>(
                    FString::Printf(TEXT("%s.%s"), *OwnerId, *Property->GetName())));
            }
        }
        return Properties;
    }

    /** Resolve a package path or an object path to the object path form the
        Asset Registry indexes by. Returns false with a reason on a path that
        is not a valid long package name. */
    bool ResolveAssetPaths(
        const FString& AssetPath,
        FString& OutObjectPath,
        FString& OutPackagePath,
        FString& OutError)
    {
        const FString Trimmed = AssetPath.TrimStartAndEnd();
        if (Trimmed.IsEmpty())
        {
            OutError = TEXT("asset_path is required.");
            return false;
        }
        if (!Trimmed.StartsWith(TEXT("/Game/")) && !Trimmed.StartsWith(TEXT("/Engine/")))
        {
            OutError = TEXT("Inspection is limited to /Game and /Engine.");
            return false;
        }
        if (Trimmed.Contains(TEXT(".")))
        {
            OutObjectPath = Trimmed;
            OutPackagePath = FPackageName::ObjectPathToPackageName(Trimmed);
            return true;
        }
        if (!FPackageName::IsValidLongPackageName(Trimmed))
        {
            OutError = FString::Printf(TEXT("'%s' is not a valid package path."), *Trimmed);
            return false;
        }
        OutPackagePath = Trimmed;
        OutObjectPath = Trimmed + TEXT(".") + FPackageName::GetLongPackageAssetName(Trimmed);
        return true;
    }

    UObject* LoadAssetForRead(const FString& ObjectPath, FString& OutError)
    {
        FAssetRegistryModule& AssetRegistry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        const FAssetData AssetData = AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath));
        if (!AssetData.IsValid())
        {
            OutError = FString::Printf(TEXT("No asset was found at '%s'."), *ObjectPath);
            return nullptr;
        }
        return AssetData.GetAsset();
    }

    // ---------------------------------------------------------------------
    // Blackboard key types.
    //
    // UMCPBridgeAILibrary::AddBlackboardKeys has its own copy of this mapping
    // and is left exactly as it was: it is the live_verified path
    // behavior_tree_build depends on, and editing it to share code here would
    // put a proven command at risk for a cosmetic gain. This copy adds what
    // reconciliation needs and add-only key creation does not: reading a key's
    // type back out, and comparing two key types for equality.
    // ---------------------------------------------------------------------

    /** Whether a type word is known and, for Object and Class, whether the
        base class resolves. Separate from MakeKeyType because validation has
        to run before any asset exists, and MakeKeyType needs an outer. */
    bool ValidateKeyType(const FString& TypeName, const FString& BaseClassPath, FString& OutError)
    {
        static const TCHAR* Known[] = {
            TEXT("Bool"), TEXT("Int"), TEXT("Float"), TEXT("String"), TEXT("Name"),
            TEXT("Vector"), TEXT("Rotator"), TEXT("Object"), TEXT("Class"),
        };
        bool bKnown = false;
        for (const TCHAR* Word : Known)
        {
            if (TypeName == Word) { bKnown = true; break; }
        }
        if (!bKnown)
        {
            OutError = FString::Printf(
                TEXT("Unknown blackboard key type '%s'. Supported: Bool, Int, Float, String, Name, ")
                TEXT("Vector, Rotator, Object, Class."),
                *TypeName);
            return false;
        }
        if (BaseClassPath.IsEmpty()) { return true; }
        if (TypeName != TEXT("Object") && TypeName != TEXT("Class"))
        {
            OutError = FString::Printf(
                TEXT("base_class only applies to an Object or Class key, not to a %s key."), *TypeName);
            return false;
        }
        if (LoadClass<UObject>(nullptr, *BaseClassPath) == nullptr)
        {
            OutError = FString::Printf(
                TEXT("%s key base_class not found: %s"), *TypeName, *BaseClassPath);
            return false;
        }
        return true;
    }

    UBlackboardKeyType* MakeKeyType(
        UBlackboardData* Owner,
        const FString& TypeName,
        const FString& BaseClassPath,
        FString& OutError)
    {
        if (TypeName == TEXT("Bool"))    { return NewObject<UBlackboardKeyType_Bool>(Owner); }
        if (TypeName == TEXT("Int"))     { return NewObject<UBlackboardKeyType_Int>(Owner); }
        if (TypeName == TEXT("Float"))   { return NewObject<UBlackboardKeyType_Float>(Owner); }
        if (TypeName == TEXT("String"))  { return NewObject<UBlackboardKeyType_String>(Owner); }
        if (TypeName == TEXT("Name"))    { return NewObject<UBlackboardKeyType_Name>(Owner); }
        if (TypeName == TEXT("Vector"))  { return NewObject<UBlackboardKeyType_Vector>(Owner); }
        if (TypeName == TEXT("Rotator")) { return NewObject<UBlackboardKeyType_Rotator>(Owner); }
        if (TypeName == TEXT("Object") || TypeName == TEXT("Class"))
        {
            UClass* Base = nullptr;
            if (!BaseClassPath.IsEmpty())
            {
                Base = LoadClass<UObject>(nullptr, *BaseClassPath);
                if (Base == nullptr)
                {
                    OutError = FString::Printf(
                        TEXT("%s key base_class not found: %s"), *TypeName, *BaseClassPath);
                    return nullptr;
                }
            }
            if (TypeName == TEXT("Object"))
            {
                UBlackboardKeyType_Object* Key = NewObject<UBlackboardKeyType_Object>(Owner);
                if (Base != nullptr) { Key->BaseClass = Base; }
                return Key;
            }
            UBlackboardKeyType_Class* Key = NewObject<UBlackboardKeyType_Class>(Owner);
            if (Base != nullptr) { Key->BaseClass = Base; }
            return Key;
        }
        OutError = FString::Printf(
            TEXT("Unknown blackboard key type '%s'. Supported: Bool, Int, Float, String, Name, ")
            TEXT("Vector, Rotator, Object, Class."),
            *TypeName);
        return nullptr;
    }

    /** The short type word for a key type instance: the inverse of MakeKeyType,
        kept beside it so the two cannot drift. */
    FString KeyTypeWord(const UBlackboardKeyType* KeyType)
    {
        if (KeyType == nullptr) { return TEXT("None"); }
        const FString ClassName = KeyType->GetClass()->GetName();
        FString Word;
        if (ClassName.Split(TEXT("BlackboardKeyType_"), nullptr, &Word)) { return Word; }
        return ClassName;
    }

    /** The base class an Object or Class key requires, as an object path, or
        empty for a key type that has no base. */
    FString KeyBaseClassPath(const UBlackboardKeyType* KeyType)
    {
        if (const UBlackboardKeyType_Object* AsObject = Cast<UBlackboardKeyType_Object>(KeyType))
        {
            return AsObject->BaseClass != nullptr ? AsObject->BaseClass->GetPathName() : FString();
        }
        if (const UBlackboardKeyType_Class* AsClass = Cast<UBlackboardKeyType_Class>(KeyType))
        {
            return AsClass->BaseClass != nullptr ? AsClass->BaseClass->GetPathName() : FString();
        }
        return FString();
    }

    /** One desired key, validated before any asset is touched. */
    struct FDesiredKey
    {
        FName Name;
        FString Type;
        FString BaseClass;
        bool bInstanceSynced = false;
        bool bHasInstanceSynced = false;
        FString Description;
        bool bHasDescription = false;
        FString Category;
        bool bHasCategory = false;
    };

    const FBlackboardEntry* FindEntry(const UBlackboardData* Blackboard, const FName Name)
    {
        if (Blackboard == nullptr) { return nullptr; }
        for (const FBlackboardEntry& Entry : Blackboard->Keys)
        {
            if (Entry.EntryName == Name) { return &Entry; }
        }
        return nullptr;
    }

    /** Whether a name is declared anywhere up the parent chain. An inherited
        key is not this asset's to add, change or remove: UBlackboardData::IsValid
        treats a name that collides with the parent chain as a broken asset. */
    bool ParentChainHasKey(const UBlackboardData* Blackboard, const FName Name)
    {
        for (const UBlackboardData* Walk = Blackboard != nullptr ? Blackboard->Parent : nullptr;
             Walk != nullptr;
             Walk = Walk->Parent)
        {
            if (FindEntry(Walk, Name) != nullptr) { return true; }
        }
        return false;
    }

    TSharedPtr<FJsonObject> DescribeKey(const FBlackboardEntry& Entry, bool bInherited)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("name"), Entry.EntryName.ToString());
        Out->SetStringField(TEXT("type"), KeyTypeWord(Entry.KeyType));
        Out->SetStringField(TEXT("type_class_path"),
            Entry.KeyType != nullptr ? Entry.KeyType->GetClass()->GetPathName() : FString());
        Out->SetStringField(TEXT("base_class"), KeyBaseClassPath(Entry.KeyType));
        Out->SetBoolField(TEXT("instance_synced"), Entry.bInstanceSynced != 0);
        Out->SetBoolField(TEXT("inherited"), bInherited);
#if WITH_EDITORONLY_DATA
        Out->SetStringField(TEXT("description"), Entry.EntryDescription);
        Out->SetStringField(TEXT("category"), Entry.EntryCategory.ToString());
#else
        Out->SetStringField(TEXT("description"), FString());
        Out->SetStringField(TEXT("category"), FString());
#endif
        return Out;
    }

    // ---------------------------------------------------------------------
    // Perception sense vocabulary.
    //
    // The seven UAISenseConfig subclasses that exist in UE4.27. Blueprint is
    // deliberately absent: UAISenseConfig_Blueprint needs a user sense class
    // this command has no way to validate, so it is a rejection by name rather
    // than a config that fails at runtime.
    // ---------------------------------------------------------------------

    UClass* SenseConfigClass(const FString& Sense)
    {
        if (Sense == TEXT("Sight"))      { return UAISenseConfig_Sight::StaticClass(); }
        if (Sense == TEXT("Hearing"))    { return UAISenseConfig_Hearing::StaticClass(); }
        if (Sense == TEXT("Damage"))     { return UAISenseConfig_Damage::StaticClass(); }
        if (Sense == TEXT("Touch"))      { return UAISenseConfig_Touch::StaticClass(); }
        if (Sense == TEXT("Team"))       { return UAISenseConfig_Team::StaticClass(); }
        if (Sense == TEXT("Prediction")) { return UAISenseConfig_Prediction::StaticClass(); }
        return nullptr;
    }

    /** The inverse: the sense word for a config instance, so an inspection
        reads back in the vocabulary a build spec writes. */
    FString SenseWord(const UAISenseConfig* Config)
    {
        if (Config == nullptr) { return TEXT("None"); }
        const FString ClassName = Config->GetClass()->GetName();
        FString Word;
        if (ClassName.Split(TEXT("AISenseConfig_"), nullptr, &Word)) { return Word; }
        return ClassName;
    }

    /** The AISense implementation class a sense word names, which is what
        DominantSense holds. Read off the config CDO rather than hard-coded, so
        it cannot disagree with the config class. */
    UClass* SenseImplementationClass(const FString& Sense)
    {
        UClass* ConfigClass = SenseConfigClass(Sense);
        if (ConfigClass == nullptr) { return nullptr; }
        const UAISenseConfig* Defaults = ConfigClass->GetDefaultObject<UAISenseConfig>();
        return Defaults != nullptr ? Defaults->GetSenseImplementation().Get() : nullptr;
    }

    /** Whether an existing sense config already holds every property the spec
        names. Only the named properties are compared, because the spec is a
        partial description of a config that also carries class defaults.

        This is what makes ai_perception_build convergent rather than merely
        idempotent: without it a rerun of a satisfied spec rebuilds the config
        objects, recompiles the Blueprint and rewrites the .uasset, which is
        real cost in a loop that reruns specs to check them.

        A float the spec wrote as a decimal that no float represents exactly
        compares unequal and is reported as a change. That direction is the safe
        one: it does work that was not needed, never skips work that was. */
    bool SenseMatchesSpec(const UAISenseConfig* Config, const TSharedPtr<FJsonObject>& Properties)
    {
        if (Config == nullptr || !Properties.IsValid()) { return false; }
        for (const auto& Pair : Properties->Values)
        {
            FProperty* Property = FindFProperty<FProperty>(Config->GetClass(), *Pair.Key);
            if (Property == nullptr) { return false; }
            const TSharedPtr<FJsonValue> Current = FJsonObjectConverter::UPropertyToJsonValue(
                Property, Property->ContainerPtrToValuePtr<void>(Config), 0, 0);
            if (!Current.IsValid() || !Pair.Value.IsValid()
                || !FJsonValue::CompareEqual(*Current, *Pair.Value))
            {
                return false;
            }
        }
        return true;
    }

    USCS_Node* AIFindComponentNode(const UBlueprint* Blueprint, const FString& ComponentName)
    {
        if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr)
        {
            return nullptr;
        }
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node != nullptr && Node->GetVariableName().ToString() == ComponentName)
            {
                return Node;
            }
        }
        return nullptr;
    }

    /** The SensesConfig array on a perception component template.
        UAIPerceptionComponent declares SensesConfig and DominantSense
        protected, so the write path goes through reflection; reflection ignores
        C++ access specifiers and the properties are UPROPERTYs, which is what
        makes this reachable at all. Reads use the public
        GetSensesConfigIterator instead. */
    FArrayProperty* SensesConfigProperty()
    {
        return FindFProperty<FArrayProperty>(
            UAIPerceptionComponent::StaticClass(), TEXT("SensesConfig"));
    }
}

// ---------------------------------------------------------------------------
// blackboard_build
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::BuildBlackboardJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Blackboard spec must be a JSON object.");
        return false;
    }

    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));

    // A plan changes nothing, so it needs no transaction; a build does, and a
    // build without one has no rollback. Checked here rather than at the top so
    // the read-only path stays usable while a caller is still deciding.
    if (!bPlanOnly && ActiveTransaction == nullptr)
    {
        OutError = TEXT("Blackboard build requires an active transaction.");
        return false;
    }

    // --- Validation, before any asset exists. ---

    FString AssetPath;
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Blackboard assets are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    FString ParentPath;
    Spec->TryGetStringField(TEXT("parent_path"), ParentPath);
    UBlackboardData* ParentAsset = nullptr;
    if (!ParentPath.IsEmpty())
    {
        FString ParentObjectPath, ParentPackagePath, PathError;
        if (!ResolveAssetPaths(ParentPath, ParentObjectPath, ParentPackagePath, PathError))
        {
            OutError = FString::Printf(TEXT("parent_path: %s"), *PathError);
            return false;
        }
        FString LoadError;
        ParentAsset = Cast<UBlackboardData>(LoadAssetForRead(ParentObjectPath, LoadError));
        if (ParentAsset == nullptr)
        {
            OutError = LoadError.IsEmpty()
                ? FString::Printf(TEXT("The asset at '%s' is not a Blackboard."), *ParentObjectPath)
                : LoadError;
            return false;
        }
    }

    TArray<FDesiredKey> Desired;
    TSet<FName> DesiredNames;
    const TArray<TSharedPtr<FJsonValue>>* KeyValues = nullptr;
    if (Spec->TryGetArrayField(TEXT("keys"), KeyValues))
    {
        for (const TSharedPtr<FJsonValue>& Value : *KeyValues)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Entry))
            {
                OutError = TEXT("Every entry of keys must be an object with name and type.");
                return false;
            }
            FDesiredKey Key;
            FString Name;
            (*Entry)->TryGetStringField(TEXT("name"), Name);
            (*Entry)->TryGetStringField(TEXT("type"), Key.Type);
            (*Entry)->TryGetStringField(TEXT("base_class"), Key.BaseClass);
            if (Name.IsEmpty() || Key.Type.IsEmpty())
            {
                OutError = TEXT("Every entry of keys needs a non-empty name and type.");
                return false;
            }
            Key.Name = FName(*Name);
            if (DesiredNames.Contains(Key.Name))
            {
                OutError = FString::Printf(
                    TEXT("keys declares '%s' twice. A blackboard key name is its identity, so "
                         "the spec cannot say two things about one key."),
                    *Name);
                return false;
            }
            if ((*Entry)->HasTypedField<EJson::Boolean>(TEXT("instance_synced")))
            {
                Key.bInstanceSynced = (*Entry)->GetBoolField(TEXT("instance_synced"));
                Key.bHasInstanceSynced = true;
            }
            if ((*Entry)->HasTypedField<EJson::String>(TEXT("description")))
            {
                Key.Description = (*Entry)->GetStringField(TEXT("description"));
                Key.bHasDescription = true;
            }
            if ((*Entry)->HasTypedField<EJson::String>(TEXT("category")))
            {
                Key.Category = (*Entry)->GetStringField(TEXT("category"));
                Key.bHasCategory = true;
            }
            // Reject an unknown type and an unresolvable base class here, where
            // nothing has been created yet.
            FString TypeError;
            if (!ValidateKeyType(Key.Type, Key.BaseClass, TypeError))
            {
                OutError = FString::Printf(TEXT("Key '%s': %s"), *Name, *TypeError);
                return false;
            }
            DesiredNames.Add(Key.Name);
            Desired.Add(Key);
        }
    }

    const bool bRemoveUnlisted = Spec->HasTypedField<EJson::Boolean>(TEXT("remove_unlisted"))
        && Spec->GetBoolField(TEXT("remove_unlisted"));
    const bool bSave = !Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        || Spec->GetBoolField(TEXT("save"));

    const FString ObjectPath =
        AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);

    // --- Classification. Runs against whatever exists today, including
    //     nothing, and is the whole answer when plan_only is set. ---

    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData ExistingData = AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath));
    UBlackboardData* Existing = nullptr;
    if (ExistingData.IsValid())
    {
        Existing = Cast<UBlackboardData>(ExistingData.GetAsset());
        if (Existing == nullptr)
        {
            OutError = FString::Printf(
                TEXT("The asset at '%s' is a %s, not a Blackboard."),
                *ObjectPath, *ExistingData.AssetClass.ToString());
            return false;
        }
    }

    TArray<FString> ToAdd;
    TArray<FString> ToUpdate;
    TArray<FString> ToRemove;
    TArray<FString> Unchanged;
    TArray<FString> ProtectedKeys;
    TArray<TSharedPtr<FJsonValue>> Errors;

    // The parent used for the inheritance test is the one this build will
    // leave behind, not the one that is there now: a spec that changes the
    // parent changes which names are inherited.
    UBlackboardData* const EffectiveParent =
        !ParentPath.IsEmpty() ? ParentAsset : (Existing != nullptr ? Existing->Parent : nullptr);
    for (const FDesiredKey& Key : Desired)
    {
        bool bInheritedName = false;
        for (const UBlackboardData* Walk = EffectiveParent; Walk != nullptr; Walk = Walk->Parent)
        {
            if (FindEntry(Walk, Key.Name) != nullptr) { bInheritedName = true; break; }
        }
        if (bInheritedName)
        {
            // Declaring a name the parent already owns is what
            // UBlackboardData::IsValid calls a broken asset, so it is refused
            // by name rather than written and then found broken later.
            ProtectedKeys.Add(Key.Name.ToString());
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("Key '%s' is inherited from the parent blackboard and cannot be redeclared "
                     "here. Change it on the parent, or drop it from this spec."),
                *Key.Name.ToString())));
            continue;
        }
        const FBlackboardEntry* Current = FindEntry(Existing, Key.Name);
        if (Current == nullptr)
        {
            ToAdd.Add(Key.Name.ToString());
            continue;
        }
        const FString CurrentType = KeyTypeWord(Current->KeyType);
        if (CurrentType != Key.Type)
        {
            // There is no retype primitive, and silently replacing the key
            // would drop every Behavior Tree selector bound to it without
            // saying so.
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("Key '%s' already exists as %s and the spec asks for %s. Remove it "
                     "explicitly with remove_unlisted, then declare it again."),
                *Key.Name.ToString(), *CurrentType, *Key.Type)));
            continue;
        }
        bool bDiffers = false;
        if (!Key.BaseClass.IsEmpty() && KeyBaseClassPath(Current->KeyType) != Key.BaseClass)
        {
            bDiffers = true;
        }
        if (Key.bHasInstanceSynced && (Current->bInstanceSynced != 0) != Key.bInstanceSynced)
        {
            bDiffers = true;
        }
#if WITH_EDITORONLY_DATA
        if (Key.bHasDescription && Current->EntryDescription != Key.Description) { bDiffers = true; }
        if (Key.bHasCategory && Current->EntryCategory != FName(*Key.Category)) { bDiffers = true; }
#endif
        (bDiffers ? ToUpdate : Unchanged).Add(Key.Name.ToString());
    }

    if (bRemoveUnlisted && Existing != nullptr)
    {
        for (const FBlackboardEntry& Entry : Existing->Keys)
        {
            if (!DesiredNames.Contains(Entry.EntryName))
            {
                ToRemove.Add(Entry.EntryName.ToString());
            }
        }
    }

    const bool bParentChanges = !ParentPath.IsEmpty()
        && (Existing == nullptr || Existing->Parent != ParentAsset);
    const int32 ExpectedChanges =
        ToAdd.Num() + ToUpdate.Num() + ToRemove.Num() + (bParentChanges ? 1 : 0);

    if (bPlanOnly)
    {
        TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
        Plan->SetBoolField(TEXT("plan_only"), true);
        Plan->SetStringField(TEXT("asset_path"), AssetPath);
        Plan->SetBoolField(TEXT("asset_exists"), Existing != nullptr);
        Plan->SetArrayField(TEXT("keys_to_add"), AIStringsToJson(ToAdd));
        Plan->SetArrayField(TEXT("keys_to_update"), AIStringsToJson(ToUpdate));
        Plan->SetArrayField(TEXT("keys_to_remove"), AIStringsToJson(ToRemove));
        Plan->SetArrayField(TEXT("unchanged_keys"), AIStringsToJson(Unchanged));
        Plan->SetArrayField(TEXT("protected_keys"), AIStringsToJson(ProtectedKeys));
        Plan->SetBoolField(TEXT("parent_changes"), bParentChanges);
        Plan->SetNumberField(TEXT("expected_change_count"), ExpectedChanges);
        Plan->SetBoolField(TEXT("converged"), ExpectedChanges == 0 && Errors.Num() == 0);
        Plan->SetArrayField(TEXT("errors"), Errors);
        Plan->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
        OutResultJson = SerializeJson(Plan);
        return true;
    }

    if (Errors.Num() > 0)
    {
        // Nothing has been touched yet, so this is a refusal rather than a
        // rollback: no asset was created and no package was dirtied.
        TSharedPtr<FJsonObject> Refused = MakeShared<FJsonObject>();
        Refused->SetStringField(TEXT("asset_path"), AssetPath);
        Refused->SetBoolField(TEXT("created"), false);
        Refused->SetBoolField(TEXT("applied"), false);
        Refused->SetNumberField(TEXT("applied_change_count"), 0);
        Refused->SetArrayField(TEXT("protected_keys"), AIStringsToJson(ProtectedKeys));
        Refused->SetArrayField(TEXT("errors"), Errors);
        Refused->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
        OutResultJson = SerializeJson(Refused);
        return true;
    }

    TArray<TSharedPtr<FJsonValue>> Warnings;

    if (ExpectedChanges == 0 && Existing != nullptr)
    {
        // Already converged. Returning before the mutation section is what
        // makes a rerun free: no Modify, no dirty package, no save.
        TSharedPtr<FJsonObject> Converged = MakeShared<FJsonObject>();
        Converged->SetStringField(TEXT("asset_path"), AssetPath);
        Converged->SetStringField(TEXT("object_path"), Existing->GetPathName());
        Converged->SetBoolField(TEXT("created"), false);
        Converged->SetBoolField(TEXT("applied"), false);
        Converged->SetBoolField(TEXT("converged"), true);
        Converged->SetNumberField(TEXT("applied_change_count"), 0);
        Converged->SetBoolField(TEXT("saved"), false);
        Converged->SetBoolField(TEXT("verified"), true);
        Converged->SetArrayField(TEXT("unchanged_keys"), AIStringsToJson(Unchanged));
        Converged->SetArrayField(TEXT("errors"), Errors);
        Converged->SetArrayField(TEXT("warnings"), Warnings);
        OutResultJson = SerializeJson(Converged);
        return true;
    }

    // --- Mutation. Everything below is inside the rollback boundary. ---

    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    UBlackboardData* Blackboard = Existing;
    bool bCreated = false;
    if (Blackboard == nullptr)
    {
        UPackage* Package = CreatePackage(*AssetPath);
        if (Package == nullptr)
        {
            OutError = FString::Printf(TEXT("Unreal could not create the package '%s'."), *AssetPath);
            return false;
        }
        Blackboard = NewObject<UBlackboardData>(
            Package,
            FName(*FPackageName::GetLongPackageAssetName(AssetPath)),
            RF_Public | RF_Standalone | RF_Transactional);
        if (Blackboard == nullptr)
        {
            OutError = FString::Printf(TEXT("Unreal could not create the asset '%s'."), *ObjectPath);
            return false;
        }
        AssetRegistry.Get().AssetCreated(Blackboard);
        Rollback.TrackCreated(Blackboard);
        bCreated = true;
    }
    Blackboard->Modify();

    auto FailRolledBack = [&](const FString& Reason) -> bool
    {
        Errors.Add(MakeShared<FJsonValueString>(Reason));
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }
        Rollback.Rollback();
        TSharedPtr<FJsonObject> Failed = MakeShared<FJsonObject>();
        Failed->SetStringField(TEXT("asset_path"), AssetPath);
        Failed->SetBoolField(TEXT("created"), false);
        Failed->SetBoolField(TEXT("applied"), false);
        Failed->SetNumberField(TEXT("applied_change_count"), 0);
        Failed->SetBoolField(TEXT("saved"), false);
        Failed->SetBoolField(TEXT("verified"), false);
        Failed->SetArrayField(TEXT("errors"), Errors);
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("The build failed and was rolled back.")));
        Failed->SetArrayField(TEXT("warnings"), Warnings);
        Failed->SetObjectField(TEXT("cleanup"),
            Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
        OutResultJson = SerializeJson(Failed);
        return true;
    };

    if (bParentChanges)
    {
        Blackboard->Parent = ParentAsset;
    }

    // Removals first: a spec that removes a key and adds one of the same name
    // with a different type is then a legal sequence rather than a collision.
    for (const FString& Name : ToRemove)
    {
        Blackboard->Keys.RemoveAll([&Name](const FBlackboardEntry& Entry)
        {
            return Entry.EntryName == FName(*Name);
        });
    }

    for (const FDesiredKey& Key : Desired)
    {
        FBlackboardEntry* Current = nullptr;
        for (FBlackboardEntry& Entry : Blackboard->Keys)
        {
            if (Entry.EntryName == Key.Name) { Current = &Entry; break; }
        }
        if (Current == nullptr)
        {
            FString TypeError;
            UBlackboardKeyType* KeyType = MakeKeyType(Blackboard, Key.Type, Key.BaseClass, TypeError);
            if (KeyType == nullptr)
            {
                return FailRolledBack(FString::Printf(TEXT("Key '%s': %s"), *Key.Name.ToString(), *TypeError));
            }
            FBlackboardEntry Entry;
            Entry.EntryName = Key.Name;
            Entry.KeyType = KeyType;
            Entry.bInstanceSynced = Key.bInstanceSynced ? 1 : 0;
#if WITH_EDITORONLY_DATA
            Entry.EntryDescription = Key.Description;
            Entry.EntryCategory = FName(*Key.Category);
#endif
            Blackboard->Keys.Add(Entry);
            continue;
        }
        // An existing key of the same type: change what the spec named and
        // leave the rest, so a partial spec is a partial update rather than a
        // silent reset of the fields it did not mention.
        if (!Key.BaseClass.IsEmpty() && KeyBaseClassPath(Current->KeyType) != Key.BaseClass)
        {
            FString TypeError;
            UBlackboardKeyType* KeyType = MakeKeyType(Blackboard, Key.Type, Key.BaseClass, TypeError);
            if (KeyType == nullptr)
            {
                return FailRolledBack(FString::Printf(TEXT("Key '%s': %s"), *Key.Name.ToString(), *TypeError));
            }
            Current->KeyType = KeyType;
        }
        if (Key.bHasInstanceSynced) { Current->bInstanceSynced = Key.bInstanceSynced ? 1 : 0; }
#if WITH_EDITORONLY_DATA
        if (Key.bHasDescription) { Current->EntryDescription = Key.Description; }
        if (Key.bHasCategory)    { Current->EntryCategory = FName(*Key.Category); }
#endif
    }

    // Derived blackboards cache their parent's keys; without this a child
    // asset keeps showing the key list from before this build.
    Blackboard->PropagateKeyChangesToDerivedBlackboardAssets();

    if (!Blackboard->IsValid())
    {
        return FailRolledBack(
            TEXT("The blackboard is not valid after the build: a key name conflicts with the "
                 "parent chain. Nothing was kept."));
    }

    // --- Verification. The asset is read again rather than the writes being
    //     trusted, which is the only thing that separates a change from a
    //     report of one. ---

    TArray<FString> Missing;
    for (const FDesiredKey& Key : Desired)
    {
        const FBlackboardEntry* Landed = FindEntry(Blackboard, Key.Name);
        if (Landed == nullptr || KeyTypeWord(Landed->KeyType) != Key.Type)
        {
            Missing.Add(Key.Name.ToString());
        }
    }
    for (const FString& Name : ToRemove)
    {
        if (FindEntry(Blackboard, FName(*Name)) != nullptr)
        {
            Missing.Add(Name);
        }
    }
    if (Missing.Num() > 0)
    {
        return FailRolledBack(FString::Printf(
            TEXT("Verification failed: %s did not land as specified."), *FString::Join(Missing, TEXT(", "))));
    }

    Blackboard->MarkPackageDirty();

    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(Blackboard->GetPathName(), SaveError);
        if (!bSaved)
        {
            return FailRolledBack(FString::Printf(TEXT("Asset save failed: %s"), *SaveError));
        }
    }

    Rollback.Commit();
    if (bCreated)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Asset creation is not undoable: undo restores level and actor state, not new packages.")));
    }
    if (ToRemove.Num() > 0)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Removed keys are still referenced by any Behavior Tree node that selected them. "
                 "Run puerts_behavior_tree_inspect on the trees that use this blackboard and check "
                 "referenced_blackboard_keys against the key list below.")));
    }

    TArray<TSharedPtr<FJsonValue>> KeysOnAsset;
    for (const FBlackboardEntry& Entry : Blackboard->Keys)
    {
        KeysOnAsset.Add(MakeShared<FJsonValueObject>(DescribeKey(Entry, false)));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("object_path"), Blackboard->GetPathName());
    Result->SetStringField(TEXT("parent_path"),
        Blackboard->Parent != nullptr ? Blackboard->Parent->GetPathName() : FString());
    Result->SetBoolField(TEXT("created"), bCreated);
    Result->SetBoolField(TEXT("applied"), true);
    Result->SetBoolField(TEXT("converged"), false);
    Result->SetNumberField(TEXT("applied_change_count"), ExpectedChanges);
    Result->SetArrayField(TEXT("keys_added"), AIStringsToJson(ToAdd));
    Result->SetArrayField(TEXT("keys_updated"), AIStringsToJson(ToUpdate));
    Result->SetArrayField(TEXT("keys_removed"), AIStringsToJson(ToRemove));
    Result->SetArrayField(TEXT("unchanged_keys"), AIStringsToJson(Unchanged));
    // Read back off the asset, not echoed from the spec.
    Result->SetArrayField(TEXT("blackboard_keys"), KeysOnAsset);
    Result->SetBoolField(TEXT("verified"), true);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetArrayField(TEXT("errors"), Errors);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    Result->SetObjectField(TEXT("cleanup"),
        Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
    OutResultJson = SerializeJson(Result);
    return true;
}

// ---------------------------------------------------------------------------
// blackboard_inspect
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::InspectBlackboardJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Inspection request must be a JSON object.");
        return false;
    }
    FString AssetPath;
    Request->TryGetStringField(TEXT("asset_path"), AssetPath);
    FString ObjectPath, PackagePath;
    if (!ResolveAssetPaths(AssetPath, ObjectPath, PackagePath, OutError)) { return false; }

    UBlackboardData* Blackboard = Cast<UBlackboardData>(LoadAssetForRead(ObjectPath, OutError));
    if (Blackboard == nullptr)
    {
        if (OutError.IsEmpty())
        {
            OutError = FString::Printf(TEXT("The asset at '%s' is not a Blackboard."), *ObjectPath);
        }
        return false;
    }

    const UPackage* Package = Blackboard->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    TArray<TSharedPtr<FJsonValue>> Keys;
    TArray<TSharedPtr<FJsonValue>> ParentChain;
    FString HashLines;
    for (const FBlackboardEntry& Entry : Blackboard->Keys)
    {
        Keys.Add(MakeShared<FJsonValueObject>(DescribeKey(Entry, false)));
        HashLines += Entry.EntryName.ToString() + TEXT("|") + KeyTypeWord(Entry.KeyType)
            + TEXT("|") + KeyBaseClassPath(Entry.KeyType)
            + TEXT("|") + (Entry.bInstanceSynced != 0 ? TEXT("synced") : TEXT("local")) + TEXT("\n");
    }
    for (const UBlackboardData* Walk = Blackboard->Parent; Walk != nullptr; Walk = Walk->Parent)
    {
        ParentChain.Add(MakeShared<FJsonValueString>(Walk->GetPathName()));
        for (const FBlackboardEntry& Entry : Walk->Keys)
        {
            Keys.Add(MakeShared<FJsonValueObject>(DescribeKey(Entry, true)));
        }
    }

    TArray<TSharedPtr<FJsonValue>> Warnings;
    if (!Blackboard->IsValid())
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("UBlackboardData::IsValid reports this asset as broken: a key name collides with "
                 "the parent chain. Behavior Trees using it will not resolve those keys.")));
    }
    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this Blackboard dirtied its package. That is a defect in the inspector, "
                 "not a property of the asset: report it.")));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), Blackboard->GetPathName());
    Result->SetStringField(TEXT("parent_path"),
        Blackboard->Parent != nullptr ? Blackboard->Parent->GetPathName() : FString());
    Result->SetArrayField(TEXT("parent_chain"), ParentChain);
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    // Unlike a Behavior Tree node, a blackboard key HAS an authored identity:
    // its name is the thing every selector binds to. Renaming a key is a
    // different key on purpose, and that is the asset's rule, not this reader's.
    Result->SetStringField(TEXT("identity_kind"), TEXT("authored_name"));
    Result->SetArrayField(TEXT("keys"), Keys);
    Result->SetNumberField(TEXT("key_count"), Blackboard->Keys.Num());
    Result->SetNumberField(TEXT("total_key_count"), Blackboard->GetNumKeys());
    Result->SetBoolField(TEXT("has_synchronized_keys"), Blackboard->HasSynchronizedKeys());
    Result->SetBoolField(TEXT("is_valid"), Blackboard->IsValid());
    Result->SetStringField(TEXT("structure_hash_sha1"), HashOf(HashLines));
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}

// ---------------------------------------------------------------------------
// eqs_inspect
//
// Read only, and read only permanently rather than until someone gets round to
// the builder. UEnvironmentQueryGraph::UpdateAsset (UE4.27,
// Engine/Plugins/AI/EnvironmentQueryEditor) opens with
// Query->GetOptionsMutable().Reset() and then rebuilds Options entirely from
// the editor graph. So Options is a COMPILED ARTIFACT, not the source of
// truth: a command that wrote Options without also authoring the matching
// UEdGraph would pass its own read-back and then be silently wiped the next
// time a human opened the asset in the EQS editor. That fails convergence and
// independent verification at the same time, so the honest surface is the read
// half alone.
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::InspectEnvQueryJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Inspection request must be a JSON object.");
        return false;
    }
    FString AssetPath;
    Request->TryGetStringField(TEXT("asset_path"), AssetPath);
    FString ObjectPath, PackagePath;
    if (!ResolveAssetPaths(AssetPath, ObjectPath, PackagePath, OutError)) { return false; }

    UEnvQuery* Query = Cast<UEnvQuery>(LoadAssetForRead(ObjectPath, OutError));
    if (Query == nullptr)
    {
        if (OutError.IsEmpty())
        {
            OutError = FString::Printf(
                TEXT("The asset at '%s' is not an Environment Query."), *ObjectPath);
        }
        return false;
    }

    const UPackage* Package = Query->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    TArray<TSharedPtr<FJsonValue>> Unsupported;
    TArray<TSharedPtr<FJsonValue>> Warnings;
    TArray<TSharedPtr<FJsonValue>> Options;
    FString HashLines;
    int32 TestCount = 0;

    const TArray<UEnvQueryOption*>& QueryOptions = Query->GetOptions();
    for (int32 Index = 0; Index < QueryOptions.Num(); ++Index)
    {
        const UEnvQueryOption* Option = QueryOptions[Index];
        if (Option == nullptr) { continue; }
        const FString OptionId = FString::Printf(TEXT("option%d"), Index);

        TSharedPtr<FJsonObject> OptionJson = MakeShared<FJsonObject>();
        OptionJson->SetStringField(TEXT("id"), OptionId);
        OptionJson->SetNumberField(TEXT("index"), Index);
        OptionJson->SetStringField(TEXT("identity"), TEXT("derived"));

        if (Option->Generator != nullptr)
        {
            TSharedPtr<FJsonObject> Generator = MakeShared<FJsonObject>();
            Generator->SetStringField(TEXT("class_path"), Option->Generator->GetClass()->GetPathName());
            Generator->SetStringField(TEXT("class_name"), Option->Generator->GetClass()->GetName());
            Generator->SetObjectField(TEXT("properties"),
                DescribeEditProperties(Option->Generator, OptionId + TEXT("/generator"), Unsupported));
            OptionJson->SetObjectField(TEXT("generator"), Generator);
            HashLines += OptionId + TEXT("|") + Option->Generator->GetClass()->GetPathName() + TEXT("\n");
        }
        else
        {
            OptionJson->SetField(TEXT("generator"), MakeShared<FJsonValueNull>());
            Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("Option %d has no generator and produces no items."), Index)));
            HashLines += OptionId + TEXT("|<no generator>\n");
        }

        TArray<TSharedPtr<FJsonValue>> Tests;
        for (int32 TestIndex = 0; TestIndex < Option->Tests.Num(); ++TestIndex)
        {
            const UEnvQueryTest* Test = Option->Tests[TestIndex];
            if (Test == nullptr) { continue; }
            ++TestCount;
            const FString TestId = FString::Printf(TEXT("%s/test%d"), *OptionId, TestIndex);
            TSharedPtr<FJsonObject> TestJson = MakeShared<FJsonObject>();
            TestJson->SetStringField(TEXT("id"), TestId);
            TestJson->SetNumberField(TEXT("index"), TestIndex);
            TestJson->SetStringField(TEXT("class_path"), Test->GetClass()->GetPathName());
            TestJson->SetStringField(TEXT("class_name"), Test->GetClass()->GetName());
            // Scoring and filtering live in ordinary edit properties on
            // UEnvQueryTest (TestPurpose, ScoringEquation, ScoringFactor,
            // FilterType and the rest), so the generic walk carries them.
            TestJson->SetObjectField(TEXT("properties"),
                DescribeEditProperties(Test, TestId, Unsupported));
            Tests.Add(MakeShared<FJsonValueObject>(TestJson));
            HashLines += TestId + TEXT("|") + Test->GetClass()->GetPathName() + TEXT("\n");
        }
        OptionJson->SetArrayField(TEXT("tests"), Tests);
        Options.Add(MakeShared<FJsonValueObject>(OptionJson));
    }

    bool bHasEditorGraph = false;
#if WITH_EDITORONLY_DATA
    bHasEditorGraph = Query->EdGraph != nullptr;
#endif
    if (!bHasEditorGraph)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This query has no editor graph. Its options were loaded from the saved asset; "
                 "opening it in the EQS editor will build a graph from them.")));
    }

    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this Environment Query dirtied its package. That is a defect in the "
                 "inspector, not a property of the asset: report it.")));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), Query->GetPathName());
    Result->SetStringField(TEXT("query_name"), Query->GetQueryName().ToString());
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    Result->SetStringField(TEXT("identity_kind"), TEXT("derived"));
    Result->SetArrayField(TEXT("options"), Options);
    Result->SetNumberField(TEXT("option_count"), Options.Num());
    Result->SetNumberField(TEXT("test_count"), TestCount);
    Result->SetBoolField(TEXT("has_editor_graph"), bHasEditorGraph);
    Result->SetBoolField(TEXT("build_supported"), false);
    Result->SetStringField(TEXT("build_unsupported_reason"),
        TEXT("UEnvironmentQueryGraph::UpdateAsset resets UEnvQuery::Options and rebuilds them from "
             "the editor graph, so Options is a compiled artifact rather than the source of truth. "
             "A build that wrote Options without authoring the matching UEdGraph would verify "
             "against itself and then be wiped the next time the asset was opened in the EQS "
             "editor. There is no eqs_build for that reason."));
    Result->SetArrayField(TEXT("unsupported_fields"), Unsupported);
    Result->SetStringField(TEXT("structure_hash_sha1"), HashOf(HashLines));
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}

// ---------------------------------------------------------------------------
// nav_inspect
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::InspectNavigationJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    // The request currently carries nothing; it is still parsed so a caller
    // that sends a field gets a clear refusal rather than silence.
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Inspection request must be a JSON object.");
        return false;
    }

    UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World == nullptr)
    {
        OutError = TEXT("There is no editor world to inspect.");
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Warnings;
    TArray<TSharedPtr<FJsonValue>> Unsupported;

    UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (NavSystem == nullptr)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This world has no navigation system. Every navigation query will refuse until "
                 "one exists, which usually means the level has no NavMeshBoundsVolume.")));
    }

    // Bounds volumes and modifier volumes, in world space. A caller planning a
    // spawn needs the box, not the actor name.
    TArray<TSharedPtr<FJsonValue>> BoundsVolumes;
    TArray<TSharedPtr<FJsonValue>> ModifierVolumes;
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
    {
        ANavMeshBoundsVolume* Volume = *It;
        if (Volume == nullptr) { continue; }
        FVector Origin = FVector::ZeroVector;
        FVector Extent = FVector::ZeroVector;
        Volume->GetActorBounds(false, Origin, Extent);
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Volume->GetName());
        Entry->SetStringField(TEXT("label"), Volume->GetActorLabel());
        Entry->SetStringField(TEXT("path"), Volume->GetPathName());
        Entry->SetObjectField(TEXT("origin"), VectorToJson(Origin));
        Entry->SetObjectField(TEXT("extent"), VectorToJson(Extent));
        Entry->SetObjectField(TEXT("min"), VectorToJson(Origin - Extent));
        Entry->SetObjectField(TEXT("max"), VectorToJson(Origin + Extent));
        Entry->SetNumberField(TEXT("supported_agents_bits"), Volume->SupportedAgents.PackedBits);
        BoundsVolumes.Add(MakeShared<FJsonValueObject>(Entry));
    }
    for (TActorIterator<ANavModifierVolume> It(World); It; ++It)
    {
        ANavModifierVolume* Volume = *It;
        if (Volume == nullptr) { continue; }
        FVector Origin = FVector::ZeroVector;
        FVector Extent = FVector::ZeroVector;
        Volume->GetActorBounds(false, Origin, Extent);
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Volume->GetName());
        Entry->SetStringField(TEXT("label"), Volume->GetActorLabel());
        Entry->SetStringField(TEXT("path"), Volume->GetPathName());
        Entry->SetObjectField(TEXT("origin"), VectorToJson(Origin));
        Entry->SetObjectField(TEXT("extent"), VectorToJson(Extent));
        const UClass* AreaClass = Volume->GetAreaClass().Get();
        Entry->SetStringField(TEXT("area_class"),
            AreaClass != nullptr ? AreaClass->GetPathName() : FString());
        ModifierVolumes.Add(MakeShared<FJsonValueObject>(Entry));
    }

    // Nav data actors and their generation settings. The settings are read by
    // reflected name rather than by casting to ARecastNavMesh, so a project
    // using a different ANavigationData subclass reports whatever it has
    // instead of reporting nothing.
    static const TCHAR* GenerationFields[] = {
        TEXT("TileSizeUU"), TEXT("CellSize"), TEXT("CellHeight"),
        TEXT("AgentRadius"), TEXT("AgentHeight"), TEXT("AgentMaxSlope"),
        TEXT("AgentMaxStepHeight"), TEXT("MinRegionArea"), TEXT("MergeRegionSize"),
        TEXT("bFixedTilePoolSize"), TEXT("TilePoolSize"), TEXT("RuntimeGeneration"),
    };
    TArray<TSharedPtr<FJsonValue>> NavData;
    for (TActorIterator<ANavigationData> It(World); It; ++It)
    {
        ANavigationData* Data = *It;
        if (Data == nullptr) { continue; }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Data->GetName());
        Entry->SetStringField(TEXT("class_path"), Data->GetClass()->GetPathName());
        Entry->SetStringField(TEXT("agent_name"), Data->GetConfig().Name.ToString());
        Entry->SetNumberField(TEXT("agent_radius"), Data->GetConfig().AgentRadius);
        Entry->SetNumberField(TEXT("agent_height"), Data->GetConfig().AgentHeight);
        Entry->SetObjectField(TEXT("default_query_extent"),
            VectorToJson(Data->GetDefaultQueryExtent()));
        Entry->SetBoolField(TEXT("is_main_nav_data"),
            NavSystem != nullptr && NavSystem->GetDefaultNavDataInstance() == Data);

        TSharedPtr<FJsonObject> Generation = MakeShared<FJsonObject>();
        for (const TCHAR* FieldName : GenerationFields)
        {
            FProperty* Property = FindFProperty<FProperty>(Data->GetClass(), FieldName);
            if (Property == nullptr) { continue; }
            const TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(
                Property, Property->ContainerPtrToValuePtr<void>(Data), 0, 0);
            if (Value.IsValid()) { Generation->SetField(FieldName, Value); }
            else
            {
                Unsupported.Add(MakeShared<FJsonValueString>(
                    FString::Printf(TEXT("%s.%s"), *Data->GetName(), FieldName)));
            }
        }
        Entry->SetObjectField(TEXT("generation"), Generation);
        NavData.Add(MakeShared<FJsonValueObject>(Entry));
    }

    // Bounds the navigation system actually registered, which is not the same
    // list as the volumes in the level: a volume in an unloaded or hidden
    // sublevel is present as an actor and absent here.
    TArray<TSharedPtr<FJsonValue>> RegisteredBounds;
    TArray<TSharedPtr<FJsonValue>> SupportedAgents;
    if (NavSystem != nullptr)
    {
        for (const FNavigationBounds& Bounds : NavSystem->GetNavigationBounds())
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetNumberField(TEXT("unique_id"), Bounds.UniqueID);
            Entry->SetObjectField(TEXT("min"), VectorToJson(Bounds.AreaBox.Min));
            Entry->SetObjectField(TEXT("max"), VectorToJson(Bounds.AreaBox.Max));
            Entry->SetNumberField(TEXT("supported_agents_bits"), Bounds.SupportedAgents.PackedBits);
            Entry->SetStringField(TEXT("level"),
                Bounds.Level.IsValid() ? Bounds.Level->GetPathName() : FString());
            RegisteredBounds.Add(MakeShared<FJsonValueObject>(Entry));
        }
        for (const FNavDataConfig& Agent : NavSystem->GetSupportedAgents())
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Agent.Name.ToString());
            Entry->SetNumberField(TEXT("agent_radius"), Agent.AgentRadius);
            Entry->SetNumberField(TEXT("agent_height"), Agent.AgentHeight);
            Entry->SetObjectField(TEXT("default_query_extent"), VectorToJson(Agent.DefaultQueryExtent));
            SupportedAgents.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    if (BoundsVolumes.Num() == 0)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This level has no NavMeshBoundsVolume, so there is no navmesh and every "
                 "navigation query will fail. That is a level authoring gap, not a query error.")));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("world"), World->GetPathName());
    Result->SetBoolField(TEXT("navigation_system_present"), NavSystem != nullptr);
    Result->SetBoolField(TEXT("is_building"),
        NavSystem != nullptr && NavSystem->GetNumRemainingBuildTasks() > 0);
    Result->SetNumberField(TEXT("remaining_build_tasks"),
        NavSystem != nullptr ? NavSystem->GetNumRemainingBuildTasks() : 0);
    Result->SetArrayField(TEXT("nav_data"), NavData);
    Result->SetArrayField(TEXT("supported_agents"), SupportedAgents);
    Result->SetArrayField(TEXT("nav_mesh_bounds_volumes"), BoundsVolumes);
    Result->SetArrayField(TEXT("nav_modifier_volumes"), ModifierVolumes);
    Result->SetArrayField(TEXT("registered_navigation_bounds"), RegisteredBounds);
    Result->SetArrayField(TEXT("unsupported_fields"), Unsupported);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}

// ---------------------------------------------------------------------------
// nav_query
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::QueryNavigationJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Navigation query request must be a JSON object.");
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* QueryValues = nullptr;
    if (!Request->TryGetArrayField(TEXT("queries"), QueryValues) || QueryValues->Num() == 0)
    {
        OutError = TEXT("queries must be a non-empty array.");
        return false;
    }
    if (QueryValues->Num() > 100)
    {
        OutError = TEXT("queries is limited to 100 entries per call.");
        return false;
    }

    UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World == nullptr)
    {
        OutError = TEXT("There is no editor world to query.");
        return false;
    }
    UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (NavSystem == nullptr)
    {
        OutError = TEXT("This world has no navigation system. Run puerts_nav_inspect: the usual "
                        "cause is a level with no NavMeshBoundsVolume.");
        return false;
    }

    // The whole batch is validated before the first query runs, so a typo in
    // entry nine is a refusal rather than eight answers and a failure.
    struct FParsedQuery
    {
        FString Id;
        FString Kind;
        FVector A = FVector::ZeroVector;
        FVector B = FVector::ZeroVector;
        FVector Extent = FVector::ZeroVector;
        float Radius = 0.0f;
    };
    TArray<FParsedQuery> Parsed;
    for (int32 Index = 0; Index < QueryValues->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* Entry = nullptr;
        if (!(*QueryValues)[Index].IsValid() || !(*QueryValues)[Index]->TryGetObject(Entry))
        {
            OutError = FString::Printf(TEXT("queries[%d] must be an object."), Index);
            return false;
        }
        FParsedQuery Query;
        (*Entry)->TryGetStringField(TEXT("id"), Query.Id);
        if (Query.Id.IsEmpty()) { Query.Id = FString::Printf(TEXT("q%d"), Index); }
        (*Entry)->TryGetStringField(TEXT("kind"), Query.Kind);

        const TSharedPtr<FJsonObject>* Object = nullptr;
        if ((*Entry)->TryGetObjectField(TEXT("point"), Object)) { Query.A = JsonToVector(*Object); }
        if ((*Entry)->TryGetObjectField(TEXT("start"), Object)) { Query.A = JsonToVector(*Object); }
        if ((*Entry)->TryGetObjectField(TEXT("origin"), Object)) { Query.A = JsonToVector(*Object); }
        if ((*Entry)->TryGetObjectField(TEXT("end"), Object)) { Query.B = JsonToVector(*Object); }
        if ((*Entry)->TryGetObjectField(TEXT("extent"), Object)) { Query.Extent = JsonToVector(*Object); }
        double Radius = 0.0;
        if ((*Entry)->TryGetNumberField(TEXT("radius"), Radius))
        {
            Query.Radius = static_cast<float>(Radius);
        }

        if (Query.Kind == TEXT("project"))
        {
            // No extent means the nav data's own default query extent, which is
            // INVALID_NAVEXTENT to the engine rather than a zero-size box.
        }
        else if (Query.Kind == TEXT("path") || Query.Kind == TEXT("raycast"))
        {
            if (!(*Entry)->HasField(TEXT("end")))
            {
                OutError = FString::Printf(TEXT("queries[%d] kind '%s' needs an end point."),
                    Index, *Query.Kind);
                return false;
            }
        }
        else if (Query.Kind == TEXT("random_point"))
        {
            if (Query.Radius <= 0.0f)
            {
                OutError = FString::Printf(
                    TEXT("queries[%d] kind 'random_point' needs a radius greater than zero."), Index);
                return false;
            }
        }
        else
        {
            OutError = FString::Printf(
                TEXT("queries[%d] kind '%s' is not one of project, path, raycast, random_point."),
                Index, *Query.Kind);
            return false;
        }
        Parsed.Add(Query);
    }

    static const TCHAR* ResultWords[] = { TEXT("Invalid"), TEXT("Error"), TEXT("Fail"), TEXT("Success") };
    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Warnings;
    bool bAnyNonDeterministic = false;

    for (const FParsedQuery& Query : Parsed)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("id"), Query.Id);
        Out->SetStringField(TEXT("kind"), Query.Kind);

        if (Query.Kind == TEXT("project"))
        {
            FNavLocation Projected;
            const FVector Extent = Query.Extent.IsNearlyZero() ? INVALID_NAVEXTENT : Query.Extent;
            const bool bFound = NavSystem->ProjectPointToNavigation(
                Query.A, Projected, Extent, static_cast<const ANavigationData*>(nullptr), nullptr);
            Out->SetBoolField(TEXT("on_navmesh"), bFound);
            Out->SetObjectField(TEXT("point"), VectorToJson(Query.A));
            Out->SetField(TEXT("projected_point"), bFound
                ? MakeShared<FJsonValueObject>(VectorToJson(Projected.Location))
                : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>()));
            if (bFound)
            {
                Out->SetNumberField(TEXT("projection_distance"),
                    FVector::Dist(Query.A, Projected.Location));
            }
        }
        else if (Query.Kind == TEXT("path"))
        {
            float Length = 0.0f;
            float Cost = 0.0f;
            const ENavigationQueryResult::Type LengthResult =
                NavSystem->GetPathLength(Query.A, Query.B, Length);
            const ENavigationQueryResult::Type CostResult =
                NavSystem->GetPathCost(Query.A, Query.B, Cost);
            const int32 LengthIndex = FMath::Clamp(static_cast<int32>(LengthResult), 0, 3);
            const int32 CostIndex = FMath::Clamp(static_cast<int32>(CostResult), 0, 3);
            Out->SetObjectField(TEXT("start"), VectorToJson(Query.A));
            Out->SetObjectField(TEXT("end"), VectorToJson(Query.B));
            Out->SetStringField(TEXT("result"), ResultWords[LengthIndex]);
            Out->SetStringField(TEXT("cost_result"), ResultWords[CostIndex]);
            // Reachable means Success, and only Success. Fail is "there is no
            // path", Error and Invalid are "the query could not be answered",
            // and collapsing the three into false would tell a caller a
            // configuration problem was a level layout problem.
            Out->SetBoolField(TEXT("reachable"), LengthResult == ENavigationQueryResult::Success);
            Out->SetNumberField(TEXT("path_length"),
                LengthResult == ENavigationQueryResult::Success ? Length : -1.0f);
            Out->SetNumberField(TEXT("path_cost"),
                CostResult == ENavigationQueryResult::Success ? Cost : -1.0f);
            Out->SetNumberField(TEXT("straight_line_distance"), FVector::Dist(Query.A, Query.B));
        }
        else if (Query.Kind == TEXT("raycast"))
        {
            FVector HitLocation = FVector::ZeroVector;
            // True means the ray HIT something, so the straight line is blocked.
            const bool bHit = UNavigationSystemV1::NavigationRaycast(
                World, Query.A, Query.B, HitLocation, nullptr, nullptr);
            Out->SetObjectField(TEXT("start"), VectorToJson(Query.A));
            Out->SetObjectField(TEXT("end"), VectorToJson(Query.B));
            Out->SetBoolField(TEXT("blocked"), bHit);
            Out->SetField(TEXT("hit_location"), bHit
                ? MakeShared<FJsonValueObject>(VectorToJson(HitLocation))
                : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>()));
        }
        else // random_point
        {
            bAnyNonDeterministic = true;
            FVector Random = FVector::ZeroVector;
            const bool bFound = UNavigationSystemV1::K2_GetRandomLocationInNavigableRadius(
                World, Query.A, Random, Query.Radius, nullptr, nullptr);
            Out->SetObjectField(TEXT("origin"), VectorToJson(Query.A));
            Out->SetNumberField(TEXT("radius"), Query.Radius);
            Out->SetBoolField(TEXT("found"), bFound);
            Out->SetField(TEXT("point"), bFound
                ? MakeShared<FJsonValueObject>(VectorToJson(Random))
                : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>()));
        }
        Results.Add(MakeShared<FJsonValueObject>(Out));
    }

    if (NavSystem->GetNumRemainingBuildTasks() > 0)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("The navmesh is still building, so these answers describe a partial navmesh. "
                 "Re-run once puerts_nav_inspect reports remaining_build_tasks as zero.")));
    }
    if (bAnyNonDeterministic)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("random_point is not deterministic: two identical calls give different points. "
                 "Do not use it in a comparison or a regression check.")));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("world"), World->GetPathName());
    Result->SetArrayField(TEXT("results"), Results);
    Result->SetNumberField(TEXT("query_count"), Results.Num());
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}

// ---------------------------------------------------------------------------
// ai_perception_build
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::BuildAIPerceptionJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Perception spec must be a JSON object.");
        return false;
    }

    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    if (!bPlanOnly && ActiveTransaction == nullptr)
    {
        OutError = TEXT("Perception build requires an active transaction.");
        return false;
    }

    FString AssetPath;
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Perception authoring is limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }
    const FString ObjectPath =
        AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);

    // The Blueprint must already exist. This command configures a controller,
    // it does not invent one: puerts_blueprint_build owns creation.
    FString LoadError;
    UBlueprint* Blueprint = Cast<UBlueprint>(LoadAssetForRead(ObjectPath, LoadError));
    if (Blueprint == nullptr)
    {
        OutError = LoadError.IsEmpty()
            ? FString::Printf(TEXT("The asset at '%s' is not a Blueprint."), *ObjectPath)
            : FString::Printf(
                TEXT("%s Create the controller with puerts_blueprint_build first; this command "
                     "configures perception on an existing one."), *LoadError);
        return false;
    }
    if (Blueprint->ParentClass == nullptr
        || !Blueprint->ParentClass->IsChildOf(AAIController::StaticClass()))
    {
        OutError = FString::Printf(
            TEXT("'%s' has parent class %s, which is not an AIController. An AIPerceptionComponent "
                 "only does anything on a controller."),
            *ObjectPath,
            Blueprint->ParentClass != nullptr ? *Blueprint->ParentClass->GetName() : TEXT("none"));
        return false;
    }

    FString ComponentName = TEXT("AIPerception");
    Spec->TryGetStringField(TEXT("component_name"), ComponentName);

    // --- Validate the whole sense list before anything is touched. ---

    struct FDesiredSense
    {
        FString Sense;
        UClass* ConfigClass = nullptr;
        TSharedPtr<FJsonObject> Properties;
    };
    TArray<FDesiredSense> Desired;
    TSet<FString> DesiredSenses;
    const TArray<TSharedPtr<FJsonValue>>* SenseValues = nullptr;
    if (Spec->TryGetArrayField(TEXT("senses"), SenseValues))
    {
        for (const TSharedPtr<FJsonValue>& Value : *SenseValues)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Entry))
            {
                OutError = TEXT("Every entry of senses must be an object with a sense field.");
                return false;
            }
            FDesiredSense Sense;
            (*Entry)->TryGetStringField(TEXT("sense"), Sense.Sense);
            Sense.ConfigClass = SenseConfigClass(Sense.Sense);
            if (Sense.ConfigClass == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("Unknown sense '%s'. UE4.27 has Sight, Hearing, Damage, Touch, Team and "
                         "Prediction. A Blueprint sense is not accepted here because its user "
                         "sense class cannot be validated."),
                    *Sense.Sense);
                return false;
            }
            if (DesiredSenses.Contains(Sense.Sense))
            {
                OutError = FString::Printf(
                    TEXT("senses declares '%s' twice. UAIPerceptionComponent holds one config per "
                         "sense class, so the spec cannot say two things about one sense."),
                    *Sense.Sense);
                return false;
            }
            // Everything except the sense word is a reflected property of the
            // config class, checked here so a misspelled field is a refusal
            // rather than a value that silently never applied.
            Sense.Properties = MakeShared<FJsonObject>();
            for (const auto& Pair : (*Entry)->Values)
            {
                if (Pair.Key == TEXT("sense")) { continue; }
                if (FindFProperty<FProperty>(Sense.ConfigClass, *Pair.Key) == nullptr)
                {
                    OutError = FString::Printf(
                        TEXT("Sense '%s' has no reflected property '%s' on %s."),
                        *Sense.Sense, *Pair.Key, *Sense.ConfigClass->GetName());
                    return false;
                }
                Sense.Properties->SetField(Pair.Key, Pair.Value);
            }
            DesiredSenses.Add(Sense.Sense);
            Desired.Add(Sense);
        }
    }

    FString DominantSense;
    Spec->TryGetStringField(TEXT("dominant_sense"), DominantSense);
    UClass* DominantImplementation = nullptr;
    if (!DominantSense.IsEmpty() && DominantSense != TEXT("None"))
    {
        DominantImplementation = SenseImplementationClass(DominantSense);
        if (DominantImplementation == nullptr)
        {
            OutError = FString::Printf(TEXT("Unknown dominant_sense '%s'."), *DominantSense);
            return false;
        }
        if (!DesiredSenses.Contains(DominantSense))
        {
            OutError = FString::Printf(
                TEXT("dominant_sense is '%s' but senses does not configure it. A dominant sense "
                     "that is not configured never reports anything."),
                *DominantSense);
            return false;
        }
    }

    const bool bRemoveUnlisted = Spec->HasTypedField<EJson::Boolean>(TEXT("remove_unlisted"))
        && Spec->GetBoolField(TEXT("remove_unlisted"));
    const bool bCompile = !Spec->HasTypedField<EJson::Boolean>(TEXT("compile"))
        || Spec->GetBoolField(TEXT("compile"));
    const bool bSave = !Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        || Spec->GetBoolField(TEXT("save"));

    FArrayProperty* SensesProperty = SensesConfigProperty();
    if (SensesProperty == nullptr)
    {
        OutError = TEXT("UAIPerceptionComponent has no reflected SensesConfig array in this build.");
        return false;
    }

    // --- Classification against what is there now. ---

    USCS_Node* Node = AIFindComponentNode(Blueprint, ComponentName);
    UAIPerceptionComponent* Template = Node != nullptr
        ? Cast<UAIPerceptionComponent>(Node->ComponentTemplate)
        : nullptr;
    if (Node != nullptr && Template == nullptr)
    {
        OutError = FString::Printf(
            TEXT("Component '%s' exists on '%s' but is a %s, not an AIPerceptionComponent."),
            *ComponentName, *ObjectPath,
            Node->ComponentTemplate != nullptr
                ? *Node->ComponentTemplate->GetClass()->GetName() : TEXT("null template"));
        return false;
    }

    TArray<FString> PresentSenses;
    if (Template != nullptr)
    {
        for (auto It = Template->GetSensesConfigIterator(); It; ++It)
        {
            if (*It != nullptr) { PresentSenses.Add(SenseWord(*It)); }
        }
    }
    TArray<FString> SensesToAdd;
    TArray<FString> SensesToUpdate;
    TArray<FString> SensesToRemove;
    TArray<FString> UnchangedSenses;
    for (const FDesiredSense& Sense : Desired)
    {
        const UAISenseConfig* Present = nullptr;
        if (Template != nullptr)
        {
            for (auto It = Template->GetSensesConfigIterator(); It; ++It)
            {
                if (*It != nullptr && (*It)->GetClass() == Sense.ConfigClass) { Present = *It; break; }
            }
        }
        if (Present == nullptr)
        {
            SensesToAdd.Add(Sense.Sense);
        }
        else
        {
            (SenseMatchesSpec(Present, Sense.Properties) ? UnchangedSenses : SensesToUpdate)
                .Add(Sense.Sense);
        }
    }
    if (bRemoveUnlisted)
    {
        for (const FString& Present : PresentSenses)
        {
            if (!DesiredSenses.Contains(Present)) { SensesToRemove.Add(Present); }
        }
    }
    const bool bComponentMissing = Template == nullptr;
    // The dominant sense is a change in its own right. Without this a spec that
    // only moved the dominant sense would classify as converged and be skipped.
    const UClass* const CurrentDominant =
        Template != nullptr ? Template->GetDominantSense().Get() : nullptr;
    const bool bDominantChanges = CurrentDominant != DominantImplementation;
    const int32 ExpectedChanges = SensesToAdd.Num() + SensesToUpdate.Num()
        + SensesToRemove.Num() + (bComponentMissing ? 1 : 0) + (bDominantChanges ? 1 : 0);

    if (bPlanOnly)
    {
        TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
        Plan->SetBoolField(TEXT("plan_only"), true);
        Plan->SetStringField(TEXT("asset_path"), AssetPath);
        Plan->SetStringField(TEXT("component_name"), ComponentName);
        Plan->SetBoolField(TEXT("component_exists"), !bComponentMissing);
        Plan->SetArrayField(TEXT("current_senses"), AIStringsToJson(PresentSenses));
        Plan->SetArrayField(TEXT("senses_to_add"), AIStringsToJson(SensesToAdd));
        Plan->SetArrayField(TEXT("senses_to_update"), AIStringsToJson(SensesToUpdate));
        Plan->SetArrayField(TEXT("senses_to_remove"), AIStringsToJson(SensesToRemove));
        Plan->SetArrayField(TEXT("unchanged_senses"), AIStringsToJson(UnchangedSenses));
        Plan->SetBoolField(TEXT("dominant_sense_changes"), bDominantChanges);
        Plan->SetNumberField(TEXT("expected_change_count"), ExpectedChanges);
        Plan->SetBoolField(TEXT("converged"), ExpectedChanges == 0);
        Plan->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
        Plan->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
        OutResultJson = SerializeJson(Plan);
        return true;
    }

    if (ExpectedChanges == 0 && Template != nullptr)
    {
        // Already converged. Returning before the mutation section is what makes
        // a rerun free: no Modify, no Blueprint compile, no dirty package, no
        // save. blackboard_build has the same early exit for the same reason.
        TSharedPtr<FJsonObject> Converged = MakeShared<FJsonObject>();
        Converged->SetStringField(TEXT("asset_path"), AssetPath);
        Converged->SetStringField(TEXT("object_path"), Blueprint->GetPathName());
        Converged->SetStringField(TEXT("component_name"), ComponentName);
        Converged->SetBoolField(TEXT("component_created"), false);
        Converged->SetBoolField(TEXT("applied"), false);
        Converged->SetBoolField(TEXT("converged"), true);
        Converged->SetNumberField(TEXT("applied_change_count"), 0);
        Converged->SetArrayField(TEXT("unchanged_senses"), AIStringsToJson(UnchangedSenses));
        Converged->SetStringField(TEXT("dominant_sense"),
            DominantImplementation != nullptr ? DominantSense : TEXT("None"));
        Converged->SetBoolField(TEXT("verified"), true);
        Converged->SetBoolField(TEXT("saved"), false);
        Converged->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
        Converged->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
        OutResultJson = SerializeJson(Converged);
        return true;
    }

    // --- Mutation. The Blueprint already existed, so nothing here creates an
    //     asset and the rollback boundary is the transaction plus the refusal
    //     to save anything that did not compile and verify. ---

    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;

    auto FailRolledBack = [&](const FString& Reason) -> bool
    {
        Errors.Add(MakeShared<FJsonValueString>(Reason));
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }
        TSharedPtr<FJsonObject> Failed = MakeShared<FJsonObject>();
        Failed->SetStringField(TEXT("asset_path"), AssetPath);
        Failed->SetStringField(TEXT("component_name"), ComponentName);
        Failed->SetBoolField(TEXT("applied"), false);
        Failed->SetNumberField(TEXT("applied_change_count"), 0);
        Failed->SetBoolField(TEXT("verified"), false);
        Failed->SetBoolField(TEXT("saved"), false);
        Failed->SetArrayField(TEXT("errors"), Errors);
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("The build failed and the transaction was cancelled. Whether the component "
                 "template really came back is not something a cancel can promise: read it with "
                 "puerts_ai_controller_inspect before trusting the asset.")));
        Failed->SetArrayField(TEXT("warnings"), Warnings);
        OutResultJson = SerializeJson(Failed);
        return true;
    };

    if (bComponentMissing)
    {
        if (!UBlueprintGraphBuilderLibrary::AddComponentToBlueprint(
                Blueprint, UAIPerceptionComponent::StaticClass(), ComponentName, FString()))
        {
            return FailRolledBack(FString::Printf(
                TEXT("Component '%s' could not be added to '%s'."), *ComponentName, *ObjectPath));
        }
        Node = AIFindComponentNode(Blueprint, ComponentName);
        Template = Node != nullptr ? Cast<UAIPerceptionComponent>(Node->ComponentTemplate) : nullptr;
        if (Template == nullptr)
        {
            return FailRolledBack(FString::Printf(
                TEXT("Component '%s' was added but has no AIPerceptionComponent template."),
                *ComponentName));
        }
    }

    Template->Modify();
    Blueprint->Modify();

    // Reconcile the array in one pass. SensesConfig is a protected UPROPERTY,
    // so the write goes through reflection; the read half above uses the public
    // iterator, which is why nothing here has to guess at the current contents.
    {
        FScriptArrayHelper Helper(SensesProperty, SensesProperty->ContainerPtrToValuePtr<void>(Template));
        FObjectProperty* ElementProperty = CastField<FObjectProperty>(SensesProperty->Inner);
        if (ElementProperty == nullptr)
        {
            return FailRolledBack(TEXT("SensesConfig is not an array of object references in this build."));
        }

        // Removal first, walking backwards so the indices stay valid.
        for (int32 Index = Helper.Num() - 1; Index >= 0; --Index)
        {
            UObject* Element = ElementProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index));
            const UAISenseConfig* Config = Cast<UAISenseConfig>(Element);
            const FString Word = SenseWord(Config);
            const bool bUnlisted = !DesiredSenses.Contains(Word);
            if (Config == nullptr || (bRemoveUnlisted && bUnlisted))
            {
                Helper.RemoveValues(Index, 1);
            }
        }

        for (const FDesiredSense& Sense : Desired)
        {
            // A listed sense is replaced wholesale rather than patched, so the
            // config that lands is the spec plus class defaults and never a
            // half of some earlier spec. This is the same rule widget_build
            // uses for a widget tree: no per-field identity, so the spec is
            // the whole thing.
            int32 Found = INDEX_NONE;
            for (int32 Index = 0; Index < Helper.Num(); ++Index)
            {
                UObject* Element = ElementProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index));
                if (Element != nullptr && Element->GetClass() == Sense.ConfigClass)
                {
                    Found = Index;
                    break;
                }
            }
            UAISenseConfig* Config = NewObject<UAISenseConfig>(
                Template, Sense.ConfigClass, NAME_None, RF_Transactional);
            if (Config == nullptr)
            {
                return FailRolledBack(FString::Printf(
                    TEXT("Sense '%s': the config object could not be created."), *Sense.Sense));
            }
            for (const auto& Pair : Sense.Properties->Values)
            {
                FProperty* Property = FindFProperty<FProperty>(Sense.ConfigClass, *Pair.Key);
                if (Property == nullptr) { continue; }
                if (!FJsonObjectConverter::JsonValueToUProperty(
                        Pair.Value, Property, Property->ContainerPtrToValuePtr<void>(Config), 0, 0))
                {
                    return FailRolledBack(FString::Printf(
                        TEXT("Sense '%s' property '%s': the value does not fit a %s."),
                        *Sense.Sense, *Pair.Key, *Property->GetCPPType()));
                }
            }
            if (Found == INDEX_NONE)
            {
                Found = Helper.AddValue();
            }
            ElementProperty->SetObjectPropertyValue(Helper.GetRawPtr(Found), Config);
        }
    }

    Template->SetDominantSense(DominantImplementation);

    FPropertyChangedEvent ChangedEvent(SensesProperty, EPropertyChangeType::ValueSet);
    Template->PostEditChangeProperty(ChangedEvent);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    TSharedPtr<FJsonObject> CompileReport;
    if (bCompile)
    {
        const FString ReportJson = UBlueprintGraphBuilderLibrary::CompileAndReport(Blueprint);
        TSharedRef<TJsonReader<>> ReportReader = TJsonReaderFactory<>::Create(ReportJson);
        if (FJsonSerializer::Deserialize(ReportReader, CompileReport) && CompileReport.IsValid())
        {
            if (!CompileReport->HasTypedField<EJson::Boolean>(TEXT("success"))
                || !CompileReport->GetBoolField(TEXT("success")))
            {
                return FailRolledBack(FString::Printf(
                    TEXT("The Blueprint did not compile after the perception change: %s"), *ReportJson));
            }
        }
    }

    // --- Verification. The component template is read again through the
    //     public iterator rather than the writes being trusted. ---

    TArray<FString> LandedSenses;
    for (auto It = Template->GetSensesConfigIterator(); It; ++It)
    {
        if (*It != nullptr) { LandedSenses.Add(SenseWord(*It)); }
    }
    TArray<FString> MissingSenses;
    for (const FDesiredSense& Sense : Desired)
    {
        if (!LandedSenses.Contains(Sense.Sense)) { MissingSenses.Add(Sense.Sense); }
    }
    for (const FString& Removed : SensesToRemove)
    {
        if (LandedSenses.Contains(Removed)) { MissingSenses.Add(Removed); }
    }
    if (MissingSenses.Num() > 0)
    {
        return FailRolledBack(FString::Printf(
            TEXT("Verification failed: %s did not land as specified."),
            *FString::Join(MissingSenses, TEXT(", "))));
    }

    Blueprint->MarkPackageDirty();
    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(Blueprint->GetPathName(), SaveError);
        if (!bSaved)
        {
            return FailRolledBack(FString::Printf(TEXT("Asset save failed: %s"), *SaveError));
        }
    }

    Warnings.Add(MakeShared<FJsonValueString>(
        TEXT("Perception configures WHAT the controller can sense, not what it does about it. "
             "Handling a stimulus needs an OnPerceptionUpdated binding in the controller graph, "
             "which puerts_blueprint_build authors.")));

    TArray<TSharedPtr<FJsonValue>> SensesOnAsset;
    TArray<TSharedPtr<FJsonValue>> Unsupported;
    for (auto It = Template->GetSensesConfigIterator(); It; ++It)
    {
        const UAISenseConfig* Config = *It;
        if (Config == nullptr) { continue; }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("sense"), SenseWord(Config));
        Entry->SetStringField(TEXT("class_path"), Config->GetClass()->GetPathName());
        Entry->SetObjectField(TEXT("properties"),
            DescribeEditProperties(Config, SenseWord(Config), Unsupported));
        SensesOnAsset.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("object_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("component_name"), ComponentName);
    Result->SetBoolField(TEXT("component_created"), bComponentMissing);
    Result->SetBoolField(TEXT("applied"), true);
    Result->SetBoolField(TEXT("converged"), false);
    Result->SetNumberField(TEXT("applied_change_count"), ExpectedChanges);
    Result->SetArrayField(TEXT("senses_added"), AIStringsToJson(SensesToAdd));
    Result->SetArrayField(TEXT("senses_updated"), AIStringsToJson(SensesToUpdate));
    Result->SetArrayField(TEXT("senses_removed"), AIStringsToJson(SensesToRemove));
    Result->SetArrayField(TEXT("unchanged_senses"), AIStringsToJson(UnchangedSenses));
    // Read off the component template, not echoed from the spec.
    Result->SetArrayField(TEXT("senses"), SensesOnAsset);
    Result->SetStringField(TEXT("dominant_sense"),
        DominantImplementation != nullptr ? DominantSense : TEXT("None"));
    Result->SetBoolField(TEXT("verified"), true);
    Result->SetBoolField(TEXT("saved"), bSaved);
    if (CompileReport.IsValid()) { Result->SetObjectField(TEXT("compile"), CompileReport); }
    Result->SetArrayField(TEXT("unsupported_fields"), Unsupported);
    Result->SetArrayField(TEXT("errors"), Errors);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}

// ---------------------------------------------------------------------------
// ai_controller_inspect
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::InspectAIControllerJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Inspection request must be a JSON object.");
        return false;
    }
    FString AssetPath;
    Request->TryGetStringField(TEXT("asset_path"), AssetPath);
    FString ObjectPath, PackagePath;
    if (!ResolveAssetPaths(AssetPath, ObjectPath, PackagePath, OutError)) { return false; }

    UBlueprint* Blueprint = Cast<UBlueprint>(LoadAssetForRead(ObjectPath, OutError));
    if (Blueprint == nullptr)
    {
        if (OutError.IsEmpty())
        {
            OutError = FString::Printf(TEXT("The asset at '%s' is not a Blueprint."), *ObjectPath);
        }
        return false;
    }

    const UPackage* Package = Blueprint->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    TArray<TSharedPtr<FJsonValue>> Warnings;
    TArray<TSharedPtr<FJsonValue>> Unsupported;

    const bool bIsAIController = Blueprint->ParentClass != nullptr
        && Blueprint->ParentClass->IsChildOf(AAIController::StaticClass());
    if (!bIsAIController)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This Blueprint does not derive from AIController. Perception and behavior tree "
                 "wiring are reported anyway, but neither does anything off a controller.")));
    }

    // Perception components, from the SimpleConstructionScript. Only components
    // this Blueprint declares are visible here: one inherited from a native C++
    // parent lives on the parent's CDO and is reported as inherited instead.
    TArray<TSharedPtr<FJsonValue>> PerceptionComponents;
    FString HashLines;
    if (Blueprint->SimpleConstructionScript != nullptr)
    {
        for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node == nullptr) { continue; }
            const UAIPerceptionComponent* Template =
                Cast<UAIPerceptionComponent>(Node->ComponentTemplate);
            if (Template == nullptr) { continue; }
            const FString Name = Node->GetVariableName().ToString();

            TArray<TSharedPtr<FJsonValue>> Senses;
            for (auto It = Template->GetSensesConfigIterator(); It; ++It)
            {
                const UAISenseConfig* Config = *It;
                if (Config == nullptr) { continue; }
                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("sense"), SenseWord(Config));
                Entry->SetStringField(TEXT("class_path"), Config->GetClass()->GetPathName());
                Entry->SetNumberField(TEXT("max_age"), Config->GetMaxAge());
                Entry->SetBoolField(TEXT("starts_enabled"), Config->IsEnabled());
                Entry->SetObjectField(TEXT("properties"),
                    DescribeEditProperties(Config, Name + TEXT("/") + SenseWord(Config), Unsupported));
                Senses.Add(MakeShared<FJsonValueObject>(Entry));
                HashLines += Name + TEXT("|") + Config->GetClass()->GetPathName() + TEXT("\n");
            }

            const UClass* Dominant = Template->GetDominantSense().Get();
            TSharedPtr<FJsonObject> Component = MakeShared<FJsonObject>();
            Component->SetStringField(TEXT("name"), Name);
            Component->SetStringField(TEXT("class_path"), Template->GetClass()->GetPathName());
            Component->SetBoolField(TEXT("inherited"), false);
            Component->SetStringField(TEXT("dominant_sense_class"),
                Dominant != nullptr ? Dominant->GetPathName() : FString());
            Component->SetArrayField(TEXT("senses"), Senses);
            Component->SetNumberField(TEXT("sense_count"), Senses.Num());
            PerceptionComponents.Add(MakeShared<FJsonValueObject>(Component));
        }
    }
    if (PerceptionComponents.Num() == 0)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This Blueprint declares no AIPerceptionComponent. It may still inherit one from "
                 "a native parent class, which this reader does not see: it reads the "
                 "SimpleConstructionScript, not the parent CDO.")));
    }

    // The controller-to-BT wiring. UE4.27 has no data-driven field for it: a
    // controller starts a tree by CALLING AAIController::RunBehaviorTree, so
    // the wiring is a graph node and the honest way to report it is to find
    // the call sites and read the asset each one names. A tree chosen at
    // runtime through a variable is reported with a null asset and named in
    // dynamic_behavior_tree_call_sites, because there is nothing to resolve.
    TArray<TSharedPtr<FJsonValue>> CallSites;
    TArray<TSharedPtr<FJsonValue>> DynamicCallSites;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr) { continue; }
        for (const UEdGraphNode* GraphNode : Graph->Nodes)
        {
            const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(GraphNode);
            if (Call == nullptr
                || Call->FunctionReference.GetMemberName() != FName(TEXT("RunBehaviorTree")))
            {
                continue;
            }
            const UEdGraphPin* AssetPin = Call->FindPin(TEXT("BTAsset"), EGPD_Input);
            const UObject* Asset = AssetPin != nullptr ? AssetPin->DefaultObject : nullptr;
            const bool bWired = AssetPin != nullptr && AssetPin->LinkedTo.Num() > 0;

            TSharedPtr<FJsonObject> Site = MakeShared<FJsonObject>();
            Site->SetStringField(TEXT("graph"), Graph->GetName());
            Site->SetStringField(TEXT("node"), GraphNode->GetName());
            Site->SetStringField(TEXT("node_guid"), GraphNode->NodeGuid.ToString());
            Site->SetStringField(TEXT("behavior_tree_path"),
                Asset != nullptr ? Asset->GetPathName() : FString());
            const UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset);
            Site->SetStringField(TEXT("blackboard_path"),
                Tree != nullptr && Tree->BlackboardAsset != nullptr
                    ? Tree->BlackboardAsset->GetPathName() : FString());
            Site->SetBoolField(TEXT("asset_is_pin_default"), Asset != nullptr && !bWired);
            CallSites.Add(MakeShared<FJsonValueObject>(Site));
            HashLines += Graph->GetName() + TEXT("|RunBehaviorTree|")
                + (Asset != nullptr ? Asset->GetPathName() : TEXT("<dynamic>")) + TEXT("\n");
            if (Asset == nullptr)
            {
                DynamicCallSites.Add(MakeShared<FJsonValueObject>(Site));
            }
        }
    }
    if (CallSites.Num() == 0 && bIsAIController)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("No RunBehaviorTree call site was found in this controller's graphs, so it starts "
                 "no behavior tree. Author one with puerts_blueprint_build: a CallFunction node "
                 "with class AIController, function RunBehaviorTree, and the tree as the BTAsset "
                 "pin default.")));
    }

    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this controller dirtied its package. That is a defect in the inspector, "
                 "not a property of the asset: report it.")));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("parent_class"),
        Blueprint->ParentClass != nullptr ? Blueprint->ParentClass->GetPathName() : FString());
    Result->SetStringField(TEXT("generated_class_path"),
        Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetPathName() : FString());
    Result->SetBoolField(TEXT("is_ai_controller"), bIsAIController);
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    Result->SetStringField(TEXT("identity_kind"), TEXT("derived"));
    Result->SetArrayField(TEXT("perception_components"), PerceptionComponents);
    Result->SetArrayField(TEXT("behavior_tree_call_sites"), CallSites);
    Result->SetArrayField(TEXT("dynamic_behavior_tree_call_sites"), DynamicCallSites);
    Result->SetArrayField(TEXT("unsupported_fields"), Unsupported);
    Result->SetStringField(TEXT("structure_hash_sha1"), HashOf(HashLines));
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}
