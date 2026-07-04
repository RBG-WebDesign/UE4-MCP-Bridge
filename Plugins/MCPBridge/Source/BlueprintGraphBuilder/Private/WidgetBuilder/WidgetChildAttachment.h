#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"


class UWidget;
class UPanelWidget;
class UPanelSlot;

class FWidgetChildAttachment
{
public:
    /**
     * Attach a child widget to its parent. Returns the created slot, or nullptr on failure.
     * Pass 5 will use the returned slot to apply slot properties (position, padding, alignment).
     */
    UPanelSlot* AttachChild(UWidget* Parent, UWidget* Child, const FString& Path, FString& OutError);

private:
    UPanelSlot* AttachToPanel(UPanelWidget* Panel, UWidget* Child, const FString& Path, FString& OutError);
};
