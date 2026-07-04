#include "BTNodeRegistry.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"

// Composites
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"

// Tasks
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BehaviorTree/Tasks/BTTask_WaitBlackboardTime.h"
#include "BehaviorTree/Tasks/BTTask_RotateToFaceBBEntry.h"
#include "BehaviorTree/Tasks/BTTask_PlayAnimation.h"
#include "BehaviorTree/Tasks/BTTask_MakeNoise.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "BehaviorTree/Tasks/BTTask_PlaySound.h"
#include "BehaviorTree/Tasks/BTTask_FinishWithResult.h"
#include "BehaviorTree/Tasks/BTTask_SetTagCooldown.h"
#include "GameplayTagContainer.h"
#include "Sound/SoundCue.h"

// Decorators
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Decorators/BTDecorator_ForceSuccess.h"
#include "BehaviorTree/Decorators/BTDecorator_Loop.h"
#include "BehaviorTree/Decorators/BTDecorator_TimeLimit.h"
#include "BehaviorTree/Decorators/BTDecorator_Cooldown.h"
#include "BehaviorTree/Decorators/BTDecorator_CompareBBEntries.h"
#include "BehaviorTree/Decorators/BTDecorator_IsAtLocation.h"
#include "BehaviorTree/Decorators/BTDecorator_DoesPathExist.h"
#include "BehaviorTree/Decorators/BTDecorator_TagCooldown.h"
#include "BehaviorTree/Decorators/BTDecorator_ConditionalLoop.h"
#include "BehaviorTree/Decorators/BTDecorator_KeepInCone.h"
#include "BehaviorTree/Decorators/BTDecorator_IsBBEntryOfClass.h"

// Services
#include "BehaviorTree/Services/BTService_DefaultFocus.h"

// Blackboard
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"

FBTNodeRegistry::FBTNodeRegistry()
{
	RegisterTypes();
}

void FBTNodeRegistry::RegisterTypes()
{
	// --- Composites ---
	CompositeTypes.Add(TEXT("Selector"), UBTComposite_Selector::StaticClass());
	CompositeTypes.Add(TEXT("Sequence"), UBTComposite_Sequence::StaticClass());
	CompositeTypes.Add(TEXT("SimpleParallel"), UBTComposite_SimpleParallel::StaticClass());

	// --- Tasks ---
	TaskTypes.Add(TEXT("MoveTo"), UBTTask_MoveTo::StaticClass());
	TaskTypes.Add(TEXT("Wait"), UBTTask_Wait::StaticClass());
	TaskTypes.Add(TEXT("WaitBlackboardTime"), UBTTask_WaitBlackboardTime::StaticClass());
	TaskTypes.Add(TEXT("RotateToFaceBBEntry"), UBTTask_RotateToFaceBBEntry::StaticClass());
	TaskTypes.Add(TEXT("PlayAnimation"), UBTTask_PlayAnimation::StaticClass());
	TaskTypes.Add(TEXT("MakeNoise"), UBTTask_MakeNoise::StaticClass());
	TaskTypes.Add(TEXT("RunBehavior"), UBTTask_RunBehavior::StaticClass());
	TaskTypes.Add(TEXT("PlaySound"), UBTTask_PlaySound::StaticClass());
	TaskTypes.Add(TEXT("FinishWithResult"), UBTTask_FinishWithResult::StaticClass());
	TaskTypes.Add(TEXT("SetTagCooldown"), UBTTask_SetTagCooldown::StaticClass());

	// --- Decorators ---
	DecoratorTypes.Add(TEXT("Blackboard"), UBTDecorator_Blackboard::StaticClass());
	DecoratorTypes.Add(TEXT("ForceSuccess"), UBTDecorator_ForceSuccess::StaticClass());
	DecoratorTypes.Add(TEXT("Loop"), UBTDecorator_Loop::StaticClass());
	DecoratorTypes.Add(TEXT("TimeLimit"), UBTDecorator_TimeLimit::StaticClass());
	DecoratorTypes.Add(TEXT("Cooldown"), UBTDecorator_Cooldown::StaticClass());
	DecoratorTypes.Add(TEXT("CompareBBEntries"), UBTDecorator_CompareBBEntries::StaticClass());
	DecoratorTypes.Add(TEXT("IsAtLocation"), UBTDecorator_IsAtLocation::StaticClass());
	DecoratorTypes.Add(TEXT("DoesPathExist"), UBTDecorator_DoesPathExist::StaticClass());
	DecoratorTypes.Add(TEXT("TagCooldown"), UBTDecorator_TagCooldown::StaticClass());
	DecoratorTypes.Add(TEXT("ConditionalLoop"), UBTDecorator_ConditionalLoop::StaticClass());
	DecoratorTypes.Add(TEXT("KeepInCone"), UBTDecorator_KeepInCone::StaticClass());
	DecoratorTypes.Add(TEXT("IsBBEntryOfClass"), UBTDecorator_IsBBEntryOfClass::StaticClass());

	// --- Services ---
	ServiceTypes.Add(TEXT("DefaultFocus"), UBTService_DefaultFocus::StaticClass());

	// --- Default Params ---
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("acceptable_radius"), TEXT("50.0"));
		DefaultParams.Add(TEXT("MoveTo"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("wait_time"), TEXT("5.0"));
		Defs.Add(TEXT("random_deviation"), TEXT("0.0"));
		DefaultParams.Add(TEXT("Wait"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("num_loops"), TEXT("3"));
		Defs.Add(TEXT("infinite_loop"), TEXT("false"));
		DefaultParams.Add(TEXT("Loop"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("time_limit"), TEXT("5.0"));
		DefaultParams.Add(TEXT("TimeLimit"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("cool_down_time"), TEXT("5.0"));
		DefaultParams.Add(TEXT("Cooldown"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("loudness"), TEXT("1.0"));
		DefaultParams.Add(TEXT("MakeNoise"), Defs);
	}
	{
		// SimpleParallel finish mode: "Immediate" or "Delayed"
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("finish_mode"), TEXT("Immediate"));
		DefaultParams.Add(TEXT("SimpleParallel"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("result"), TEXT("Succeeded"));
		DefaultParams.Add(TEXT("FinishWithResult"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("cooldown_duration"), TEXT("5.0"));
		Defs.Add(TEXT("add_to_existing"), TEXT("true"));
		DefaultParams.Add(TEXT("SetTagCooldown"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("acceptable_radius"), TEXT("50.0"));
		Defs.Add(TEXT("inverse_condition"), TEXT("false"));
		DefaultParams.Add(TEXT("IsAtLocation"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("path_query_type"), TEXT("NavMesh"));
		Defs.Add(TEXT("inverse_condition"), TEXT("false"));
		DefaultParams.Add(TEXT("DoesPathExist"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("cool_down_time"), TEXT("5.0"));
		Defs.Add(TEXT("add_to_existing"), TEXT("true"));
		DefaultParams.Add(TEXT("TagCooldown"), Defs);
	}
	{
		TMap<FString, FString> Defs;
		Defs.Add(TEXT("cone_half_angle"), TEXT("45.0"));
		DefaultParams.Add(TEXT("KeepInCone"), Defs);
	}

	// --- BB Key Type Requirements ---
	{
		TMap<FString, TSet<FString>> Reqs;
		TSet<FString> KeyTypes;
		KeyTypes.Add(TEXT("Object"));
		KeyTypes.Add(TEXT("Vector"));
		Reqs.Add(TEXT("blackboard_key"), KeyTypes);
		BBKeyTypeRequirements.Add(TEXT("MoveTo"), Reqs);
	}
	{
		TMap<FString, TSet<FString>> Reqs;
		TSet<FString> KeyTypes;
		KeyTypes.Add(TEXT("Object"));
		KeyTypes.Add(TEXT("Vector"));
		Reqs.Add(TEXT("blackboard_key"), KeyTypes);
		BBKeyTypeRequirements.Add(TEXT("RotateToFaceBBEntry"), Reqs);
	}
	{
		TMap<FString, TSet<FString>> Reqs;
		TSet<FString> KeyTypes;
		KeyTypes.Add(TEXT("Float"));
		Reqs.Add(TEXT("blackboard_key"), KeyTypes);
		BBKeyTypeRequirements.Add(TEXT("WaitBlackboardTime"), Reqs);
	}
	{
		TMap<FString, TSet<FString>> Reqs;
		TSet<FString> KeyTypes;
		KeyTypes.Add(TEXT("Object"));
		KeyTypes.Add(TEXT("Vector"));
		Reqs.Add(TEXT("blackboard_key"), KeyTypes);
		BBKeyTypeRequirements.Add(TEXT("DefaultFocus"), Reqs);
	}
	{
		TMap<FString, TSet<FString>> Reqs;
		TSet<FString> KeyTypes;
		KeyTypes.Add(TEXT("Object"));
		KeyTypes.Add(TEXT("Vector"));
		Reqs.Add(TEXT("blackboard_key"), KeyTypes);
		BBKeyTypeRequirements.Add(TEXT("IsAtLocation"), Reqs);
	}
	{
		TMap<FString, TSet<FString>> Reqs;
		TSet<FString> KeyTypesA;
		KeyTypesA.Add(TEXT("Object"));
		KeyTypesA.Add(TEXT("Vector"));
		Reqs.Add(TEXT("blackboard_key_a"), KeyTypesA);
		TSet<FString> KeyTypesB;
		KeyTypesB.Add(TEXT("Object"));
		KeyTypesB.Add(TEXT("Vector"));
		Reqs.Add(TEXT("blackboard_key_b"), KeyTypesB);
		BBKeyTypeRequirements.Add(TEXT("DoesPathExist"), Reqs);
	}
	{
		TMap<FString, TSet<FString>> Reqs;
		TSet<FString> KeyTypes;
		KeyTypes.Add(TEXT("Object"));
		KeyTypes.Add(TEXT("Vector"));
		Reqs.Add(TEXT("cone_origin"), KeyTypes);
		TSet<FString> ObsTypes;
		ObsTypes.Add(TEXT("Object"));
		ObsTypes.Add(TEXT("Vector"));
		Reqs.Add(TEXT("observed"), ObsTypes);
		BBKeyTypeRequirements.Add(TEXT("KeepInCone"), Reqs);
	}
	{
		TMap<FString, TSet<FString>> Reqs;
		TSet<FString> KeyTypes;
		KeyTypes.Add(TEXT("Object"));
		Reqs.Add(TEXT("blackboard_key"), KeyTypes);
		BBKeyTypeRequirements.Add(TEXT("IsBBEntryOfClass"), Reqs);
	}

	// --- Valid Conditions ---
	{
		// Blackboard decorator: basic ops for Object/Vector, arithmetic for numeric
		TSet<FString> Conds;
		Conds.Add(TEXT("IsSet"));
		Conds.Add(TEXT("IsNotSet"));
		Conds.Add(TEXT("Equal"));
		Conds.Add(TEXT("NotEqual"));
		Conds.Add(TEXT("Less"));
		Conds.Add(TEXT("LessOrEqual"));
		Conds.Add(TEXT("Greater"));
		Conds.Add(TEXT("GreaterOrEqual"));
		ValidConditions.Add(TEXT("Blackboard"), Conds);
	}
	{
		// ConditionalLoop: inherits from Blackboard, same conditions
		TSet<FString> Conds;
		Conds.Add(TEXT("IsSet"));
		Conds.Add(TEXT("IsNotSet"));
		Conds.Add(TEXT("Equal"));
		Conds.Add(TEXT("NotEqual"));
		Conds.Add(TEXT("Less"));
		Conds.Add(TEXT("LessOrEqual"));
		Conds.Add(TEXT("Greater"));
		Conds.Add(TEXT("GreaterOrEqual"));
		ValidConditions.Add(TEXT("ConditionalLoop"), Conds);
	}
	{
		// CompareBBEntries: EBlackBoardEntryComparison only has Equal and NotEqual in UE4.27.
		// For numeric comparisons, use the Blackboard decorator with arithmetic conditions instead.
		TSet<FString> Conds;
		Conds.Add(TEXT("Equal"));
		Conds.Add(TEXT("NotEqual"));
		ValidConditions.Add(TEXT("CompareBBEntries"), Conds);
	}
}

bool FBTNodeRegistry::IsComposite(const FString& Type) const
{
	return CompositeTypes.Contains(Type);
}

bool FBTNodeRegistry::IsTask(const FString& Type) const
{
	return TaskTypes.Contains(Type);
}

bool FBTNodeRegistry::IsDecorator(const FString& Type) const
{
	return DecoratorTypes.Contains(Type);
}

bool FBTNodeRegistry::IsService(const FString& Type) const
{
	return ServiceTypes.Contains(Type);
}

bool FBTNodeRegistry::IsKnownType(const FString& Type) const
{
	return IsComposite(Type) || IsTask(Type) || IsDecorator(Type) || IsService(Type);
}

TSubclassOf<UBTCompositeNode> FBTNodeRegistry::GetCompositeClass(const FString& Type) const
{
	const auto* Found = CompositeTypes.Find(Type);
	return Found ? *Found : nullptr;
}

TSubclassOf<UBTTaskNode> FBTNodeRegistry::GetTaskClass(const FString& Type) const
{
	const auto* Found = TaskTypes.Find(Type);
	return Found ? *Found : nullptr;
}

TSubclassOf<UBTDecorator> FBTNodeRegistry::GetDecoratorClass(const FString& Type) const
{
	const auto* Found = DecoratorTypes.Find(Type);
	return Found ? *Found : nullptr;
}

TSubclassOf<UBTService> FBTNodeRegistry::GetServiceClass(const FString& Type) const
{
	const auto* Found = ServiceTypes.Find(Type);
	return Found ? *Found : nullptr;
}

const TMap<FString, FString>* FBTNodeRegistry::GetDefaultParams(const FString& Type) const
{
	return DefaultParams.Find(Type);
}

const TMap<FString, TSet<FString>>* FBTNodeRegistry::GetBBKeyRequirements(const FString& Type) const
{
	return BBKeyTypeRequirements.Find(Type);
}

const TSet<FString>* FBTNodeRegistry::GetValidConditions(const FString& Type) const
{
	return ValidConditions.Find(Type);
}

// Helper: get FBlackboardKeySelector* from a UBTNode via reflection (BlackboardKey is protected)
static FBlackboardKeySelector* GetBlackboardKeySelector(UBTNode* Node, const FName& PropName = TEXT("BlackboardKey"))
{
	FProperty* Prop = Node->GetClass()->FindPropertyByName(PropName);
	if (!Prop) return nullptr;
	return Prop->ContainerPtrToValuePtr<FBlackboardKeySelector>(Node);
}

// Helper: resolve a blackboard key selector by name
static void ResolveBlackboardKey(UBTNode* Node, const FString& KeyName, UBlackboardData* Blackboard, const FName& PropName = TEXT("BlackboardKey"))
{
	FBlackboardKeySelector* BBKey = GetBlackboardKeySelector(Node, PropName);
	if (BBKey)
	{
		BBKey->SelectedKeyName = FName(*KeyName);
		if (Blackboard)
		{
			BBKey->ResolveSelectedKey(*Blackboard);
		}
	}
}

// Helper: set uint8 property via reflection
static void SetUInt8Property(UObject* Obj, const FName& PropName, uint8 Value)
{
	FProperty* Prop = Obj->GetClass()->FindPropertyByName(PropName);
	if (Prop)
	{
		uint8* Ptr = Prop->ContainerPtrToValuePtr<uint8>(Obj);
		if (Ptr) *Ptr = Value;
	}
}

// Helper: set bool property via reflection (safe for uint8:1 bitfields)
static void SetBoolProperty(UObject* Obj, const FName& PropName, bool Value)
{
	FBoolProperty* BoolProp = CastField<FBoolProperty>(Obj->GetClass()->FindPropertyByName(PropName));
	if (BoolProp)
	{
		BoolProp->SetPropertyValue_InContainer(Obj, Value);
	}
}

// Helper: set float property via reflection
static void SetFloatProperty(UObject* Obj, const FName& PropName, float Value)
{
	FProperty* Prop = Obj->GetClass()->FindPropertyByName(PropName);
	if (Prop)
	{
		float* Ptr = Prop->ContainerPtrToValuePtr<float>(Obj);
		if (Ptr) *Ptr = Value;
	}
}

// Helper: set int property via reflection
static void SetIntProperty(UObject* Obj, const FName& PropName, int32 Value)
{
	FProperty* Prop = Obj->GetClass()->FindPropertyByName(PropName);
	if (Prop)
	{
		int32* Ptr = Prop->ContainerPtrToValuePtr<int32>(Obj);
		if (Ptr) *Ptr = Value;
	}
}

void FBTNodeRegistry::ApplyParams(
	UBTNode* Node,
	const FString& Type,
	const TMap<FString, FString>& Params,
	UBlackboardData* Blackboard) const
{
	if (!Node) return;

	// Merge defaults with explicit params (explicit wins)
	TMap<FString, FString> Merged;
	const TMap<FString, FString>* Defaults = GetDefaultParams(Type);
	if (Defaults)
	{
		Merged = *Defaults;
	}
	for (const auto& Pair : Params)
	{
		Merged.Add(Pair.Key, Pair.Value);
	}

	// --- MoveTo ---
	if (Type == TEXT("MoveTo"))
	{
		UBTTask_MoveTo* MoveToNode = Cast<UBTTask_MoveTo>(Node);
		if (!MoveToNode) return;

		if (const FString* KeyName = Merged.Find(TEXT("blackboard_key")))
		{
			ResolveBlackboardKey(Node, *KeyName, Blackboard);
		}
		if (const FString* Radius = Merged.Find(TEXT("acceptable_radius")))
		{
			MoveToNode->AcceptableRadius = FCString::Atof(**Radius);
		}
	}
	// --- Wait ---
	else if (Type == TEXT("Wait"))
	{
		UBTTask_Wait* WaitNode = Cast<UBTTask_Wait>(Node);
		if (!WaitNode) return;

		if (const FString* Time = Merged.Find(TEXT("wait_time")))
		{
			WaitNode->WaitTime = FCString::Atof(**Time);
		}
		if (const FString* Dev = Merged.Find(TEXT("random_deviation")))
		{
			WaitNode->RandomDeviation = FCString::Atof(**Dev);
		}
	}
	// --- WaitBlackboardTime ---
	else if (Type == TEXT("WaitBlackboardTime"))
	{
		if (const FString* KeyName = Merged.Find(TEXT("blackboard_key")))
		{
			ResolveBlackboardKey(Node, *KeyName, Blackboard);
		}
	}
	// --- RotateToFaceBBEntry ---
	else if (Type == TEXT("RotateToFaceBBEntry"))
	{
		if (const FString* KeyName = Merged.Find(TEXT("blackboard_key")))
		{
			ResolveBlackboardKey(Node, *KeyName, Blackboard);
		}
	}
	// --- PlayAnimation ---
	else if (Type == TEXT("PlayAnimation"))
	{
		// Animation asset must be set separately (path-based loading)
		// bNonBlocking and bLooping are settable
		if (const FString* NonBlocking = Merged.Find(TEXT("non_blocking")))
		{
			SetBoolProperty(Node, TEXT("bNonBlocking"), *NonBlocking == TEXT("true"));
		}
		if (const FString* Looping = Merged.Find(TEXT("looping")))
		{
			SetBoolProperty(Node, TEXT("bLooping"), *Looping == TEXT("true"));
		}
	}
	// --- MakeNoise ---
	else if (Type == TEXT("MakeNoise"))
	{
		if (const FString* Loudness = Merged.Find(TEXT("loudness")))
		{
			SetFloatProperty(Node, TEXT("Loudness"), FCString::Atof(**Loudness));
		}
	}
	// --- RunBehavior ---
	else if (Type == TEXT("RunBehavior"))
	{
		// BehaviorAsset must be set separately (path-based loading)
		// No simple params to apply
	}
	// --- PlaySound ---
	else if (Type == TEXT("PlaySound"))
	{
		if (const FString* SoundPath = Merged.Find(TEXT("sound_to_play")))
		{
			USoundCue* Sound = FindObject<USoundCue>(ANY_PACKAGE, **SoundPath);
			if (!Sound)
			{
				Sound = LoadObject<USoundCue>(nullptr, **SoundPath);
			}
			if (Sound)
			{
				UBTTask_PlaySound* PlaySoundNode = Cast<UBTTask_PlaySound>(Node);
				if (PlaySoundNode)
				{
					PlaySoundNode->SoundToPlay = Sound;
				}
			}
		}
	}
	// --- FinishWithResult ---
	else if (Type == TEXT("FinishWithResult"))
	{
		if (const FString* Result = Merged.Find(TEXT("result")))
		{
			// EBTNodeResult: Succeeded=0, Failed=1, Aborted=2
			uint8 ResultVal = 0;
			if (*Result == TEXT("Failed")) ResultVal = 1;
			else if (*Result == TEXT("Aborted")) ResultVal = 2;
			SetUInt8Property(Node, TEXT("Result"), ResultVal);
		}
	}
	// --- SetTagCooldown ---
	else if (Type == TEXT("SetTagCooldown"))
	{
		if (const FString* TagStr = Merged.Find(TEXT("cooldown_tag")))
		{
			FProperty* TagProp = Node->GetClass()->FindPropertyByName(TEXT("CooldownTag"));
			if (TagProp)
			{
				FGameplayTag* TagPtr = TagProp->ContainerPtrToValuePtr<FGameplayTag>(Node);
				if (TagPtr)
				{
					*TagPtr = FGameplayTag::RequestGameplayTag(FName(**TagStr), false);
				}
			}
		}
		if (const FString* Duration = Merged.Find(TEXT("cooldown_duration")))
		{
			SetFloatProperty(Node, TEXT("CooldownDuration"), FCString::Atof(**Duration));
		}
		if (const FString* AddExisting = Merged.Find(TEXT("add_to_existing")))
		{
			SetBoolProperty(Node, TEXT("bAddToExistingDuration"), *AddExisting == TEXT("true"));
		}
	}
	// --- Blackboard decorator ---
	else if (Type == TEXT("Blackboard"))
	{
		UBTDecorator_Blackboard* BBDec = Cast<UBTDecorator_Blackboard>(Node);
		if (!BBDec) return;

		if (const FString* KeyName = Merged.Find(TEXT("blackboard_key")))
		{
			ResolveBlackboardKey(Node, *KeyName, Blackboard);
		}
		if (const FString* Condition = Merged.Find(TEXT("condition")))
		{
			// Basic operations (for Object/Vector/Name/Enum keys)
			if (*Condition == TEXT("IsSet"))
			{
				SetUInt8Property(Node, TEXT("BasicOperation"), static_cast<uint8>(EBasicKeyOperation::Set));
			}
			else if (*Condition == TEXT("IsNotSet"))
			{
				SetUInt8Property(Node, TEXT("BasicOperation"), static_cast<uint8>(EBasicKeyOperation::NotSet));
			}
			// Arithmetic operations (for Int/Float keys)
			else if (*Condition == TEXT("Equal"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::Equal));
			}
			else if (*Condition == TEXT("NotEqual"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::NotEqual));
			}
			else if (*Condition == TEXT("Less"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::Less));
			}
			else if (*Condition == TEXT("LessOrEqual"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::LessOrEqual));
			}
			else if (*Condition == TEXT("Greater"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::Greater));
			}
			else if (*Condition == TEXT("GreaterOrEqual"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::GreaterOrEqual));
			}
		}
		// Comparison value for arithmetic operations
		if (const FString* IntVal = Merged.Find(TEXT("int_value")))
		{
			SetIntProperty(Node, TEXT("IntValue"), FCString::Atoi(**IntVal));
		}
		if (const FString* FloatVal = Merged.Find(TEXT("float_value")))
		{
			SetFloatProperty(Node, TEXT("FloatValue"), FCString::Atof(**FloatVal));
		}
	}
	// --- ForceSuccess decorator ---
	else if (Type == TEXT("ForceSuccess"))
	{
		// No params -- wraps child and forces success
	}
	// --- Loop decorator ---
	else if (Type == TEXT("Loop"))
	{
		if (const FString* NumLoops = Merged.Find(TEXT("num_loops")))
		{
			SetIntProperty(Node, TEXT("NumLoops"), FCString::Atoi(**NumLoops));
		}
		if (const FString* Infinite = Merged.Find(TEXT("infinite_loop")))
		{
			SetBoolProperty(Node, TEXT("bInfiniteLoop"), *Infinite == TEXT("true"));
		}
	}
	// --- TimeLimit decorator ---
	else if (Type == TEXT("TimeLimit"))
	{
		if (const FString* Limit = Merged.Find(TEXT("time_limit")))
		{
			SetFloatProperty(Node, TEXT("TimeLimit"), FCString::Atof(**Limit));
		}
	}
	// --- Cooldown decorator ---
	else if (Type == TEXT("Cooldown"))
	{
		if (const FString* CDTime = Merged.Find(TEXT("cool_down_time")))
		{
			SetFloatProperty(Node, TEXT("CoolDownTime"), FCString::Atof(**CDTime));
		}
	}
	// --- CompareBBEntries decorator ---
	else if (Type == TEXT("CompareBBEntries"))
	{
		if (const FString* KeyA = Merged.Find(TEXT("blackboard_key_a")))
		{
			ResolveBlackboardKey(Node, *KeyA, Blackboard, TEXT("BlackboardKeyA"));
		}
		if (const FString* KeyB = Merged.Find(TEXT("blackboard_key_b")))
		{
			ResolveBlackboardKey(Node, *KeyB, Blackboard, TEXT("BlackboardKeyB"));
		}
		if (const FString* Op = Merged.Find(TEXT("operator")))
		{
			// EBlackBoardEntryComparison only has Equal (0) and NotEqual (1) in UE4.27.
			// Arithmetic comparisons (Less, Greater, etc.) are not supported by CompareBBEntries.
			// Use the Blackboard decorator with arithmetic conditions for numeric comparisons.
			FProperty* OpProp = Node->GetClass()->FindPropertyByName(TEXT("Operator"));
			if (OpProp)
			{
				uint8* OpPtr = OpProp->ContainerPtrToValuePtr<uint8>(Node);
				if (OpPtr)
				{
					if (*Op == TEXT("Equal"))          *OpPtr = static_cast<uint8>(EBlackBoardEntryComparison::Equal);
					else if (*Op == TEXT("NotEqual"))   *OpPtr = static_cast<uint8>(EBlackBoardEntryComparison::NotEqual);
				}
			}
		}
	}
	// --- IsAtLocation decorator ---
	else if (Type == TEXT("IsAtLocation"))
	{
		if (const FString* KeyName = Merged.Find(TEXT("blackboard_key")))
		{
			ResolveBlackboardKey(Node, *KeyName, Blackboard);
		}
		if (const FString* Radius = Merged.Find(TEXT("acceptable_radius")))
		{
			SetFloatProperty(Node, TEXT("AcceptableRadius"), FCString::Atof(**Radius));
		}
		if (const FString* Inverse = Merged.Find(TEXT("inverse_condition")))
		{
			SetBoolProperty(Node, TEXT("bInverseCondition"), *Inverse == TEXT("true"));
		}
	}
	// --- DoesPathExist decorator ---
	else if (Type == TEXT("DoesPathExist"))
	{
		if (const FString* KeyA = Merged.Find(TEXT("blackboard_key_a")))
		{
			ResolveBlackboardKey(Node, *KeyA, Blackboard, TEXT("BlackboardKeyA"));
		}
		if (const FString* KeyB = Merged.Find(TEXT("blackboard_key_b")))
		{
			ResolveBlackboardKey(Node, *KeyB, Blackboard, TEXT("BlackboardKeyB"));
		}
		if (const FString* QueryType = Merged.Find(TEXT("path_query_type")))
		{
			FProperty* QProp = Node->GetClass()->FindPropertyByName(TEXT("PathQueryType"));
			if (QProp)
			{
				uint8* QPtr = QProp->ContainerPtrToValuePtr<uint8>(Node);
				if (QPtr)
				{
					if (*QueryType == TEXT("NavMesh"))                *QPtr = static_cast<uint8>(EPathExistsQueryType::NavmeshRaycast2D);
					else if (*QueryType == TEXT("HierarchicalQuery")) *QPtr = static_cast<uint8>(EPathExistsQueryType::HierarchicalQuery);
				}
			}
		}
		if (const FString* Inverse = Merged.Find(TEXT("inverse_condition")))
		{
			SetBoolProperty(Node, TEXT("bInverseCondition"), *Inverse == TEXT("true"));
		}
	}
	// --- TagCooldown decorator ---
	else if (Type == TEXT("TagCooldown"))
	{
		if (const FString* Tag = Merged.Find(TEXT("cooldown_tag")))
		{
			FProperty* TagProp = Node->GetClass()->FindPropertyByName(TEXT("CooldownTag"));
			if (TagProp)
			{
				FGameplayTag* TagPtr = TagProp->ContainerPtrToValuePtr<FGameplayTag>(Node);
				if (TagPtr)
				{
					*TagPtr = FGameplayTag::RequestGameplayTag(FName(**Tag), false);
				}
			}
		}
		if (const FString* CDTime = Merged.Find(TEXT("cool_down_time")))
		{
			SetFloatProperty(Node, TEXT("CoolDownTime"), FCString::Atof(**CDTime));
		}
		if (const FString* AddExisting = Merged.Find(TEXT("add_to_existing")))
		{
			SetBoolProperty(Node, TEXT("bAddToExistingDuration"), *AddExisting == TEXT("true"));
		}
	}
	// --- ConditionalLoop decorator (inherits from Blackboard) ---
	else if (Type == TEXT("ConditionalLoop"))
	{
		if (const FString* KeyName = Merged.Find(TEXT("blackboard_key")))
		{
			ResolveBlackboardKey(Node, *KeyName, Blackboard);
		}
		if (const FString* Condition = Merged.Find(TEXT("condition")))
		{
			if (*Condition == TEXT("IsSet"))
			{
				SetUInt8Property(Node, TEXT("BasicOperation"), static_cast<uint8>(EBasicKeyOperation::Set));
			}
			else if (*Condition == TEXT("IsNotSet"))
			{
				SetUInt8Property(Node, TEXT("BasicOperation"), static_cast<uint8>(EBasicKeyOperation::NotSet));
			}
			else if (*Condition == TEXT("Equal"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::Equal));
			}
			else if (*Condition == TEXT("NotEqual"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::NotEqual));
			}
			else if (*Condition == TEXT("Less"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::Less));
			}
			else if (*Condition == TEXT("LessOrEqual"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::LessOrEqual));
			}
			else if (*Condition == TEXT("Greater"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::Greater));
			}
			else if (*Condition == TEXT("GreaterOrEqual"))
			{
				SetUInt8Property(Node, TEXT("ArithmeticOperation"), static_cast<uint8>(EArithmeticKeyOperation::GreaterOrEqual));
			}
		}
		if (const FString* IntVal = Merged.Find(TEXT("int_value")))
		{
			SetIntProperty(Node, TEXT("IntValue"), FCString::Atoi(**IntVal));
		}
		if (const FString* FloatVal = Merged.Find(TEXT("float_value")))
		{
			SetFloatProperty(Node, TEXT("FloatValue"), FCString::Atof(**FloatVal));
		}
	}
	// --- KeepInCone decorator ---
	else if (Type == TEXT("KeepInCone"))
	{
		if (const FString* Angle = Merged.Find(TEXT("cone_half_angle")))
		{
			SetFloatProperty(Node, TEXT("ConeHalfAngle"), FCString::Atof(**Angle));
		}
		if (const FString* Origin = Merged.Find(TEXT("cone_origin")))
		{
			ResolveBlackboardKey(Node, *Origin, Blackboard, TEXT("ConeOrigin"));
		}
		if (const FString* Observed = Merged.Find(TEXT("observed")))
		{
			ResolveBlackboardKey(Node, *Observed, Blackboard, TEXT("Observed"));
		}
	}
	// --- IsBBEntryOfClass decorator ---
	else if (Type == TEXT("IsBBEntryOfClass"))
	{
		if (const FString* KeyName = Merged.Find(TEXT("blackboard_key")))
		{
			ResolveBlackboardKey(Node, *KeyName, Blackboard);
		}
		if (const FString* ClassName = Merged.Find(TEXT("test_class")))
		{
			UClass* TestClass = FindObject<UClass>(ANY_PACKAGE, **ClassName);
			if (!TestClass)
			{
				TestClass = LoadObject<UClass>(nullptr, **ClassName);
			}
			if (TestClass)
			{
				FProperty* ClassProp = Node->GetClass()->FindPropertyByName(TEXT("TestClass"));
				if (ClassProp)
				{
					UClass** ClassPtr = ClassProp->ContainerPtrToValuePtr<UClass*>(Node);
					if (ClassPtr) *ClassPtr = TestClass;
				}
			}
		}
	}
	// --- SimpleParallel composite ---
	else if (Type == TEXT("SimpleParallel"))
	{
		if (const FString* Mode = Merged.Find(TEXT("finish_mode")))
		{
			// EBTParallelMode: AbortBackground = 0, WaitForBackground = 1
			FProperty* ModeProp = Node->GetClass()->FindPropertyByName(TEXT("FinishMode"));
			if (ModeProp)
			{
				uint8* ModePtr = ModeProp->ContainerPtrToValuePtr<uint8>(Node);
				if (ModePtr)
				{
					if (*Mode == TEXT("Immediate"))  *ModePtr = 0;  // AbortBackground
					else if (*Mode == TEXT("Delayed")) *ModePtr = 1;  // WaitForBackground
				}
			}
		}
	}
	// --- DefaultFocus service ---
	else if (Type == TEXT("DefaultFocus"))
	{
		if (const FString* KeyName = Merged.Find(TEXT("blackboard_key")))
		{
			ResolveBlackboardKey(Node, *KeyName, Blackboard);
		}
	}
}
