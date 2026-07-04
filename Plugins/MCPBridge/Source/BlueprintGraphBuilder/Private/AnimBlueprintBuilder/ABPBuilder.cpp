#include "ABPBuilder.h"
#include "ABPBuildSpec.h"
#include "ABPNodeRegistry.h"
#include "ABPAssetFactory.h"
#include "ABPJsonParser.h"
#include "ABPValidator.h"
#include "ABPVariableBuilder.h"
#include "ABPAnimGraphBuilder.h"
#include "ABPStateMachineBuilder.h"
#include "BlueprintGraphBuilderLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"

FString FAnimBPBuilder::Build(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& SkeletonPath,
	const FString& JsonString)
{
	// Step 1: PARSE
	FAnimBPBuildSpec Spec;
	FString ParseError = FAnimBPJsonParser::Parse(JsonString, Spec);
	if (!ParseError.IsEmpty())
	{
		return ParseError;
	}

	// Step 2: VALIDATE
	TArray<FString> ValidationErrors = FAnimBPValidator::Validate(Spec);
	if (ValidationErrors.Num() > 0)
	{
		return FString::Join(ValidationErrors, TEXT("\n"));
	}

	// Step 3: CREATE ASSET
	FString Error;
	USkeleton* Skeleton = FAnimBPAssetFactory::ResolveSkeleton(SkeletonPath, Error);
	if (!Skeleton)
	{
		return Error;
	}

	UAnimBlueprint* AnimBP = FAnimBPAssetFactory::Create(PackagePath, AssetName, Skeleton, Error);
	if (!AnimBP)
	{
		return Error;
	}

	// Step 4: VARIABLES
	{
		FString VarError = FAnimBPVariableBuilder::AddVariables(AnimBP, Spec.Variables);
		if (!VarError.IsEmpty()) return VarError;
	}

	// Compile after variables so they are available for later steps
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	{
		FKismetCompilerOptions CompileOptions;
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(AnimBP, CompileOptions, &Results);
		if (Results.NumErrors > 0)
		{
			return FString::Printf(TEXT("[AnimBPBuilder] compile after variables failed with %d error(s)"), Results.NumErrors);
		}
	}

	// Step 5: BUILD AnimGraph pipeline (StateMachine, Slot, Root wiring)
	FAnimBPBuildContext BuildCtx;
	BuildCtx.AnimBlueprint = AnimBP;
	BuildCtx.Skeleton = Skeleton;
	BuildCtx.Registry = &Registry;

	{
		FString BuildError = FAnimBPAnimGraphBuilder::Build(Spec, BuildCtx);
		if (!BuildError.IsEmpty()) return BuildError;
	}

	// Step 5b: BUILD States
	{
		FString StatesError = FAnimBPStateMachineBuilder::BuildStates(Spec, BuildCtx);
		if (!StatesError.IsEmpty()) return StatesError;
	}

	// Step 5c: BUILD Transitions
	{
		FString TransError = FAnimBPStateMachineBuilder::BuildTransitions(Spec, BuildCtx);
		if (!TransError.IsEmpty()) return TransError;
	}

	// Step 9: EVENT GRAPH (delegate to existing BlueprintGraphBuilder)
	if (!Spec.EventGraphJson.IsEmpty())
	{
		UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON(
			AnimBP, Spec.EventGraphJson, /*bClearExistingGraph=*/ false);
	}

	// Step 10: FINAL COMPILE
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
		FKismetCompilerOptions CompileOptions;
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(AnimBP, CompileOptions, &Results);
		if (Results.NumErrors > 0)
		{
			return FString::Printf(TEXT("[AnimBPBuilder] final compile failed with %d error(s)"), Results.NumErrors);
		}
	}
	AnimBP->MarkPackageDirty();

	UE_LOG(LogTemp, Log, TEXT("[AnimBPBuilder] built AnimBP '%s'"), *AnimBP->GetName());
	return FString();
}

FString FAnimBPBuilder::Rebuild(UAnimBlueprint* AnimBP, const FString& JsonString)
{
	if (!AnimBP) return TEXT("[AnimBPBuilder] AnimBlueprint is null");

	// Parse + validate
	FAnimBPBuildSpec Spec;
	FString ParseError = FAnimBPJsonParser::Parse(JsonString, Spec);
	if (!ParseError.IsEmpty()) return ParseError;

	TArray<FString> Errors = FAnimBPValidator::Validate(Spec);
	if (Errors.Num() > 0) return FString::Join(Errors, TEXT("\n"));

	// Variables (add missing only, skip existing)
	FString VarError = FAnimBPVariableBuilder::AddVariables(AnimBP, Spec.Variables);
	if (!VarError.IsEmpty()) return VarError;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	{
		FKismetCompilerOptions CompileOptions;
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(AnimBP, CompileOptions, &Results);
		if (Results.NumErrors > 0)
		{
			return FString::Printf(TEXT("[AnimBPBuilder] compile after variables failed with %d error(s)"), Results.NumErrors);
		}
	}

	// Build context
	FAnimBPBuildContext Ctx;
	Ctx.AnimBlueprint = AnimBP;
	Ctx.Skeleton = AnimBP->TargetSkeleton;
	Ctx.Registry = &Registry;

	// NOTE: v1 Rebuild assumes a clean AnimBP (no existing graph nodes to clear).
	// A full implementation would clear existing AnimGraph and state machine nodes first.

	// Same build steps as Build (AnimGraph, States, Transitions, EventGraph)
	FString GraphError = FAnimBPAnimGraphBuilder::Build(Spec, Ctx);
	if (!GraphError.IsEmpty()) return GraphError;

	FString StateError = FAnimBPStateMachineBuilder::BuildStates(Spec, Ctx);
	if (!StateError.IsEmpty()) return StateError;

	FString TransError = FAnimBPStateMachineBuilder::BuildTransitions(Spec, Ctx);
	if (!TransError.IsEmpty()) return TransError;

	if (!Spec.EventGraphJson.IsEmpty())
	{
		UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON(
			AnimBP, Spec.EventGraphJson, /*bClearExistingGraph=*/ false);
	}

	// Final compile
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
		FKismetCompilerOptions CompileOptions;
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(AnimBP, CompileOptions, &Results);
		if (Results.NumErrors > 0)
		{
			return FString::Printf(TEXT("[AnimBPBuilder] final compile failed with %d error(s)"), Results.NumErrors);
		}
	}
	AnimBP->MarkPackageDirty();

	UE_LOG(LogTemp, Log, TEXT("[AnimBPBuilder] rebuilt AnimBP '%s'"), *AnimBP->GetName());
	return FString();
}

FString FAnimBPBuilder::Validate(const FString& JsonString)
{
	// Parse JSON
	FAnimBPBuildSpec Spec;
	FString ParseError = FAnimBPJsonParser::Parse(JsonString, Spec);
	if (!ParseError.IsEmpty())
	{
		return ParseError;
	}

	// Validate spec
	TArray<FString> ValidationErrors = FAnimBPValidator::Validate(Spec);
	if (ValidationErrors.Num() > 0)
	{
		return FString::Join(ValidationErrors, TEXT("\n"));
	}

	return FString();
}
