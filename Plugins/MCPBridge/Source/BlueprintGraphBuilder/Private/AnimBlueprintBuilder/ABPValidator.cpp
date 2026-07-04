#include "ABPValidator.h"
#include "ABPBuildSpec.h"
#include "Animation/AnimSequence.h"

TArray<FString> FAnimBPValidator::Validate(const FAnimBPBuildSpec& Spec)
{
	TArray<FString> Errors;

	// Rule 1: Exactly one entry state
	{
		int32 EntryCount = 0;
		for (const FAnimBPStateSpec& State : Spec.States)
		{
			if (State.bIsEntry)
			{
				EntryCount++;
			}
		}
		if (EntryCount == 0)
		{
			Errors.Add(TEXT("[ABPValidator] no entry state found (exactly one state must have bIsEntry=true)"));
		}
		else if (EntryCount > 1)
		{
			Errors.Add(FString::Printf(TEXT("[ABPValidator] found %d entry states (exactly one allowed)"), EntryCount));
		}
	}

	// Rule 2: Unique state IDs
	TSet<FString> StateIds;
	for (int32 Index = 0; Index < Spec.States.Num(); ++Index)
	{
		const FAnimBPStateSpec& State = Spec.States[Index];
		if (State.Id.IsEmpty())
		{
			Errors.Add(FString::Printf(TEXT("[ABPValidator] state at index %d has empty id"), Index));
			continue;
		}
		if (StateIds.Contains(State.Id))
		{
			Errors.Add(FString::Printf(TEXT("[ABPValidator] duplicate state id '%s'"), *State.Id));
		}
		else
		{
			StateIds.Add(State.Id);
		}
	}

	// Rule 3: Transition references valid
	for (const FAnimBPTransitionSpec& Trans : Spec.Transitions)
	{
		if (!StateIds.Contains(Trans.From))
		{
			Errors.Add(FString::Printf(TEXT("[ABPValidator] transition references unknown 'from' state '%s'"), *Trans.From));
		}
		if (!StateIds.Contains(Trans.To))
		{
			Errors.Add(FString::Printf(TEXT("[ABPValidator] transition references unknown 'to' state '%s'"), *Trans.To));
		}
	}

	// Rule 4: Animation paths loadable
	for (const FAnimBPStateSpec& State : Spec.States)
	{
		if (!State.Animation.IsEmpty())
		{
			UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *State.Animation);
			if (!Seq)
			{
				Errors.Add(FString::Printf(TEXT("[ABPValidator] state '%s' animation path '%s' failed to load"), *State.Id, *State.Animation));
			}
		}
	}

	// Rule 5: Variable references exist
	{
		TSet<FString> VarNames;
		for (const FAnimBPVariableSpec& Var : Spec.Variables)
		{
			VarNames.Add(Var.Name);
		}

		for (const FAnimBPTransitionSpec& Trans : Spec.Transitions)
		{
			if (Trans.Condition.Type == TEXT("bool_variable"))
			{
				if (!VarNames.Contains(Trans.Condition.Variable))
				{
					Errors.Add(FString::Printf(
						TEXT("[ABPValidator] transition %s->%s references unknown variable '%s'"),
						*Trans.From, *Trans.To, *Trans.Condition.Variable));
				}
			}
		}
	}

	// Rule: condition type must be known
	for (const FAnimBPTransitionSpec& Trans : Spec.Transitions)
	{
		if (!Trans.Condition.Type.IsEmpty()
			&& Trans.Condition.Type != TEXT("bool_variable")
			&& Trans.Condition.Type != TEXT("time_remaining"))
		{
			Errors.Add(FString::Printf(
				TEXT("[ABPValidator] transition %s->%s has unknown condition type '%s'"),
				*Trans.From, *Trans.To, *Trans.Condition.Type));
		}
	}

	// Rule 6: Exactly one StateMachine in pipeline
	{
		int32 SMCount = 0;
		for (const FAnimBPAnimGraphNodeSpec& Node : Spec.AnimGraphPipeline)
		{
			if (Node.Type == TEXT("StateMachine"))
			{
				SMCount++;
			}
		}
		if (SMCount == 0)
		{
			Errors.Add(TEXT("[ABPValidator] pipeline has no StateMachine node (exactly one required)"));
		}
		else if (SMCount > 1)
		{
			Errors.Add(FString::Printf(TEXT("[ABPValidator] pipeline has %d StateMachine nodes (exactly one allowed)"), SMCount));
		}
	}

	// Rule 7: TimeRemaining only from non-looping states
	for (const FAnimBPTransitionSpec& Trans : Spec.Transitions)
	{
		if (Trans.Condition.Type == TEXT("time_remaining"))
		{
			for (const FAnimBPStateSpec& State : Spec.States)
			{
				if (State.Id == Trans.From && State.bLooping)
				{
					Errors.Add(FString::Printf(
						TEXT("[ABPValidator] transition %s->%s uses time_remaining but source state '%s' is looping"),
						*Trans.From, *Trans.To, *State.Id));
					break;
				}
			}
		}
	}

	// Rule 8: Non-negative blend time
	for (const FAnimBPTransitionSpec& Trans : Spec.Transitions)
	{
		if (Trans.BlendTime < 0.0f)
		{
			Errors.Add(FString::Printf(
				TEXT("[ABPValidator] transition %s->%s has negative BlendTime %.3f"),
				*Trans.From, *Trans.To, Trans.BlendTime));
		}
	}

	// Rule 9: Variable types are bool
	for (const FAnimBPVariableSpec& Var : Spec.Variables)
	{
		if (Var.Type != TEXT("bool"))
		{
			Errors.Add(FString::Printf(
				TEXT("[ABPValidator] variable '%s' has unsupported type '%s' (only 'bool' is supported)"),
				*Var.Name, *Var.Type));
		}
	}

	// Rule 10: Unique pipeline node IDs
	{
		TSet<FString> PipelineIds;
		for (const FAnimBPAnimGraphNodeSpec& Node : Spec.AnimGraphPipeline)
		{
			if (PipelineIds.Contains(Node.Id))
			{
				Errors.Add(FString::Printf(TEXT("[ABPValidator] duplicate pipeline node id '%s'"), *Node.Id));
			}
			else
			{
				PipelineIds.Add(Node.Id);
			}
		}
	}

	return Errors;
}
