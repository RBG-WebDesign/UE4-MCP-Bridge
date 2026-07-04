#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "BPGLogCategories.h"
#include "BlueprintMutator/BPNodeRegistry.h"

DEFINE_LOG_CATEGORY(LogBlueprintInspector);
DEFINE_LOG_CATEGORY(LogBlueprintMutator);

class FBlueprintGraphBuilderModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FBPNodeRegistry::Initialize();
    }
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FBlueprintGraphBuilderModule, MCPBridgeGraphBuilder)
