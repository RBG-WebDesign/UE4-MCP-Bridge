#include "WidgetChildAttachment.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/Widget.h"

UPanelSlot* FWidgetChildAttachment::AttachChild(UWidget* Parent, UWidget* Child, const FString& Path, FString& OutError)
{
    if (!Parent || !Child)
    {
        OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Null parent or child in attachment"), *Path);
        return nullptr;
    }

    // Both panel widgets (CanvasPanel, VBox, HBox, Overlay) and content widgets
    // (Button, Border, SizeBox) inherit from UPanelWidget in UE4.
    // The validator enforces the single-child rule for content widgets.
    UPanelWidget* Panel = Cast<UPanelWidget>(Parent);
    if (Panel)
    {
        return AttachToPanel(Panel, Child, Path, OutError);
    }

    OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Parent '%s' cannot accept children"),
        *Path, *Parent->GetName());
    return nullptr;
}

UPanelSlot* FWidgetChildAttachment::AttachToPanel(UPanelWidget* Panel, UWidget* Child, const FString& Path, FString& OutError)
{
    Panel->AddChild(Child);

    if (!Child->Slot)
    {
        OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Slot is null after attaching '%s' to panel '%s'"),
            *Path, *Child->GetName(), *Panel->GetName());
        return nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] %s: Attached '%s' to panel '%s'"),
        *Path, *Child->GetName(), *Panel->GetName());

    return Child->Slot;
}
