#include "AutoPIEHelper.h"

#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#endif

void UAutoPIEHelper::StartPIE()
{
#if WITH_EDITOR
	if (!GEditor)
	{
		UE_LOG(LogTemp, Error, TEXT("AutoPIEHelper::StartPIE - GEditor is null"));
		return;
	}

	if (GEditor->PlayWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("AutoPIEHelper::StartPIE - PIE is already running"));
		return;
	}

	// Defer to next tick so the Python handler can return first.
	// Without this, the game thread is blocked and PIE never initializes.
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([]()
	{
		if (!GEditor) return;

		// UE4.27 API: RequestPlaySession with individual parameters.
		// bAtPlayerStart=true uses the PlayerStart in the level.
		// bSimulateInEditor=false starts real PIE (not SIE).
		GEditor->RequestPlaySession(
			true,      // bAtPlayerStart
			nullptr,   // DestinationViewport
			false      // bSimulateInEditor -- false = real PIE with PlayerController
		);

		UE_LOG(LogTemp, Log, TEXT("AutoPIEHelper: PIE requested on deferred tick"));
	}));
#endif
}

void UAutoPIEHelper::StopPIE()
{
#if WITH_EDITOR
	if (!GEditor) return;

	if (!GEditor->PlayWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("AutoPIEHelper::StopPIE - No PIE session running"));
		return;
	}

	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([]()
	{
		if (GEditor)
		{
			GEditor->RequestEndPlayMap();
			UE_LOG(LogTemp, Log, TEXT("AutoPIEHelper: PIE stop requested on deferred tick"));
		}
	}));
#endif
}

bool UAutoPIEHelper::IsPIERunning()
{
#if WITH_EDITOR
	return GEditor && GEditor->PlayWorld != nullptr;
#else
	return false;
#endif
}

bool UAutoPIEHelper::PlayCameraShakeOnPlayer(const FString& ShakeClassPath, float Scale)
{
#if WITH_EDITOR
	if (!GEditor || !GEditor->PlayWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("PlayCameraShakeOnPlayer: No PIE session running"));
		return false;
	}

	// Load the shake class from asset path
	UClass* ShakeClass = LoadClass<UCameraShakeBase>(nullptr, *ShakeClassPath);
	if (!ShakeClass)
	{
		UE_LOG(LogTemp, Error, TEXT("PlayCameraShakeOnPlayer: Could not load shake class '%s'"), *ShakeClassPath);
		return false;
	}

	// Get first player controller from PIE world
	APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("PlayCameraShakeOnPlayer: No PlayerController in PIE world"));
		return false;
	}

	APlayerCameraManager* CamMgr = PC->PlayerCameraManager;
	if (!CamMgr)
	{
		UE_LOG(LogTemp, Error, TEXT("PlayCameraShakeOnPlayer: No PlayerCameraManager"));
		return false;
	}

	// UE4.27: PlayCameraShake(TSubclassOf<UCameraShake>, float Scale)
	CamMgr->StartCameraShake(ShakeClass, Scale);

	UE_LOG(LogTemp, Log, TEXT("PlayCameraShakeOnPlayer: Playing '%s' with scale %.2f"), *ShakeClassPath, Scale);
	return true;
#else
	return false;
#endif
}

void UAutoPIEHelper::PlayCameraShakeByPath(UObject* WorldContextObject, const FString& ShakeClassPath, float Scale)
{
	if (!WorldContextObject)
	{
		UE_LOG(LogTemp, Error, TEXT("PlayCameraShakeByPath: No world context"));
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("PlayCameraShakeByPath: Could not get world"));
		return;
	}

	UClass* ShakeClass = LoadClass<UCameraShakeBase>(nullptr, *ShakeClassPath);
	if (!ShakeClass)
	{
		UE_LOG(LogTemp, Error, TEXT("PlayCameraShakeByPath: Could not load shake class '%s'"), *ShakeClassPath);
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("PlayCameraShakeByPath: No PlayerController"));
		return;
	}

	APlayerCameraManager* CamMgr = PC->PlayerCameraManager;
	if (!CamMgr)
	{
		UE_LOG(LogTemp, Error, TEXT("PlayCameraShakeByPath: No PlayerCameraManager"));
		return;
	}

	CamMgr->StartCameraShake(ShakeClass, Scale);
	UE_LOG(LogTemp, Log, TEXT("PlayCameraShakeByPath: Playing '%s' scale %.2f"), *ShakeClassPath, Scale);
}
