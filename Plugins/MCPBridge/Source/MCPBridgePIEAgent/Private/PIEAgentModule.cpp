// Copyright 2026 RareBird Games. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "PIEAgentRuntime.h"

namespace
{
    UPIEAgentRuntime* GPIEAgentRuntime = nullptr;
}

UPIEAgentRuntime* GetPIEAgentRuntime()
{
    return GPIEAgentRuntime;
}

void SetPIEAgentRuntime(UPIEAgentRuntime* Runtime)
{
    GPIEAgentRuntime = Runtime;
}

class FMCPBridgePIEAgentModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UPIEAgentRuntime* Runtime = NewObject<UPIEAgentRuntime>(GetTransientPackage());
        Runtime->AddToRoot();
        Runtime->Initialize();
        SetPIEAgentRuntime(Runtime);
    }

    virtual void ShutdownModule() override
    {
        if (UPIEAgentRuntime* Runtime = GetPIEAgentRuntime())
        {
            Runtime->Shutdown();
            Runtime->RemoveFromRoot();
        }
        SetPIEAgentRuntime(nullptr);
    }
};

IMPLEMENT_MODULE(FMCPBridgePIEAgentModule, MCPBridgePIEAgent)
