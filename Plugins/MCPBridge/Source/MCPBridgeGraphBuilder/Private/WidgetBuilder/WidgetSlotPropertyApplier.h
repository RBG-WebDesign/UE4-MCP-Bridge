#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprintSpec.h"

class UPanelSlot;

class FWidgetSlotPropertyApplier
{
public:
    /**
     * Apply slot properties from the spec to the given slot.
     * Casts to the concrete slot type (CanvasPanelSlot, VerticalBoxSlot, etc.)
     * and sets only the properties that were specified in JSON.
     */
    static bool ApplySlotProperties(UPanelSlot* Slot, const FWidgetSlotSpec& SlotSpec, const FString& Path, FString& OutError);
};
