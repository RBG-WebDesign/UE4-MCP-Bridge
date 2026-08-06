// Copyright 2026 RareBird Games. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BSPOps.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Engine/Polys.h"
#include "GameFramework/Volume.h"
#include "Model.h"

/**
 * Give a freshly spawned volume the box brush the editor's own "Place
 * Actors" placement would have given it, and answer whether it now has one.
 *
 * GEditor->AddActor calls World->SpawnActor directly. That is the whole
 * placement path for a normal actor, but it is only half of one for an
 * AVolume: the Place Actors panel reaches a volume through
 * UActorFactoryBoxVolume, whose PostSpawnActor is what builds the cube.
 * Spawned without it, a TriggerVolume, BlockingVolume, PostProcessVolume or
 * NavMeshBoundsVolume has a null Brush, zero bounds and no collision. That is
 * not a cosmetic gap: an empty volume triggers nothing, blocks nothing and
 * bounds no navmesh - confirmed live, a spawned NavMeshBoundsVolume produced
 * a RecastNavMesh with zero navigable area, reported as a successful build
 * over nothing.
 *
 * The engine's own helper for this, CreateBrushForVolumeActor, is a free
 * function in UnrealEd's ActorFactory.cpp with no declaration in any header,
 * so it cannot be called from here. This is that function, less the builder
 * choice: a 200-unit cube, the same one the editor places, sized from there
 * by whatever scale the caller applies afterward.
 *
 * ONE implementation, called from both puerts_spawn_actor's SpawnActorJson
 * and puerts_scene_batch's upsert_actor: this used to be two independently
 * hand-written copies (scene_batch's own, and a second, less complete one in
 * SpawnActorJson that skipped Polys/BrushBuilder/csgPrepMovingBrush), which
 * is exactly the kind of drift finding 0af already flagged for the actor
 * class allowlist - the same rule enforced twice is a rule that can only stay
 * in sync by someone remembering to update both.
 */
inline bool EnsureVolumeBrush(AActor* Actor)
{
    AVolume* Volume = Cast<AVolume>(Actor);
    if (Volume == nullptr) { return true; }
    if (Volume->Brush != nullptr) { return true; }

    UCubeBuilder* Builder = NewObject<UCubeBuilder>();
    Builder->X = 200.0f;
    Builder->Y = 200.0f;
    Builder->Z = 200.0f;

    Volume->PreEditChange(nullptr);
    const EObjectFlags BrushFlags = Volume->GetFlags() & (RF_Transient | RF_Transactional);
    Volume->PolyFlags = 0;
    Volume->Brush = NewObject<UModel>(Volume, NAME_None, BrushFlags);
    Volume->Brush->Initialize(nullptr, true);
    Volume->Brush->Polys = NewObject<UPolys>(Volume->Brush, NAME_None, BrushFlags);
    Volume->GetBrushComponent()->Brush = Volume->Brush;
    Volume->BrushBuilder = DuplicateObject<UBrushBuilder>(Builder, Volume);
    Builder->Build(Volume->GetWorld(), Volume);
    FBSPOps::csgPrepMovingBrush(Volume);

    // Untextured, so a volume forms no dependency on a material it never
    // renders. The engine helper does the same and gives the same reason.
    for (FPoly& Poly : Volume->Brush->Polys->Element) { Poly.Material = nullptr; }
    Volume->PostEditChange();

    return Volume->Brush != nullptr && Volume->Brush->Polys->Element.Num() > 0;
}
