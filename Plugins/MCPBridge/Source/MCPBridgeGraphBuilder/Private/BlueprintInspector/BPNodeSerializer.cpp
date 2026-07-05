// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPNodeSerializer.h"
#include "BPPinSerializer.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"

namespace
{
    FString EnabledStateToString(ENodeEnabledState State)
    {
        switch (State)
        {
            case ENodeEnabledState::Disabled:        return TEXT("Disabled");
            case ENodeEnabledState::DevelopmentOnly: return TEXT("DevelopmentOnly");
            default:                                  return TEXT("Enabled");
        }
    }
}

TSharedPtr<FJsonObject> FBPNodeSerializer::Serialize(UEdGraphNode* Node)
{
    TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
    if (!Node) return O;

    O->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
    O->SetStringField(TEXT("class"), Node->GetClass()->GetName());
    O->SetStringField(TEXT("title"),
        Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
    O->SetStringField(TEXT("comment"), Node->NodeComment);

    TSharedPtr<FJsonObject> Pos = MakeShared<FJsonObject>();
    Pos->SetNumberField(TEXT("x"), Node->NodePosX);
    Pos->SetNumberField(TEXT("y"), Node->NodePosY);
    O->SetObjectField(TEXT("position"), Pos);

    O->SetStringField(TEXT("enabled"), EnabledStateToString(Node->GetDesiredEnabledState()));

    // Per-type metadata enrichment (opportunistic).
    // IMPORTANT: check UK2Node_CustomEvent before UK2Node_Event — CustomEvent inherits from Event.
    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    if (UK2Node_CallFunction* CF = Cast<UK2Node_CallFunction>(Node))
    {
        Meta->SetStringField(TEXT("target_function"), CF->GetFunctionName().ToString());
        if (UClass* TC = CF->FunctionReference.GetMemberParentClass(nullptr))
        {
            Meta->SetStringField(TEXT("target_class"), TC->GetPathName());
        }
        else
        {
            Meta->SetStringField(TEXT("target_class"), FString());
        }
    }
    else if (UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node))
    {
        Meta->SetStringField(TEXT("event_name"), CE->CustomFunctionName.ToString());
    }
    else if (UK2Node_Event* E = Cast<UK2Node_Event>(Node))
    {
        Meta->SetStringField(TEXT("event_name"), E->EventReference.GetMemberName().ToString());
    }
    else if (UK2Node_Variable* V = Cast<UK2Node_Variable>(Node))
    {
        Meta->SetStringField(TEXT("var_name"), V->GetVarName().ToString());
    }
    O->SetObjectField(TEXT("metadata"), Meta);

    // Pins
    TArray<TSharedPtr<FJsonValue>> Pins;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        Pins.Add(MakeShared<FJsonValueObject>(FBPPinSerializer::Serialize(Pin)));
    }
    O->SetArrayField(TEXT("pins"), Pins);
    return O;
}
