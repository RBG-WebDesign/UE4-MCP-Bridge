#include "MCPPuerTSBridgeService.h"

#include "GameFramework/GameModeBase.h"
#include "Engine/World.h"
#include "GameMapsSettings.h"
#include "Json.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

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
    const bool bReadConfig = PersistedConfig.Read(IniPath);
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
