#include "ShakeTriggerActor.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"

AShakeTriggerActor::AShakeTriggerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	RootComponent = Trigger;

	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetBoxExtent(FVector(200.f, 200.f, 200.f));
}

void AShakeTriggerActor::BeginPlay()
{
	Super::BeginPlay();

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AShakeTriggerActor::OnOverlapBegin);

	UE_LOG(LogTemp, Log, TEXT("ShakeTriggerActor: Ready. ShakeClass=%s Scale=%.2f"),
		ShakeClass ? *ShakeClass->GetName() : TEXT("NONE"), ShakeScale);
}

void AShakeTriggerActor::OnOverlapBegin(UPrimitiveComponent* OverlappedComponent,
                                        AActor* OtherActor,
                                        UPrimitiveComponent* OtherComp,
                                        int32 OtherBodyIndex,
                                        bool bFromSweep,
                                        const FHitResult& SweepResult)
{
	// Only trigger for pawns (the player character)
	if (!OtherActor || !OtherActor->IsA(APawn::StaticClass()))
	{
		return;
	}

	if (!ShakeClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShakeTriggerActor: Overlap detected but no ShakeClass assigned"));
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC || !PC->PlayerCameraManager)
	{
		UE_LOG(LogTemp, Error, TEXT("ShakeTriggerActor: No PlayerController or CameraManager"));
		return;
	}

	PC->PlayerCameraManager->StartCameraShake(ShakeClass, ShakeScale);

	UE_LOG(LogTemp, Log, TEXT("ShakeTriggerActor: Playing shake '%s' scale %.2f"),
		*ShakeClass->GetName(), ShakeScale);
}
