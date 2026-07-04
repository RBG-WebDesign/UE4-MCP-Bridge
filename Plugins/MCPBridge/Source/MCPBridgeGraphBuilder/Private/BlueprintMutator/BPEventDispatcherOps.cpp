#include "BPEventDispatcherOps.h"
#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "BlueprintInspector/BPTypeDescriptor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "BPEventDispatcherOps"

namespace
{
    struct FDispatcherParamSpec
    {
        FString Name;
        FEdGraphPinType Type;
    };

    /**
     * Parse `{"parameters": [{name, type}]}`. Returns true even if parameters is empty
     * or missing. Failure only on malformed param elements.
     */
    bool ParseSignature(const FString& Json, TArray<FDispatcherParamSpec>& OutParams)
    {
        OutParams.Reset();
        if (Json.IsEmpty()) return true;

        TSharedPtr<FJsonObject> Obj;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("AddEventDispatcher: bad SignatureJson: %s"), *Json);
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
        if (!Obj->TryGetArrayField(TEXT("parameters"), Params)) return true;

        for (const TSharedPtr<FJsonValue>& Elem : *Params)
        {
            const TSharedPtr<FJsonObject>* ElemObj = nullptr;
            if (!Elem->TryGetObject(ElemObj)) return false;

            FDispatcherParamSpec Spec;
            if (!(*ElemObj)->TryGetStringField(TEXT("name"), Spec.Name) || Spec.Name.IsEmpty()) return false;

            const TSharedPtr<FJsonObject>* TypeObj = nullptr;
            if (!(*ElemObj)->TryGetObjectField(TEXT("type"), TypeObj)) return false;

            FString TypeJsonStr;
            const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TypeJsonStr);
            FJsonSerializer::Serialize(TypeObj->ToSharedRef(), Writer);
            Spec.Type = FBPTypeDescriptor::FromJsonString(TypeJsonStr);
            if (Spec.Type.PinCategory.IsNone())
            {
                UE_LOG(LogBlueprintMutator, Warning, TEXT("AddEventDispatcher: failed to parse param type"));
                return false;
            }

            OutParams.Add(MoveTemp(Spec));
        }
        return true;
    }

    UEdGraph* FindDispatcherGraph(UBlueprint* Blueprint, const FString& Name)
    {
        const FName N(*Name);
        for (UEdGraph* G : Blueprint->DelegateSignatureGraphs)
        {
            if (G && G->GetFName() == N) return G;
        }
        return nullptr;
    }
}

FString FBPEventDispatcherOps::AddEventDispatcher(UBlueprint* Blueprint, const FString& DispatcherName, const FString& SignatureJson)
{
    if (!Blueprint)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("AddEventDispatcher: null blueprint"));
        return FString();
    }
    if (DispatcherName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("AddEventDispatcher: empty dispatcher name"));
        return FString();
    }

    // Collision check: function graph with same name.
    {
        const FName N(*DispatcherName);
        for (UEdGraph* G : Blueprint->FunctionGraphs)
        {
            if (G && G->GetFName() == N)
            {
                UE_LOG(LogBlueprintMutator, Warning,
                    TEXT("AddEventDispatcher: function '%s' with same name already exists"), *DispatcherName);
                return FString();
            }
        }
    }

    if (FindDispatcherGraph(Blueprint, DispatcherName))
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("AddEventDispatcher: dispatcher '%s' already exists"), *DispatcherName);
        return FString();
    }

    TArray<FDispatcherParamSpec> Params;
    if (!ParseSignature(SignatureJson, Params)) return FString();

    FString ResultName;
    const bool bOk = FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("AddEventDispatcher", "Soul Juice: Add Event Dispatcher"),
        [&]() -> bool
        {
            UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
                Blueprint, FName(*DispatcherName),
                UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            if (!NewGraph)
            {
                UE_LOG(LogBlueprintMutator, Error,
                    TEXT("AddEventDispatcher: CreateNewGraph failed for '%s'"), *DispatcherName);
                return false;
            }

            Blueprint->DelegateSignatureGraphs.Add(NewGraph);

            FGraphNodeCreator<UK2Node_FunctionEntry> Creator(*NewGraph);
            UK2Node_FunctionEntry* EntryNode = Creator.CreateNode();
            EntryNode->FunctionReference.SetSelfMember(FName(*DispatcherName));
            EntryNode->bIsEditable = true;
            Creator.Finalize();

            for (const FDispatcherParamSpec& P : Params)
            {
                UEdGraphPin* Pin = EntryNode->CreateUserDefinedPin(
                    FName(*P.Name), P.Type, EGPD_Output, /*bUseUniqueName=*/true);
                if (!Pin)
                {
                    UE_LOG(LogBlueprintMutator, Warning,
                        TEXT("AddEventDispatcher: CreateUserDefinedPin failed for '%s'"), *P.Name);
                }
            }

            ResultName = NewGraph->GetName();
            return true;
        });

    return bOk ? ResultName : FString();
}

bool FBPEventDispatcherOps::RemoveEventDispatcher(UBlueprint* Blueprint, const FString& DispatcherName)
{
    if (!Blueprint)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveEventDispatcher: null blueprint"));
        return false;
    }
    if (DispatcherName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveEventDispatcher: empty dispatcher name"));
        return false;
    }

    UEdGraph* Graph = FindDispatcherGraph(Blueprint, DispatcherName);
    if (!Graph)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("RemoveEventDispatcher: dispatcher '%s' not found"), *DispatcherName);
        return false;
    }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("RemoveEventDispatcher", "Soul Juice: Remove Event Dispatcher"),
        [&]() -> bool
        {
            FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::Recompile);
            return true;
        });
}

#undef LOCTEXT_NAMESPACE
