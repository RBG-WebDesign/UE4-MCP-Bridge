#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "Camera/CameraShakeBase.h"
#include "ShakeTriggerActor.generated.h"

/**
 * Runtime actor that plays a camera shake when the player overlaps a trigger box.
 * Spawned and configured by Python (editor-side), runs in C++ (runtime-side).
 * No Blueprint graph wiring needed.
 */
UCLASS()
class BLUEPRINTGRAPHBUILDER_API AShakeTriggerActor : public AActor
{
	GENERATED_BODY()

public:
	AShakeTriggerActor();

protected:
	virtual void BeginPlay() override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shake")
	UBoxComponent* Trigger;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shake")
	TSubclassOf<UCameraShakeBase> ShakeClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shake")
	float ShakeScale = 1.0f;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComponent,
	                    AActor* OtherActor,
	                    UPrimitiveComponent* OtherComp,
	                    int32 OtherBodyIndex,
	                    bool bFromSweep,
	                    const FHitResult& SweepResult);
};
