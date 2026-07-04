#include "BTJsonParser.h"
#include "BTBuildSpec.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

static FString ParseNode(const TSharedPtr<FJsonObject>& NodeObj, FBTNodeSpec& OutNode, const FString& Path)
{
	if (!NodeObj.IsValid())
	{
		return FString::Printf(TEXT("[BTJsonParser] null node at %s"), *Path);
	}

	// id (required)
	if (!NodeObj->TryGetStringField(TEXT("id"), OutNode.Id) || OutNode.Id.IsEmpty())
	{
		return FString::Printf(TEXT("[BTJsonParser] missing or empty 'id' at %s"), *Path);
	}

	// type (required)
	if (!NodeObj->TryGetStringField(TEXT("type"), OutNode.Type) || OutNode.Type.IsEmpty())
	{
		return FString::Printf(TEXT("[BTJsonParser] missing or empty 'type' at %s/%s"), *Path, *OutNode.Id);
	}

	// name (optional)
	NodeObj->TryGetStringField(TEXT("name"), OutNode.Name);

	// params (optional)
	const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
	if (NodeObj->TryGetObjectField(TEXT("params"), ParamsObj) && ParamsObj->IsValid())
	{
		for (const auto& Pair : (*ParamsObj)->Values)
		{
			FString Value;
			if (Pair.Value->TryGetString(Value))
			{
				OutNode.Params.Add(Pair.Key, Value);
			}
			else
			{
				// Convert numbers and booleans to string
				double NumVal;
				if (Pair.Value->TryGetNumber(NumVal))
				{
					OutNode.Params.Add(Pair.Key, FString::SanitizeFloat(NumVal));
				}
				else
				{
					bool BoolVal;
					if (Pair.Value->TryGetBool(BoolVal))
					{
						OutNode.Params.Add(Pair.Key, BoolVal ? TEXT("true") : TEXT("false"));
					}
				}
			}
		}
	}

	FString NodePath = FString::Printf(TEXT("%s/%s"), *Path, *OutNode.Id);

	// children (optional)
	const TArray<TSharedPtr<FJsonValue>>* ChildrenArr = nullptr;
	if (NodeObj->TryGetArrayField(TEXT("children"), ChildrenArr))
	{
		for (int32 i = 0; i < ChildrenArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* ChildObj = nullptr;
			if (!(*ChildrenArr)[i]->TryGetObject(ChildObj))
			{
				return FString::Printf(TEXT("[BTJsonParser] child %d is not an object at %s"), i, *NodePath);
			}
			FBTNodeSpec ChildSpec;
			FString ChildError = ParseNode(*ChildObj, ChildSpec, NodePath);
			if (!ChildError.IsEmpty()) return ChildError;
			OutNode.Children.Add(MoveTemp(ChildSpec));
		}
	}

	// decorators (optional)
	const TArray<TSharedPtr<FJsonValue>>* DecoratorsArr = nullptr;
	if (NodeObj->TryGetArrayField(TEXT("decorators"), DecoratorsArr))
	{
		for (int32 i = 0; i < DecoratorsArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* DecObj = nullptr;
			if (!(*DecoratorsArr)[i]->TryGetObject(DecObj))
			{
				return FString::Printf(TEXT("[BTJsonParser] decorator %d is not an object at %s"), i, *NodePath);
			}
			FBTNodeSpec DecSpec;
			FString DecError = ParseNode(*DecObj, DecSpec, NodePath + TEXT("/decorators"));
			if (!DecError.IsEmpty()) return DecError;
			OutNode.Decorators.Add(MoveTemp(DecSpec));
		}
	}

	// services (optional)
	const TArray<TSharedPtr<FJsonValue>>* ServicesArr = nullptr;
	if (NodeObj->TryGetArrayField(TEXT("services"), ServicesArr))
	{
		for (int32 i = 0; i < ServicesArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* SvcObj = nullptr;
			if (!(*ServicesArr)[i]->TryGetObject(SvcObj))
			{
				return FString::Printf(TEXT("[BTJsonParser] service %d is not an object at %s"), i, *NodePath);
			}
			FBTNodeSpec SvcSpec;
			FString SvcError = ParseNode(*SvcObj, SvcSpec, NodePath + TEXT("/services"));
			if (!SvcError.IsEmpty()) return SvcError;
			OutNode.Services.Add(MoveTemp(SvcSpec));
		}
	}

	return FString();
}

FString FBTJsonParser::Parse(const FString& JsonString, FBTBuildSpec& OutSpec)
{
	TSharedPtr<FJsonObject> RootObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		return TEXT("[BTJsonParser] failed to parse JSON");
	}

	const TSharedPtr<FJsonObject>* RootNodeObj = nullptr;
	if (!RootObj->TryGetObjectField(TEXT("root"), RootNodeObj))
	{
		return TEXT("[BTJsonParser] missing 'root' field");
	}

	return ParseNode(*RootNodeObj, OutSpec.Root, TEXT("root"));
}
