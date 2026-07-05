// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPPinSerializer.h"
#include "BPTypeDescriptor.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphNode.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

TSharedPtr<FJsonObject> FBPPinSerializer::Serialize(const UEdGraphPin* Pin)
{
    TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
    if (!Pin) return O;

    O->SetStringField(TEXT("pin_id"), Pin->PinId.ToString());
    O->SetStringField(TEXT("name"), Pin->PinName.ToString());
    O->SetStringField(TEXT("direction"),
        Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
    O->SetObjectField(TEXT("type"), FBPTypeDescriptor::ToJson(Pin->PinType));
    O->SetStringField(TEXT("default_value"), Pin->DefaultValue);

    TArray<TSharedPtr<FJsonValue>> Links;
    for (UEdGraphPin* Linked : Pin->LinkedTo)
    {
        if (!Linked || !Linked->GetOwningNode()) continue;
        TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
        L->SetStringField(TEXT("node_guid"), Linked->GetOwningNode()->NodeGuid.ToString());
        L->SetStringField(TEXT("pin_id"), Linked->PinId.ToString());
        Links.Add(MakeShared<FJsonValueObject>(L));
    }
    O->SetArrayField(TEXT("links"), Links);
    return O;
}
