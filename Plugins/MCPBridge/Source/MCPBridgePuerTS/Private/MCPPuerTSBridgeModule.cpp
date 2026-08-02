#include "MCPPuerTSBridgeService.h"

#include "JsEnv.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
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
            UE_LOG(LogMCPPuerTSBridgeModule, Display,
                TEXT("MCPBridge lifecycle: startup skipped, -MCPPuerTSBridgeDisabled was passed."));
            return;
        }

        UE_LOG(LogMCPPuerTSBridgeModule, Display, TEXT("MCPBridge lifecycle: startup begin."));

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

        // Release the bridge from UEngine::PreExit rather than from ShutdownModule.
        //
        // UE4.27 calls ShutdownModule from FModuleManager::UnloadModulesAtShutdown
        // (LaunchEngineLoop.cpp:4294). By then AppPreExit has destroyed GThreadPool,
        // GLargeThreadPool, GBackgroundPriorityThreadPool and GIOThreadPool
        // (LaunchEngineLoop.cpp:5817-5837), StopRenderingThread has run (4273), and
        // StaticExit has closed the object subsystem. FJsEnv's destructor cannot
        // finish there: FJsEnvImpl::StopPolling joins the PuerTS polling thread and
        // then waits on a task it dispatched to ENamedThreads::GameThread
        // (JsEnvImpl.cpp:187, 322), and node::FreeEnvironment afterwards has to pump
        // the libuv loop until every handle closes. The editor window disappears and
        // the exit code is set, but the process never finishes exiting: it keeps its
        // named pipe listening and its plugin DLLs locked, so the project cannot be
        // rebuilt until the machine is restarted. taskkill answers "There is no
        // running instance of the task" for such a process.
        //
        // OnEnginePreExit is broadcast at the top of UEngine::PreExit
        // (UnrealEngine.cpp:1878), reached from FEngineLoop::Exit line 4208, while
        // the game thread still services the task graph and the object subsystem is
        // still up. Everything the destructor needs is alive at that point.
        EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddRaw(
            this, &FMCPPuerTSBridgeModule::HandleEnginePreExit);

        ScriptEnvironment = MakeUnique<PUERTS_NAMESPACE::FJsEnv>(Service->GetAllowedScriptRoot());
        TArray<TPair<FString, UObject*>> Arguments;
        Arguments.Emplace(TEXT("bridge"), Service);
        ScriptEnvironment->Start(Service->GetBootstrapModule(), Arguments);
        UE_LOG(LogMCPPuerTSBridgeModule, Display,
            TEXT("MCPBridge lifecycle: startup complete, bootstrap '%s' loaded from the approved script root."),
            *Service->GetBootstrapModule());
    }

    virtual void ShutdownModule() override
    {
        if (EnginePreExitHandle.IsValid())
        {
            FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitHandle);
            EnginePreExitHandle.Reset();
        }
        // Fallback only. On a normal editor close HandleEnginePreExit has already
        // run and this is a no-op; it still matters for a module unloaded on its own
        // (hot reload, plugin disable) where OnEnginePreExit never fires.
        ReleaseBridge(TEXT("ShutdownModule"));
    }

private:
    void HandleEnginePreExit()
    {
        ReleaseBridge(TEXT("OnEnginePreExit"));
    }

    void ReleaseBridge(const TCHAR* Trigger)
    {
        if (ScriptEnvironment.IsValid() == false && Service == nullptr)
        {
            return;
        }

        const double StartSeconds = FPlatformTime::Seconds();
        UE_LOG(LogMCPPuerTSBridgeModule, Display,
            TEXT("MCPBridge lifecycle: shutdown begin (trigger: %s)."), Trigger);

        if (ScriptEnvironment.IsValid())
        {
            UE_LOG(LogMCPPuerTSBridgeModule, Display,
                TEXT("MCPBridge lifecycle: releasing the PuerTS script environment."));
            ScriptEnvironment.Reset();
            UE_LOG(LogMCPPuerTSBridgeModule, Display,
                TEXT("MCPBridge lifecycle: PuerTS script environment released after %.3f s."),
                FPlatformTime::Seconds() - StartSeconds);
        }

        if (Service != nullptr)
        {
            Service->Shutdown();
            Service->RemoveFromRoot();
            Service = nullptr;
            UE_LOG(LogMCPPuerTSBridgeModule, Display, TEXT("MCPBridge lifecycle: bridge service released."));
        }

        UE_LOG(LogMCPPuerTSBridgeModule, Display,
            TEXT("MCPBridge lifecycle: shutdown complete in %.3f s."),
            FPlatformTime::Seconds() - StartSeconds);
    }

    UMCPPuerTSBridgeService* Service = nullptr;
    TUniquePtr<PUERTS_NAMESPACE::FJsEnv> ScriptEnvironment;
    FDelegateHandle EnginePreExitHandle;
};

IMPLEMENT_MODULE(FMCPPuerTSBridgeModule, MCPBridgePuerTS)
