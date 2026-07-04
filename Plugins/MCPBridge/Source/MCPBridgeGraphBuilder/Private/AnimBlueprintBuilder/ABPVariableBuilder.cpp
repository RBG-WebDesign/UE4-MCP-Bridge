#include "ABPVariableBuilder.h"
#include "ABPBuildSpec.h"
#include "Animation/AnimBlueprint.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"

FString FAnimBPVariableBuilder::AddVariables(
	UAnimBlueprint* AnimBP,
	const TArray<FAnimBPVariableSpec>& Variables)
{
	if (!AnimBP) return TEXT("[ABPVariableBuilder] AnimBP is null");

	for (const FAnimBPVariableSpec& Var : Variables)
	{
		// Check if variable already exists (for Rebuild path)
		if (FBlueprintEditorUtils::FindMemberVariableGuidByName(AnimBP, FName(*Var.Name)).IsValid())
		{
			continue;
		}

		FEdGraphPinType PinType;
		if (Var.Type == TEXT("bool"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else
		{
			return FString::Printf(TEXT("[ABPVariableBuilder] unsupported variable type '%s' for '%s'"),
				*Var.Type, *Var.Name);
		}

		bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(AnimBP, FName(*Var.Name), PinType);
		if (!bSuccess)
		{
			return FString::Printf(TEXT("[ABPVariableBuilder] failed to add variable '%s'"), *Var.Name);
		}

		// Set default value
		if (!Var.Default.IsEmpty() && Var.Default == TEXT("true"))
		{
			for (FBPVariableDescription& VarDesc : AnimBP->NewVariables)
			{
				if (VarDesc.VarName == FName(*Var.Name))
				{
					VarDesc.DefaultValue = TEXT("true");
					break;
				}
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[ABPVariableBuilder] added %s variable '%s'"), *Var.Type, *Var.Name);
	}

	return FString();
}
