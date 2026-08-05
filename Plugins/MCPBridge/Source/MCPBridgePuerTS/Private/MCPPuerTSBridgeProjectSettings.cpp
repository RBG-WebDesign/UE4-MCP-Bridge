#include "MCPPuerTSBridgeService.h"

#include "GameFramework/GameModeBase.h"
#include "Engine/World.h"
#include "GameMapsSettings.h"
#include "HAL/FileManager.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
    /** Accepts "GameMapsSettings", "UGameMapsSettings", or the full
        "/Script/EngineSettings.GameMapsSettings" path. Only already-loaded
        classes resolve, which is the normal case: the editor constructs every
        settings CDO on startup to render the Project Settings window. */
    UClass* ResolveSettingsClass(const FString& Section)
    {
        if (Section.IsEmpty())
        {
            return nullptr;
        }
        if (Section.Contains(TEXT(".")))
        {
            return FindObject<UClass>(nullptr, *Section);
        }
        if (UClass* Direct = FindObject<UClass>(ANY_PACKAGE, *Section))
        {
            return Direct;
        }
        const FString Alternate = Section.StartsWith(TEXT("U"))
            ? Section.RightChop(1)
            : TEXT("U") + Section;
        return FindObject<UClass>(ANY_PACKAGE, *Alternate);
    }

    FString SuggestConfigProperties(UClass* Class, const FString& Requested)
    {
        TArray<FString> Close;
        TArray<FString> Any;
        for (TFieldIterator<FProperty> It(Class); It; ++It)
        {
            if (!It->HasAnyPropertyFlags(CPF_Config))
            {
                continue;
            }
            const FString Name = It->GetName();
            if (Any.Num() < 5)
            {
                Any.Add(Name);
            }
            if (Name.Contains(Requested) || Requested.Contains(Name))
            {
                Close.Add(Name);
                if (Close.Num() >= 5)
                {
                    break;
                }
            }
        }
        const TArray<FString>& Best = Close.Num() > 0 ? Close : Any;
        return Best.Num() > 0 ? FString::Join(Best, TEXT(", ")) : TEXT("(this class exposes no config properties)");
    }

    FString StringifyJsonValue(const TSharedPtr<FJsonValue>& Value)
    {
        if (!Value.IsValid())
        {
            return FString();
        }
        TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
        Wrapper->SetField(TEXT("value"), Value);
        FString Text;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
        FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);
        return Text;
    }
}

bool UMCPPuerTSBridgeService::ConfigureProjectMapsJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError)
{
    TSharedPtr<FJsonObject> Request;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RequestJson), Request)
        || !Request.IsValid())
    {
        OutError = TEXT("Project settings request must be a JSON object.");
        return false;
    }

    FString GameMap;
    FString EditorMap;
    FString GameMode;
    Request->TryGetStringField(TEXT("game_default_map"), GameMap);
    Request->TryGetStringField(TEXT("editor_startup_map"), EditorMap);
    Request->TryGetStringField(TEXT("global_default_game_mode"), GameMode);
    if (GameMap.IsEmpty() && EditorMap.IsEmpty() && GameMode.IsEmpty())
    {
        OutError = TEXT("Provide at least one of: game_default_map, editor_startup_map, global_default_game_mode");
        return false;
    }

    const auto ValidateMap = [&OutError](const FString& Value, const TCHAR* Field) -> bool
    {
        if (Value.IsEmpty()) { return true; }
        FString Filename;
        if (!Value.StartsWith(TEXT("/Game/"))
            || !FPackageName::TryConvertLongPackageNameToFilename(
                Value, Filename, FPackageName::GetMapPackageExtension())
            || !FPaths::FileExists(Filename))
        {
            OutError = FString::Printf(TEXT("%s must name an existing saved /Game map; got '%s'."), Field, *Value);
            return false;
        }
        return true;
    };
    if (!ValidateMap(GameMap, TEXT("game_default_map"))
        || !ValidateMap(EditorMap, TEXT("editor_startup_map")))
    {
        return false;
    }
    if (!GameMode.IsEmpty())
    {
        FSoftClassPath ClassPath(GameMode);
        UClass* Class = ClassPath.TryLoadClass<AGameModeBase>();
        if (Class == nullptr)
        {
            OutError = FString::Printf(
                TEXT("global_default_game_mode must resolve to a GameModeBase class; got '%s'."),
                *GameMode);
            return false;
        }
    }

    UGameMapsSettings* Settings = UGameMapsSettings::GetGameMapsSettings();
    if (Settings == nullptr)
    {
        OutError = TEXT("UGameMapsSettings CDO is unavailable.");
        return false;
    }
    TSharedPtr<FJsonObject> Updated = MakeShared<FJsonObject>();
    const FString IniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
    const bool bIniExisted = FPaths::FileExists(IniPath);
    TArray<uint8> PreviousIniBytes;
    if (bIniExisted && !FFileHelper::LoadFileToArray(PreviousIniBytes, *IniPath))
    {
        OutError = TEXT("Could not snapshot DefaultEngine.ini before changing project settings.");
        return false;
    }
    const FString PreviousGameMap = UGameMapsSettings::GetGameDefaultMap();
    const FSoftObjectPath PreviousEditorMap = Settings->EditorStartupMap;
    const FString PreviousGameMode = UGameMapsSettings::GetGlobalDefaultGameMode();
    if (!GameMap.IsEmpty())
    {
        UGameMapsSettings::SetGameDefaultMap(GameMap);
        Updated->SetStringField(TEXT("GameDefaultMap"), GameMap);
    }
    if (!EditorMap.IsEmpty())
    {
        Settings->EditorStartupMap = FSoftObjectPath(EditorMap);
        Updated->SetStringField(TEXT("EditorStartupMap"), EditorMap);
    }
    if (!GameMode.IsEmpty())
    {
        UGameMapsSettings::SetGlobalDefaultGameMode(GameMode);
        Updated->SetStringField(TEXT("GlobalDefaultGameMode"), GameMode);
    }
    Settings->UpdateDefaultConfigFile();
    FConfigFile PersistedConfig;
    const bool bReadConfig = FPaths::FileExists(IniPath);
    PersistedConfig.Read(IniPath);
    FString PersistedGameMap;
    FString PersistedEditorMap;
    FString PersistedGameMode;
    const TCHAR* Section = TEXT("/Script/EngineSettings.GameMapsSettings");
    const bool bMatches = bReadConfig
        && (GameMap.IsEmpty() || (PersistedConfig.GetString(Section, TEXT("GameDefaultMap"), PersistedGameMap) && PersistedGameMap == GameMap))
        && (EditorMap.IsEmpty() || (PersistedConfig.GetString(Section, TEXT("EditorStartupMap"), PersistedEditorMap) && PersistedEditorMap == EditorMap))
        && (GameMode.IsEmpty() || (PersistedConfig.GetString(Section, TEXT("GlobalDefaultGameMode"), PersistedGameMode) && PersistedGameMode == GameMode));
    if (!bMatches)
    {
        UGameMapsSettings::SetGameDefaultMap(PreviousGameMap);
        Settings->EditorStartupMap = PreviousEditorMap;
        UGameMapsSettings::SetGlobalDefaultGameMode(PreviousGameMode);
        const bool bRestored = bIniExisted
            ? FFileHelper::SaveArrayToFile(PreviousIniBytes, *IniPath)
            : IFileManager::Get().Delete(*IniPath, false, true, true);
        TArray<uint8> RestoredBytes;
        const bool bRestoreVerified = bRestored && (bIniExisted
            ? (FFileHelper::LoadFileToArray(RestoredBytes, *IniPath) && RestoredBytes == PreviousIniBytes)
            : !FPaths::FileExists(IniPath));
        OutError = bRestoreVerified
            ? TEXT("DefaultEngine.ini did not contain the requested values; previous settings restored.")
            : TEXT("DefaultEngine.ini verification failed and its previous bytes could not be restored.");
        return false;
    }
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("updated"), Updated);
    Result->SetStringField(TEXT("ini"), IniPath);
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
    FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
    return true;
}

bool UMCPPuerTSBridgeService::PatchProjectSettingsJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError)
{
    TSharedPtr<FJsonObject> Request;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RequestJson), Request)
        || !Request.IsValid())
    {
        OutError = TEXT("Project settings request must be a JSON object.");
        return false;
    }

    FString SectionName;
    Request->TryGetStringField(TEXT("section"), SectionName);
    const TSharedPtr<FJsonObject>* Properties = nullptr;
    if (!Request->TryGetObjectField(TEXT("properties"), Properties)
        || (*Properties)->Values.Num() == 0)
    {
        OutError = TEXT("properties must be a non-empty object of {propertyName: value}.");
        return false;
    }
    const bool bPlanOnly = Request->HasField(TEXT("plan_only")) && Request->GetBoolField(TEXT("plan_only"));

    UClass* Class = ResolveSettingsClass(SectionName);
    if (Class == nullptr)
    {
        OutError = FString::Printf(
            TEXT("section '%s' does not name a loaded UClass. Use the settings class behind the ")
            TEXT("Project Settings page, such as GameMapsSettings or ProjectPackagingSettings."),
            *SectionName);
        return false;
    }
    UObject* Settings = Class->GetDefaultObject();
    if (Settings == nullptr)
    {
        OutError = FString::Printf(TEXT("%s has no class default object."), *Class->GetName());
        return false;
    }

    // Resolve every property before touching anything. A property without
    // CPF_Config would set in memory, report success, and silently vanish on
    // the next editor start, which is the failure this check exists to refuse.
    struct FPendingWrite
    {
        FProperty* Property = nullptr;
        FString Name;
        TSharedPtr<FJsonValue> Requested;
        TSharedPtr<FJsonValue> Previous;
    };
    TArray<FPendingWrite> Pending;
    TArray<FString> Unchanged;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
    {
        FProperty* Property = FindFProperty<FProperty>(Class, *Pair.Key);
        if (Property == nullptr)
        {
            OutError = FString::Printf(
                TEXT("%s has no reflected property '%s'. Closest config properties: %s"),
                *Class->GetName(), *Pair.Key, *SuggestConfigProperties(Class, Pair.Key));
            return false;
        }
        if (!Property->HasAnyPropertyFlags(CPF_Config))
        {
            OutError = FString::Printf(
                TEXT("%s.%s is not a config property, so it would change in memory and never reach the ini. Refused."),
                *Class->GetName(), *Pair.Key);
            return false;
        }
        TSharedPtr<FJsonValue> Current = FJsonObjectConverter::UPropertyToJsonValue(
            Property, Property->ContainerPtrToValuePtr<void>(Settings));
        if (!Current.IsValid())
        {
            OutError = FString::Printf(TEXT("%s.%s could not be serialized for rollback."), *Class->GetName(), *Pair.Key);
            return false;
        }
        // Convergent: a value already equal to the request is left alone, so a
        // rerun rewrites nothing and the ini keeps its current timestamp.
        if (StringifyJsonValue(Current) == StringifyJsonValue(Pair.Value))
        {
            Unchanged.Add(Pair.Key);
            continue;
        }
        Pending.Add(FPendingWrite{ Property, Pair.Key, Pair.Value, Current });
    }

    const FString IniPath = Settings->GetDefaultConfigFilename();
    const FString ConfigSection = Class->GetPathName();

    if (bPlanOnly || Pending.Num() == 0)
    {
        TArray<TSharedPtr<FJsonValue>> ToApply;
        for (const FPendingWrite& Write : Pending)
        {
            ToApply.Add(MakeShared<FJsonValueString>(Write.Name));
        }
        TArray<TSharedPtr<FJsonValue>> Same;
        for (const FString& Name : Unchanged)
        {
            Same.Add(MakeShared<FJsonValueString>(Name));
        }
        TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
        Plan->SetStringField(TEXT("section"), ConfigSection);
        Plan->SetStringField(TEXT("ini"), IniPath);
        Plan->SetArrayField(TEXT("properties_to_apply"), ToApply);
        Plan->SetArrayField(TEXT("unchanged_properties"), Same);
        Plan->SetNumberField(TEXT("expected_change_count"), Pending.Num());
        Plan->SetBoolField(TEXT("applied"), false);
        TSharedRef<TJsonWriter<>> PlanWriter = TJsonWriterFactory<>::Create(&OutResultJson);
        FJsonSerializer::Serialize(Plan.ToSharedRef(), PlanWriter);
        return true;
    }

    const bool bIniExisted = FPaths::FileExists(IniPath);
    TArray<uint8> PreviousIniBytes;
    if (bIniExisted && !FFileHelper::LoadFileToArray(PreviousIniBytes, *IniPath))
    {
        OutError = FString::Printf(TEXT("Could not snapshot %s before changing project settings."), *IniPath);
        return false;
    }

    const auto RestoreEverything = [&]() -> bool
    {
        for (const FPendingWrite& Write : Pending)
        {
            FJsonObjectConverter::JsonValueToUProperty(
                Write.Previous, Write.Property, Write.Property->ContainerPtrToValuePtr<void>(Settings));
        }
        const bool bRestored = bIniExisted
            ? FFileHelper::SaveArrayToFile(PreviousIniBytes, *IniPath)
            : IFileManager::Get().Delete(*IniPath, false, true, true);
        TArray<uint8> RestoredBytes;
        return bRestored && (bIniExisted
            ? (FFileHelper::LoadFileToArray(RestoredBytes, *IniPath) && RestoredBytes == PreviousIniBytes)
            : !FPaths::FileExists(IniPath));
    };

    for (const FPendingWrite& Write : Pending)
    {
        if (!FJsonObjectConverter::JsonValueToUProperty(
                Write.Requested, Write.Property, Write.Property->ContainerPtrToValuePtr<void>(Settings)))
        {
            const bool bRolledBack = RestoreEverything();
            OutError = FString::Printf(
                TEXT("%s.%s rejected the supplied value for its reflected type. %s"),
                *Class->GetName(), *Write.Name,
                bRolledBack
                    ? TEXT("Every earlier property in this request was rolled back.")
                    : TEXT("Rollback of the earlier properties FAILED; inspect the ini before continuing."));
            return false;
        }
        FPropertyChangedEvent ChangedEvent(Write.Property, EPropertyChangeType::ValueSet);
        Settings->PostEditChangeProperty(ChangedEvent);
    }

    Settings->UpdateDefaultConfigFile();

    // Verify against the file on disk, not against the object we just wrote.
    FConfigFile Persisted;
    Persisted.Read(IniPath);
    const FConfigSection* Written = Persisted.Find(ConfigSection);
    TArray<TSharedPtr<FJsonValue>> Applied;
    TArray<TSharedPtr<FJsonValue>> Unverified;
    for (const FPendingWrite& Write : Pending)
    {
        const bool bContainer = Write.Property->IsA<FArrayProperty>()
            || Write.Property->IsA<FMapProperty>()
            || Write.Property->IsA<FSetProperty>();
        if (bContainer)
        {
            // ponytail: container properties are confirmed present in the ini
            // section but not compared element by element, because the default
            // ini array syntax (+Key=) is not a straight string match. Upgrade
            // to an element-wise compare if a container setting ever silently
            // half-writes.
            if (Written == nullptr || Written->Find(FName(*Write.Name)) == nullptr)
            {
                const bool bRolledBack = RestoreEverything();
                OutError = FString::Printf(
                    TEXT("%s did not persist %s.%s. %s"), *IniPath, *Class->GetName(), *Write.Name,
                    bRolledBack ? TEXT("Previous settings restored.") : TEXT("Restore FAILED."));
                return false;
            }
            Unverified.Add(MakeShared<FJsonValueString>(Write.Name));
            Applied.Add(MakeShared<FJsonValueString>(Write.Name));
            continue;
        }

        FString Expected;
        Write.Property->ExportTextItem(
            Expected, Write.Property->ContainerPtrToValuePtr<void>(Settings), nullptr, Settings, PPF_None);
        FString PersistedValue;
        if (!Persisted.GetString(*ConfigSection, *Write.Name, PersistedValue)
            || PersistedValue.TrimQuotes() != Expected.TrimQuotes())
        {
            const bool bRolledBack = RestoreEverything();
            OutError = FString::Printf(
                TEXT("%s holds '%s' for %s.%s but '%s' was requested. %s"),
                *IniPath, *PersistedValue, *Class->GetName(), *Write.Name, *Expected,
                bRolledBack
                    ? TEXT("Previous settings restored.")
                    : TEXT("Restore FAILED; inspect the ini before continuing."));
            return false;
        }
        Applied.Add(MakeShared<FJsonValueString>(Write.Name));
    }

    TArray<TSharedPtr<FJsonValue>> Same;
    for (const FString& Name : Unchanged)
    {
        Same.Add(MakeShared<FJsonValueString>(Name));
    }
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("section"), ConfigSection);
    Result->SetStringField(TEXT("ini"), IniPath);
    Result->SetArrayField(TEXT("applied_properties"), Applied);
    Result->SetArrayField(TEXT("unchanged_properties"), Same);
    Result->SetArrayField(TEXT("persisted_but_not_value_compared"), Unverified);
    Result->SetBoolField(TEXT("applied"), true);
    TSharedRef<TJsonWriter<>> ResultWriter = TJsonWriterFactory<>::Create(&OutResultJson);
    FJsonSerializer::Serialize(Result.ToSharedRef(), ResultWriter);
    return true;
}
