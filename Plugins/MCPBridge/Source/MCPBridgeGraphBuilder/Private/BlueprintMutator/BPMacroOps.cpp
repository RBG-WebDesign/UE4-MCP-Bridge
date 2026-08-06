// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPMacroOps.h"
#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "BlueprintInspector/BPTypeDescriptor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "BPMacroOps"

namespace
{
    struct FMacroParam
    {
        FString Name;
        FEdGraphPinType Type;
    };

    UEdGraph* FindMacro(UBlueprint* Blueprint, const FString& Name)
    {
        if (!Blueprint) return nullptr;
        const FName Wanted(*Name);
        for (UEdGraph* Graph : Blueprint->MacroGraphs)
        {
            if (Graph && Graph->GetFName() == Wanted) return Graph;
        }
        return nullptr;
    }

    bool GraphNameAvailable(UBlueprint* Blueprint, const FString& Name, UEdGraph* Except = nullptr)
    {
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        const FName Wanted(*Name);
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph && Graph != Except && Graph->GetFName() == Wanted) return false;
        }
        return true;
    }

    bool ParseParams(const FString& Json, const TCHAR* Context,
        TArray<FMacroParam>& Out, FString& OutError)
    {
        Out.Reset();
        TSharedPtr<FJsonValue> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || !Root->TryGetArray(Values))
        {
            OutError = FString::Printf(TEXT("%s must be a JSON array."), Context);
            return false;
        }
        TSet<FName> Names;
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FString Name;
            const TSharedPtr<FJsonObject>* TypeObject = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Object)
                || !(*Object)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty()
                || !(*Object)->TryGetObjectField(TEXT("type"), TypeObject))
            {
                OutError = FString::Printf(TEXT("%s entries need non-empty name and type."), Context);
                return false;
            }
            const FName PinName(*Name);
            if (Names.Contains(PinName))
            {
                OutError = FString::Printf(TEXT("%s contains duplicate pin '%s'."), Context, *Name);
                return false;
            }
            Names.Add(PinName);
            FString TypeJson;
            const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TypeJson);
            FJsonSerializer::Serialize(TypeObject->ToSharedRef(), Writer);
            FMacroParam Param;
            Param.Name = Name;
            Param.Type = FBPTypeDescriptor::FromJsonString(TypeJson);
            if (Param.Type.PinCategory.IsNone())
            {
                OutError = FString::Printf(TEXT("%s pin '%s' has an unresolved type."), Context, *Name);
                return false;
            }
            Out.Add(MoveTemp(Param));
        }
        return true;
    }

    void FindTunnels(UEdGraph* Graph, UK2Node_Tunnel*& Entry, UK2Node_Tunnel*& Exit)
    {
        Entry = nullptr;
        Exit = nullptr;
        if (!Graph) return;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node);
            if (!Tunnel) continue;
            if (Tunnel->bCanHaveOutputs) Entry = Tunnel;
            if (Tunnel->bCanHaveInputs) Exit = Tunnel;
        }
    }

    bool ReplacePins(UK2Node_Tunnel* Tunnel, const TArray<FMacroParam>& Desired,
        EEdGraphPinDirection Direction, const TCHAR* Context, FString& OutError)
    {
        if (!Tunnel)
        {
            OutError = FString::Printf(TEXT("%s tunnel is missing."), Context);
            return false;
        }
        Tunnel->Modify();
        TSet<FName> DesiredNames;
        for (const FMacroParam& Param : Desired) DesiredNames.Add(FName(*Param.Name));
        TArray<TSharedPtr<FUserPinInfo>> Existing = Tunnel->UserDefinedPins;
        for (const TSharedPtr<FUserPinInfo>& Pin : Existing)
        {
            if (Pin.IsValid() && !DesiredNames.Contains(Pin->PinName))
                Tunnel->RemoveUserDefinedPin(Pin);
        }
        TArray<TSharedPtr<FUserPinInfo>> Ordered;
        for (const FMacroParam& Param : Desired)
        {
            TSharedPtr<FUserPinInfo>* ExistingPin = Tunnel->UserDefinedPins.FindByPredicate(
                [&](const TSharedPtr<FUserPinInfo>& Pin)
                { return Pin.IsValid() && Pin->PinName == FName(*Param.Name); });
            if (ExistingPin && (*ExistingPin)->PinType == Param.Type)
            {
                Ordered.Add(*ExistingPin);
                continue;
            }
            if (ExistingPin) Tunnel->RemoveUserDefinedPin(*ExistingPin);
            FText PinError;
            if (!Tunnel->CanCreateUserDefinedPin(Param.Type, Direction, PinError))
            {
                OutError = FString::Printf(TEXT("%s pin '%s' is invalid: %s"),
                    Context, *Param.Name, *PinError.ToString());
                return false;
            }
            UEdGraphPin* Created = Tunnel->CreateUserDefinedPin(
                FName(*Param.Name), Param.Type, Direction, /*bUseUniqueName=*/true);
            if (!Created || Created->PinName != FName(*Param.Name))
            {
                OutError = FString::Printf(TEXT("%s pin '%s' could not be created exactly."),
                    Context, *Param.Name);
                return false;
            }
            TSharedPtr<FUserPinInfo>* Added = Tunnel->UserDefinedPins.FindByPredicate(
                [&](const TSharedPtr<FUserPinInfo>& Pin)
                { return Pin.IsValid() && Pin->PinName == FName(*Param.Name); });
            if (Added) Ordered.Add(*Added);
        }
        if (Ordered.Num() == Tunnel->UserDefinedPins.Num()) Tunnel->UserDefinedPins = Ordered;
        Tunnel->ReconstructNode();
        return true;
    }
}

FString FBPMacroOps::AddMacro(UBlueprint* Blueprint, const FString& Name,
    const FString& InputsJson, const FString& OutputsJson, FString& OutError)
{
    if (!Blueprint || Name.IsEmpty()) { OutError = TEXT("AddMacro needs a Blueprint and name."); return FString(); }
    if (!GraphNameAvailable(Blueprint, Name)) { OutError = FString::Printf(TEXT("Graph '%s' already exists."), *Name); return FString(); }
    TArray<FMacroParam> Inputs, Outputs;
    if (!ParseParams(InputsJson, TEXT("macro inputs"), Inputs, OutError)
        || !ParseParams(OutputsJson, TEXT("macro outputs"), Outputs, OutError)) return FString();

    FString CreatedName;
    const bool bOk = FBPMutatorHelpers::RunMutation(Blueprint,
        LOCTEXT("AddMacro", "MCP Bridge: Add Macro"), [&]()
        {
            UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
                Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            if (!Graph) { OutError = TEXT("CreateNewGraph failed."); return false; }
            FBlueprintEditorUtils::AddMacroGraph(Blueprint, Graph, /*bIsUserCreated=*/true, nullptr);
            UK2Node_Tunnel* Entry = nullptr;
            UK2Node_Tunnel* Exit = nullptr;
            FindTunnels(Graph, Entry, Exit);
            if (!ReplacePins(Entry, Inputs, EGPD_Output, TEXT("macro input"), OutError)
                || !ReplacePins(Exit, Outputs, EGPD_Input, TEXT("macro output"), OutError)) return false;
            CreatedName = Graph->GetName();
            return true;
        });
    return bOk ? CreatedName : FString();
}

bool FBPMacroOps::RemoveMacro(UBlueprint* Blueprint, const FString& Name, FString& OutError)
{
    UEdGraph* Graph = FindMacro(Blueprint, Name);
    if (!Graph) { OutError = FString::Printf(TEXT("Macro '%s' not found."), *Name); return false; }
    return FBPMutatorHelpers::RunMutation(Blueprint,
        LOCTEXT("RemoveMacro", "MCP Bridge: Remove Macro"), [&]()
        {
            FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::Recompile);
            return true;
        });
}

bool FBPMacroOps::RenameMacro(UBlueprint* Blueprint, const FString& From,
    const FString& To, FString& OutError)
{
    UEdGraph* Graph = FindMacro(Blueprint, From);
    if (!Graph) { OutError = FString::Printf(TEXT("Macro '%s' not found."), *From); return false; }
    if (!GraphNameAvailable(Blueprint, To, Graph))
    {
        OutError = FString::Printf(TEXT("Graph '%s' already exists."), *To);
        return false;
    }
    return FBPMutatorHelpers::RunMutation(Blueprint,
        LOCTEXT("RenameMacro", "MCP Bridge: Rename Macro"), [&]()
        {
            FBlueprintEditorUtils::RenameGraph(Graph, To);
            if (Graph->GetName() != To)
            {
                OutError = FString::Printf(TEXT("UE4 refused macro rename to '%s'."), *To);
                return false;
            }
            return true;
        });
}

bool FBPMacroOps::SetMacroSignature(UBlueprint* Blueprint, const FString& Name,
    const FString& InputsJson, const FString& OutputsJson, FString& OutError)
{
    UEdGraph* Graph = FindMacro(Blueprint, Name);
    if (!Graph) { OutError = FString::Printf(TEXT("Macro '%s' not found."), *Name); return false; }
    TArray<FMacroParam> Inputs, Outputs;
    if (!ParseParams(InputsJson, TEXT("macro inputs"), Inputs, OutError)
        || !ParseParams(OutputsJson, TEXT("macro outputs"), Outputs, OutError)) return false;
    return FBPMutatorHelpers::RunMutation(Blueprint,
        LOCTEXT("SetMacroSignature", "MCP Bridge: Set Macro Signature"), [&]()
        {
            UK2Node_Tunnel* Entry = nullptr;
            UK2Node_Tunnel* Exit = nullptr;
            FindTunnels(Graph, Entry, Exit);
            return ReplacePins(Entry, Inputs, EGPD_Output, TEXT("macro input"), OutError)
                && ReplacePins(Exit, Outputs, EGPD_Input, TEXT("macro output"), OutError);
        });
}

#undef LOCTEXT_NAMESPACE