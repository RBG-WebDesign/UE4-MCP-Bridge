// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAnimBPBuildSpec;
struct FAnimBPBuildContext;
class UAnimBlueprint;
class UEdGraph;

class FAnimBPAnimGraphBuilder
{
public:
	static FString Build(const FAnimBPBuildSpec& Spec, FAnimBPBuildContext& Ctx);

	/** The AnimGraph among an AnimBlueprint's function graphs, or null.
	    One definition, because the clear pass and the build pass have to agree
	    about which graph they are talking about; two copies of this lookup that
	    drifted would let a rebuild clear one graph and populate another. */
	static UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBlueprint);
};
