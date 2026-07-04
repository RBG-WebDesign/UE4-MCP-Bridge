#include "BTValidator.h"
#include "BTBuildSpec.h"
#include "BTNodeRegistry.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"

static FString GetBBKeyTypeName(UBlackboardData* BB, const FString& KeyName)
{
	if (!BB) return FString();

	for (const FBlackboardEntry& Entry : BB->Keys)
	{
		if (Entry.EntryName.ToString() == KeyName && Entry.KeyType)
		{
			// Extract type name from class name: "BlackboardKeyType_Object" -> "Object"
			FString ClassName = Entry.KeyType->GetClass()->GetName();
			FString TypeName;
			if (ClassName.Split(TEXT("BlackboardKeyType_"), nullptr, &TypeName))
			{
				return TypeName;
			}
			return ClassName;
		}
	}
	// Check parent blackboard
	if (BB->Parent)
	{
		return GetBBKeyTypeName(BB->Parent, KeyName);
	}
	return FString();
}

static bool BBKeyExists(UBlackboardData* BB, const FString& KeyName)
{
	if (!BB) return false;
	for (const FBlackboardEntry& Entry : BB->Keys)
	{
		if (Entry.EntryName.ToString() == KeyName) return true;
	}
	// Check parent blackboard
	if (BB->Parent)
	{
		return BBKeyExists(BB->Parent, KeyName);
	}
	return false;
}

// Arithmetic conditions require numeric BB key types (Int or Float)
static bool IsArithmeticCondition(const FString& Condition)
{
	return Condition == TEXT("Equal") || Condition == TEXT("NotEqual")
		|| Condition == TEXT("Less") || Condition == TEXT("LessOrEqual")
		|| Condition == TEXT("Greater") || Condition == TEXT("GreaterOrEqual");
}

static bool IsNumericBBKeyType(const FString& TypeName)
{
	return TypeName == TEXT("Int") || TypeName == TEXT("Float");
}

TArray<FString> FBTValidator::Validate(
	const FBTBuildSpec& Spec,
	const FBTNodeRegistry& Registry,
	UBlackboardData* Blackboard)
{
	TArray<FString> Errors;
	TSet<FString> SeenIds;

	// Rule 1: Root must exist
	if (Spec.Root.Type.IsEmpty())
	{
		Errors.Add(TEXT("[BTValidator] root node has no type"));
		return Errors;
	}

	// Rule 1: Root must be composite
	if (!Registry.IsComposite(Spec.Root.Type))
	{
		Errors.Add(FString::Printf(TEXT("[BTValidator] root type '%s' is not a composite"), *Spec.Root.Type));
	}

	ValidateNode(Spec.Root, Registry, Blackboard, SeenIds, TEXT("root"), Errors);

	return Errors;
}

void FBTValidator::ValidateNode(
	const FBTNodeSpec& Node,
	const FBTNodeRegistry& Registry,
	UBlackboardData* Blackboard,
	TSet<FString>& SeenIds,
	const FString& Path,
	TArray<FString>& OutErrors)
{
	FString NodePath = FString::Printf(TEXT("%s/%s"), *Path, *Node.Id);

	// Rule 7: Unique IDs
	if (Node.Id.IsEmpty())
	{
		OutErrors.Add(FString::Printf(TEXT("[BTValidator] node at %s has empty id"), *Path));
	}
	else if (SeenIds.Contains(Node.Id))
	{
		OutErrors.Add(FString::Printf(TEXT("[BTValidator] duplicate id '%s' at %s"), *Node.Id, *Path));
	}
	else
	{
		SeenIds.Add(Node.Id);
	}

	// Rule 9: Unknown type
	if (!Registry.IsKnownType(Node.Type))
	{
		OutErrors.Add(FString::Printf(TEXT("[BTValidator] unknown type '%s' at %s"), *Node.Type, *NodePath));
		return;  // cannot validate children/params of unknown type
	}

	// Rule 2: Composites must have at least one child
	if (Registry.IsComposite(Node.Type) && Node.Children.Num() == 0)
	{
		// SimpleParallel needs exactly 2 children (main task + background)
		OutErrors.Add(FString::Printf(TEXT("[BTValidator] composite '%s' has no children at %s"), *Node.Id, *NodePath));
	}

	// Rule 2b: SimpleParallel must have exactly 2 children
	if (Node.Type == TEXT("SimpleParallel") && Node.Children.Num() != 0 && Node.Children.Num() != 2)
	{
		OutErrors.Add(FString::Printf(TEXT("[BTValidator] SimpleParallel '%s' must have exactly 2 children (main task + background), has %d at %s"),
			*Node.Id, Node.Children.Num(), *NodePath));
	}

	// Rule 3: Tasks cannot have children
	if (Registry.IsTask(Node.Type) && Node.Children.Num() > 0)
	{
		OutErrors.Add(FString::Printf(TEXT("[BTValidator] task '%s' has children at %s"), *Node.Id, *NodePath));
	}

	// Rule 4: Only decorator types in decorators array
	for (const FBTNodeSpec& Dec : Node.Decorators)
	{
		if (!Dec.Type.IsEmpty() && Registry.IsKnownType(Dec.Type) && !Registry.IsDecorator(Dec.Type))
		{
			OutErrors.Add(FString::Printf(TEXT("[BTValidator] '%s' in decorators is not a decorator type at %s"), *Dec.Type, *NodePath));
		}
	}

	// Rule 4b: Only service types in services array
	for (const FBTNodeSpec& Svc : Node.Services)
	{
		if (!Svc.Type.IsEmpty() && Registry.IsKnownType(Svc.Type) && !Registry.IsService(Svc.Type))
		{
			OutErrors.Add(FString::Printf(TEXT("[BTValidator] '%s' in services is not a service type at %s"), *Svc.Type, *NodePath));
		}
	}

	// Rule 4c: Services can only be on composite nodes
	if (Node.Services.Num() > 0 && !Registry.IsComposite(Node.Type))
	{
		OutErrors.Add(FString::Printf(TEXT("[BTValidator] services on non-composite '%s' (%s) at %s; services must be on composites"),
			*Node.Id, *Node.Type, *NodePath));
	}

	// Rule 5 & 6: BB key validation (check all BB key params)
	auto ValidateBBKey = [&](const FString& ParamName, const FString& KeyName)
	{
		if (Blackboard && !BBKeyExists(Blackboard, KeyName))
		{
			OutErrors.Add(FString::Printf(TEXT("[BTValidator] blackboard key '%s' not found at %s"), *KeyName, *NodePath));
		}

		// Rule 6: BB key type compatibility
		if (Blackboard)
		{
			const TMap<FString, TSet<FString>>* Reqs = Registry.GetBBKeyRequirements(Node.Type);
			if (Reqs)
			{
				const TSet<FString>* AllowedTypes = Reqs->Find(ParamName);
				if (AllowedTypes)
				{
					FString ActualType = GetBBKeyTypeName(Blackboard, KeyName);
					if (!ActualType.IsEmpty() && !AllowedTypes->Contains(ActualType))
					{
						OutErrors.Add(FString::Printf(
							TEXT("[BTValidator] BB key '%s' is type '%s', but '%s' requires one of: %s at %s"),
							*KeyName, *ActualType, *Node.Type,
							*FString::Join(AllowedTypes->Array(), TEXT(", ")),
							*NodePath
						));
					}
				}
			}
		}
	};

	if (const FString* KeyName = Node.Params.Find(TEXT("blackboard_key")))
	{
		ValidateBBKey(TEXT("blackboard_key"), *KeyName);
	}
	// CompareBBEntries has two keys
	if (const FString* KeyA = Node.Params.Find(TEXT("blackboard_key_a")))
	{
		ValidateBBKey(TEXT("blackboard_key_a"), *KeyA);
	}
	if (const FString* KeyB = Node.Params.Find(TEXT("blackboard_key_b")))
	{
		ValidateBBKey(TEXT("blackboard_key_b"), *KeyB);
	}
	// KeepInCone has cone_origin and observed BB keys
	if (const FString* ConeOrigin = Node.Params.Find(TEXT("cone_origin")))
	{
		ValidateBBKey(TEXT("cone_origin"), *ConeOrigin);
	}
	if (const FString* Observed = Node.Params.Find(TEXT("observed")))
	{
		ValidateBBKey(TEXT("observed"), *Observed);
	}

	// Rule 8: Condition enum validation (generalized using ValidConditions registry)
	if (const FString* Condition = Node.Params.Find(TEXT("condition")))
	{
		const TSet<FString>* ValidConds = Registry.GetValidConditions(Node.Type);
		if (ValidConds && !ValidConds->Contains(*Condition))
		{
			OutErrors.Add(FString::Printf(
				TEXT("[BTValidator] invalid condition '%s' at %s (valid: %s)"),
				**Condition, *NodePath,
				*FString::Join(ValidConds->Array(), TEXT(", "))
			));
		}
	}
	// CompareBBEntries uses "operator" param
	if (const FString* Op = Node.Params.Find(TEXT("operator")))
	{
		const TSet<FString>* ValidConds = Registry.GetValidConditions(Node.Type);
		if (ValidConds && !ValidConds->Contains(*Op))
		{
			OutErrors.Add(FString::Printf(
				TEXT("[BTValidator] invalid operator '%s' at %s (valid: %s)"),
				**Op, *NodePath,
				*FString::Join(ValidConds->Array(), TEXT(", "))
			));
		}
	}

	// Rule 8b: Arithmetic conditions require numeric BB key type (Blackboard and ConditionalLoop)
	if (Node.Type == TEXT("Blackboard") || Node.Type == TEXT("ConditionalLoop"))
	{
		if (const FString* Condition = Node.Params.Find(TEXT("condition")))
		{
			if (IsArithmeticCondition(*Condition))
			{
				if (const FString* KeyName = Node.Params.Find(TEXT("blackboard_key")))
				{
					if (Blackboard)
					{
						FString ActualType = GetBBKeyTypeName(Blackboard, *KeyName);
						if (!ActualType.IsEmpty() && !IsNumericBBKeyType(ActualType))
						{
							OutErrors.Add(FString::Printf(
								TEXT("[BTValidator] arithmetic condition '%s' requires numeric BB key, but '%s' is type '%s' at %s"),
								**Condition, **KeyName, *ActualType, *NodePath
							));
						}
					}
				}
			}
		}
	}

	// Rule 10: DoesPathExist requires both blackboard_key_a and blackboard_key_b
	if (Node.Type == TEXT("DoesPathExist"))
	{
		bool HasKeyA = Node.Params.Contains(TEXT("blackboard_key_a"));
		bool HasKeyB = Node.Params.Contains(TEXT("blackboard_key_b"));
		if (!HasKeyA || !HasKeyB)
		{
			OutErrors.Add(FString::Printf(
				TEXT("[BTValidator] DoesPathExist '%s' requires both blackboard_key_a and blackboard_key_b at %s"),
				*Node.Id, *NodePath
			));
		}
	}

	// Rule 11: TagCooldown and SetTagCooldown require non-empty cooldown_tag
	if (Node.Type == TEXT("TagCooldown") || Node.Type == TEXT("SetTagCooldown"))
	{
		const FString* Tag = Node.Params.Find(TEXT("cooldown_tag"));
		if (!Tag || Tag->IsEmpty())
		{
			OutErrors.Add(FString::Printf(
				TEXT("[BTValidator] %s '%s' requires non-empty cooldown_tag at %s"),
				*Node.Type, *Node.Id, *NodePath
			));
		}
	}

	// Rule 12: FinishWithResult result must be a valid EBTNodeResult value
	if (Node.Type == TEXT("FinishWithResult"))
	{
		if (const FString* Result = Node.Params.Find(TEXT("result")))
		{
			if (*Result != TEXT("Succeeded") && *Result != TEXT("Failed") && *Result != TEXT("Aborted"))
			{
				OutErrors.Add(FString::Printf(
					TEXT("[BTValidator] FinishWithResult '%s' has invalid result '%s' at %s (valid: Succeeded, Failed, Aborted)"),
					*Node.Id, **Result, *NodePath
				));
			}
		}
	}

	// Recurse into children
	for (const FBTNodeSpec& Child : Node.Children)
	{
		ValidateNode(Child, Registry, Blackboard, SeenIds, NodePath, OutErrors);
	}

	// Recurse into decorators
	for (const FBTNodeSpec& Dec : Node.Decorators)
	{
		ValidateNode(Dec, Registry, Blackboard, SeenIds, NodePath + TEXT("/decorators"), OutErrors);
	}

	// Recurse into services
	for (const FBTNodeSpec& Svc : Node.Services)
	{
		ValidateNode(Svc, Registry, Blackboard, SeenIds, NodePath + TEXT("/services"), OutErrors);
	}
}
