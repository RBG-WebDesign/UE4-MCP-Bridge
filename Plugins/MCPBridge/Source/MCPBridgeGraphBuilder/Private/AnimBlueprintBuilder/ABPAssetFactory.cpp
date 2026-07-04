#include "ABPAssetFactory.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Factories/AnimBlueprintFactory.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"

USkeleton* FAnimBPAssetFactory::ResolveSkeleton(const FString& SkeletonPath, FString& OutError)
{
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		OutError = FString::Printf(TEXT("[AnimBPAssetFactory] skeleton not found: %s"), *SkeletonPath);
	}
	return Skeleton;
}

UAnimBlueprint* FAnimBPAssetFactory::Create(
	const FString& PackagePath,
	const FString& AssetName,
	USkeleton* Skeleton,
	FString& OutError)
{
	if (!Skeleton)
	{
		OutError = TEXT("[AnimBPAssetFactory] skeleton is null");
		return nullptr;
	}

	// Check if asset already exists (use AssetRegistry, not FindObject, to catch unloaded assets)
	FString FullPath = PackagePath / AssetName;
	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FString ObjectPath = FullPath + TEXT(".") + AssetName;
	FAssetData ExistingAsset = RegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath));
	if (ExistingAsset.IsValid())
	{
		OutError = FString::Printf(TEXT("[AnimBPAssetFactory] asset already exists: %s"), *FullPath);
		return nullptr;
	}

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;
	Factory->ParentClass = UAnimInstance::StaticClass();

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UAnimBlueprint::StaticClass(), Factory);

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(NewAsset);
	if (!AnimBP)
	{
		OutError = TEXT("[AnimBPAssetFactory] failed to create AnimBlueprint asset");
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(AnimBP);

	UE_LOG(LogTemp, Log, TEXT("[AnimBPAssetFactory] created AnimBP '%s' with skeleton '%s'"),
		*AnimBP->GetName(), *Skeleton->GetName());

	return AnimBP;
}
