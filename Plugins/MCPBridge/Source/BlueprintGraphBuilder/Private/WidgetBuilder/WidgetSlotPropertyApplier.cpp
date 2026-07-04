#include "WidgetSlotPropertyApplier.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"

bool FWidgetSlotPropertyApplier::ApplySlotProperties(UPanelSlot* Slot, const FWidgetSlotSpec& SlotSpec, const FString& Path, FString& OutError)
{
    if (!Slot)
    {
        OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Slot is null, cannot apply slot properties"), *Path);
        return false;
    }

    // CanvasPanelSlot: position, size, alignment, zOrder, autoSize
    if (UCanvasPanelSlot* Canvas = Cast<UCanvasPanelSlot>(Slot))
    {
        if (SlotSpec.bHasPosition)
        {
            Canvas->SetPosition(SlotSpec.Position);
            UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] %s: Slot position = (%f, %f)"), *Path, SlotSpec.Position.X, SlotSpec.Position.Y);
        }
        if (SlotSpec.bHasSize)
        {
            Canvas->SetSize(SlotSpec.Size);
            UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] %s: Slot size = (%f, %f)"), *Path, SlotSpec.Size.X, SlotSpec.Size.Y);
        }
        if (SlotSpec.bHasAlignment)
        {
            Canvas->SetAlignment(SlotSpec.Alignment);
        }
        if (SlotSpec.bHasZOrder)
        {
            Canvas->SetZOrder(SlotSpec.ZOrder);
        }
        if (SlotSpec.bHasAutoSize)
        {
            Canvas->SetAutoSize(SlotSpec.bAutoSize);
        }
        return true;
    }

    // VerticalBoxSlot: padding, alignment
    if (UVerticalBoxSlot* VBox = Cast<UVerticalBoxSlot>(Slot))
    {
        if (SlotSpec.bHasPadding)
        {
            VBox->SetPadding(SlotSpec.Padding);
        }
        if (SlotSpec.bHasAlignment)
        {
            VBox->SetHorizontalAlignment(SlotSpec.Alignment.X < 0.33f ? HAlign_Left : SlotSpec.Alignment.X < 0.66f ? HAlign_Center : HAlign_Right);
            VBox->SetVerticalAlignment(SlotSpec.Alignment.Y < 0.33f ? VAlign_Top : SlotSpec.Alignment.Y < 0.66f ? VAlign_Center : VAlign_Bottom);
        }
        return true;
    }

    // HorizontalBoxSlot: padding, alignment
    if (UHorizontalBoxSlot* HBox = Cast<UHorizontalBoxSlot>(Slot))
    {
        if (SlotSpec.bHasPadding)
        {
            HBox->SetPadding(SlotSpec.Padding);
        }
        if (SlotSpec.bHasAlignment)
        {
            HBox->SetHorizontalAlignment(SlotSpec.Alignment.X < 0.33f ? HAlign_Left : SlotSpec.Alignment.X < 0.66f ? HAlign_Center : HAlign_Right);
            HBox->SetVerticalAlignment(SlotSpec.Alignment.Y < 0.33f ? VAlign_Top : SlotSpec.Alignment.Y < 0.66f ? VAlign_Center : VAlign_Bottom);
        }
        return true;
    }

    // OverlaySlot: padding, alignment
    if (UOverlaySlot* OverlayS = Cast<UOverlaySlot>(Slot))
    {
        if (SlotSpec.bHasPadding)
        {
            OverlayS->SetPadding(SlotSpec.Padding);
        }
        if (SlotSpec.bHasAlignment)
        {
            OverlayS->SetHorizontalAlignment(SlotSpec.Alignment.X < 0.33f ? HAlign_Left : SlotSpec.Alignment.X < 0.66f ? HAlign_Center : HAlign_Right);
            OverlayS->SetVerticalAlignment(SlotSpec.Alignment.Y < 0.33f ? VAlign_Top : SlotSpec.Alignment.Y < 0.66f ? VAlign_Center : VAlign_Bottom);
        }
        return true;
    }

    // Unknown slot type -- not an error, just skip (content widget slots may not need properties)
    UE_LOG(LogTemp, Warning, TEXT("[WidgetBuilder] %s: Unrecognized slot type '%s', skipping slot properties"),
        *Path, *Slot->GetClass()->GetName());
    return true;
}
