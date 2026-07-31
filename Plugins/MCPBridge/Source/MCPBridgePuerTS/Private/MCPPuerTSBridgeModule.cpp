#include "MCPPuerTSBridgeService.h"

#include "JsEnv.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NamespaceDef.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPPuerTSBridgeModule, Log, All);

class FMCPPuerTSBridgeModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("MCPPuerTSBridgeDisabled")))
        {
            return;
        }

        Service = NewObject<UMCPPuerTSBridgeService>(GetTransientPackage());
        if (Service == nullptr)
        {
            UE_LOG(LogMCPPuerTSBridgeModule, Error, TEXT("Could not allocate the bridge service."));
            return;
        }
        Service->AddToRoot();

        FString Error;
        if (!Service->Initialize(Error))
        {
            UE_LOG(LogMCPPuerTSBridgeModule, Error, TEXT("Bridge initialization failed: %s"), *Error);
            Service->RemoveFromRoot();
            Service = nullptr;
            return;
        }

        ScriptEnvironment = MakeUnique<PUERTS_NAMESPACE::FJsEnv>(Service->GetAllowedScriptRoot());
        TArray<TPair<FString, UObject*>> Arguments;
        Arguments.Emplace(TEXT("bridge"), Service);
        ScriptEnvironment->Start(Service->GetBootstrapModule(), Arguments);
        UE_LOG(LogMCPPuerTSBridgeModule, Display, TEXT("PuerTS named-pipe bootstrap loaded from the approved script root."));
    }

    virtual void ShutdownModule() override
    {
        ScriptEnvironment.Reset();
        if (Service != nullptr)
        {
            Service->Shutdown();
            Service->RemoveFromRoot();
            Service = nullptr;
        }
    }

private:
    UMCPPuerTSBridgeService* Service = nullptr;
    TUniquePtr<PUERTS_NAMESPACE::FJsEnv> ScriptEnvironment;
};

IMPLEMENT_MODULE(FMCPPuerTSBridgeModule, MCPBridgePuerTS)
