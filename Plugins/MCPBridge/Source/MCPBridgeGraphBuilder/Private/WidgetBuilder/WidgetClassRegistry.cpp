#include "WidgetClassRegistry.h"
#include "Components/CanvasPanel.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Spacer.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/GridPanel.h"
#include "Components/WrapBox.h"
#include "Components/ScaleBox.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/RichTextBlock.h"

void FWidgetClassRegistry::RegisterPanel(const FString& TypeName, TSubclassOf<UWidget> WidgetClass)
{
	FWidgetTypeInfo Info;
	Info.WidgetClass = WidgetClass;
	Info.Category = EWidgetCategory::Panel;
	AddCommonProperties(Info);
	TypeRegistry.Add(TypeName, MoveTemp(Info));
}

void FWidgetClassRegistry::RegisterContent(const FString& TypeName, TSubclassOf<UWidget> WidgetClass,
	TArray<FWidgetPropertyDescriptor> TypeSpecificProps)
{
	FWidgetTypeInfo Info;
	Info.WidgetClass = WidgetClass;
	Info.Category = EWidgetCategory::Content;
	AddCommonProperties(Info);
	for (auto& Prop : TypeSpecificProps) { Info.SupportedProperties.Add(MoveTemp(Prop)); }
	TypeRegistry.Add(TypeName, MoveTemp(Info));
}

void FWidgetClassRegistry::RegisterLeaf(const FString& TypeName, TSubclassOf<UWidget> WidgetClass,
	TArray<FWidgetPropertyDescriptor> TypeSpecificProps)
{
	FWidgetTypeInfo Info;
	Info.WidgetClass = WidgetClass;
	Info.Category = EWidgetCategory::Leaf;
	AddCommonProperties(Info);
	for (auto& Prop : TypeSpecificProps) { Info.SupportedProperties.Add(MoveTemp(Prop)); }
	TypeRegistry.Add(TypeName, MoveTemp(Info));
}

FWidgetClassRegistry::FWidgetClassRegistry()
{
	RegisterTypes();
}

void FWidgetClassRegistry::AddCommonProperties(FWidgetTypeInfo& Info)
{
	Info.SupportedProperties.Add({TEXT("visibility"), EJson::String});
	Info.SupportedProperties.Add({TEXT("renderOpacity"), EJson::Number});
	Info.SupportedProperties.Add({TEXT("isEnabled"), EJson::Boolean});
}

void FWidgetClassRegistry::RegisterTypes()
{
	// Pass 1: CanvasPanel
	FWidgetTypeInfo CanvasPanelInfo;
	CanvasPanelInfo.WidgetClass = UCanvasPanel::StaticClass();
	CanvasPanelInfo.Category = EWidgetCategory::Panel;
	AddCommonProperties(CanvasPanelInfo);
	TypeRegistry.Add(TEXT("CanvasPanel"), MoveTemp(CanvasPanelInfo));

	// Pass 2: Leaf widgets
	FWidgetTypeInfo TextBlockInfo;
	TextBlockInfo.WidgetClass = UTextBlock::StaticClass();
	TextBlockInfo.Category = EWidgetCategory::Leaf;
	AddCommonProperties(TextBlockInfo);
	TextBlockInfo.SupportedProperties.Add({TEXT("text"), EJson::String});
	TextBlockInfo.SupportedProperties.Add({TEXT("justification"), EJson::String});
	TextBlockInfo.SupportedProperties.Add({TEXT("color"), EJson::Object});
	TypeRegistry.Add(TEXT("TextBlock"), MoveTemp(TextBlockInfo));

	FWidgetTypeInfo ImageInfo;
	ImageInfo.WidgetClass = UImage::StaticClass();
	ImageInfo.Category = EWidgetCategory::Leaf;
	AddCommonProperties(ImageInfo);
	TypeRegistry.Add(TEXT("Image"), MoveTemp(ImageInfo));

	FWidgetTypeInfo SpacerInfo;
	SpacerInfo.WidgetClass = USpacer::StaticClass();
	SpacerInfo.Category = EWidgetCategory::Leaf;
	AddCommonProperties(SpacerInfo);
	TypeRegistry.Add(TEXT("Spacer"), MoveTemp(SpacerInfo));

	// Pass 3: Layout panels
	FWidgetTypeInfo VerticalBoxInfo;
	VerticalBoxInfo.WidgetClass = UVerticalBox::StaticClass();
	VerticalBoxInfo.Category = EWidgetCategory::Panel;
	AddCommonProperties(VerticalBoxInfo);
	TypeRegistry.Add(TEXT("VerticalBox"), MoveTemp(VerticalBoxInfo));

	FWidgetTypeInfo HorizontalBoxInfo;
	HorizontalBoxInfo.WidgetClass = UHorizontalBox::StaticClass();
	HorizontalBoxInfo.Category = EWidgetCategory::Panel;
	AddCommonProperties(HorizontalBoxInfo);
	TypeRegistry.Add(TEXT("HorizontalBox"), MoveTemp(HorizontalBoxInfo));

	FWidgetTypeInfo OverlayInfo;
	OverlayInfo.WidgetClass = UOverlay::StaticClass();
	OverlayInfo.Category = EWidgetCategory::Panel;
	AddCommonProperties(OverlayInfo);
	TypeRegistry.Add(TEXT("Overlay"), MoveTemp(OverlayInfo));

	// Pass 4: Content widgets (single child)
	FWidgetTypeInfo ButtonInfo;
	ButtonInfo.WidgetClass = UButton::StaticClass();
	ButtonInfo.Category = EWidgetCategory::Content;
	AddCommonProperties(ButtonInfo);
	TypeRegistry.Add(TEXT("Button"), MoveTemp(ButtonInfo));

	FWidgetTypeInfo BorderInfo;
	BorderInfo.WidgetClass = UBorder::StaticClass();
	BorderInfo.Category = EWidgetCategory::Content;
	AddCommonProperties(BorderInfo);
	TypeRegistry.Add(TEXT("Border"), MoveTemp(BorderInfo));

	FWidgetTypeInfo SizeBoxInfo;
	SizeBoxInfo.WidgetClass = USizeBox::StaticClass();
	SizeBoxInfo.Category = EWidgetCategory::Content;
	AddCommonProperties(SizeBoxInfo);
	TypeRegistry.Add(TEXT("SizeBox"), MoveTemp(SizeBoxInfo));

	// Pass 8: new panels
	RegisterPanel(TEXT("ScrollBox"), UScrollBox::StaticClass());
	RegisterPanel(TEXT("GridPanel"), UGridPanel::StaticClass());
	RegisterPanel(TEXT("WrapBox"),   UWrapBox::StaticClass());

	// Pass 8: new content
	RegisterContent(TEXT("ScaleBox"), UScaleBox::StaticClass(),
		{ {TEXT("stretch"), EJson::String}, {TEXT("stretchDirection"), EJson::String},
		  {TEXT("userSpecifiedScale"), EJson::Number} });

	// Pass 8: new leaf
	RegisterLeaf(TEXT("ProgressBar"), UProgressBar::StaticClass(),
		{ {TEXT("percent"), EJson::Number}, {TEXT("fillColorAndOpacity"), EJson::Object},
		  {TEXT("barFillType"), EJson::String}, {TEXT("isMarquee"), EJson::Boolean} });

	RegisterLeaf(TEXT("Slider"), USlider::StaticClass(),
		{ {TEXT("value"), EJson::Number}, {TEXT("minValue"), EJson::Number},
		  {TEXT("maxValue"), EJson::Number}, {TEXT("stepSize"), EJson::Number},
		  {TEXT("orientation"), EJson::String} });

	RegisterLeaf(TEXT("CheckBox"), UCheckBox::StaticClass(),
		{ {TEXT("isChecked"), EJson::Boolean} });

	RegisterLeaf(TEXT("EditableTextBox"), UEditableTextBox::StaticClass(),
		{ {TEXT("text"), EJson::String}, {TEXT("hintText"), EJson::String},
		  {TEXT("isReadOnly"), EJson::Boolean}, {TEXT("justification"), EJson::String} });

	RegisterLeaf(TEXT("RichTextBlock"), URichTextBlock::StaticClass(),
		{ {TEXT("text"), EJson::String}, {TEXT("justification"), EJson::String},
		  {TEXT("autoWrapText"), EJson::Boolean} });
}

TSubclassOf<UWidget> FWidgetClassRegistry::ResolveWidgetClass(const FString& TypeName) const
{
	const FWidgetTypeInfo* Info = TypeRegistry.Find(TypeName);
	return Info ? Info->WidgetClass : nullptr;
}

bool FWidgetClassRegistry::IsSupportedType(const FString& TypeName) const
{
	return TypeRegistry.Contains(TypeName);
}

EWidgetCategory FWidgetClassRegistry::GetCategory(const FString& TypeName) const
{
	const FWidgetTypeInfo* Info = TypeRegistry.Find(TypeName);
	return Info ? Info->Category : EWidgetCategory::Leaf;
}

TArray<FString> FWidgetClassRegistry::GetSupportedTypes() const
{
	TArray<FString> Types;
	TypeRegistry.GetKeys(Types);
	return Types;
}

const TArray<FWidgetPropertyDescriptor>* FWidgetClassRegistry::GetSupportedProperties(const FString& TypeName) const
{
	const FWidgetTypeInfo* Info = TypeRegistry.Find(TypeName);
	return Info ? &Info->SupportedProperties : nullptr;
}
