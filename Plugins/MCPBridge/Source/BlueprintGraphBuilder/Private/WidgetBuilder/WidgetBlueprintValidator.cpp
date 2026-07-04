#include "WidgetBlueprintValidator.h"
#include "WidgetClassRegistry.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

static bool ValidateWidgetPropertyValues(
    const FWidgetNodeSpec& Node,
    const FString& Path,
    FString& OutError)
{
    const FString& T = Node.Type;
    const auto& Props = Node.Properties;

    // ProgressBar
    if (T == TEXT("ProgressBar"))
    {
        const TSharedPtr<FJsonValue>* PctVal = Props.Find(TEXT("percent"));
        if (PctVal && (*PctVal)->Type == EJson::Number)
        {
            double V = (*PctVal)->AsNumber();
            if (V < 0.0 || V > 1.0)
            {
                OutError = FString::Printf(TEXT("[WidgetBuilder] %s: 'percent' %.2f is outside [0.0, 1.0]"), *Path, V);
                return false;
            }
        }
        const TSharedPtr<FJsonValue>* FillTypeVal = Props.Find(TEXT("barFillType"));
        if (FillTypeVal && (*FillTypeVal)->Type == EJson::String)
        {
            static const TSet<FString> ValidFillTypes = {
                TEXT("LeftToRight"), TEXT("RightToLeft"), TEXT("FillFromCenter"),
                TEXT("TopToBottom"), TEXT("BottomToTop")
            };
            if (!ValidFillTypes.Contains((*FillTypeVal)->AsString()))
            {
                OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Unknown barFillType '%s'"), *Path, *(*FillTypeVal)->AsString());
                return false;
            }
        }
        const TSharedPtr<FJsonValue>* ColorVal = Props.Find(TEXT("fillColorAndOpacity"));
        if (ColorVal && (*ColorVal)->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject>& Obj = (*ColorVal)->AsObject();
            for (const FString& Ch : { FString(TEXT("r")), FString(TEXT("g")), FString(TEXT("b")), FString(TEXT("a")) })
            {
                double ChVal;
                if (!Obj->TryGetNumberField(Ch, ChVal))
                {
                    OutError = FString::Printf(TEXT("[WidgetBuilder] %s: fillColorAndOpacity missing field '%s'"), *Path, *Ch);
                    return false;
                }
                if (ChVal < 0.0 || ChVal > 1.0)
                {
                    OutError = FString::Printf(TEXT("[WidgetBuilder] %s: fillColorAndOpacity '%s' %.2f outside [0,1]"), *Path, *Ch, ChVal);
                    return false;
                }
            }
        }
    }

    // Slider
    if (T == TEXT("Slider"))
    {
        double MinV = 0.0, MaxV = 1.0;
        bool bHasMin = false, bHasMax = false;
        const TSharedPtr<FJsonValue>* MinVal = Props.Find(TEXT("minValue"));
        const TSharedPtr<FJsonValue>* MaxVal = Props.Find(TEXT("maxValue"));
        if (MinVal && (*MinVal)->Type == EJson::Number) { MinV = (*MinVal)->AsNumber(); bHasMin = true; }
        if (MaxVal && (*MaxVal)->Type == EJson::Number) { MaxV = (*MaxVal)->AsNumber(); bHasMax = true; }

        if (bHasMin && bHasMax && MaxV < MinV)
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Slider maxValue %.2f < minValue %.2f"), *Path, MaxV, MinV);
            return false;
        }

        const TSharedPtr<FJsonValue>* StepVal = Props.Find(TEXT("stepSize"));
        if (StepVal && (*StepVal)->Type == EJson::Number && (*StepVal)->AsNumber() <= 0.0)
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Slider stepSize must be > 0"), *Path);
            return false;
        }

        const TSharedPtr<FJsonValue>* ValProp = Props.Find(TEXT("value"));
        if (ValProp && (*ValProp)->Type == EJson::Number)
        {
            double V = (*ValProp)->AsNumber();
            // Use explicit defaults if one bound missing
            double EffMin = bHasMin ? MinV : 0.0;
            double EffMax = bHasMax ? MaxV : 1.0;
            if (V < EffMin || V > EffMax)
            {
                OutError = FString::Printf(TEXT("[WidgetBuilder] %s: 'value' %.2f is outside [minValue=%.2f, maxValue=%.2f]"),
                    *Path, V, EffMin, EffMax);
                return false;
            }
        }

        const TSharedPtr<FJsonValue>* OrientVal = Props.Find(TEXT("orientation"));
        if (OrientVal && (*OrientVal)->Type == EJson::String)
        {
            const FString& S = (*OrientVal)->AsString();
            if (S != TEXT("Horizontal") && S != TEXT("Vertical"))
            {
                OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Unknown orientation '%s'"), *Path, *S);
                return false;
            }
        }
    }

    // Justification (TextBlock, EditableTextBox, RichTextBlock share same valid values)
    static const TSet<FString> ValidJustification = { TEXT("Left"), TEXT("Center"), TEXT("Right") };
    if (T == TEXT("TextBlock") || T == TEXT("EditableTextBox") || T == TEXT("RichTextBlock"))
    {
        const TSharedPtr<FJsonValue>* JustVal = Props.Find(TEXT("justification"));
        if (JustVal && (*JustVal)->Type == EJson::String && !ValidJustification.Contains((*JustVal)->AsString()))
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Unknown justification '%s'"), *Path, *(*JustVal)->AsString());
            return false;
        }
    }

    // ScaleBox
    if (T == TEXT("ScaleBox"))
    {
        static const TSet<FString> ValidStretch = {
            TEXT("None"), TEXT("Fill"), TEXT("ScaleToFit"), TEXT("ScaleToFitX"),
            TEXT("ScaleToFitY"), TEXT("ScaleToFill"), TEXT("ScaleBySafeZone"), TEXT("UserSpecified")
        };
        static const TSet<FString> ValidStretchDir = { TEXT("Both"), TEXT("DownOnly"), TEXT("UpOnly") };

        const TSharedPtr<FJsonValue>* StretchVal = Props.Find(TEXT("stretch"));
        if (StretchVal && (*StretchVal)->Type == EJson::String && !ValidStretch.Contains((*StretchVal)->AsString()))
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Unknown stretch '%s'"), *Path, *(*StretchVal)->AsString());
            return false;
        }
        const TSharedPtr<FJsonValue>* DirVal = Props.Find(TEXT("stretchDirection"));
        if (DirVal && (*DirVal)->Type == EJson::String && !ValidStretchDir.Contains((*DirVal)->AsString()))
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Unknown stretchDirection '%s'"), *Path, *(*DirVal)->AsString());
            return false;
        }
    }

    // Visibility (all widgets)
    const TSharedPtr<FJsonValue>* VisVal = Props.Find(TEXT("visibility"));
    if (VisVal && (*VisVal)->Type == EJson::String)
    {
        static const TSet<FString> ValidVis = {
            TEXT("Visible"), TEXT("Hidden"), TEXT("Collapsed"),
            TEXT("HitTestInvisible"), TEXT("SelfHitTestInvisible")
        };
        if (!ValidVis.Contains((*VisVal)->AsString()))
        {
            OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Unknown visibility '%s'"), *Path, *(*VisVal)->AsString());
            return false;
        }
    }

    return true;
}

bool FWidgetBlueprintValidator::Validate(
	const FWidgetBlueprintSpec& Spec,
	const FWidgetClassRegistry& Registry,
	FString& OutError)
{
	// Root must exist
	if (Spec.Root.Type.IsEmpty())
	{
		OutError = TEXT("[WidgetBuilder] Root widget has no type");
		return false;
	}

	// Root must be a panel type
	if (!Registry.IsSupportedType(Spec.Root.Type))
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Root type '%s' is not supported. Supported: %s"),
			*Spec.Root.Type, *FString::Join(Registry.GetSupportedTypes(), TEXT(", ")));
		return false;
	}

	if (Registry.GetCategory(Spec.Root.Type) != EWidgetCategory::Panel)
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] Root widget must be a Panel type, got '%s'"), *Spec.Root.Type);
		return false;
	}

	TSet<FString> SeenNames;
	return ValidateNode(Spec.Root, Registry, SeenNames, TEXT(""), OutError);
}

bool FWidgetBlueprintValidator::ValidateNode(
	const FWidgetNodeSpec& Node,
	const FWidgetClassRegistry& Registry,
	TSet<FString>& SeenNames,
	const FString& Path,
	FString& OutError)
{
	FString NodePath = Path.IsEmpty() ? Node.Name : FString::Printf(TEXT("%s.%s"), *Path, *Node.Name);

	// Name must not be empty
	if (Node.Name.IsEmpty())
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Widget name is empty"), *NodePath);
		return false;
	}

	// Name must be unique
	if (SeenNames.Contains(Node.Name))
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Duplicate widget name '%s'"), *NodePath, *Node.Name);
		return false;
	}
	SeenNames.Add(Node.Name);

	// Type must be supported
	if (!Registry.IsSupportedType(Node.Type))
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Unsupported widget type '%s'"), *NodePath, *Node.Type);
		return false;
	}

	// Validate children based on category
	EWidgetCategory Category = Registry.GetCategory(Node.Type);
	switch (Category)
	{
	case EWidgetCategory::Leaf:
		if (Node.Children.Num() > 0)
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Leaf widget '%s' cannot have children"), *NodePath, *Node.Type);
			return false;
		}
		break;
	case EWidgetCategory::Content:
		if (Node.Children.Num() > 1)
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Content widget '%s' can have at most 1 child, got %d"),
				*NodePath, *Node.Type, Node.Children.Num());
			return false;
		}
		break;
	case EWidgetCategory::Panel:
		// 0..N children, no restriction
		break;
	}

	// Validate properties against registry
	const TArray<FWidgetPropertyDescriptor>* SupportedProps = Registry.GetSupportedProperties(Node.Type);
	if (SupportedProps)
	{
		for (const auto& Pair : Node.Properties)
		{
			bool bFound = false;
			for (const FWidgetPropertyDescriptor& Desc : *SupportedProps)
			{
				if (Desc.Name == Pair.Key)
				{
					bFound = true;
					// Check JSON type matches expected
					if (Pair.Value.IsValid() && Pair.Value->Type != Desc.ExpectedType)
					{
						OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Property '%s' has wrong type"), *NodePath, *Pair.Key);
						return false;
					}
					break;
				}
			}
			if (!bFound)
			{
				OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Unsupported property '%s' on %s"), *NodePath, *Pair.Key, *Node.Type);
				return false;
			}
		}
	}
	else if (Node.Properties.Num() > 0)
	{
		// No properties defined for this type but some were provided
		TArray<FString> Keys;
		Node.Properties.GetKeys(Keys);
		OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Unsupported property '%s' on %s"), *NodePath, *Keys[0], *Node.Type);
		return false;
	}

	if (!ValidateWidgetPropertyValues(Node, NodePath, OutError))
	{
		return false;
	}

	// Validate slot field values
	if (Node.bHasSlot)
	{
		const FWidgetSlotSpec& Slot = Node.Slot;

		if (Slot.bHasRow && Slot.Row < 0)
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] %s.slot: 'row' must be >= 0"), *NodePath);
			return false;
		}
		if (Slot.bHasColumn && Slot.Column < 0)
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] %s.slot: 'column' must be >= 0"), *NodePath);
			return false;
		}
		if (Slot.bHasRowSpan && Slot.RowSpan < 1)
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] %s.slot: 'rowSpan' must be >= 1"), *NodePath);
			return false;
		}
		if (Slot.bHasColumnSpan && Slot.ColumnSpan < 1)
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] %s.slot: 'columnSpan' must be >= 1"), *NodePath);
			return false;
		}

		static const TSet<FString> ValidHAlign = { TEXT("Left"), TEXT("Center"), TEXT("Right"), TEXT("Fill") };
		static const TSet<FString> ValidVAlign = { TEXT("Top"), TEXT("Center"), TEXT("Bottom"), TEXT("Fill") };

		if (Slot.bHasHorizontalAlignment && !ValidHAlign.Contains(Slot.HorizontalAlignment))
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] %s.slot: Unknown horizontalAlignment '%s'"), *NodePath, *Slot.HorizontalAlignment);
			return false;
		}
		if (Slot.bHasVerticalAlignment && !ValidVAlign.Contains(Slot.VerticalAlignment))
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] %s.slot: Unknown verticalAlignment '%s'"), *NodePath, *Slot.VerticalAlignment);
			return false;
		}
	}

	// Recurse into children
	for (const FWidgetNodeSpec& Child : Node.Children)
	{
		if (!ValidateNode(Child, Registry, SeenNames, NodePath, OutError))
		{
			return false;
		}
	}

	return true;
}
