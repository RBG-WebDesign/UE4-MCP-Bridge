#include "WidgetBlueprintFinalizer.h"
#include "WidgetBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "Engine/Blueprint.h"

bool FWidgetBlueprintFinalizer::Finalize(
	UWidgetBlueprint* WidgetBlueprint,
	bool bSave,
	FString& OutError)
{
	if (!WidgetBlueprint)
	{
		OutError = TEXT("[WidgetBuilder] Cannot finalize null WidgetBlueprint");
		return false;
	}

	// Step 1: Mark structurally modified
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	// Step 2: Compile
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);

	// Step 3: Check compile status
	if (WidgetBlueprint->Status != BS_UpToDate)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Blueprint compile failed. Status: %d"), (int32)WidgetBlueprint->Status);
		return false;
	}

	// Step 4: Mark package dirty
	WidgetBlueprint->GetOutermost()->SetDirtyFlag(true);

	// Step 5: Save if requested
	if (bSave)
	{
		UPackage* Package = WidgetBlueprint->GetOutermost();
		FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		bool bSaved = UPackage::SavePackage(Package, WidgetBlueprint, RF_Public | RF_Standalone, *PackageFilename);
		if (!bSaved)
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] Failed to save package to '%s'"), *PackageFilename);
			return false;
		}
		UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] Saved package to '%s'"), *PackageFilename);
	}

	UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] Finalized and compiled '%s'"), *WidgetBlueprint->GetName());
	return true;
}
