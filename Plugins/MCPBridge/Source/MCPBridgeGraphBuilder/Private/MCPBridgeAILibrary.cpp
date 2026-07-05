#include "MCPBridgeAILibrary.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	UBlackboardKeyType* MakeKeyType(UBlackboardData* Owner, const FString& TypeName, const FString& BaseClassPath, FString& OutError)
	{
		if (TypeName == TEXT("Bool"))    return NewObject<UBlackboardKeyType_Bool>(Owner);
		if (TypeName == TEXT("Int"))     return NewObject<UBlackboardKeyType_Int>(Owner);
		if (TypeName == TEXT("Float"))   return NewObject<UBlackboardKeyType_Float>(Owner);
		if (TypeName == TEXT("String"))  return NewObject<UBlackboardKeyType_String>(Owner);
		if (TypeName == TEXT("Name"))    return NewObject<UBlackboardKeyType_Name>(Owner);
		if (TypeName == TEXT("Vector"))  return NewObject<UBlackboardKeyType_Vector>(Owner);
		if (TypeName == TEXT("Rotator")) return NewObject<UBlackboardKeyType_Rotator>(Owner);
		if (TypeName == TEXT("Object"))
		{
			UBlackboardKeyType_Object* Key = NewObject<UBlackboardKeyType_Object>(Owner);
			if (!BaseClassPath.IsEmpty())
			{
				if (UClass* Base = LoadClass<UObject>(nullptr, *BaseClassPath))
				{
					Key->BaseClass = Base;
				}
				else
				{
					OutError = FString::Printf(TEXT("Object key base_class not found: %s"), *BaseClassPath);
					return nullptr;
				}
			}
			return Key;
		}
		if (TypeName == TEXT("Class"))
		{
			UBlackboardKeyType_Class* Key = NewObject<UBlackboardKeyType_Class>(Owner);
			if (!BaseClassPath.IsEmpty())
			{
				if (UClass* Base = LoadClass<UObject>(nullptr, *BaseClassPath))
				{
					Key->BaseClass = Base;
				}
				else
				{
					OutError = FString::Printf(TEXT("Class key base_class not found: %s"), *BaseClassPath);
					return nullptr;
				}
			}
			return Key;
		}
		OutError = FString::Printf(
			TEXT("Unknown blackboard key type '%s'. Supported: Bool, Int, Float, String, Name, Vector, Rotator, Object, Class"),
			*TypeName);
		return nullptr;
	}

	bool HasKey(const UBlackboardData* Blackboard, const FName KeyName)
	{
		for (const FBlackboardEntry& Entry : Blackboard->Keys)
		{
			if (Entry.EntryName == KeyName)
			{
				return true;
			}
		}
		return false;
	}
}

FString UMCPBridgeAILibrary::AddBlackboardKeys(UBlackboardData* Blackboard, const FString& KeysJson)
{
	if (!Blackboard)
	{
		return TEXT("Blackboard is null");
	}

	TArray<TSharedPtr<FJsonValue>> KeyArray;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(KeysJson);
	if (!FJsonSerializer::Deserialize(Reader, KeyArray))
	{
		return TEXT("KeysJson is not a valid JSON array");
	}

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& Value : KeyArray)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Obj))
		{
			return TEXT("Each key entry must be an object {name, type, base_class?}");
		}
		FString Name, Type, BaseClass;
		(*Obj)->TryGetStringField(TEXT("name"), Name);
		(*Obj)->TryGetStringField(TEXT("type"), Type);
		(*Obj)->TryGetStringField(TEXT("base_class"), BaseClass);
		if (Name.IsEmpty() || Type.IsEmpty())
		{
			return TEXT("Key entry missing 'name' or 'type'");
		}
		if (HasKey(Blackboard, *Name))
		{
			continue;
		}

		FString TypeError;
		UBlackboardKeyType* KeyType = MakeKeyType(Blackboard, Type, BaseClass, TypeError);
		if (!KeyType)
		{
			return TypeError;
		}

		FBlackboardEntry Entry;
		Entry.EntryName = *Name;
		Entry.KeyType = KeyType;
		Blackboard->Keys.Add(Entry);
		++Added;
	}

	if (Added > 0)
	{
		Blackboard->MarkPackageDirty();
	}
	return FString();
}

FString UMCPBridgeAILibrary::SetBlackboardAsset(UBehaviorTree* BehaviorTree, UBlackboardData* Blackboard)
{
	if (!BehaviorTree)
	{
		return TEXT("BehaviorTree is null");
	}
	if (!Blackboard)
	{
		return TEXT("Blackboard is null");
	}
	BehaviorTree->BlackboardAsset = Blackboard;
	BehaviorTree->MarkPackageDirty();
	return FString();
}

FString UMCPBridgeAILibrary::ListBlackboardKeys(UBlackboardData* Blackboard)
{
	if (!Blackboard)
	{
		return TEXT("[]");
	}
	TArray<TSharedPtr<FJsonValue>> KeyArray;
	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Entry.EntryName.ToString());
		Obj->SetStringField(TEXT("type"), Entry.KeyType ? Entry.KeyType->GetClass()->GetName() : TEXT("None"));
		KeyArray.Add(MakeShared<FJsonValueObject>(Obj));
	}
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(KeyArray, Writer);
	return Out;
}
