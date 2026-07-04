#include "BTBuilder.h"
#include "BTBuildSpec.h"
#include "BTJsonParser.h"
#include "BTValidator.h"
#include "BTNodeFactory.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"

#if WITH_EDITOR
#include "BTEditorGraphSync.h"
#endif

FString FBTBuilder::Build(UBehaviorTree* BT, const FString& JsonString)
{
	if (!BT)
	{
		return TEXT("[BTBuilder] BehaviorTree is null");
	}

	// 1. Parse
	FBTBuildSpec Spec;
	FString ParseError = FBTJsonParser::Parse(JsonString, Spec);
	if (!ParseError.IsEmpty()) return ParseError;

	// 2. Validate
	UBlackboardData* BB = BT->BlackboardAsset;
	TArray<FString> Errors = FBTValidator::Validate(Spec, Registry, BB);
	if (Errors.Num() > 0)
	{
		return FString::Join(Errors, TEXT("\n"));
	}

	// 3. Build runtime tree (two-phase)
	FBTBuildContext Ctx;
	Ctx.BehaviorTree = BT;
	Ctx.Blackboard = BB;
	Ctx.Registry = &Registry;

	FString BuildError = FBTNodeFactory::BuildTree(Spec, Ctx);
	if (!BuildError.IsEmpty()) return BuildError;

	// 4. Commit: set RootNode (atomic -- only on full success)
	// Do NOT clear BT->RootNode before this point.
	UBTNode** RootFound = Ctx.NodeMap.Find(Spec.Root.Id);
	if (!RootFound || !*RootFound)
	{
		return TEXT("[BTBuilder] root node not found in NodeMap after build");
	}

	UBTCompositeNode* NewRoot = Cast<UBTCompositeNode>(*RootFound);
	if (!NewRoot)
	{
		return TEXT("[BTBuilder] root node is not a composite");
	}

	BT->RootNode = NewRoot;

	// 5. Sync editor graph (editor-only, non-fatal)
#if WITH_EDITOR
	FBTEditorGraphSync::Sync(BT);
#endif

	UE_LOG(LogTemp, Log, TEXT("[BTBuilder] built BT '%s' with %d nodes"),
		*BT->GetName(), Ctx.NodeMap.Num());

	return FString();
}
