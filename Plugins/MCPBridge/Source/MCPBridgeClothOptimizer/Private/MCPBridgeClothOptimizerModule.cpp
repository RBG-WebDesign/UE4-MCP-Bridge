// Copyright 2026 RareBird Games. All Rights Reserved.

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "SClothOptimizerPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

static const FName ClothOptimizerTabName(TEXT("MCPBridgeClothOptimizer"));

class FMCPBridgeClothOptimizerModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            ClothOptimizerTabName,
            FOnSpawnTab::CreateRaw(this, &FMCPBridgeClothOptimizerModule::SpawnTab))
            .SetDisplayName(FText::FromString(TEXT("Cloth Optimizer")))
            .SetTooltipText(FText::FromString(TEXT("Analyze and optimize UE4.27 NvCloth assets.")))
            .SetMenuType(ETabSpawnerMenuType::Enabled)
            .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMCPBridgeClothOptimizerModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
        if (UToolMenus::IsToolMenuUIEnabled())
        {
            UToolMenus::UnregisterOwner(this);
        }
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ClothOptimizerTabName);
    }

private:
    TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
    {
        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [
                SNew(SClothOptimizerPanel)
            ];
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
        FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("WindowLayout"));
        Section.AddMenuEntry(
            TEXT("MCPBridge_OpenClothOptimizer"),
            FText::FromString(TEXT("Cloth Optimizer")),
            FText::FromString(TEXT("Open the local NvCloth analysis and optimization panel.")),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateRaw(this, &FMCPBridgeClothOptimizerModule::OpenPanel)));
    }

    void OpenPanel()
    {
        FGlobalTabmanager::Get()->TryInvokeTab(ClothOptimizerTabName);
    }
};

IMPLEMENT_MODULE(FMCPBridgeClothOptimizerModule, MCPBridgeClothOptimizer)
