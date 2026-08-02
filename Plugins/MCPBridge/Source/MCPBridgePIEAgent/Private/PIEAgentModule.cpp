// Copyright 2026 RareBird Games. All Rights Reserved.

#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "PIEAgentRuntime.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPBridgePIEAgentModule, Log, All);

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

        // Release the runtime from UEngine::PreExit rather than from ShutdownModule.
        //
        // Initialize registers two things that outlive this object unless they are
        // explicitly removed: a core ticker bound to it with
        // FTickerDelegate::CreateUObject, and an FOutputDevice (FPIEAgentLogSink) held
        // by GLog whose backing memory this UObject owns. UE4.27 calls ShutdownModule
        // from FModuleManager::UnloadModulesAtShutdown (LaunchEngineLoop.cpp:4294),
        // which runs AFTER StaticExit has destroyed every UObject. By then this runtime
        // is gone, and GLog has been holding a freed log sink since StaticExit ran, so
        // the editor stops dead on the very line StaticExit itself emits
        // ("LogExit: Object subsystem successfully closed."). The window is destroyed
        // and the exit code is set, but the process never finishes exiting: it keeps
        // the project's plugin DLLs locked, so the project cannot be rebuilt, and it
        // cannot be killed - taskkill answers "There is no running instance of the
        // task". Only a reboot clears it.
        //
        // OnEnginePreExit is broadcast at the top of UEngine::PreExit
        // (UnrealEngine.cpp:1878), reached from FEngineLoop::Exit line 4208, while the
        // object system is still up and the ticker and GLog are still valid.
        //
        // Measured on BridgeInstallTest: with this module loaded the editor never
        // exited; with it absent the identical editor closed in 4.1 s. See
        // docs/CAPABILITY_FINDINGS.md defect 0f.
        EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddRaw(
            this, &FMCPBridgePIEAgentModule::HandleEnginePreExit);

        UE_LOG(LogMCPBridgePIEAgentModule, Display,
            TEXT("MCPBridge lifecycle: PIE agent runtime started."));
    }

    virtual void ShutdownModule() override
    {
        if (EnginePreExitHandle.IsValid())
        {
            FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitHandle);
            EnginePreExitHandle.Reset();
        }
        // Fallback only. On a normal editor close HandleEnginePreExit has already run
        // and this is a no-op; it still matters for a module unloaded on its own
        // (hot reload, plugin disable) where OnEnginePreExit never fires.
        ReleaseRuntime(TEXT("ShutdownModule"));
    }

private:
    void HandleEnginePreExit()
    {
        ReleaseRuntime(TEXT("OnEnginePreExit"));
    }

    void ReleaseRuntime(const TCHAR* Trigger)
    {
        UPIEAgentRuntime* Runtime = GetPIEAgentRuntime();
        if (Runtime == nullptr)
        {
            return;
        }

        UE_LOG(LogMCPBridgePIEAgentModule, Display,
            TEXT("MCPBridge lifecycle: PIE agent shutdown begin (trigger: %s)."), Trigger);

        // Unregisters the core ticker and removes the log sink from GLog.
        Runtime->Shutdown();
        Runtime->RemoveFromRoot();
        SetPIEAgentRuntime(nullptr);

        UE_LOG(LogMCPBridgePIEAgentModule, Display,
            TEXT("MCPBridge lifecycle: PIE agent runtime released, ticker and log sink unregistered."));
    }

    FDelegateHandle EnginePreExitHandle;
};

IMPLEMENT_MODULE(FMCPBridgePIEAgentModule, MCPBridgePIEAgent)
