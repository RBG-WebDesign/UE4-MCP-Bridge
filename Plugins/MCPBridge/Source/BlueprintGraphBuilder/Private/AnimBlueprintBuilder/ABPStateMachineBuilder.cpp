#include "ABPStateMachineBuilder.h"
#include "ABPBuildSpec.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetMathLibrary.h"
#include "EdGraphSchema_K2.h"

FString FAnimBPStateMachineBuilder::BuildStates(const FAnimBPBuildSpec& Spec, FAnimBPBuildContext& Ctx)
{
	if (!Ctx.StateMachineGraph)
	{
		return TEXT("[ABPStateMachineBuilder] StateMachineGraph is null -- AnimGraph must be built first");
	}

	// Create all state nodes
	for (int32 Index = 0; Index < Spec.States.Num(); ++Index)
	{
		const FAnimBPStateSpec& State = Spec.States[Index];

		// 1. Create state node
		UAnimStateNode* StateNode = NewObject<UAnimStateNode>(Ctx.StateMachineGraph);
		StateNode->CreateNewGuid();
		StateNode->PostPlacedNewNode();
		StateNode->AllocateDefaultPins();
		Ctx.StateMachineGraph->AddNode(StateNode, false, false);

		// 2. Set state name via bound graph rename
		StateNode->GetBoundGraph()->Rename(*State.Name, nullptr, REN_None);

		// 3. Get the state's inner graph and result node
		UAnimationStateGraph* StateGraph = Cast<UAnimationStateGraph>(StateNode->GetBoundGraph());
		if (!StateGraph)
		{
			return FString::Printf(TEXT("[ABPStateMachineBuilder] failed to get state graph for state: %s"), *State.Name);
		}

		UAnimGraphNode_StateResult* ResultNode = StateGraph->MyResultNode;
		if (!ResultNode)
		{
			return FString::Printf(TEXT("[ABPStateMachineBuilder] no result node in state graph for state: %s"), *State.Name);
		}

		// 4. Create SequencePlayer node
		UAnimGraphNode_SequencePlayer* SeqPlayer = NewObject<UAnimGraphNode_SequencePlayer>(StateGraph);
		SeqPlayer->CreateNewGuid();
		SeqPlayer->PostPlacedNewNode();
		SeqPlayer->AllocateDefaultPins();
		StateGraph->AddNode(SeqPlayer, false, false);

		// 5. Load animation and set properties
		UAnimSequence* AnimSeq = LoadObject<UAnimSequence>(nullptr, *State.Animation);
		if (!AnimSeq)
		{
			return FString::Printf(TEXT("[ABPStateMachineBuilder] animation not found: %s"), *State.Animation);
		}
		SeqPlayer->Node.Sequence = AnimSeq;
		SeqPlayer->Node.bLoopAnimation = State.bLooping;

		// 6. Wire SequencePlayer output pose to StateResult input pose
		UEdGraphPin* SeqOutput = nullptr;
		UEdGraphPin* ResultInput = nullptr;

		for (UEdGraphPin* Pin : SeqPlayer->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				SeqOutput = Pin;
				break;
			}
		}

		for (UEdGraphPin* Pin : ResultNode->Pins)
		{
			if (Pin->Direction == EGPD_Input)
			{
				ResultInput = Pin;
				break;
			}
		}

		if (!SeqOutput)
			return FString::Printf(TEXT("[ABPStateMachineBuilder] no output pose pin on SequencePlayer for state '%s'"), *State.Name);
		if (!ResultInput)
			return FString::Printf(TEXT("[ABPStateMachineBuilder] no input pin on StateResult for state '%s'"), *State.Name);
		SeqOutput->MakeLinkTo(ResultInput);

		// 7. Store in context and position nodes
		Ctx.StateNodeMap.Add(State.Id, StateNode);
		StateNode->NodePosX = (Index % 3) * 400;
		StateNode->NodePosY = (Index / 3) * 300;
		SeqPlayer->NodePosX = -200;
		SeqPlayer->NodePosY = 0;
	}

	// 8. Wire entry state
	for (const FAnimBPStateSpec& State : Spec.States)
	{
		if (State.bIsEntry)
		{
			UAnimStateNode* EntryState = Ctx.StateNodeMap.FindRef(State.Id);
			if (EntryState && Ctx.StateMachineGraph->EntryNode)
			{
				UEdGraphPin* EntryOutput = nullptr;
				for (UEdGraphPin* Pin : Ctx.StateMachineGraph->EntryNode->Pins)
				{
					if (Pin->Direction == EGPD_Output)
					{
						EntryOutput = Pin;
						break;
					}
				}

				UEdGraphPin* StateInput = nullptr;
				for (UEdGraphPin* Pin : EntryState->Pins)
				{
					if (Pin->Direction == EGPD_Input)
					{
						StateInput = Pin;
						break;
					}
				}

				if (!EntryOutput)
					return TEXT("[ABPStateMachineBuilder] no output pin on entry node");
				if (!StateInput)
					return FString::Printf(TEXT("[ABPStateMachineBuilder] no input pin on entry state '%s'"), *State.Id);
				EntryOutput->MakeLinkTo(StateInput);
			}
			break;
		}
	}

	return FString();
}

FString FAnimBPStateMachineBuilder::BuildTransitions(const FAnimBPBuildSpec& Spec, FAnimBPBuildContext& Ctx)
{
	if (!Ctx.StateMachineGraph)
	{
		return TEXT("[ABPStateMachineBuilder] StateMachineGraph is null -- cannot build transitions");
	}

	for (int32 Index = 0; Index < Spec.Transitions.Num(); ++Index)
	{
		const FAnimBPTransitionSpec& Trans = Spec.Transitions[Index];

		// 1. Look up states
		UAnimStateNode* FromState = Ctx.StateNodeMap.FindRef(Trans.From);
		UAnimStateNode* ToState = Ctx.StateNodeMap.FindRef(Trans.To);
		if (!FromState)
		{
			return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: source state '%s' not found"), Index, *Trans.From);
		}
		if (!ToState)
		{
			return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: target state '%s' not found"), Index, *Trans.To);
		}

		// 2. Create transition node
		UAnimStateTransitionNode* TransNode = NewObject<UAnimStateTransitionNode>(Ctx.StateMachineGraph);
		TransNode->CreateNewGuid();
		TransNode->PostPlacedNewNode();
		TransNode->AllocateDefaultPins();
		Ctx.StateMachineGraph->AddNode(TransNode, false, false);

		// 3. Wire state pins: FromState output -> TransNode input, TransNode output -> ToState input
		UEdGraphPin* FromOutput = nullptr;
		for (UEdGraphPin* Pin : FromState->Pins)
		{
			if (Pin->Direction == EGPD_Output) { FromOutput = Pin; break; }
		}
		UEdGraphPin* TransInput = nullptr;
		for (UEdGraphPin* Pin : TransNode->Pins)
		{
			if (Pin->Direction == EGPD_Input) { TransInput = Pin; break; }
		}
		if (!FromOutput)
			return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: no output pin on source state '%s'"), Index, *Trans.From);
		if (!TransInput)
			return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: no input pin on transition node"), Index);
		FromOutput->MakeLinkTo(TransInput);

		UEdGraphPin* TransOutput = nullptr;
		for (UEdGraphPin* Pin : TransNode->Pins)
		{
			if (Pin->Direction == EGPD_Output) { TransOutput = Pin; break; }
		}
		UEdGraphPin* ToInput = nullptr;
		for (UEdGraphPin* Pin : ToState->Pins)
		{
			if (Pin->Direction == EGPD_Input) { ToInput = Pin; break; }
		}
		if (!TransOutput)
			return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: no output pin on transition node"), Index);
		if (!ToInput)
			return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: no input pin on target state '%s'"), Index, *Trans.To);
		TransOutput->MakeLinkTo(ToInput);

		// 4. Set blend time
		TransNode->CrossfadeDuration = Trans.BlendTime;

		// 5. Build condition graph
		if (Trans.Condition.Type == TEXT("bool_variable"))
		{
			UAnimationTransitionGraph* TransGraph = Cast<UAnimationTransitionGraph>(TransNode->GetBoundGraph());
			if (!TransGraph)
			{
				return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: no transition graph"), Index);
			}

			UAnimGraphNode_TransitionResult* ResultNode = TransGraph->MyResultNode;
			if (!ResultNode)
			{
				return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: no result node in transition graph"), Index);
			}

			// Find bCanEnterTransition input pin
			UEdGraphPin* CanEnterPin = nullptr;
			for (UEdGraphPin* Pin : ResultNode->Pins)
			{
				if (Pin->PinName == TEXT("bCanEnterTransition") && Pin->Direction == EGPD_Input)
				{
					CanEnterPin = Pin;
					break;
				}
			}
			if (!CanEnterPin)
			{
				return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: no bCanEnterTransition pin on result node"), Index);
			}

			// Create variable get node for the bool variable
			UK2Node_VariableGet* VarGet = NewObject<UK2Node_VariableGet>(TransGraph);
			VarGet->VariableReference.SetSelfMember(FName(*Trans.Condition.Variable));
			VarGet->CreateNewGuid();
			VarGet->PostPlacedNewNode();
			VarGet->AllocateDefaultPins();
			TransGraph->AddNode(VarGet, false, false);
			VarGet->NodePosX = ResultNode->NodePosX - 300;
			VarGet->NodePosY = ResultNode->NodePosY;

			// Find bool output pin
			UEdGraphPin* BoolOutput = nullptr;
			for (UEdGraphPin* Pin : VarGet->Pins)
			{
				if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
				{
					BoolOutput = Pin;
					break;
				}
			}
			if (!BoolOutput)
			{
				return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: no bool output pin on variable get for '%s'"), Index, *Trans.Condition.Variable);
			}

			if (Trans.Condition.Value == TEXT("true"))
			{
				// Wire directly: VarGet bool -> CanEnterTransition
				BoolOutput->MakeLinkTo(CanEnterPin);
			}
			else
			{
				// Insert NOT node between VarGet and CanEnterTransition
				UK2Node_CallFunction* NotNode = NewObject<UK2Node_CallFunction>(TransGraph);
				NotNode->CreateNewGuid();
				NotNode->PostPlacedNewNode();
				NotNode->FunctionReference.SetExternalMember(
					GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Not_Bool),
					UKismetMathLibrary::StaticClass());
				NotNode->AllocateDefaultPins();
				TransGraph->AddNode(NotNode, false, false);
				NotNode->NodePosX = ResultNode->NodePosX - 150;
				NotNode->NodePosY = ResultNode->NodePosY;

				UEdGraphPin* NotInput = NotNode->FindPin(TEXT("A"));
				UEdGraphPin* NotOutput = NotNode->GetReturnValuePin();
				if (!NotInput || !NotOutput)
				{
					return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: failed to find NOT node pins"), Index);
				}

				BoolOutput->MakeLinkTo(NotInput);
				NotOutput->MakeLinkTo(CanEnterPin);
			}
		}
		else if (Trans.Condition.Type == TEXT("time_remaining"))
		{
			// Use UE4's built-in automatic transition rule based on sequence player remaining time.
			// No condition graph needed -- the engine handles this natively.

			// bAutomaticRuleBasedOnSequencePlayerInState is a uint8:1 bitfield in UE4.27.
			// Direct assignment can corrupt adjacent bits. Use FBoolProperty for safe set.
			FBoolProperty* AutoRuleProp = CastField<FBoolProperty>(
				TransNode->GetClass()->FindPropertyByName(TEXT("bAutomaticRuleBasedOnSequencePlayerInState")));
			if (AutoRuleProp)
			{
				AutoRuleProp->SetPropertyValue_InContainer(TransNode, true);
			}
			else
			{
				return FString::Printf(TEXT("[ABPStateMachineBuilder] transition %d: could not find bAutomaticRuleBasedOnSequencePlayerInState property"), Index);
			}

			// AutomaticRuleTriggerTime is a regular float, safe to set directly
			TransNode->AutomaticRuleTriggerTime = Trans.Condition.Threshold;
		}
	}

	return FString();
}
