#include "ABPJsonParser.h"
#include "ABPBuildSpec.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

static FString ParseVariables(const TSharedPtr<FJsonObject>& RootObj, TArray<FAnimBPVariableSpec>& OutVariables)
{
	const TArray<TSharedPtr<FJsonValue>>* VarsArr = nullptr;
	if (!RootObj->TryGetArrayField(TEXT("variables"), VarsArr))
	{
		return FString(); // optional section
	}

	for (int32 i = 0; i < VarsArr->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* VarObj = nullptr;
		if (!(*VarsArr)[i]->TryGetObject(VarObj))
		{
			return FString::Printf(TEXT("[ABPJsonParser] variables[%d] is not an object"), i);
		}

		FAnimBPVariableSpec Var;

		if (!(*VarObj)->TryGetStringField(TEXT("name"), Var.Name) || Var.Name.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] variables[%d] missing required field 'name'"), i);
		}

		if (!(*VarObj)->TryGetStringField(TEXT("type"), Var.Type) || Var.Type.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] variables[%d] missing required field 'type'"), i);
		}

		if (Var.Type != TEXT("bool"))
		{
			return FString::Printf(TEXT("[ABPJsonParser] variables[%d] unsupported type '%s' (only 'bool' supported in v1)"), i, *Var.Type);
		}

		if (!(*VarObj)->TryGetStringField(TEXT("default"), Var.Default))
		{
			Var.Default = TEXT("false");
		}

		OutVariables.Add(MoveTemp(Var));
	}

	return FString();
}

static FString ParseAnimGraphPipeline(const TSharedPtr<FJsonObject>& RootObj, TArray<FAnimBPAnimGraphNodeSpec>& OutPipeline)
{
	const TSharedPtr<FJsonObject>* AnimGraphObj = nullptr;
	if (!RootObj->TryGetObjectField(TEXT("anim_graph"), AnimGraphObj))
	{
		return TEXT("[ABPJsonParser] missing required section 'anim_graph'");
	}

	const TArray<TSharedPtr<FJsonValue>>* PipelineArr = nullptr;
	if (!(*AnimGraphObj)->TryGetArrayField(TEXT("pipeline"), PipelineArr))
	{
		return TEXT("[ABPJsonParser] missing required field 'anim_graph.pipeline'");
	}

	for (int32 i = 0; i < PipelineArr->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* NodeObj = nullptr;
		if (!(*PipelineArr)[i]->TryGetObject(NodeObj))
		{
			return FString::Printf(TEXT("[ABPJsonParser] anim_graph.pipeline[%d] is not an object"), i);
		}

		FAnimBPAnimGraphNodeSpec Node;

		if (!(*NodeObj)->TryGetStringField(TEXT("id"), Node.Id) || Node.Id.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] anim_graph.pipeline[%d] missing required field 'id'"), i);
		}

		if (!(*NodeObj)->TryGetStringField(TEXT("type"), Node.Type) || Node.Type.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] anim_graph.pipeline[%d] missing required field 'type'"), i);
		}

		if (!(*NodeObj)->TryGetStringField(TEXT("name"), Node.Name) || Node.Name.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] anim_graph.pipeline[%d] missing required field 'name'"), i);
		}

		OutPipeline.Add(MoveTemp(Node));
	}

	return FString();
}

static FString ParseTransitionCondition(const TSharedPtr<FJsonObject>& CondObj, FAnimBPTransitionConditionSpec& OutCondition, const FString& Path)
{
	if (!CondObj->TryGetStringField(TEXT("type"), OutCondition.Type) || OutCondition.Type.IsEmpty())
	{
		return FString::Printf(TEXT("[ABPJsonParser] %s.condition missing required field 'type'"), *Path);
	}

	if (OutCondition.Type == TEXT("bool_variable"))
	{
		if (!CondObj->TryGetStringField(TEXT("variable"), OutCondition.Variable) || OutCondition.Variable.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] %s.condition(bool_variable) missing required field 'variable'"), *Path);
		}

		bool BoolValue = false;
		if (!CondObj->TryGetBoolField(TEXT("value"), BoolValue))
		{
			return FString::Printf(TEXT("[ABPJsonParser] %s.condition(bool_variable) missing required field 'value'"), *Path);
		}
		OutCondition.Value = BoolValue ? TEXT("true") : TEXT("false");
	}
	else if (OutCondition.Type == TEXT("time_remaining"))
	{
		double ThresholdVal = 0.0;
		if (!CondObj->TryGetNumberField(TEXT("threshold"), ThresholdVal))
		{
			return FString::Printf(TEXT("[ABPJsonParser] %s.condition(time_remaining) missing required field 'threshold'"), *Path);
		}
		OutCondition.Threshold = static_cast<float>(ThresholdVal);
	}
	else
	{
		return FString::Printf(TEXT("[ABPJsonParser] %s.condition has unknown type '%s'"), *Path, *OutCondition.Type);
	}

	return FString();
}

static FString ParseStateMachine(const TSharedPtr<FJsonObject>& RootObj, TArray<FAnimBPStateSpec>& OutStates, TArray<FAnimBPTransitionSpec>& OutTransitions)
{
	const TSharedPtr<FJsonObject>* SMObj = nullptr;
	if (!RootObj->TryGetObjectField(TEXT("state_machine"), SMObj))
	{
		return TEXT("[ABPJsonParser] missing required section 'state_machine'");
	}

	// Parse states
	const TArray<TSharedPtr<FJsonValue>>* StatesArr = nullptr;
	if (!(*SMObj)->TryGetArrayField(TEXT("states"), StatesArr))
	{
		return TEXT("[ABPJsonParser] missing required field 'state_machine.states'");
	}

	for (int32 i = 0; i < StatesArr->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* StateObj = nullptr;
		if (!(*StatesArr)[i]->TryGetObject(StateObj))
		{
			return FString::Printf(TEXT("[ABPJsonParser] state_machine.states[%d] is not an object"), i);
		}

		FAnimBPStateSpec State;

		if (!(*StateObj)->TryGetStringField(TEXT("id"), State.Id) || State.Id.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] state_machine.states[%d] missing required field 'id'"), i);
		}

		if (!(*StateObj)->TryGetStringField(TEXT("name"), State.Name) || State.Name.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] state_machine.states[%d] missing required field 'name'"), i);
		}

		if (!(*StateObj)->TryGetStringField(TEXT("animation"), State.Animation) || State.Animation.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] state_machine.states[%d] missing required field 'animation'"), i);
		}

		(*StateObj)->TryGetBoolField(TEXT("looping"), State.bLooping);
		(*StateObj)->TryGetBoolField(TEXT("is_entry"), State.bIsEntry);

		OutStates.Add(MoveTemp(State));
	}

	// Parse transitions
	const TArray<TSharedPtr<FJsonValue>>* TransArr = nullptr;
	if (!(*SMObj)->TryGetArrayField(TEXT("transitions"), TransArr))
	{
		return TEXT("[ABPJsonParser] missing required field 'state_machine.transitions'");
	}

	for (int32 i = 0; i < TransArr->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* TransObj = nullptr;
		if (!(*TransArr)[i]->TryGetObject(TransObj))
		{
			return FString::Printf(TEXT("[ABPJsonParser] state_machine.transitions[%d] is not an object"), i);
		}

		FAnimBPTransitionSpec Trans;
		FString TransPath = FString::Printf(TEXT("state_machine.transitions[%d]"), i);

		if (!(*TransObj)->TryGetStringField(TEXT("from"), Trans.From) || Trans.From.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] %s missing required field 'from'"), *TransPath);
		}

		if (!(*TransObj)->TryGetStringField(TEXT("to"), Trans.To) || Trans.To.IsEmpty())
		{
			return FString::Printf(TEXT("[ABPJsonParser] %s missing required field 'to'"), *TransPath);
		}

		double BlendVal = 0.0;
		if ((*TransObj)->TryGetNumberField(TEXT("blend_time"), BlendVal))
		{
			Trans.BlendTime = static_cast<float>(BlendVal);
		}

		const TSharedPtr<FJsonObject>* CondObj = nullptr;
		if (!(*TransObj)->TryGetObjectField(TEXT("condition"), CondObj))
		{
			return FString::Printf(TEXT("[ABPJsonParser] %s missing required field 'condition'"), *TransPath);
		}

		FString CondError = ParseTransitionCondition(*CondObj, Trans.Condition, TransPath);
		if (!CondError.IsEmpty())
		{
			return CondError;
		}

		OutTransitions.Add(MoveTemp(Trans));
	}

	return FString();
}

static FString ParseEventGraph(const TSharedPtr<FJsonObject>& RootObj, FString& OutEventGraphJson)
{
	const TSharedPtr<FJsonObject>* EventGraphObj = nullptr;
	if (!RootObj->TryGetObjectField(TEXT("event_graph"), EventGraphObj))
	{
		return FString(); // optional section
	}

	// Serialize the event_graph object back to a JSON string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (!FJsonSerializer::Serialize((*EventGraphObj).ToSharedRef(), Writer))
	{
		return TEXT("[ABPJsonParser] failed to serialize event_graph back to JSON string");
	}
	Writer->Close();

	OutEventGraphJson = OutputString;
	return FString();
}

FString FAnimBPJsonParser::Parse(const FString& JsonString, FAnimBPBuildSpec& OutSpec)
{
	TSharedPtr<FJsonObject> RootObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		return TEXT("[ABPJsonParser] failed to parse JSON");
	}

	// 1. Variables (optional)
	FString Error = ParseVariables(RootObj, OutSpec.Variables);
	if (!Error.IsEmpty()) return Error;

	// 2. Anim graph pipeline (required)
	Error = ParseAnimGraphPipeline(RootObj, OutSpec.AnimGraphPipeline);
	if (!Error.IsEmpty()) return Error;

	// 3. State machine states and transitions (required)
	Error = ParseStateMachine(RootObj, OutSpec.States, OutSpec.Transitions);
	if (!Error.IsEmpty()) return Error;

	// 4. Event graph (optional, forwarded as raw JSON string)
	Error = ParseEventGraph(RootObj, OutSpec.EventGraphJson);
	if (!Error.IsEmpty()) return Error;

	return FString();
}
