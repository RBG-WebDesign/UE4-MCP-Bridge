#include "WidgetPropertyApplier.h"
#include "Dom/JsonObject.h"
#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/RichTextBlock.h"
#include "Components/ScaleBox.h"

static ESlateVisibility ParseVisibility(const FString& Value)
{
    if (Value == TEXT("Collapsed")) return ESlateVisibility::Collapsed;
    if (Value == TEXT("Hidden")) return ESlateVisibility::Hidden;
    if (Value == TEXT("HitTestInvisible")) return ESlateVisibility::HitTestInvisible;
    if (Value == TEXT("SelfHitTestInvisible")) return ESlateVisibility::SelfHitTestInvisible;
    return ESlateVisibility::Visible;
}

static ETextJustify::Type ParseJustification(const FString& Value)
{
    if (Value == TEXT("Center")) return ETextJustify::Center;
    if (Value == TEXT("Right")) return ETextJustify::Right;
    return ETextJustify::Left;
}

static EProgressBarFillType::Type ParseBarFillType(const FString& Value)
{
    if (Value == TEXT("RightToLeft"))    return EProgressBarFillType::RightToLeft;
    if (Value == TEXT("FillFromCenter")) return EProgressBarFillType::FillFromCenter;
    if (Value == TEXT("TopToBottom"))    return EProgressBarFillType::TopToBottom;
    if (Value == TEXT("BottomToTop"))    return EProgressBarFillType::BottomToTop;
    return EProgressBarFillType::LeftToRight;
}

static EOrientation ParseSliderOrientation(const FString& Value)
{
    if (Value == TEXT("Vertical")) return EOrientation::Orient_Vertical;
    return EOrientation::Orient_Horizontal;
}

static EStretch::Type ParseStretch(const FString& Value)
{
    if (Value == TEXT("Fill"))            return EStretch::Fill;
    if (Value == TEXT("ScaleToFit"))      return EStretch::ScaleToFit;
    if (Value == TEXT("ScaleToFitX"))     return EStretch::ScaleToFitX;
    if (Value == TEXT("ScaleToFitY"))     return EStretch::ScaleToFitY;
    if (Value == TEXT("ScaleToFill"))     return EStretch::ScaleToFill;
    if (Value == TEXT("ScaleBySafeZone")) return EStretch::ScaleBySafeZone;
    if (Value == TEXT("UserSpecified"))   return EStretch::UserSpecified;
    return EStretch::None;
}

static EStretchDirection::Type ParseStretchDirection(const FString& Value)
{
    if (Value == TEXT("DownOnly")) return EStretchDirection::DownOnly;
    if (Value == TEXT("UpOnly"))   return EStretchDirection::UpOnly;
    return EStretchDirection::Both;
}

bool FWidgetPropertyApplier::ApplyProperties(
    UWidget* Widget,
    const FString& TypeName,
    const TMap<FString, TSharedPtr<FJsonValue>>& Properties,
    const FString& Path,
    FString& OutError)
{
    if (!Widget || Properties.Num() == 0)
    {
        return true;
    }

    // Common widget properties (apply to all widget types)
    const TSharedPtr<FJsonValue>* VisVal = Properties.Find(TEXT("visibility"));
    if (VisVal && (*VisVal)->Type == EJson::String)
    {
        Widget->SetVisibility(ParseVisibility((*VisVal)->AsString()));
        UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] %s: visibility = %s"), *Path, *(*VisVal)->AsString());
    }

    const TSharedPtr<FJsonValue>* OpacityVal = Properties.Find(TEXT("renderOpacity"));
    if (OpacityVal && (*OpacityVal)->Type == EJson::Number)
    {
        Widget->SetRenderOpacity(static_cast<float>((*OpacityVal)->AsNumber()));
        UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] %s: renderOpacity = %f"), *Path, (*OpacityVal)->AsNumber());
    }

    const TSharedPtr<FJsonValue>* EnabledVal = Properties.Find(TEXT("isEnabled"));
    if (EnabledVal && (*EnabledVal)->Type == EJson::Boolean)
    {
        Widget->SetIsEnabled((*EnabledVal)->AsBool());
    }

    // TextBlock-specific properties
    if (TypeName == TEXT("TextBlock"))
    {
        UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
        if (TextBlock)
        {
            const TSharedPtr<FJsonValue>* TextVal = Properties.Find(TEXT("text"));
            if (TextVal && (*TextVal)->Type == EJson::String)
            {
                TextBlock->SetText(FText::FromString((*TextVal)->AsString()));
                UE_LOG(LogTemp, Log, TEXT("[WidgetBuilder] %s: text = '%s'"), *Path, *(*TextVal)->AsString());
            }

            const TSharedPtr<FJsonValue>* JustVal = Properties.Find(TEXT("justification"));
            if (JustVal && (*JustVal)->Type == EJson::String)
            {
                TextBlock->SetJustification(ParseJustification((*JustVal)->AsString()));
            }

            const TSharedPtr<FJsonValue>* ColorVal = Properties.Find(TEXT("color"));
            if (ColorVal && (*ColorVal)->Type == EJson::Object)
            {
                const TSharedPtr<FJsonObject>& ColorObj = (*ColorVal)->AsObject();
                FLinearColor Color(
                    static_cast<float>(ColorObj->GetNumberField(TEXT("r"))),
                    static_cast<float>(ColorObj->GetNumberField(TEXT("g"))),
                    static_cast<float>(ColorObj->GetNumberField(TEXT("b"))),
                    ColorObj->HasField(TEXT("a")) ? static_cast<float>(ColorObj->GetNumberField(TEXT("a"))) : 1.0f
                );
                TextBlock->SetColorAndOpacity(FSlateColor(Color));
            }
        }
    }
    else if (TypeName == TEXT("ProgressBar"))
    {
        UProgressBar* PB = Cast<UProgressBar>(Widget);
        if (PB) return ApplyProgressBarProperties(PB, Properties, Path, OutError);
    }
    else if (TypeName == TEXT("Slider"))
    {
        USlider* Sl = Cast<USlider>(Widget);
        if (Sl) return ApplySliderProperties(Sl, Properties, Path, OutError);
    }
    else if (TypeName == TEXT("CheckBox"))
    {
        UCheckBox* CB = Cast<UCheckBox>(Widget);
        if (CB) return ApplyCheckBoxProperties(CB, Properties, Path, OutError);
    }
    else if (TypeName == TEXT("EditableTextBox"))
    {
        UEditableTextBox* ETB = Cast<UEditableTextBox>(Widget);
        if (ETB) return ApplyEditableTextBoxProperties(ETB, Properties, Path, OutError);
    }
    else if (TypeName == TEXT("RichTextBlock"))
    {
        URichTextBlock* RTB = Cast<URichTextBlock>(Widget);
        if (RTB) return ApplyRichTextBlockProperties(RTB, Properties, Path, OutError);
    }
    else if (TypeName == TEXT("ScaleBox"))
    {
        UScaleBox* SB = Cast<UScaleBox>(Widget);
        if (SB) return ApplyScaleBoxProperties(SB, Properties, Path, OutError);
    }

    return true;
}

bool FWidgetPropertyApplier::ApplyProgressBarProperties(
    UProgressBar* Widget,
    const TMap<FString, TSharedPtr<FJsonValue>>& Props,
    const FString& Path,
    FString& OutError)
{
    const TSharedPtr<FJsonValue>* PctVal = Props.Find(TEXT("percent"));
    if (PctVal && (*PctVal)->Type == EJson::Number)
    {
        Widget->SetPercent(static_cast<float>((*PctVal)->AsNumber()));
    }

    const TSharedPtr<FJsonValue>* FillTypeVal = Props.Find(TEXT("barFillType"));
    if (FillTypeVal && (*FillTypeVal)->Type == EJson::String)
    {
        Widget->BarFillType = ParseBarFillType((*FillTypeVal)->AsString());
    }

    const TSharedPtr<FJsonValue>* MarqueeVal = Props.Find(TEXT("isMarquee"));
    if (MarqueeVal && (*MarqueeVal)->Type == EJson::Boolean)
    {
        Widget->bIsMarquee = (*MarqueeVal)->AsBool();
    }

    const TSharedPtr<FJsonValue>* ColorVal = Props.Find(TEXT("fillColorAndOpacity"));
    if (ColorVal && (*ColorVal)->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject>& Obj = (*ColorVal)->AsObject();
        FLinearColor Color(
            static_cast<float>(Obj->GetNumberField(TEXT("r"))),
            static_cast<float>(Obj->GetNumberField(TEXT("g"))),
            static_cast<float>(Obj->GetNumberField(TEXT("b"))),
            Obj->HasField(TEXT("a")) ? static_cast<float>(Obj->GetNumberField(TEXT("a"))) : 1.0f
        );
        Widget->SetFillColorAndOpacity(Color);
    }

    return true;
}

bool FWidgetPropertyApplier::ApplySliderProperties(
    USlider* Widget,
    const TMap<FString, TSharedPtr<FJsonValue>>& Props,
    const FString& Path,
    FString& OutError)
{
    const TSharedPtr<FJsonValue>* ValProp = Props.Find(TEXT("value"));
    if (ValProp && (*ValProp)->Type == EJson::Number)
    {
        Widget->SetValue(static_cast<float>((*ValProp)->AsNumber()));
    }

    const TSharedPtr<FJsonValue>* MinVal = Props.Find(TEXT("minValue"));
    if (MinVal && (*MinVal)->Type == EJson::Number)
    {
        Widget->MinValue = static_cast<float>((*MinVal)->AsNumber());
    }

    const TSharedPtr<FJsonValue>* MaxVal = Props.Find(TEXT("maxValue"));
    if (MaxVal && (*MaxVal)->Type == EJson::Number)
    {
        Widget->MaxValue = static_cast<float>((*MaxVal)->AsNumber());
    }

    const TSharedPtr<FJsonValue>* StepVal = Props.Find(TEXT("stepSize"));
    if (StepVal && (*StepVal)->Type == EJson::Number)
    {
        Widget->StepSize = static_cast<float>((*StepVal)->AsNumber());
    }

    const TSharedPtr<FJsonValue>* OrientVal = Props.Find(TEXT("orientation"));
    if (OrientVal && (*OrientVal)->Type == EJson::String)
    {
        Widget->Orientation = ParseSliderOrientation((*OrientVal)->AsString());
    }

    return true;
}

bool FWidgetPropertyApplier::ApplyCheckBoxProperties(
    UCheckBox* Widget,
    const TMap<FString, TSharedPtr<FJsonValue>>& Props,
    const FString& Path,
    FString& OutError)
{
    const TSharedPtr<FJsonValue>* CheckedVal = Props.Find(TEXT("isChecked"));
    if (CheckedVal && (*CheckedVal)->Type == EJson::Boolean)
    {
        Widget->SetIsChecked((*CheckedVal)->AsBool());
    }
    return true;
}

bool FWidgetPropertyApplier::ApplyEditableTextBoxProperties(
    UEditableTextBox* Widget,
    const TMap<FString, TSharedPtr<FJsonValue>>& Props,
    const FString& Path,
    FString& OutError)
{
    const TSharedPtr<FJsonValue>* TextVal = Props.Find(TEXT("text"));
    if (TextVal && (*TextVal)->Type == EJson::String)
    {
        Widget->SetText(FText::FromString((*TextVal)->AsString()));
    }

    const TSharedPtr<FJsonValue>* HintVal = Props.Find(TEXT("hintText"));
    if (HintVal && (*HintVal)->Type == EJson::String)
    {
        Widget->SetHintText(FText::FromString((*HintVal)->AsString()));
    }

    const TSharedPtr<FJsonValue>* ReadOnlyVal = Props.Find(TEXT("isReadOnly"));
    if (ReadOnlyVal && (*ReadOnlyVal)->Type == EJson::Boolean)
    {
        Widget->SetIsReadOnly((*ReadOnlyVal)->AsBool());
    }

    const TSharedPtr<FJsonValue>* JustVal = Props.Find(TEXT("justification"));
    if (JustVal && (*JustVal)->Type == EJson::String)
    {
        Widget->SetJustification(ParseJustification((*JustVal)->AsString()));
    }

    return true;
}

bool FWidgetPropertyApplier::ApplyRichTextBlockProperties(
    URichTextBlock* Widget,
    const TMap<FString, TSharedPtr<FJsonValue>>& Props,
    const FString& Path,
    FString& OutError)
{
    const TSharedPtr<FJsonValue>* TextVal = Props.Find(TEXT("text"));
    if (TextVal && (*TextVal)->Type == EJson::String)
    {
        Widget->SetText(FText::FromString((*TextVal)->AsString()));
    }

    const TSharedPtr<FJsonValue>* JustVal = Props.Find(TEXT("justification"));
    if (JustVal && (*JustVal)->Type == EJson::String)
    {
        Widget->SetJustification(ParseJustification((*JustVal)->AsString()));
    }

    const TSharedPtr<FJsonValue>* AutoWrapVal = Props.Find(TEXT("autoWrapText"));
    if (AutoWrapVal && (*AutoWrapVal)->Type == EJson::Boolean)
    {
        Widget->SetAutoWrapText((*AutoWrapVal)->AsBool());
    }

    return true;
}

bool FWidgetPropertyApplier::ApplyScaleBoxProperties(
    UScaleBox* Widget,
    const TMap<FString, TSharedPtr<FJsonValue>>& Props,
    const FString& Path,
    FString& OutError)
{
    const TSharedPtr<FJsonValue>* StretchVal = Props.Find(TEXT("stretch"));
    if (StretchVal && (*StretchVal)->Type == EJson::String)
    {
        Widget->SetStretch(ParseStretch((*StretchVal)->AsString()));
    }

    const TSharedPtr<FJsonValue>* DirVal = Props.Find(TEXT("stretchDirection"));
    if (DirVal && (*DirVal)->Type == EJson::String)
    {
        Widget->SetStretchDirection(ParseStretchDirection((*DirVal)->AsString()));
    }

    const TSharedPtr<FJsonValue>* ScaleVal = Props.Find(TEXT("userSpecifiedScale"));
    if (ScaleVal && (*ScaleVal)->Type == EJson::Number)
    {
        Widget->SetUserSpecifiedScale(static_cast<float>((*ScaleVal)->AsNumber()));
    }

    return true;
}
