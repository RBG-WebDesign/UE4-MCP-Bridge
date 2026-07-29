// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CanonFontLibrary.generated.h"

class UFont;
class UFontFace;

UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UCanonFontLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Configure an existing UFont as a runtime composite font backed by FontFace assets. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "BlueprintGraphBuilder|Fonts")
	static bool ConfigureRuntimeFont(
		UFont* Font,
		const TArray<UFontFace*>& Faces,
		const TArray<FName>& TypefaceNames,
		int32 DefaultFaceIndex,
		FString& OutError);
};
