#include "BPFunctionOps.h"
#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "BlueprintInspector/BPTypeDescriptor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "BPFunctionOps"

namespace
{
    struct FParamSpec
    {
        FString Name;
        FEdGraphPinType Type;
    };

    /**
     * Parse `[{name, type: TypeDescriptor}]` JSON array into a typed spec list.
     * Returns true on success even if the array is empty. Logs at Warning and
     * returns false on any malformed element.
     */
    bool ParseParamArray(const FString& Json, TArray<FParamSpec>& OutParams, const TCHAR* Ctx)
    {
        OutParams.Reset();
        if (Json.IsEmpty() || Json == TEXT("[]")) return true;

        TSharedPtr<FJsonValue> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: failed to parse params JSON: %s"), Ctx, *Json);
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
        if (!Root->TryGetArray(ArrPtr))
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: expected JSON array, got %s"), Ctx, *Json);
            return false;
        }

        for (const TSharedPtr<FJsonValue>& Elem : *ArrPtr)
        {
            const TSharedPtr<FJsonObject>* Obj = nullptr;
            if (!Elem->TryGetObject(Obj))
            {
                UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: expected param object"), Ctx);
                return false;
            }

            FParamSpec Spec;
            if (!(*Obj)->TryGetStringField(TEXT("name"), Spec.Name) || Spec.Name.IsEmpty())
            {
                UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: param missing 'name'"), Ctx);
                return false;
            }

            const TSharedPtr<FJsonObject>* TypeObj = nullptr;
            if (!(*Obj)->TryGetObjectField(TEXT("type"), TypeObj))
            {
                UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: param '%s' missing 'type'"), Ctx, *Spec.Name);
                return false;
            }

            FString TypeJsonStr;
            const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TypeJsonStr);
            FJsonSerializer::Serialize(TypeObj->ToSharedRef(), Writer);

            Spec.Type = FBPTypeDescriptor::FromJsonString(TypeJsonStr);
            if (Spec.Type.PinCategory.IsNone())
            {
                UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: param '%s' type failed to parse: %s"), Ctx, *Spec.Name, *TypeJsonStr);
                return false;
            }
            OutParams.Add(MoveTemp(Spec));
        }
        return true;
    }

    /** Find a function graph by name — returns nullptr if not present. Case-sensitive compare on FName. */
    UEdGraph* FindFunctionGraph(UBlueprint* Blueprint, const FString& Name)
    {
        const FName N(*Name);
        for (UEdGraph* G : Blueprint->FunctionGraphs)
        {
            if (G && G->GetFName() == N) return G;
        }
        return nullptr;
    }
}

FString FBPFunctionOps::AddFunction(UBlueprint* Blueprint, const FString& FunctionName, const FString& InputsJson, const FString& OutputsJson)
{
    if (!Blueprint)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("AddFunction: null blueprint"));
        return FString();
    }
    if (FunctionName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("AddFunction: empty function name"));
        return FString();
    }
    if (FindFunctionGraph(Blueprint, FunctionName))
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("AddFunction: function '%s' already exists"), *FunctionName);
        return FString();
    }

    TArray<FParamSpec> Inputs, Outputs;
    if (!ParseParamArray(InputsJson,  Inputs,  TEXT("AddFunction.Inputs")))  return FString();
    if (!ParseParamArray(OutputsJson, Outputs, TEXT("AddFunction.Outputs"))) return FString();

    FString ResultGraphName;
    const bool bOk = FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("AddFunction", "Soul Juice: Add Function"),
        [&]() -> bool
        {
            // 1. Create the graph shell.
            UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
                Blueprint, FName(*FunctionName),
                UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            if (!NewGraph)
            {
                UE_LOG(LogBlueprintMutator, Error, TEXT("AddFunction: CreateNewGraph failed for '%s'"), *FunctionName);
                return false;
            }

            // 2. Seed with entry (and result if outputs) via the engine's function-graph factory.
            //    The template param is the signature-source type (UClass or UFunction). We pass
            //    nullptr as a UClass* (parameter-less seeding); user-defined pins are added
            //    explicitly below so Type Descriptor rules apply uniformly.
            FBlueprintEditorUtils::AddFunctionGraph<UClass>(
                Blueprint, NewGraph, /*bIsUserCreated=*/true, /*SignatureFromClass=*/(UClass*)nullptr);

            // 3. Locate the auto-placed entry + (optional) result nodes.
            UK2Node_FunctionEntry*  EntryNode  = nullptr;
            UK2Node_FunctionResult* ResultNode = nullptr;
            for (UEdGraphNode* N : NewGraph->Nodes)
            {
                if (!EntryNode)  EntryNode  = Cast<UK2Node_FunctionEntry>(N);
                if (!ResultNode) ResultNode = Cast<UK2Node_FunctionResult>(N);
            }
            if (!EntryNode)
            {
                UE_LOG(LogBlueprintMutator, Error, TEXT("AddFunction: entry node missing after AddFunctionGraph"));
                return false;
            }

            // 4. Inputs → entry-node user-defined pins (EGPD_Output: entry emits INTO the body).
            for (const FParamSpec& P : Inputs)
            {
                UEdGraphPin* Pin = EntryNode->CreateUserDefinedPin(
                    FName(*P.Name), P.Type, EGPD_Output, /*bUseUniqueName=*/true);
                if (!Pin)
                {
                    UE_LOG(LogBlueprintMutator, Warning,
                        TEXT("AddFunction: CreateUserDefinedPin failed for input '%s'"), *P.Name);
                }
            }

            // 5. Outputs → result-node user-defined pins (EGPD_Input: result consumes FROM the body).
            //    AddFunctionGraph only spawns a result node if outputs were declared up-front;
            //    since we passed none, we have to spawn it ourselves when Outputs.Num() > 0.
            if (Outputs.Num() > 0)
            {
                if (!ResultNode)
                {
                    FGraphNodeCreator<UK2Node_FunctionResult> Creator(*NewGraph);
                    ResultNode = Creator.CreateNode();
                    ResultNode->NodePosX = EntryNode->NodePosX + 400;
                    ResultNode->NodePosY = EntryNode->NodePosY;
                    // FunctionReference on the result node mirrors the entry's — engine pattern.
                    ResultNode->FunctionReference = EntryNode->FunctionReference;
                    Creator.Finalize();
                }
                for (const FParamSpec& P : Outputs)
                {
                    UEdGraphPin* Pin = ResultNode->CreateUserDefinedPin(
                        FName(*P.Name), P.Type, EGPD_Input, /*bUseUniqueName=*/true);
                    if (!Pin)
                    {
                        UE_LOG(LogBlueprintMutator, Warning,
                            TEXT("AddFunction: CreateUserDefinedPin failed for output '%s'"), *P.Name);
                    }
                }
            }

            ResultGraphName = NewGraph->GetName();
            return true;
        });

    return bOk ? ResultGraphName : FString();
}

bool FBPFunctionOps::RemoveFunction(UBlueprint* Blueprint, const FString& FunctionName)
{
    if (!Blueprint)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveFunction: null blueprint"));
        return false;
    }
    if (FunctionName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveFunction: empty function name"));
        return false;
    }

    UEdGraph* Graph = FindFunctionGraph(Blueprint, FunctionName);
    if (!Graph)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("RemoveFunction: function '%s' not found"), *FunctionName);
        return false;
    }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("RemoveFunction", "Soul Juice: Remove Function"),
        [&]() -> bool
        {
            FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::Recompile);
            return true;
        });
}

#undef LOCTEXT_NAMESPACE
