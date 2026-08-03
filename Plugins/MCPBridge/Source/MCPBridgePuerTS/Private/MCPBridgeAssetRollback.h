// Copyright 2026 RareBird Games. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetRegistryModule.h"
#include "Json.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#endif

/**
 * Rollback boundary for a command that creates assets before it can finish
 * validating them.
 *
 * The problem it exists for: a builder that must create a package and an asset
 * in order to run the engine validation that decides whether the request was
 * legal at all. When that validation then fails, the half-built asset is
 * already in the Asset Registry and its package is already dirty, so the
 * editor's save-on-exit flow offers it in the Save Content prompt and writes it
 * to disk during close - even when the user answers "Don't Save", because the
 * unattended save flow does not consult that answer. See
 * docs/CAPABILITY_FINDINGS.md defect 0c.
 *
 * Usage: Track() every asset as it is created, then either Commit() on success
 * or Rollback() on failure. Rollback restores the world to the state Snapshot()
 * observed, and reports what it did as structured evidence rather than
 * asserting that it worked.
 *
 * Deliberately not an RAII guard: the failure decision here is "the errors
 * array is non-empty", not an early return or an exception, so the call sites
 * are explicit.
 */
class FBridgeAssetRollback
{
public:
    /** Record the pre-command state of one package path. Call before creating anything. */
    void Snapshot(const FString& PackagePath)
    {
        FPackageState State;
        State.PackagePath = PackagePath;
        State.Filename = PackageFilename(PackagePath);
        State.bFileExisted = !State.Filename.IsEmpty()
            && IFileManager::Get().FileExists(*State.Filename);
        const UPackage* Existing = FindPackage(nullptr, *PackagePath);
        State.bPackageExisted = Existing != nullptr;
        State.bWasDirty = Existing != nullptr && Existing->IsDirty();
        Snapshots.Add(State);
    }

    /** Record an asset the command just created, so Rollback can remove it.
        Only pass assets that were newly created; a pre-existing asset must
        never be deleted by a failed build. */
    void TrackCreated(UObject* Asset)
    {
        if (Asset != nullptr)
        {
            CreatedAssets.Add(Asset);
            CreatedPaths.Add(Asset->GetPathName());
        }
    }

    /** The request succeeded: keep everything. */
    void Commit() { bCommitted = true; }

    /**
     * Undo every creation this boundary tracked and restore the recorded dirty
     * state. Safe to call more than once.
     *
     * Order matters. The transaction is cancelled by the caller BEFORE this
     * runs, so that its undo records - which reference objects this function is
     * about to destroy - are replayed against live objects rather than dead
     * ones.
     */
    void Rollback()
    {
        if (bCommitted || bRolledBack) { return; }
        bRolledBack = true;
        bAttempted = true;

        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

        for (const TWeakObjectPtr<UObject>& Weak : CreatedAssets)
        {
            UObject* Asset = Weak.Get();
            if (Asset == nullptr)
            {
                continue;
            }
            const FString Path = Asset->GetPathName();

            // Tell the Asset Registry first, while the object is still whole:
            // AssetDeleted reads the asset to build the FAssetData it removes.
            AssetRegistryModule.Get().AssetDeleted(Asset);

            // Strip the flags that make an object worth saving, then move it out
            // of its package entirely so no save path can reach it by name.
            Asset->ClearFlags(RF_Public | RF_Standalone);
            Asset->SetFlags(RF_Transient);
            Asset->Rename(
                nullptr,
                GetTransientPackage(),
                REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
            Asset->MarkPendingKill();

            RemovedPaths.Add(Path);
        }
        CreatedAssets.Reset();

        for (const FPackageState& State : Snapshots)
        {
            UPackage* Package = FindPackage(nullptr, *State.PackagePath);
            if (Package != nullptr)
            {
                // A package the command created is left empty and clean rather
                // than destroyed: CreatePackage would hand the same object back
                // on a retry anyway, and an undirty package is invisible to the
                // save-on-exit flow, which is the property that matters.
                Package->SetDirtyFlag(State.bWasDirty);
                if (!State.bPackageExisted)
                {
                    Package->ClearFlags(RF_Public | RF_Standalone);
                }
            }

            // Anything written before the failure is removed, but only if this
            // command is what put it there.
            if (!State.bFileExisted && !State.Filename.IsEmpty()
                && IFileManager::Get().FileExists(*State.Filename))
            {
                FilesCreated.Add(State.Filename);
                if (IFileManager::Get().Delete(*State.Filename, false, true, true))
                {
                    FilesRemoved.Add(State.Filename);
                }
                else
                {
                    CleanupErrors.Add(FString::Printf(
                        TEXT("Could not delete the file written before failure: %s"), *State.Filename));
                }
            }
        }

        // Verify rather than assume. Anything still dirty, still on disk, or
        // still in the registry is a cleanup error, not a silent pass.
        for (const FPackageState& State : Snapshots)
        {
            const UPackage* Package = FindPackage(nullptr, *State.PackagePath);
            if (Package != nullptr && Package->IsDirty() && !State.bWasDirty)
            {
                CleanupErrors.Add(FString::Printf(
                    TEXT("Package is still dirty after rollback: %s"), *State.PackagePath));
            }
            if (!State.bFileExisted && !State.Filename.IsEmpty()
                && IFileManager::Get().FileExists(*State.Filename))
            {
                CleanupErrors.Add(FString::Printf(
                    TEXT("File still exists after rollback: %s"), *State.Filename));
            }
        }
        for (const FString& Path : RemovedPaths)
        {
            if (AssetRegistryModule.Get().GetAssetByObjectPath(FName(*Path)).IsValid())
            {
                CleanupErrors.Add(FString::Printf(
                    TEXT("Asset is still in the Asset Registry after rollback: %s"), *Path));
            }
        }
    }

    /** Packages that are dirty right now, among the ones tracked. */
    TArray<FString> DirtyPackages() const
    {
        TArray<FString> Dirty;
        for (const FPackageState& State : Snapshots)
        {
            const UPackage* Package = FindPackage(nullptr, *State.PackagePath);
            if (Package != nullptr && Package->IsDirty())
            {
                Dirty.Add(State.PackagePath);
            }
        }
        return Dirty;
    }

    /**
     * Source-control status of the tracked files, as a flat list of
     * "<file>: <status>". Reported rather than acted on: this boundary must
     * never add or check out anything, and this is how a caller can see that it
     * did not.
     */
    TArray<FString> SourceControlState() const
    {
        TArray<FString> Out;
#if WITH_EDITOR
        ISourceControlModule& Module = ISourceControlModule::Get();
        if (!Module.IsEnabled())
        {
            Out.Add(TEXT("source control disabled"));
            return Out;
        }
        ISourceControlProvider& Provider = Module.GetProvider();
        for (const FPackageState& State : Snapshots)
        {
            if (State.Filename.IsEmpty()) { continue; }
            const FString Absolute = FPaths::ConvertRelativePathToFull(State.Filename);
            // Cached state only. Asking the server would be a source-control
            // operation, which is exactly what this must not perform.
            const FSourceControlStatePtr FileState =
                Provider.GetState(Absolute, EStateCacheUsage::Use);
            const TCHAR* Status = TEXT("not tracked");
            if (FileState.IsValid())
            {
                if (FileState->IsAdded()) { Status = TEXT("opened for add"); }
                else if (FileState->IsCheckedOut()) { Status = TEXT("checked out"); }
                else if (FileState->IsSourceControlled()) { Status = TEXT("controlled, not opened"); }
            }
            Out.Add(FString::Printf(TEXT("%s: %s"), *State.PackagePath, Status));
        }
#else
        Out.Add(TEXT("source control unavailable outside the editor"));
#endif
        return Out;
    }

    /** The structured cleanup evidence the caller returns to the client. */
    TSharedPtr<FJsonObject> BuildEvidence(
        const TArray<FString>& DirtyBefore,
        const TArray<FString>& SourceControlBefore) const
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetBoolField(TEXT("rollback_attempted"), bAttempted);
        Out->SetBoolField(TEXT("rollback_succeeded"), bAttempted && CleanupErrors.Num() == 0);
        Out->SetArrayField(TEXT("created_assets"), ToJson(CreatedPaths));
        Out->SetArrayField(TEXT("removed_assets"), ToJson(RemovedPaths));
        Out->SetArrayField(TEXT("dirty_packages_before"), ToJson(DirtyBefore));
        Out->SetArrayField(TEXT("dirty_packages_after"), ToJson(DirtyPackages()));
        Out->SetArrayField(TEXT("files_created"), ToJson(FilesCreated));
        Out->SetArrayField(TEXT("files_removed"), ToJson(FilesRemoved));
        Out->SetArrayField(TEXT("source_control_before"), ToJson(SourceControlBefore));
        Out->SetArrayField(TEXT("source_control_after"), ToJson(SourceControlState()));
        Out->SetArrayField(TEXT("cleanup_errors"), ToJson(CleanupErrors));
        return Out;
    }

    static FString PackageFilename(const FString& PackagePath)
    {
        // A package that is already on disk answers with its real extension.
        // This matters for levels: a map is a .umap, and appending
        // GetAssetPackageExtension() to one produces a .uasset path that never
        // exists, so bFileExisted reads false for a file that is right there
        // and the source-control status reports "not tracked" for a file that
        // is. Asset builders are unaffected: they Snapshot() before creating
        // anything, so the package does not exist yet and the fallback below is
        // what runs, exactly as before.
        FString Existing;
        if (FPackageName::DoesPackageExist(PackagePath, nullptr, &Existing))
        {
            return Existing;
        }
        FString Filename;
        if (!FPackageName::TryConvertLongPackageNameToFilename(
                PackagePath, Filename, FPackageName::GetAssetPackageExtension()))
        {
            return FString();
        }
        return Filename;
    }

private:
    struct FPackageState
    {
        FString PackagePath;
        FString Filename;
        bool bPackageExisted = false;
        bool bWasDirty = false;
        bool bFileExisted = false;
    };

    static TArray<TSharedPtr<FJsonValue>> ToJson(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Values.Num());
        for (const FString& Value : Values) { Out.Add(MakeShared<FJsonValueString>(Value)); }
        return Out;
    }

    TArray<FPackageState> Snapshots;
    TArray<TWeakObjectPtr<UObject>> CreatedAssets;
    TArray<FString> CreatedPaths;
    TArray<FString> RemovedPaths;
    TArray<FString> FilesCreated;
    TArray<FString> FilesRemoved;
    TArray<FString> CleanupErrors;
    bool bCommitted = false;
    bool bRolledBack = false;
    bool bAttempted = false;
};
