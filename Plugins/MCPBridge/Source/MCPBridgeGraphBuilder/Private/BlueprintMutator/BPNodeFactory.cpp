// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPNodeFactory.h"
#include "BPGLogCategories.h"
#include "EdGraph/EdGraph.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Knot.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_CreateWidget.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_Select.h"
#include "K2Node_FormatText.h"
#include "Engine/Blueprint.h"
#include "../BlueprintInspector/BPTypeDescriptor.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace
{
    bool ParseJsonConfig(const FString& ConfigJson, TSharedPtr<FJsonObject>& OutObj)
    {
        if (ConfigJson.IsEmpty()) { OutObj = MakeShared<FJsonObject>(); return true; }
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConfigJson);
        if (!FJsonSerializer::Deserialize(Reader, OutObj) || !OutObj.IsValid())
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("BPNodeFactory: failed to parse ConfigJson: %s"), *ConfigJson);
            return false;
        }
        return true;
    }
}

UEdGraphNode* FBPNodeFactory::CreateCallFunction(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString TargetClassPath;
    FString FunctionName;
    Cfg->TryGetStringField(TEXT("targetClass"), TargetClassPath);
    Cfg->TryGetStringField(TEXT("functionName"), FunctionName);

    if (TargetClassPath.IsEmpty() || FunctionName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("CreateCallFunction: ConfigJson missing targetClass and/or functionName"));
        return nullptr;
    }

    UClass* TargetClass = LoadObject<UClass>(nullptr, *TargetClassPath);
    if (!TargetClass)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("CreateCallFunction: target class not found '%s'"), *TargetClassPath);
        return nullptr;
    }
    UFunction* Func = TargetClass->FindFunctionByName(*FunctionName);
    if (!Func)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("CreateCallFunction: function '%s' not found on '%s'"), *FunctionName, *TargetClassPath);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
    UK2Node_CallFunction* Node = Creator.CreateNode();
    Node->SetFromFunction(Func);
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();

    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateEvent(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString ParentClassPath, EventName;
    Cfg->TryGetStringField(TEXT("parentClass"), ParentClassPath);
    Cfg->TryGetStringField(TEXT("eventName"), EventName);
    if (ParentClassPath.IsEmpty() || EventName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateEvent: missing parentClass/eventName"));
        return nullptr;
    }
    UClass* ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
    if (!ParentClass)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateEvent: parent class not found '%s'"), *ParentClassPath);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
    UK2Node_Event* Node = Creator.CreateNode();
    Node->EventReference.SetExternalMember(FName(*EventName), ParentClass);
    Node->bOverrideFunction = true;
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateCustomEvent(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString EventName;
    Cfg->TryGetStringField(TEXT("eventName"), EventName);
    if (EventName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateCustomEvent: missing eventName"));
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
    UK2Node_CustomEvent* Node = Creator.CreateNode();
    Node->CustomFunctionName = FName(*EventName);
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();

    const TArray<TSharedPtr<FJsonValue>>* ParamsArr = nullptr;
    if (Cfg->TryGetArrayField(TEXT("parameters"), ParamsArr))
    {
        for (const TSharedPtr<FJsonValue>& ParamVal : *ParamsArr)
        {
            const TSharedPtr<FJsonObject>* ParamObj = nullptr;
            if (!ParamVal->TryGetObject(ParamObj)) continue;
            FString ParamName;
            (*ParamObj)->TryGetStringField(TEXT("name"), ParamName);
            const TSharedPtr<FJsonObject>* TypeObj = nullptr;
            if (ParamName.IsEmpty() || !(*ParamObj)->TryGetObjectField(TEXT("type"), TypeObj)) continue;

            FString TypeJsonStr;
            const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&TypeJsonStr);
            FJsonSerializer::Serialize(TypeObj->ToSharedRef(), W);
            const FEdGraphPinType PinType = FBPTypeDescriptor::FromJsonString(TypeJsonStr);
            if (PinType.PinCategory.IsNone()) continue;
            Node->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Output, /*bUseUniqueName=*/true);
        }
    }
    return Node;
}

namespace
{
    template<typename TNode>
    UEdGraphNode* CreateVariableRefNode(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson, const TCHAR* DebugLabel)
    {
        if (!Graph) return nullptr;
        TSharedPtr<FJsonObject> Cfg;
        if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

        FString VarName, Scope, TargetClassPath;
        Cfg->TryGetStringField(TEXT("varName"), VarName);
        Cfg->TryGetStringField(TEXT("scope"), Scope);
        Cfg->TryGetStringField(TEXT("targetClass"), TargetClassPath);
        if (VarName.IsEmpty())
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: missing varName"), DebugLabel);
            return nullptr;
        }

        // scope "target" reads or writes the variable on ANOTHER object: the
        // node grows a "self" input pin (UK2Node_Variable::CreatePinForSelf,
        // K2Node_Variable.cpp:112, unhidden because the reference is not a
        // self context) that the caller wires the object into. Without it the
        // only reachable variables are the Blueprint's own, which is what put
        // a SaveGame subclass's field out of reach.
        UClass* TargetClass = nullptr;
        const bool bTargetScope = (Scope == TEXT("target"));
        if (!Scope.IsEmpty() && Scope != TEXT("self") && !bTargetScope)
        {
            UE_LOG(LogBlueprintMutator, Warning,
                TEXT("%s: scope '%s' is not one of 'self', 'target'"), DebugLabel, *Scope);
            return nullptr;
        }
        if (bTargetScope)
        {
            if (TargetClassPath.IsEmpty())
            {
                UE_LOG(LogBlueprintMutator, Warning,
                    TEXT("%s: scope 'target' needs targetClass, the class that owns '%s'"),
                    DebugLabel, *VarName);
                return nullptr;
            }
            TargetClass = LoadObject<UClass>(nullptr, *TargetClassPath);
            if (TargetClass == nullptr)
            {
                UE_LOG(LogBlueprintMutator, Warning,
                    TEXT("%s: targetClass '%s' was not found"), DebugLabel, *TargetClassPath);
                return nullptr;
            }
            // Checked here rather than left to the compiler: a member
            // reference naming no property allocates no data pin, so the
            // connection that wanted it is reported as a missing pin instead
            // of as the misspelling it is.
            if (FindFProperty<FProperty>(TargetClass, *VarName) == nullptr)
            {
                UE_LOG(LogBlueprintMutator, Warning,
                    TEXT("%s: '%s' has no property named '%s'"),
                    DebugLabel, *TargetClass->GetPathName(), *VarName);
                return nullptr;
            }
        }

        FGraphNodeCreator<TNode> Creator(*Graph);
        TNode* Node = Creator.CreateNode();
        if (bTargetScope)
        {
            Node->VariableReference.SetExternalMember(FName(*VarName), TargetClass);
        }
        else
        {
            Node->VariableReference.SetSelfMember(FName(*VarName));
        }
        Node->NodePosX = FMath::RoundToInt(Pos.X);
        Node->NodePosY = FMath::RoundToInt(Pos.Y);
        Creator.Finalize();
        return Node;
    }
}

UEdGraphNode* FBPNodeFactory::CreateVariableGet(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    return CreateVariableRefNode<UK2Node_VariableGet>(Graph, Pos, ConfigJson, TEXT("CreateVariableGet"));
}

UEdGraphNode* FBPNodeFactory::CreateVariableSet(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    return CreateVariableRefNode<UK2Node_VariableSet>(Graph, Pos, ConfigJson, TEXT("CreateVariableSet"));
}

UEdGraphNode* FBPNodeFactory::CreateBranch(UEdGraph* Graph, FVector2D Pos, const FString& /*ConfigJson*/)
{
    if (!Graph) return nullptr;
    FGraphNodeCreator<UK2Node_IfThenElse> Creator(*Graph);
    UK2Node_IfThenElse* Node = Creator.CreateNode();
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateSequence(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    int32 NumOutputs = 2;
    Cfg->TryGetNumberField(TEXT("numOutputs"), NumOutputs);
    if (NumOutputs < 2) NumOutputs = 2;

    FGraphNodeCreator<UK2Node_ExecutionSequence> Creator(*Graph);
    UK2Node_ExecutionSequence* Node = Creator.CreateNode();
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();

    auto CountExecOutputs = [](UK2Node_ExecutionSequence* N)
    {
        int32 Count = 0;
        for (UEdGraphPin* P : N->Pins)
        {
            if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                ++Count;
        }
        return Count;
    };
    while (CountExecOutputs(Node) < NumOutputs) Node->AddInputPin();
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateKnot(UEdGraph* Graph, FVector2D Pos, const FString& /*ConfigJson*/)
{
    if (!Graph) return nullptr;
    FGraphNodeCreator<UK2Node_Knot> Creator(*Graph);
    UK2Node_Knot* Node = Creator.CreateNode();
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateComment(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FGraphNodeCreator<UEdGraphNode_Comment> Creator(*Graph);
    UEdGraphNode_Comment* Node = Creator.CreateNode();
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();

    FString Text;
    if (Cfg->TryGetStringField(TEXT("text"), Text)) Node->NodeComment = Text;
    int32 W = 400, H = 200;
    Cfg->TryGetNumberField(TEXT("width"), W);
    Cfg->TryGetNumberField(TEXT("height"), H);
    Node->NodeWidth = W;
    Node->NodeHeight = H;

    const TArray<TSharedPtr<FJsonValue>>* ColorArr = nullptr;
    if (Cfg->TryGetArrayField(TEXT("color"), ColorArr) && ColorArr->Num() == 4)
    {
        const float R = (float)(*ColorArr)[0]->AsNumber();
        const float G = (float)(*ColorArr)[1]->AsNumber();
        const float B = (float)(*ColorArr)[2]->AsNumber();
        const float A = (float)(*ColorArr)[3]->AsNumber();
        Node->CommentColor = FLinearColor(R, G, B, A);
    }
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateCast(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString TargetClassPath, Purity;
    Cfg->TryGetStringField(TEXT("targetClass"), TargetClassPath);
    Cfg->TryGetStringField(TEXT("purity"), Purity);
    if (TargetClassPath.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateCast: missing targetClass"));
        return nullptr;
    }
    UClass* TargetClass = LoadObject<UClass>(nullptr, *TargetClassPath);
    if (!TargetClass)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateCast: target class not found '%s'"), *TargetClassPath);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_DynamicCast> Creator(*Graph);
    UK2Node_DynamicCast* Node = Creator.CreateNode();
    Node->TargetType = TargetClass;
    const bool bPure = (Purity == TEXT("pure"));
    // UE 4.27 exposes SetPurity(bool) on UK2Node_DynamicCast. Fallback: direct bIsPureCast assignment.
    Node->SetPurity(bPure);
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateCreateWidget(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString WidgetClassPath;
    Cfg->TryGetStringField(TEXT("widgetClass"), WidgetClassPath);
    if (WidgetClassPath.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateCreateWidget: missing widgetClass"));
        return nullptr;
    }
    UClass* WidgetClass = LoadObject<UClass>(nullptr, *WidgetClassPath);
    if (!WidgetClass)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateCreateWidget: class not found '%s'"), *WidgetClassPath);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_CreateWidget> Creator(*Graph);
    UK2Node_CreateWidget* Node = Creator.CreateNode();
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();

    if (UEdGraphPin* ClassPin = Node->FindPin(TEXT("Class")))
    {
        ClassPin->DefaultObject = WidgetClass;
    }
    else
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateCreateWidget: 'Class' pin not found after Finalize"));
    }
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateSpawnActor(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString ActorClassPath;
    Cfg->TryGetStringField(TEXT("actorClass"), ActorClassPath);
    if (ActorClassPath.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateSpawnActor: missing actorClass"));
        return nullptr;
    }
    UClass* ActorClass = LoadObject<UClass>(nullptr, *ActorClassPath);
    if (!ActorClass)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateSpawnActor: class not found '%s'"), *ActorClassPath);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_SpawnActorFromClass> Creator(*Graph);
    UK2Node_SpawnActorFromClass* Node = Creator.CreateNode();
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();

    if (UEdGraphPin* ClassPin = Node->FindPin(TEXT("Class")))
    {
        ClassPin->DefaultObject = ActorClass;
    }
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateMacroInstance(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString MacroBPPath, MacroName;
    Cfg->TryGetStringField(TEXT("macroBP"), MacroBPPath);
    Cfg->TryGetStringField(TEXT("macroName"), MacroName);
    if (MacroBPPath.IsEmpty() || MacroName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateMacroInstance: missing macroBP/macroName"));
        return nullptr;
    }
    UBlueprint* MacroLib = Cast<UBlueprint>(LoadObject<UObject>(nullptr, *MacroBPPath));
    if (!MacroLib)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateMacroInstance: macro library BP not found '%s'"), *MacroBPPath);
        return nullptr;
    }
    UEdGraph* MacroGraph = nullptr;
    for (UEdGraph* G : MacroLib->MacroGraphs)
    {
        if (G && G->GetName() == MacroName) { MacroGraph = G; break; }
    }
    if (!MacroGraph)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateMacroInstance: macro '%s' not found in '%s'"), *MacroName, *MacroBPPath);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_MacroInstance> Creator(*Graph);
    UK2Node_MacroInstance* Node = Creator.CreateNode();
    Node->SetMacroGraph(MacroGraph);
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateMakeStruct(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString StructPath;
    Cfg->TryGetStringField(TEXT("structType"), StructPath);
    if (StructPath.IsEmpty()) return nullptr;
    UScriptStruct* StructType = LoadObject<UScriptStruct>(nullptr, *StructPath);
    if (!StructType)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateMakeStruct: struct not found '%s'"), *StructPath);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_MakeStruct> Creator(*Graph);
    UK2Node_MakeStruct* Node = Creator.CreateNode();
    Node->StructType = StructType;
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateBreakStruct(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString StructPath;
    Cfg->TryGetStringField(TEXT("structType"), StructPath);
    if (StructPath.IsEmpty()) return nullptr;
    UScriptStruct* StructType = LoadObject<UScriptStruct>(nullptr, *StructPath);
    if (!StructType)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateBreakStruct: struct not found '%s'"), *StructPath);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_BreakStruct> Creator(*Graph);
    UK2Node_BreakStruct* Node = Creator.CreateNode();
    Node->StructType = StructType;
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateSelect(UEdGraph* Graph, FVector2D Pos, const FString& /*ConfigJson*/)
{
    if (!Graph) return nullptr;
    FGraphNodeCreator<UK2Node_Select> Creator(*Graph);
    UK2Node_Select* Node = Creator.CreateNode();
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();
    return Node;
}

UEdGraphNode* FBPNodeFactory::CreateFormatText(UEdGraph* Graph, FVector2D Pos, const FString& /*ConfigJson*/)
{
    if (!Graph) return nullptr;
    FGraphNodeCreator<UK2Node_FormatText> Creator(*Graph);
    UK2Node_FormatText* Node = Creator.CreateNode();
    Node->NodePosX = FMath::RoundToInt(Pos.X);
    Node->NodePosY = FMath::RoundToInt(Pos.Y);
    Creator.Finalize();
    return Node;
}
