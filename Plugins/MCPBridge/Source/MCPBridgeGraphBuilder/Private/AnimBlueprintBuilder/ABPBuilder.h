// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABPNodeRegistry.h"

class UAnimBlueprint;

class FAnimBPBuilder
{
public:
	FString Build(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& SkeletonPath,
		const FString& JsonString
	);

	FString Rebuild(UAnimBlueprint* AnimBP, const FString& JsonString);
	FString Validate(const FString& JsonString);

	/**
	 * Empty the AnimGraph of everything this builder generates, leaving the
	 * nodes the schema refuses to delete (the Root pose node, and anything else
	 * whose CanUserDeleteNode is false).
	 *
	 * The pass Rebuild never had. Without it Rebuild appended: a second run over
	 * an existing AnimBlueprint added a second state machine and a second copy
	 * of every state beside the first, so the asset had two of everything and
	 * the last one wired to Root won. That is a convergence failure, and it is
	 * why anim_blueprint_build shipped CREATE-ONLY.
	 *
	 * Removing a UAnimGraphNode_StateMachine also removes its inner state
	 * machine graph, because UAnimGraphNode_StateMachineBase::DestroyNode calls
	 * FBlueprintEditorUtils::RemoveGraph on it
	 * (AnimGraph/Private/AnimGraphNode_StateMachineBase.cpp:153-166). So the
	 * states and transitions inside go with the node rather than being orphaned
	 * in the Blueprint.
	 *
	 * THIS ALONE DOES NOT MAKE A PATCH COMMAND SAFE. It makes a rerun converge;
	 * it does not make a FAILED rerun recoverable. The builder compiles between
	 * the clear and the end, and FBridgeAssetRollback can only delete an asset
	 * the command created, so a clear that succeeds followed by a build that
	 * fails leaves an emptied AnimBlueprint with no way back. A content snapshot
	 * is the missing half. See docs/CAPABILITY_FINDINGS.md finding 0t.
	 *
	 * Returns an error string, empty on success. OutRemovedNodes counts what it
	 * removed, so a caller can tell a cleared graph from a graph that was
	 * already empty.
	 */
	static FString ClearGeneratedGraph(UAnimBlueprint* AnimBP, int32& OutRemovedNodes);

private:
	FAnimBPNodeRegistry Registry;
};
