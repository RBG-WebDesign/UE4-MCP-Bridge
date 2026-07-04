#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPBridgeProfilerLibrary.generated.h"

UCLASS()
class MCPBRIDGEEDITORPANEL_API UMCPBridgeProfilerLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "MCP Bridge|Profiler")
    static FString SummarizeTraceFile(const FString& TracePath);
};

