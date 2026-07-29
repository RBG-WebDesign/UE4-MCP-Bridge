// Copyright 2026 RareBird Games. All Rights Reserved.

#include "GarmentMeshLibrary.h"

#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODImporterData.h"

bool UGarmentMeshLibrary::StripSectionsToMaterials(USkeletalMesh* Mesh, const TArray<int32>& KeepMaterialIndices)
{
	if (!Mesh || KeepMaterialIndices.Num() == 0)
	{
		return false;
	}
	FSkeletalMeshModel* Model = Mesh->GetImportedModel();
	if (!Model || Model->LODModels.Num() == 0)
	{
		return false;
	}
	const int32 NumMaterials = Mesh->Materials.Num();
	for (int32 Keep : KeepMaterialIndices)
	{
		if (Keep < 0 || Keep >= NumMaterials)
		{
			return false;
		}
	}

	// Old material index -> new compacted index. Kept order = original order.
	TArray<int32> OldToNew;
	OldToNew.Init(INDEX_NONE, NumMaterials);
	TArray<FSkeletalMaterial> NewMaterials;
	for (int32 Old = 0; Old < NumMaterials; ++Old)
	{
		if (KeepMaterialIndices.Contains(Old))
		{
			OldToNew[Old] = NewMaterials.Num();
			NewMaterials.Add(Mesh->Materials[Old]);
		}
	}

	// UE4.26+ meshes imported from FBX carry raw import data. PostEditChange
	// rebuilds every LODModel from it, so surgery on the LODModel alone is
	// resurrected on the next rebuild. Strip the import data first (it is the
	// build source of truth), validating every LOD before mutating anything.
	const int32 NumLODs = Model->LODModels.Num();
	TArray<FSkeletalMeshImportData> StrippedImportData;
	TArray<bool> HasImportData;
	StrippedImportData.SetNum(NumLODs);
	HasImportData.Init(false, NumLODs);

	for (int32 LodIdx = 0; LodIdx < NumLODs; ++LodIdx)
	{
		if (Mesh->IsLODImportedDataEmpty(LodIdx))
		{
			continue;
		}
		HasImportData[LodIdx] = true;
		FSkeletalMeshImportData& ImportData = StrippedImportData[LodIdx];
		Mesh->LoadLODImportedData(LodIdx, ImportData);

		TArray<SkeletalMeshImportData::FTriangle> KeptFaces;
		KeptFaces.Reserve(ImportData.Faces.Num());
		for (const SkeletalMeshImportData::FTriangle& Face : ImportData.Faces)
		{
			const int32 NewMatIndex =
				Face.MatIndex < NumMaterials ? OldToNew[Face.MatIndex] : INDEX_NONE;
			if (NewMatIndex == INDEX_NONE)
			{
				continue;
			}
			SkeletalMeshImportData::FTriangle NewFace = Face;
			NewFace.MatIndex = (uint16)NewMatIndex;
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				SkeletalMeshImportData::FVertex& Wedge = ImportData.Wedges[NewFace.WedgeIndex[Corner]];
				Wedge.MatIndex = (uint8)NewMatIndex;
			}
			KeptFaces.Add(NewFace);
		}
		if (KeptFaces.Num() == 0)
		{
			return false; // LOD would end up empty; nothing has been mutated yet
		}
		ImportData.Faces = MoveTemp(KeptFaces);

		// Compact the import-time material table to mirror the new slot order.
		if (ImportData.Materials.Num() == NumMaterials)
		{
			TArray<SkeletalMeshImportData::FMaterial> KeptImportMaterials;
			for (int32 Old = 0; Old < NumMaterials; ++Old)
			{
				if (OldToNew[Old] != INDEX_NONE)
				{
					KeptImportMaterials.Add(ImportData.Materials[Old]);
				}
			}
			ImportData.Materials = MoveTemp(KeptImportMaterials);
		}
		ImportData.MaxMaterialIndex = (uint32)FMath::Max(0, NewMaterials.Num() - 1);
	}

	Mesh->PreEditChange(nullptr);

	for (int32 LodIdx = 0; LodIdx < NumLODs; ++LodIdx)
	{
		if (HasImportData[LodIdx])
		{
			Mesh->SaveLODImportedData(LodIdx, StrippedImportData[LodIdx]);
		}

		FSkeletalMeshLODModel& LOD = Model->LODModels[LodIdx];

		// Rebuild sections, vertex bases, and the index buffer, skipping
		// every section whose material is not kept. Each section's indices
		// reference LOD-wide vertex ids (BaseVertexIndex + i), so removing a
		// section shifts every later section's vertex ids by its vertex count.
		// When import data exists this is redundant (PostEditChange rebuilds
		// from it) but keeps the in-memory model consistent either way.
		TArray<FSkelMeshSection> NewSections;
		TArray<uint32> NewIndexBuffer;
		uint32 RunningBaseVertex = 0;

		for (const FSkelMeshSection& Section : LOD.Sections)
		{
			const int32 NewMatIndex =
				Section.MaterialIndex < NumMaterials ? OldToNew[Section.MaterialIndex] : INDEX_NONE;
			if (NewMatIndex == INDEX_NONE)
			{
				continue; // dropped section: skip its verts and indices
			}
			FSkelMeshSection NewSection = Section;
			NewSection.MaterialIndex = (uint16)NewMatIndex;
			const uint32 OldBaseVertex = Section.BaseVertexIndex;
			NewSection.BaseVertexIndex = RunningBaseVertex;
			NewSection.BaseIndex = NewIndexBuffer.Num();

			const uint32 FirstIndex = Section.BaseIndex;
			const uint32 IndexCount = Section.NumTriangles * 3;
			for (uint32 i = 0; i < IndexCount; ++i)
			{
				const uint32 OldVertId = LOD.IndexBuffer[FirstIndex + i];
				NewIndexBuffer.Add(OldVertId - OldBaseVertex + RunningBaseVertex);
			}
			RunningBaseVertex += NewSection.SoftVertices.Num();
			NewSections.Add(MoveTemp(NewSection));
		}

		if (NewSections.Num() == 0 && !HasImportData[LodIdx])
		{
			// A LOD would end up empty: abort the whole operation untouched.
			Mesh->PostEditChange();
			return false;
		}

		LOD.Sections = MoveTemp(NewSections);
		LOD.IndexBuffer = MoveTemp(NewIndexBuffer);
		LOD.NumVertices = RunningBaseVertex;
		// ActiveBoneIndices/RequiredBones stay as the superset: safe, just
		// slightly conservative. UserSectionsData is keyed by
		// OriginalDataSectionIndex, which surviving sections retain.
	}

	Mesh->Materials = NewMaterials;
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	return true;
}
