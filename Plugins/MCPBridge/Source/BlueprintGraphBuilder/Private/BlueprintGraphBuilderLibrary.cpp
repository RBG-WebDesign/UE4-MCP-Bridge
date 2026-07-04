#include "BlueprintGraphBuilderLibrary.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "JsonObjectConverter.h"
#include "UObject/TextProperty.h"

void UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON(
    UBlueprint* Blueprint,
    const FString& JsonString,
    bool bClearExistingGraph)
{
    if (!Blueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: Blueprint is null"));
        return;
    }

    // --- Step 1: Parse JSON ---
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: Invalid JSON: %s"), *JsonString);
        return;
    }

    // --- Step 2: Get event graph ---
    UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (!Graph && Blueprint->UbergraphPages.Num() > 0)
    {
        Graph = Blueprint->UbergraphPages[0];
    }
    if (!Graph)
    {
        UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: No event graph found on Blueprint"));
        return;
    }

    // --- Step 3: Clear existing nodes ---
    if (bClearExistingGraph)
    {
        TArray<UEdGraphNode*> NodesToRemove = Graph->Nodes;
        for (UEdGraphNode* Node : NodesToRemove)
        {
            FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile=*/true);
        }
    }

    // --- Step 4: Spawn nodes ---
    const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("nodes"), NodesArray))
    {
        UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: Missing 'nodes' array"));
        return;
    }

    TMap<FString, UEdGraphNode*> NodeMap;

    for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
    {
        const TSharedPtr<FJsonObject>* NodeObj = nullptr;
        if (!NodeValue->TryGetObject(NodeObj))
        {
            continue;
        }

        FString NodeId, NodeType;
        (*NodeObj)->TryGetStringField(TEXT("id"), NodeId);
        (*NodeObj)->TryGetStringField(TEXT("type"), NodeType);

        if (NodeId.IsEmpty() || NodeType.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("BuildBlueprintFromJSON: Skipping node with missing id or type"));
            continue;
        }

        UEdGraphNode* SpawnedNode = nullptr;

        if (NodeType == TEXT("BeginPlay"))
        {
            FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
            UK2Node_Event* EventNode = Creator.CreateNode();
            EventNode->EventReference.SetExternalMember(
                FName(TEXT("ReceiveBeginPlay")),
                AActor::StaticClass()
            );
            EventNode->bOverrideFunction = true;
            EventNode->NodePosX = 0;
            EventNode->NodePosY = 0;
            Creator.Finalize();
            SpawnedNode = EventNode;
        }
        else if (NodeType == TEXT("ActorBeginOverlap"))
        {
            FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
            UK2Node_Event* EventNode = Creator.CreateNode();
            EventNode->EventReference.SetExternalMember(
                FName(TEXT("ReceiveActorBeginOverlap")),
                AActor::StaticClass()
            );
            EventNode->bOverrideFunction = true;
            EventNode->NodePosX = 0;
            EventNode->NodePosY = 200;
            Creator.Finalize();
            SpawnedNode = EventNode;
        }
        else if (NodeType == TEXT("ActorEndOverlap"))
        {
            FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
            UK2Node_Event* EventNode = Creator.CreateNode();
            EventNode->EventReference.SetExternalMember(
                FName(TEXT("ReceiveActorEndOverlap")),
                AActor::StaticClass()
            );
            EventNode->bOverrideFunction = true;
            EventNode->NodePosX = 0;
            EventNode->NodePosY = 400;
            Creator.Finalize();
            SpawnedNode = EventNode;
        }
        else if (NodeType == TEXT("PrintString"))
        {
            UFunction* Func = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString"));
            if (!Func)
            {
                UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: PrintString function not found"));
                return;
            }
            FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
            UK2Node_CallFunction* CallNode = Creator.CreateNode();
            CallNode->SetFromFunction(Func);
            CallNode->NodePosX = 300;
            CallNode->NodePosY = 0;
            Creator.Finalize();

            // Apply params as pin defaults (e.g. InString)
            const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
            if ((*NodeObj)->TryGetObjectField(TEXT("params"), ParamsObj))
            {
                for (auto& KV : (*ParamsObj)->Values)
                {
                    UEdGraphPin* Pin = CallNode->FindPin(FName(*KV.Key));
                    if (Pin && KV.Value.IsValid())
                    {
                        FString Val;
                        if (KV.Value->TryGetString(Val))
                        {
                            Pin->DefaultValue = Val;
                        }
                    }
                }
            }

            SpawnedNode = CallNode;
        }
        else if (NodeType == TEXT("CallFunction"))
        {
            // Generic CallFunction: looks up function by "class" and "function" params
            FString ClassName, FuncName;
            const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
            if ((*NodeObj)->TryGetObjectField(TEXT("params"), ParamsObj))
            {
                (*ParamsObj)->TryGetStringField(TEXT("class"), ClassName);
                (*ParamsObj)->TryGetStringField(TEXT("function"), FuncName);
            }

            UClass* TargetClass = FindObject<UClass>(ANY_PACKAGE, *ClassName);
            if (!TargetClass)
            {
                UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: Class '%s' not found for CallFunction"), *ClassName);
                continue;
            }

            UFunction* Func = TargetClass->FindFunctionByName(*FuncName);
            if (!Func)
            {
                UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: Function '%s' not found on '%s'"), *FuncName, *ClassName);
                continue;
            }

            FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
            UK2Node_CallFunction* CallNode = Creator.CreateNode();
            CallNode->SetFromFunction(Func);
            CallNode->NodePosX = 300;
            CallNode->NodePosY = 200;
            Creator.Finalize();

            // Apply additional params as pin default values
            if (ParamsObj)
            {
                for (auto& KV : (*ParamsObj)->Values)
                {
                    // Skip "class" and "function" -- those are routing params
                    if (KV.Key == TEXT("class") || KV.Key == TEXT("function")) continue;

                    UEdGraphPin* Pin = CallNode->FindPin(FName(*KV.Key));
                    if (Pin && KV.Value.IsValid())
                    {
                        FString Val;
                        if (KV.Value->TryGetString(Val))
                        {
                            Pin->DefaultValue = Val;
                        }
                        else
                        {
                            double NumVal;
                            if (KV.Value->TryGetNumber(NumVal))
                            {
                                Pin->DefaultValue = FString::SanitizeFloat(NumVal);
                            }
                            else
                            {
                                bool BoolVal;
                                if (KV.Value->TryGetBool(BoolVal))
                                {
                                    Pin->DefaultValue = BoolVal ? TEXT("true") : TEXT("false");
                                }
                            }
                        }
                        UE_LOG(LogTemp, Log, TEXT("BuildBlueprintFromJSON: Set pin '%s' = '%s'"), *KV.Key, *Pin->DefaultValue);
                    }
                }
            }

            SpawnedNode = CallNode;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("BuildBlueprintFromJSON: Unknown node type '%s', skipping"), *NodeType);
            continue;
        }

        NodeMap.Add(NodeId, SpawnedNode);
    }

    // --- Step 5: Wire connections ---
    const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
    if (RootObject->TryGetArrayField(TEXT("connections"), ConnectionsArray))
    {
        for (const TSharedPtr<FJsonValue>& ConnValue : *ConnectionsArray)
        {
            const TSharedPtr<FJsonObject>* ConnObj = nullptr;
            if (!ConnValue->TryGetObject(ConnObj))
            {
                continue;
            }

            FString FromStr, ToStr;
            (*ConnObj)->TryGetStringField(TEXT("from"), FromStr);
            (*ConnObj)->TryGetStringField(TEXT("to"), ToStr);

            // Parse "nodeId.pinRole"
            FString FromNodeId, FromPinRole, ToNodeId, ToPinRole;
            FromStr.Split(TEXT("."), &FromNodeId, &FromPinRole);
            ToStr.Split(TEXT("."), &ToNodeId, &ToPinRole);

            UEdGraphNode** FromNodePtr = NodeMap.Find(FromNodeId);
            UEdGraphNode** ToNodePtr = NodeMap.Find(ToNodeId);
            if (!FromNodePtr || !ToNodePtr)
            {
                UE_LOG(LogTemp, Warning, TEXT("BuildBlueprintFromJSON: Connection references unknown node(s): %s -> %s"), *FromStr, *ToStr);
                continue;
            }

            // For exec connections: source uses PN_Then (output), target uses PN_Execute (input)
            UEdGraphPin* SourcePin = (*FromNodePtr)->FindPin(UEdGraphSchema_K2::PN_Then);
            UEdGraphPin* TargetPin = (*ToNodePtr)->FindPin(UEdGraphSchema_K2::PN_Execute);

            if (!SourcePin || !TargetPin)
            {
                UE_LOG(LogTemp, Warning, TEXT("BuildBlueprintFromJSON: Could not find exec pins for connection %s -> %s"), *FromStr, *ToStr);
                continue;
            }

            SourcePin->MakeLinkTo(TargetPin);
        }
    }

    // --- Step 6: Mark and compile ---
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Blueprint->MarkPackageDirty();

    UE_LOG(LogTemp, Log, TEXT("BuildBlueprintFromJSON: Done. %d nodes spawned."), NodeMap.Num());
}

bool UBlueprintGraphBuilderLibrary::AddComponentToBlueprint(
    UBlueprint* Blueprint,
    TSubclassOf<UActorComponent> ComponentClass,
    const FString& ComponentName,
    const FString& AttachToName)
{
    if (!Blueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("AddComponentToBlueprint: Blueprint is null"));
        return false;
    }

    if (!ComponentClass)
    {
        UE_LOG(LogTemp, Error, TEXT("AddComponentToBlueprint: ComponentClass is null"));
        return false;
    }

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (!SCS)
    {
        UE_LOG(LogTemp, Error, TEXT("AddComponentToBlueprint: Blueprint has no SimpleConstructionScript"));
        return false;
    }

    // Create the SCS node
    USCS_Node* NewNode = SCS->CreateNode(ComponentClass, *ComponentName);
    if (!NewNode)
    {
        UE_LOG(LogTemp, Error, TEXT("AddComponentToBlueprint: Failed to create SCS node for %s"), *ComponentName);
        return false;
    }

    // Attach to parent or add as root
    if (!AttachToName.IsEmpty())
    {
        // Find the parent node
        TArray<USCS_Node*> AllNodes = SCS->GetAllNodes();
        USCS_Node* ParentNode = nullptr;
        for (USCS_Node* Node : AllNodes)
        {
            if (Node && Node->GetVariableName().ToString() == AttachToName)
            {
                ParentNode = Node;
                break;
            }
        }

        if (ParentNode)
        {
            ParentNode->AddChildNode(NewNode);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("AddComponentToBlueprint: Parent '%s' not found, adding as root"), *AttachToName);
            SCS->AddNode(NewNode);
        }
    }
    else
    {
        SCS->AddNode(NewNode);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogTemp, Log, TEXT("AddComponentToBlueprint: Added '%s' (%s) to %s"),
        *ComponentName, *ComponentClass->GetName(), *Blueprint->GetName());

    return true;
}

bool UBlueprintGraphBuilderLibrary::SetComponentProperty(
    UBlueprint* Blueprint,
    const FString& ComponentName,
    const FString& PropertyName,
    const FString& JsonValue)
{
    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty: Blueprint or SCS is null"));
        return false;
    }

    // Find the SCS node by name
    TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
    USCS_Node* TargetNode = nullptr;
    for (USCS_Node* Node : AllNodes)
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            TargetNode = Node;
            break;
        }
    }

    if (!TargetNode)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty: Component '%s' not found"), *ComponentName);
        return false;
    }

    UActorComponent* Template = TargetNode->ComponentTemplate;
    if (!Template)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty: Component '%s' has no template"), *ComponentName);
        return false;
    }

    // Find the property by name
    FProperty* Property = Template->GetClass()->FindPropertyByName(*PropertyName);
    if (!Property)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty: Property '%s' not found on %s"),
            *PropertyName, *Template->GetClass()->GetName());
        return false;
    }

    // Set the property value from string using ImportText (works in UE4.27)
    void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Template);
    if (!Property->ImportText(*JsonValue, ValuePtr, 0, Template))
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty: Failed to set '%s' from value: %s"),
            *PropertyName, *JsonValue);
        return false;
    }

    Template->MarkPackageDirty();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty: Set %s.%s = %s"),
        *ComponentName, *PropertyName, *JsonValue);

    return true;
}
