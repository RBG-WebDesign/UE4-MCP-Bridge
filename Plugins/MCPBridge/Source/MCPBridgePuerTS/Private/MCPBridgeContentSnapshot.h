// Copyright 2026 RareBird Games. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "HAL/FileManager.h"
#include "Json.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "PackageTools.h"
#include "UObject/Package.h"

/**
 * The other half of FBridgeAssetRollback: a restore boundary for a command that
 * MUTATES an asset which already existed.
 *
 * FBridgeAssetRollback can delete an asset the command created. That covers
 * every create-only builder and nothing else. A command that edits an existing
 * asset needs the previous CONTENTS back, and UE4.27 gives it no undo route:
 * FKismetEditorUtilities::CompileBlueprint runs inside every Blueprint-derived
 * builder, a cancelled transaction cannot un-run a compile, and finding 0r
 * already established that UObject::Modify silently skips a class default
 * object so the values a caller cares about most were never in the undo buffer
 * to begin with. That is why anim_blueprint_build shipped create-only, and why
 * lane W's AnimGraph clear pass made the FAILURE case worse rather than better:
 * a failed rerun used to leave a duplicated AnimBlueprint and now leaves an
 * emptied one.
 *
 * THE SNAPSHOT IS THE FILE ON DISK. That is the whole idea, and it is the
 * reason this is thirty lines rather than three hundred.
 *
 * The obvious alternative was an in-memory copy: duplicate the UBlueprint into
 * the transient package before mutating and put it back with
 * FKismetEditorUtilities::ReplaceBlueprint on failure, which is what the engine's
 * own Blueprint merge tool does (Developer/Merge/Private/SBlueprintMerge.cpp:337-356
 * and :485). It was rejected after reading what it costs:
 *
 *   - ReplaceBlueprint duplicates through UBlueprint::PostDuplicate, and
 *     FBlueprintEditorUtils::PostDuplicateBlueprint generates a NEW Blueprint
 *     guid, a new VarGuid for every variable and a new NodeGuid for every node,
 *     and builds a fresh generated class WITHOUT carrying the old CDO's property
 *     values across (BlueprintEditorUtils.cpp, the PostDuplicateBlueprint body).
 *     So the restore silently drops every variable default and reopens finding
 *     0r from the other end.
 *   - The transient duplicate has to survive the compile it is protecting
 *     against, and FBlueprintCompilationManager collects garbage. An unrooted
 *     snapshot is a dangling pointer waiting for a slow build.
 *   - After the mutating compile reinstances the class, the snapshot's
 *     GeneratedClass pointer refers to a trashed REINST_ class, and
 *     PostDuplicateBlueprint dereferences exactly that pointer behind a
 *     check(OldCDO != nullptr).
 *
 * The file on disk has none of those problems. It is byte-exact, it holds the
 * generated class and its CDO because both are serialized into the package, and
 * restoring it is UPackageTools::ReloadPackages, which is the same operation the
 * Content Browser's Reload performs and which the engine already wires to
 * Blueprint reinstancing through UPackageTools::HandlePackageReloaded.
 *
 * The price is one precondition, and it is stated rather than assumed: the
 * asset must be SAVED and CLEAN before the mutation. Capture() refuses
 * otherwise, and refusing is correct - a dirty asset has in-memory edits no
 * on-disk snapshot can represent, and a command that pretended otherwise would
 * quietly discard the user's unsaved work on its failure path.
 *
 * The second half of the contract is the caller's: a command using this must
 * not write to disk until it has verified its result. As long as that holds,
 * the on-disk asset is intact at every failure exit, so even a Restore() that
 * fails outright leaves a recoverable asset - the worst case is a wrong object
 * in memory that closing the editor fixes. That is the difference from finding
 * 0t's emptied AnimBlueprint, which was written out before anyone noticed.
 *
 * Usage: Capture() before the first mutation, then Restore() on the failure
 * path AFTER the transaction is cancelled. Restore() reports what it did;
 * whether the asset actually came back is decided by the caller re-reading it
 * with an independent inspector, never by trusting this class. Finding 0r
 * settled that rule and it applies here unchanged.
 */
class FBridgeContentSnapshot
{
public:
    /**
     * Record the on-disk state of one package, and refuse if it cannot serve as
     * a restore source. Returns false with a caller-facing reason.
     */
    bool Capture(const FString& InPackagePath, FString& OutRefusal)
    {
        PackagePath = InPackagePath;
        bCaptured = false;

        if (!FPackageName::DoesPackageExist(PackagePath, nullptr, &Filename))
        {
            OutRefusal = FString::Printf(
                TEXT("'%s' has no file on disk, so there is nothing to restore if this "
                     "operation fails. Save the asset first."), *PackagePath);
            return false;
        }

        UPackage* Package = FindPackage(nullptr, *PackagePath);
        if (Package != nullptr && Package->IsDirty())
        {
            OutRefusal = FString::Printf(
                TEXT("'%s' has unsaved changes. This operation can only restore the asset "
                     "as it exists on disk, so it refuses rather than risk discarding "
                     "in-memory edits. Save the asset and run this again."), *PackagePath);
            return false;
        }
        if (Package != nullptr && Package->HasAnyPackageFlags(PKG_InMemoryOnly))
        {
            OutRefusal = FString::Printf(
                TEXT("'%s' is an in-memory-only package and cannot be reloaded, so it "
                     "cannot be restored."), *PackagePath);
            return false;
        }

        FileHashBefore = HashFile(Filename);
        if (FileHashBefore.IsEmpty())
        {
            OutRefusal = FString::Printf(
                TEXT("'%s' could not be read from disk, so it cannot serve as a restore "
                     "source."), *Filename);
            return false;
        }

        bCaptured = true;
        return true;
    }

    /**
     * Put the on-disk contents back into memory. Call only AFTER the caller has
     * cancelled its transaction.
     *
     * Reloading destroys the old UObjects, so every pointer the caller held into
     * this package is dead when this returns. Re-resolve by object path.
     */
    bool Restore(FString& OutError)
    {
        if (!bCaptured)
        {
            OutError = TEXT("No snapshot was captured, so nothing can be restored.");
            return false;
        }
        if (bRestored) { return bRestoreSucceeded; }
        bRestored = true;
        bAttempted = true;

        // The snapshot source is the file. If something wrote to it between
        // Capture and here, the restore would install content nobody asked for,
        // which is worse than declining. This is the one check that has to
        // happen before the reload rather than after.
        FileHashAfter = HashFile(Filename);
        bSourceIntact = !FileHashAfter.IsEmpty() && FileHashAfter == FileHashBefore;
        if (!bSourceIntact)
        {
            OutError = FString::Printf(
                TEXT("The file that was going to be restored changed during the operation "
                     "(%s before, %s now), so it was NOT reloaded. Treat '%s' as damaged and "
                     "recover it from source control."),
                *FileHashBefore, *FileHashAfter, *PackagePath);
            RestoreErrors.Add(OutError);
            return false;
        }

        UPackage* Package = FindPackage(nullptr, *PackagePath);
        if (Package == nullptr)
        {
            // Nothing is loaded, so the on-disk asset is already the only truth.
            // Not an error: the next load reads the snapshot.
            bRestoreSucceeded = true;
            bReloaded = false;
            return true;
        }

        FText ReloadError;
        // AssumePositive, not Interactive. The dirty flag is guaranteed set here
        // (the caller just mutated the asset), and Interactive would put a modal
        // dialog in front of a headless command. AssumePositive clears the flag
        // and reloads, which is exactly the intent: discard the in-memory
        // mutation, take the file.
        const TArray<UPackage*> ToReload = { Package };
        bReloaded = UPackageTools::ReloadPackages(
            ToReload, ReloadError, UPackageTools::EReloadPackagesInteractionMode::AssumePositive);

        if (!ReloadError.IsEmpty())
        {
            RestoreErrors.Add(ReloadError.ToString());
        }
        if (!bReloaded)
        {
            OutError = FString::Printf(
                TEXT("Reloading '%s' from disk failed: %s. The file itself is unchanged, so "
                     "the asset is recoverable by closing and reopening the editor."),
                *PackagePath, ReloadError.IsEmpty() ? TEXT("no reason reported") : *ReloadError.ToString());
            return false;
        }

        // The reload destroyed the objects the just-cancelled transaction's undo
        // records point at. FBlueprintUnloader resets the buffer for exactly this
        // reason when it unloads a Blueprint (Kismet2.cpp, UnloadBlueprint), and
        // package reload does not do it for us. Unconditionally rather than
        // behind IsReferencedByUndoBuffer: a command that reached this line
        // mutated the asset inside a transaction, so the answer is always yes,
        // and the check costs a full reference walk to learn that.
        if (GEditor != nullptr && GEditor->Trans != nullptr)
        {
            GEditor->Trans->Reset(NSLOCTEXT("MCPBridge", "BridgeContentRestored",
                "Restored an asset from disk after a failed edit"));
            bUndoHistoryCleared = true;
        }

        // Verify what can be verified from here. Whether the CONTENT came back
        // is the caller's read-back to decide, not this class's claim.
        UPackage* Reloaded = FindPackage(nullptr, *PackagePath);
        if (Reloaded != nullptr && Reloaded->IsDirty())
        {
            RestoreErrors.Add(FString::Printf(
                TEXT("'%s' is still dirty after being restored from disk."), *PackagePath));
        }
        if (HashFile(Filename) != FileHashBefore)
        {
            RestoreErrors.Add(FString::Printf(
                TEXT("Restoring '%s' modified its file, which it must never do."), *Filename));
        }

        bRestoreSucceeded = RestoreErrors.Num() == 0;
        if (!bRestoreSucceeded)
        {
            OutError = FString::Join(RestoreErrors, TEXT(" "));
        }
        return bRestoreSucceeded;
    }

    /** Structured evidence, in the shape FBridgeAssetRollback::BuildEvidence uses. */
    TSharedPtr<FJsonObject> BuildEvidence() const
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetBoolField(TEXT("snapshot_captured"), bCaptured);
        Out->SetStringField(TEXT("snapshot_kind"), TEXT("package_file"));
        Out->SetStringField(TEXT("snapshot_file"), Filename);
        Out->SetStringField(TEXT("snapshot_file_hash"), FileHashBefore);
        Out->SetBoolField(TEXT("restore_attempted"), bAttempted);
        Out->SetBoolField(TEXT("restore_source_intact"), bSourceIntact);
        Out->SetBoolField(TEXT("package_reloaded"), bReloaded);
        Out->SetBoolField(TEXT("undo_history_cleared"), bUndoHistoryCleared);
        // Deliberately NOT called restore_succeeded. This says the mechanism ran
        // without complaining; only a read-back through an independent inspector
        // can say the content came back, and the caller reports that separately.
        Out->SetBoolField(TEXT("restore_mechanism_ok"), bAttempted && bRestoreSucceeded);
        TArray<TSharedPtr<FJsonValue>> Errors;
        for (const FString& Error : RestoreErrors) { Errors.Add(MakeShared<FJsonValueString>(Error)); }
        Out->SetArrayField(TEXT("restore_errors"), Errors);
        return Out;
    }

    bool WasCaptured() const { return bCaptured; }

private:
    static FString HashFile(const FString& InFilename)
    {
        if (InFilename.IsEmpty() || !IFileManager::Get().FileExists(*InFilename))
        {
            return FString();
        }
        const FMD5Hash Hash = FMD5Hash::HashFile(*InFilename);
        return Hash.IsValid() ? LexToString(Hash) : FString();
    }

    FString PackagePath;
    FString Filename;
    FString FileHashBefore;
    FString FileHashAfter;
    TArray<FString> RestoreErrors;
    bool bCaptured = false;
    bool bAttempted = false;
    bool bRestored = false;
    bool bRestoreSucceeded = false;
    bool bSourceIntact = false;
    bool bReloaded = false;
    bool bUndoHistoryCleared = false;
};
