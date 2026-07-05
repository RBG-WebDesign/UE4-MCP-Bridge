// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class FJsonObject;

class FBPTypeDescriptor
{
public:
    /** Serialize an FEdGraphPinType into a JSON object matching the Type Descriptor schema. */
    static TSharedPtr<FJsonObject> ToJson(const FEdGraphPinType& PinType);

    /** Reverse: build an FEdGraphPinType from a Type Descriptor JSON object.
     *  Returns false on malformed input; PinType may be partially filled.
     *  Note: when sub_category_object is set, resolves the asset via FindObject
     *  first (fast, side-effect-free) and falls back to StaticLoadObject
     *  (synchronous load). Suitable for mutator/write paths. */
    static bool FromJson(const TSharedPtr<FJsonObject>& JsonObject, FEdGraphPinType& OutPinType);

    /** Convenience: serialize to compact JSON string. */
    static FString ToJsonString(const FEdGraphPinType& PinType);

    /** Convenience: parse from string (returns defaulted type on failure). */
    static FEdGraphPinType FromJsonString(const FString& JsonString);
};
