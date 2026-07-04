#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/EngineTypes.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

class UEdGraph;
class UEdGraphNode;

namespace BPNodeFactoryInternal
{
    /** Parse a JSON object; empty string -> empty object. Logs+fails on malformed input. */
    bool ParseJsonConfig(const FString& ConfigJson, TSharedPtr<FJsonObject>& OutObj);

    /**
     * Finalize the node via its creator and position it. Returns the node pointer for
     * chaining at the end of a factory body. Template so it works for any FGraphNodeCreator<T>
     * and any UK2Node derivative without further casts.
     */
    template <typename NodeT>
    UEdGraphNode* PositionAndFinalize(FGraphNodeCreator<NodeT>& Creator, NodeT* Node, FVector2D Pos)
    {
        Node->NodePosX = static_cast<int32>(Pos.X);
        Node->NodePosY = static_cast<int32>(Pos.Y);
        Creator.Finalize();
        return Node;
    }
}
