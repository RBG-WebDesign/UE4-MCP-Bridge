// Copyright 2026 RareBird Games. All Rights Reserved.

#include "FolderVisibilityLibrary.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/BlacklistNames.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Settings/ContentBrowserSettings.h"

namespace
{
	const FName FolderVisOwner(TEXT("SinfeldFolderVisibility"));
	const TCHAR* FolderVisSection = TEXT("FolderVisibility");
	const TCHAR* FolderVisKey = TEXT("HiddenFolders");

	FString FolderVisIniPath()
	{
		return FPaths::ProjectConfigDir() / TEXT("FolderVisibility.ini");
	}

	FString NormalizeFolderPath(const FString& In)
	{
		FString Path = In.TrimStartAndEnd();
		while (Path.EndsWith(TEXT("/")))
		{
			Path.LeftChopInline(1);
		}
		return Path;
	}
}

TArray<FString> UFolderVisibilityLibrary::LoadHiddenFolders()
{
	TArray<FString> Out;
	GConfig->GetArray(FolderVisSection, FolderVisKey, Out, FolderVisIniPath());
	return Out;
}

void UFolderVisibilityLibrary::SaveHiddenFolders(const TArray<FString>& Folders)
{
	GConfig->SetArray(FolderVisSection, FolderVisKey, Folders, FolderVisIniPath());
	GConfig->Flush(false, FolderVisIniPath());
}

void UFolderVisibilityLibrary::ReapplyBlacklist(const TArray<FString>& Folders)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	const TSharedRef<FBlacklistPaths>& Blacklist = AssetTools.GetFolderBlacklist();
	// FBlacklistPaths has no per-item removal, only owner unregistration: the
	// persisted list is authoritative, so wipe our owner and re-add.
	Blacklist->UnregisterOwner(FolderVisOwner);
	for (const FString& Folder : Folders)
	{
		Blacklist->AddBlacklistItem(FolderVisOwner, Folder);
	}
	// SPathView/SAssetView do not watch the blacklist delegate. A manual
	// PostEditChange broadcasts OnSettingChanged with NAME_None, which
	// SPathView::HandleSettingChanged treats as "repopulate everything".
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool UFolderVisibilityLibrary::HideFolder(const FString& FolderPath)
{
	const FString Path = NormalizeFolderPath(FolderPath);
	if (!Path.StartsWith(TEXT("/Game/")) || Path == TEXT("/Game"))
	{
		return false; // only project content subfolders; never the root
	}
	TArray<FString> Hidden = LoadHiddenFolders();
	Hidden.AddUnique(Path);
	SaveHiddenFolders(Hidden);
	ReapplyBlacklist(Hidden);
	return true;
}

bool UFolderVisibilityLibrary::ShowFolder(const FString& FolderPath)
{
	const FString Path = NormalizeFolderPath(FolderPath);
	TArray<FString> Hidden = LoadHiddenFolders();
	if (Hidden.Remove(Path) == 0)
	{
		return false;
	}
	SaveHiddenFolders(Hidden);
	ReapplyBlacklist(Hidden);
	return true;
}

int32 UFolderVisibilityLibrary::ShowAllFolders()
{
	TArray<FString> Hidden = LoadHiddenFolders();
	const int32 Count = Hidden.Num();
	Hidden.Reset();
	SaveHiddenFolders(Hidden);
	ReapplyBlacklist(Hidden);
	return Count;
}

TArray<FString> UFolderVisibilityLibrary::GetHiddenFolders()
{
	return LoadHiddenFolders();
}

void UFolderVisibilityLibrary::ApplyPersistedHiddenFolders()
{
	const TArray<FString> Hidden = LoadHiddenFolders();
	if (Hidden.Num() > 0)
	{
		ReapplyBlacklist(Hidden);
	}
}
