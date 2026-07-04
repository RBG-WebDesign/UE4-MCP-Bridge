#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"

class UWidget;
class UProgressBar;
class USlider;
class UCheckBox;
class UEditableTextBox;
class URichTextBlock;
class UScaleBox;

class FWidgetPropertyApplier
{
public:
    /**
     * Apply widget-level properties from JSON values to the constructed widget.
     * Handles common properties (visibility, renderOpacity, isEnabled) and
     * type-specific properties (TextBlock text/justification/color).
     */
    static bool ApplyProperties(
        UWidget* Widget,
        const FString& TypeName,
        const TMap<FString, TSharedPtr<FJsonValue>>& Properties,
        const FString& Path,
        FString& OutError);

private:
    static bool ApplyProgressBarProperties(UProgressBar* Widget, const TMap<FString, TSharedPtr<FJsonValue>>& Props, const FString& Path, FString& OutError);
    static bool ApplySliderProperties(USlider* Widget, const TMap<FString, TSharedPtr<FJsonValue>>& Props, const FString& Path, FString& OutError);
    static bool ApplyCheckBoxProperties(UCheckBox* Widget, const TMap<FString, TSharedPtr<FJsonValue>>& Props, const FString& Path, FString& OutError);
    static bool ApplyEditableTextBoxProperties(UEditableTextBox* Widget, const TMap<FString, TSharedPtr<FJsonValue>>& Props, const FString& Path, FString& OutError);
    static bool ApplyRichTextBlockProperties(URichTextBlock* Widget, const TMap<FString, TSharedPtr<FJsonValue>>& Props, const FString& Path, FString& OutError);
    static bool ApplyScaleBoxProperties(UScaleBox* Widget, const TMap<FString, TSharedPtr<FJsonValue>>& Props, const FString& Path, FString& OutError);
};
