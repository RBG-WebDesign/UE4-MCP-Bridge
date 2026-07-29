// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FolderVisibilityLibrary.generated.h"

/**
 * Content Browser folder hiding, display-only.
 *
 * Uses the engine's dormant folder blacklist (IAssetTools::GetFolderBlacklist,
 * consumed by SPathView + SAssetView in 4.27 but shipped with no UI). Hidden
 * folders stay on disk, referenced, and cookable; only browser display changes.
 * The hidden list persists in Config/FolderVisibility.ini and is re-applied at
 * module startup. All entries use one owner name so this feature can never
 * disturb blacklist entries added by other systems.
 */
UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UFolderVisibilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Hide a /Game/... folder (and its subfolders) in the Content Browser.
	    Refuses /Game itself. Returns false for invalid paths. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MCP Bridge|Folder Visibility")
	static bool HideFolder(const FString& FolderPath);

	/** Unhide a folder previously hidden by HideFolder. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MCP Bridge|Folder Visibility")
	static bool ShowFolder(const FString& FolderPath);

	/** Escape hatch: unhide everything this feature hid. Returns the count. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MCP Bridge|Folder Visibility")
	static int32 ShowAllFolders();

	/** Currently hidden folders (from the persisted list). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MCP Bridge|Folder Visibility")
	static TArray<FString> GetHiddenFolders();

	/** Re-applies the persisted list to the live blacklist. Called at module
	    startup; safe to call any time (idempotent). */
	static void ApplyPersistedHiddenFolders();

private:
	static TArray<FString> LoadHiddenFolders();
	static void SaveHiddenFolders(const TArray<FString>& Folders);
	/** Unregister our owner and re-add Folders, then refresh browser views. */
	static void ReapplyBlacklist(const TArray<FString>& Folders);
};
