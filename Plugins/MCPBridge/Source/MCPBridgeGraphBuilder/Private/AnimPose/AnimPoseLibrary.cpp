// Copyright 2026 RareBird Games. All Rights Reserved.

#include "AnimPoseLibrary.h"

#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AnimPoseLibrary"

namespace
{
	/** One bone's correction, already decided by the Python layer. */
	struct FBonePlan
	{
		FName Bone;
		FQuat Delta;
		TArray<float> Weights;
	};

	FString MakeError(const FString& Message)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"%s\"}"),
			*Message.ReplaceCharWithEscapedChar());
	}

	bool ParsePlan(const FString& PlanJSON, TArray<FBonePlan>& OutPlans, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PlanJSON);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("Plan JSON did not parse");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* BoneArray = nullptr;
		if (!Root->TryGetArrayField(TEXT("bones"), BoneArray) || BoneArray == nullptr)
		{
			OutError = TEXT("Plan JSON has no 'bones' array");
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Entry : *BoneArray)
		{
			const TSharedPtr<FJsonObject>* BoneObject = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(BoneObject) || BoneObject == nullptr)
			{
				OutError = TEXT("Malformed entry in 'bones'");
				return false;
			}

			FString BoneName;
			if (!(*BoneObject)->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
			{
				OutError = TEXT("Bone entry is missing 'bone'");
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* QuatArray = nullptr;
			if (!(*BoneObject)->TryGetArrayField(TEXT("delta_quat"), QuatArray)
				|| QuatArray == nullptr || QuatArray->Num() != 4)
			{
				OutError = FString::Printf(
					TEXT("Bone '%s' needs a 4-element 'delta_quat'"), *BoneName);
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* WeightArray = nullptr;
			if (!(*BoneObject)->TryGetArrayField(TEXT("weights"), WeightArray)
				|| WeightArray == nullptr || WeightArray->Num() == 0)
			{
				OutError = FString::Printf(
					TEXT("Bone '%s' needs a non-empty 'weights' array"), *BoneName);
				return false;
			}

			FBonePlan Plan;
			Plan.Bone = FName(*BoneName);
			Plan.Delta = FQuat(
				(*QuatArray)[0]->AsNumber(),
				(*QuatArray)[1]->AsNumber(),
				(*QuatArray)[2]->AsNumber(),
				(*QuatArray)[3]->AsNumber());
			Plan.Delta.Normalize();

			Plan.Weights.Reserve(WeightArray->Num());
			for (const TSharedPtr<FJsonValue>& Weight : *WeightArray)
			{
				Plan.Weights.Add(static_cast<float>(Weight->AsNumber()));
			}

			OutPlans.Add(MoveTemp(Plan));
		}

		if (OutPlans.Num() == 0)
		{
			OutError = TEXT("Plan contains no bones");
			return false;
		}
		return true;
	}
}

FString UAnimPoseLibrary::ValidateReanchorPlan(UAnimSequence* Target, const FString& PlanJSON)
{
	if (Target == nullptr)
	{
		return MakeError(TEXT("Target sequence is null"));
	}

	TArray<FBonePlan> Plans;
	FString ParseError;
	if (!ParsePlan(PlanJSON, Plans, ParseError))
	{
		return MakeError(ParseError);
	}

	int32 MissingTracks = 0;
	int32 WouldExpand = 0;
	for (const FBonePlan& Plan : Plans)
	{
		if (!UAnimationBlueprintLibrary::IsValidRawAnimationTrackName(
			Target, Plan.Bone.ToString()))
		{
			MissingTracks += 1;
			continue;
		}
		const FRawAnimSequenceTrack& Track =
			UAnimationBlueprintLibrary::GetRawAnimationTrackByName(Target, Plan.Bone);
		if (Track.RotKeys.Num() < Plan.Weights.Num())
		{
			WouldExpand += 1;
		}
	}

	return FString::Printf(
		TEXT("{\"success\":true,\"dry_run\":true,\"bones_in_plan\":%d,")
		TEXT("\"missing_tracks\":%d,\"tracks_needing_expansion\":%d}"),
		Plans.Num(), MissingTracks, WouldExpand);
}

FString UAnimPoseLibrary::ApplyReanchorPlan(UAnimSequence* Target, const FString& PlanJSON)
{
	if (Target == nullptr)
	{
		return MakeError(TEXT("Target sequence is null"));
	}

	TArray<FBonePlan> Plans;
	FString ParseError;
	if (!ParsePlan(PlanJSON, Plans, ParseError))
	{
		return MakeError(ParseError);
	}

	// The Python handler already opened a transaction, but the sequence still
	// has to be told to record itself into it or the undo entry is empty.
	// Note: raw animation data undo through FinalizeBoneAnimation is not
	// something this code can guarantee, which is why the Python side takes an
	// asset backup rather than trusting Ctrl+Z.
	const FScopedTransaction Transaction(
		LOCTEXT("ReanchorAnimation", "Re-anchor animation pose"));
	Target->Modify();

	int32 BonesWritten = 0;
	int64 KeysWritten = 0;
	TArray<FString> Expanded;
	TArray<FString> Missing;

	for (const FBonePlan& Plan : Plans)
	{
		const FString BoneString = Plan.Bone.ToString();
		if (!UAnimationBlueprintLibrary::IsValidRawAnimationTrackName(Target, BoneString))
		{
			Missing.Add(BoneString);
			continue;
		}

		FRawAnimSequenceTrack& Track =
			UAnimationBlueprintLibrary::GetRawAnimationTrackByName(Target, Plan.Bone);
		if (Track.RotKeys.Num() == 0)
		{
			Missing.Add(BoneString);
			continue;
		}

		// A constant track holds one key meaning "static for the whole clip".
		// A time-varying correction cannot be expressed against that, so widen
		// it by repeating the last key before applying anything.
		const int32 TargetKeyCount = Plan.Weights.Num();
		if (Track.RotKeys.Num() < TargetKeyCount)
		{
			const FQuat LastRotation = Track.RotKeys.Last();
			while (Track.RotKeys.Num() < TargetKeyCount)
			{
				Track.RotKeys.Add(LastRotation);
			}
			Expanded.Add(BoneString);
		}

		const int32 KeyCount = FMath::Min(Track.RotKeys.Num(), TargetKeyCount);
		bool bTouchedThisBone = false;
		for (int32 KeyIndex = 0; KeyIndex < KeyCount; ++KeyIndex)
		{
			const float Weight = Plan.Weights[KeyIndex];
			if (Weight <= 0.0f)
			{
				continue;
			}

			const FQuat Original = Track.RotKeys[KeyIndex];
			const FQuat Corrected = Plan.Delta * Original;
			// FQuat::Slerp takes the shortest arc, matching quat_slerp in
			// anim_math.py, so a correction never spins a bone the long way.
			Track.RotKeys[KeyIndex] = (Weight >= 1.0f)
				? Corrected.GetNormalized()
				: FQuat::Slerp(Original, Corrected, Weight).GetNormalized();
			KeysWritten += 1;
			bTouchedThisBone = true;
		}

		if (bTouchedThisBone)
		{
			BonesWritten += 1;
		}
	}

	// Recompression is expensive and only correct once every track is final,
	// so this is called a single time rather than per bone.
	UAnimationBlueprintLibrary::FinalizeBoneAnimation(Target);
	Target->MarkPackageDirty();

	FString ExpandedJson;
	for (const FString& Name : Expanded)
	{
		ExpandedJson += (ExpandedJson.IsEmpty() ? TEXT("\"") : TEXT(",\"")) + Name + TEXT("\"");
	}
	FString MissingJson;
	for (const FString& Name : Missing)
	{
		MissingJson += (MissingJson.IsEmpty() ? TEXT("\"") : TEXT(",\"")) + Name + TEXT("\"");
	}

	return FString::Printf(
		TEXT("{\"success\":true,\"dry_run\":false,\"bones_written\":%d,")
		TEXT("\"keys_written\":%lld,\"tracks_expanded\":[%s],\"missing_tracks\":[%s]}"),
		BonesWritten, KeysWritten, *ExpandedJson, *MissingJson);
}

#undef LOCTEXT_NAMESPACE
