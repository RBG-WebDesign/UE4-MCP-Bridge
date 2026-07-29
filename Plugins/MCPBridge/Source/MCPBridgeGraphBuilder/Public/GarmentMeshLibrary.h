// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GarmentMeshLibrary.generated.h"

class USkeletalMesh;

/**
 * Editor utilities for wardrobe garment meshes imported from Character
 * Creator, which arrive carrying the whole character (eyes, teeth, old
 * clothes). StripSectionsToMaterials removes every LOD-0 section whose
 * material is not in the keep list and compacts the material table, turning
 * a full-character import into a lean garment asset in place.
 *
 * ALWAYS run on a duplicate (EditorAssetLibrary.duplicate_asset first).
 * Geometry surgery on imported model data is not undoable.
 */
UCLASS()
class MCPBRIDGEGRAPHBUILDER_API UGarmentMeshLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Removes all sections (all LODs) whose material index is not listed.
	    Compacts Materials and remaps section indices. Returns false and
	    leaves the mesh untouched on any validation failure. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MCP Bridge|Garment Tools")
	static bool StripSectionsToMaterials(USkeletalMesh* Mesh, const TArray<int32>& KeepMaterialIndices);
};
