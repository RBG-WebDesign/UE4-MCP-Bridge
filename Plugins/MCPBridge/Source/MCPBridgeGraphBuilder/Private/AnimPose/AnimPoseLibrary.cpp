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

	FString SerializeJson(const TSharedPtr<FJsonObject>& Object)
	{
		FString Json;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Json;
	}

	// Named MakeJsonError, not MakeError: Engine/Templates/ValueOrError.h declares
	// a global MakeError template that wins overload resolution here and fails
	// with a TValueOrError_ErrorProxy -> FString conversion error.
	FString MakeJsonError(const FString& Message)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), Message);
		return SerializeJson(Result);
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

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Root->Values)
		{
			if (Field.Key != TEXT("bones") && Field.Key != TEXT("num_frames"))
			{
				OutError = FString::Printf(TEXT("Unsupported plan field '%s'"), *Field.Key);
				return false;
			}
		}
		if (Root->HasField(TEXT("num_frames")))
		{
			double NumFrames = 0.0;
			if (!Root->TryGetNumberField(TEXT("num_frames"), NumFrames)
				|| !FMath::IsFinite(NumFrames)
				|| NumFrames < 1.0
				|| NumFrames != FMath::FloorToDouble(NumFrames))
			{
				OutError = TEXT("num_frames must be a positive integer");
				return false;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* BoneArray = nullptr;
		if (!Root->TryGetArrayField(TEXT("bones"), BoneArray) || BoneArray == nullptr)
		{
			OutError = TEXT("Plan JSON has no 'bones' array");
			return false;
		}

		TSet<FName> SeenBones;
		for (const TSharedPtr<FJsonValue>& Entry : *BoneArray)
		{
			const TSharedPtr<FJsonObject>* BoneObject = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(BoneObject) || BoneObject == nullptr)
			{
				OutError = TEXT("Malformed entry in 'bones'");
				return false;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*BoneObject)->Values)
			{
				if (Field.Key != TEXT("bone") && Field.Key != TEXT("delta_quat")
					&& Field.Key != TEXT("weights"))
				{
					OutError = FString::Printf(TEXT("Unsupported field '%s' in bone plan"), *Field.Key);
					return false;
				}
			}

			FString BoneName;
			if (!(*BoneObject)->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
			{
				OutError = TEXT("Bone entry is missing 'bone'");
				return false;
			}
			const FName Bone = FName(*BoneName);
			if (SeenBones.Contains(Bone))
			{
				OutError = FString::Printf(TEXT("Bone '%s' appears more than once"), *BoneName);
				return false;
			}
			SeenBones.Add(Bone);

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
			double Quaternion[4] = {};
			for (int32 Index = 0; Index < 4; ++Index)
			{
				if (!(*QuatArray)[Index].IsValid()
					|| !(*QuatArray)[Index]->TryGetNumber(Quaternion[Index])
					|| !FMath::IsFinite(Quaternion[Index]))
				{
					OutError = FString::Printf(
						TEXT("Bone '%s' has a non-finite delta_quat value"), *BoneName);
					return false;
				}
			}
			const double QuaternionLengthSquared = Quaternion[0] * Quaternion[0]
				+ Quaternion[1] * Quaternion[1] + Quaternion[2] * Quaternion[2]
				+ Quaternion[3] * Quaternion[3];
			if (QuaternionLengthSquared <= SMALL_NUMBER)
			{
				OutError = FString::Printf(TEXT("Bone '%s' has a zero-length delta_quat"), *BoneName);
				return false;
			}
			Plan.Bone = Bone;
			Plan.Delta = FQuat(
				static_cast<float>(Quaternion[0]), static_cast<float>(Quaternion[1]),
				static_cast<float>(Quaternion[2]), static_cast<float>(Quaternion[3]));
			Plan.Delta.Normalize();

			Plan.Weights.Reserve(WeightArray->Num());
			for (const TSharedPtr<FJsonValue>& Weight : *WeightArray)
			{
				double Value = 0.0;
				if (!Weight.IsValid() || !Weight->TryGetNumber(Value)
					|| !FMath::IsFinite(Value) || Value < 0.0 || Value > 1.0)
				{
					OutError = FString::Printf(
						TEXT("Bone '%s' weights must be finite numbers in [0, 1]"), *BoneName);
					return false;
				}
				Plan.Weights.Add(static_cast<float>(Value));
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
		return MakeJsonError(TEXT("Target sequence is null"));
	}

	TArray<FBonePlan> Plans;
	FString ParseError;
	if (!ParsePlan(PlanJSON, Plans, ParseError))
	{
		return MakeJsonError(ParseError);
	}

	// GetAnimationTrackNames returns through an out-parameter in 4.27.
	TArray<FName> TrackNames;
	UAnimationBlueprintLibrary::GetAnimationTrackNames(Target, TrackNames);

	int32 MissingTracks = 0;
	int32 WouldExpand = 0;
	for (const FBonePlan& Plan : Plans)
	{
		const int32 TrackIndex = TrackNames.IndexOfByKey(Plan.Bone);
		if (TrackIndex == INDEX_NONE)
		{
			MissingTracks += 1;
			continue;
		}
		const FRawAnimSequenceTrack& Track = Target->GetRawAnimationTrack(TrackIndex);
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
		return MakeJsonError(TEXT("Target sequence is null"));
	}

	TArray<FBonePlan> Plans;
	FString ParseError;
	if (!ParsePlan(PlanJSON, Plans, ParseError))
	{
		return MakeJsonError(ParseError);
	}

	// The Python handler already opened a transaction, but the sequence still
	// has to be told to record itself into it or the undo entry is empty.
	// Note: raw animation data undo through FinalizeBoneAnimation is not
	// something this code can guarantee, which is why the Python side takes an
	// asset backup rather than trusting Ctrl+Z.
	const FScopedTransaction Transaction(
		LOCTEXT("ReanchorAnimation", "Re-anchor animation pose"));
	if (!Target->Modify())
	{
		return MakeJsonError(TEXT("Target sequence Modify() returned false; no writes were attempted"));
	}

	int32 BonesWritten = 0;
	int64 KeysWritten = 0;
	TArray<FString> Expanded;
	TArray<FString> Missing;

	// Resolved once. Indexing into the sequence is what yields a mutable track;
	// GetRawAnimationTrackByName returns a const reference and cannot be written
	// through.
	TArray<FName> TrackNames;
	UAnimationBlueprintLibrary::GetAnimationTrackNames(Target, TrackNames);

	for (const FBonePlan& Plan : Plans)
	{
		const FString BoneString = Plan.Bone.ToString();
		const int32 TrackIndex = TrackNames.IndexOfByKey(Plan.Bone);
		if (TrackIndex == INDEX_NONE)
		{
			Missing.Add(BoneString);
			continue;
		}

		FRawAnimSequenceTrack& Track = Target->GetRawAnimationTrack(TrackIndex);
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

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("dry_run"), false);
	Result->SetNumberField(TEXT("bones_written"), BonesWritten);
	Result->SetNumberField(TEXT("keys_written"), static_cast<double>(KeysWritten));
	TArray<TSharedPtr<FJsonValue>> ExpandedValues;
	for (const FString& Name : Expanded)
	{
		ExpandedValues.Add(MakeShared<FJsonValueString>(Name));
	}
	TArray<TSharedPtr<FJsonValue>> MissingValues;
	for (const FString& Name : Missing)
	{
		MissingValues.Add(MakeShared<FJsonValueString>(Name));
	}
	Result->SetArrayField(TEXT("tracks_expanded"), ExpandedValues);
	Result->SetArrayField(TEXT("missing_tracks"), MissingValues);
	return SerializeJson(Result);
}

#undef LOCTEXT_NAMESPACE
