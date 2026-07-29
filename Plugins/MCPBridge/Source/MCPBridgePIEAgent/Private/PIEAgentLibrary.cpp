// Copyright 2026 RareBird Games. All Rights Reserved.

#include "PIEAgentLibrary.h"
#include "PIEAgentRuntime.h"

namespace
{
    FString RuntimeUnavailable()
    {
        return TEXT("{\"success\":false,\"data\":{},\"error\":\"PIE agent runtime is unavailable\"}");
    }
}

FString UPIEAgentLibrary::StartMoveTo(const FString& RequestJson)
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->StartMoveTo(RequestJson) : RuntimeUnavailable();
}

FString UPIEAgentLibrary::LookAt(const FString& RequestJson)
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->LookAt(RequestJson) : RuntimeUnavailable();
}

FString UPIEAgentLibrary::SetKeyState(const FString& KeyName, bool bPressed)
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->SetKeyState(KeyName, bPressed) : RuntimeUnavailable();
}

FString UPIEAgentLibrary::SetAxisState(const FString& AxisKeyName, float Value)
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->SetAxisState(AxisKeyName, Value) : RuntimeUnavailable();
}

FString UPIEAgentLibrary::ClearAxisState()
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->ClearAxisState() : RuntimeUnavailable();
}

FString UPIEAgentLibrary::StartPress(const FString& RequestJson)
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->StartPress(RequestJson) : RuntimeUnavailable();
}

FString UPIEAgentLibrary::Observe(const FString& RequestJson)
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->Observe(RequestJson) : RuntimeUnavailable();
}

FString UPIEAgentLibrary::RecordStart(const FString& RequestJson)
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->RecordStart(RequestJson) : RuntimeUnavailable();
}

FString UPIEAgentLibrary::RecordStop()
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->RecordStop() : RuntimeUnavailable();
}

FString UPIEAgentLibrary::StartExpect(const FString& RequestJson)
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->StartExpect(RequestJson) : RuntimeUnavailable();
}

FString UPIEAgentLibrary::StartReplay(const FString& RequestJson)
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->StartReplay(RequestJson) : RuntimeUnavailable();
}

FString UPIEAgentLibrary::GetOperationStatus(int32 OperationId)
{
    return GetPIEAgentRuntime() ? GetPIEAgentRuntime()->GetOperationStatus(OperationId) : RuntimeUnavailable();
}
