// Copyright 2026 RareBird Games. All Rights Reserved.

#include "ABPBuilder.h"
#include "ABPBuildSpec.h"
#include "ABPNodeRegistry.h"
#include "ABPAssetFactory.h"
#include "ABPJsonParser.h"
#include "ABPValidator.h"
#include "ABPVariableBuilder.h"
#include "ABPAnimGraphBuilder.h"
#include "ABPStateMachineBuilder.h"
#include "Animation/AnimBlueprint.h"
#include "BlueprintGraphBuilderLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"

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
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(AnimBP, EBlueprintCompileOptions::None, &Results);
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
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(AnimBP, EBlueprintCompileOptions::None, &Results);
		if (Results.NumErrors > 0)
		{
			return FString::Printf(TEXT("[AnimBPBuilder] final compile failed with %d error(s)"), Results.NumErrors);
		}
	}
	AnimBP->MarkPackageDirty();

	UE_LOG(LogTemp, Log, TEXT("[AnimBPBuilder] built AnimBP '%s'"), *AnimBP->GetName());
	return FString();
}

FString FAnimBPBuilder::ClearGeneratedGraph(UAnimBlueprint* AnimBP, int32& OutRemovedNodes)
{
	OutRemovedNodes = 0;
	if (!AnimBP) return TEXT("[AnimBPBuilder] AnimBlueprint is null");

	UEdGraph* AnimGraph = FAnimBPAnimGraphBuilder::FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		return TEXT("[AnimBPBuilder] AnimGraph not found in AnimBlueprint");
	}

	// A copy, because RemoveNode mutates AnimGraph->Nodes while we walk it.
	TArray<UEdGraphNode*> Nodes = AnimGraph->Nodes;
	AnimGraph->Modify();
	for (UEdGraphNode* Node : Nodes)
	{
		if (!Node) continue;
		// CanUserDeleteNode rather than a Root cast: it is the schema's own
		// answer, so anything else 4.27 protects in an AnimGraph is protected
		// here too instead of being discovered by a crash.
		if (!Node->CanUserDeleteNode()) continue;
		FBlueprintEditorUtils::RemoveNode(AnimBP, Node, /*bDontRecompile=*/ true);
		++OutRemovedNodes;
	}

	// Nothing left should be linked, because RemoveNode breaks both ends of
	// every link it touches. A surviving link means a protected node was wired
	// to another protected node, which is the graph's own business, not this
	// pass's, so it is left alone.
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
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(AnimBP, EBlueprintCompileOptions::None, &Results);
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

	// CLEAR, then build. Before this existed, Rebuild appended: a rerun added a
	// second state machine and a second copy of every state beside the first,
	// so the asset had two of everything and whichever one ended up wired to
	// Root was the one that played. That is why anim_blueprint_build ships
	// create-only.
	//
	// Placed here, after the variables compile and before the graph is
	// populated, on purpose. Clearing earlier would leave the Root pose node
	// unconnected across that compile, and the compile-after-variables check
	// above fails the whole rebuild on any compiler error.
	//
	// This makes a rerun CONVERGE. It does not make a failed rerun
	// RECOVERABLE: everything below compiles the asset, and the rollback
	// boundary the native command owns can only delete an asset it created, so
	// a clear that succeeds followed by a build step that fails leaves an
	// emptied AnimBlueprint. Finding 0t records that, and it is why there is
	// still no anim_blueprint_patch.
	int32 RemovedNodes = 0;
	FString ClearError = ClearGeneratedGraph(AnimBP, RemovedNodes);
	if (!ClearError.IsEmpty()) return ClearError;
	UE_LOG(LogTemp, Log, TEXT("[AnimBPBuilder] cleared %d AnimGraph node(s) before rebuild"), RemovedNodes);

	// Same build steps as Build (AnimGraph, States, Transitions, EventGraph)
	FString GraphError = FAnimBPAnimGraphBuilder::Build(Spec, Ctx);
	if (!GraphError.IsEmpty()) return GraphError;

	FString StateError = FAnimBPStateMachineBuilder::BuildStates(Spec, Ctx);
	if (!StateError.IsEmpty()) return StateError;

	FString TransError = FAnimBPStateMachineBuilder::BuildTransitions(Spec, Ctx);
	if (!TransError.IsEmpty()) return TransError;

	if (!Spec.EventGraphJson.IsEmpty())
	{
		// true here where Build passes false: a rebuild that appended to the
		// event graph would leave the same duplicate-everything result the
		// AnimGraph clear pass above exists to stop.
		UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON(
			AnimBP, Spec.EventGraphJson, /*bClearExistingGraph=*/ true);
	}

	// Final compile
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(AnimBP, EBlueprintCompileOptions::None, &Results);
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
