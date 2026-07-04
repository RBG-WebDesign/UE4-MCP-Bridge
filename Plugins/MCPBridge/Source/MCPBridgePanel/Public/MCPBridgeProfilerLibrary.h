#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPBridgeProfilerLibrary.generated.h"

UCLASS()
class MCPBRIDGEPANEL_API UMCPBridgeProfilerLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "MCP Bridge|Profiler")
    static FString SummarizeTraceFile(const FString& TracePath);
};

