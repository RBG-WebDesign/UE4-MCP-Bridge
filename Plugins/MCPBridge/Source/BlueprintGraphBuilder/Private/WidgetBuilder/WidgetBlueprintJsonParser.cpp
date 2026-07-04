#include "WidgetBlueprintJsonParser.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Valid keys at node level
static const TSet<FString> ValidNodeKeys = { TEXT("type"), TEXT("name"), TEXT("properties"), TEXT("slot"), TEXT("children") };
// Valid keys in slot
static const TSet<FString> ValidSlotKeys = {
	TEXT("position"), TEXT("size"), TEXT("alignment"), TEXT("padding"),
	TEXT("zOrder"), TEXT("autoSize"),
	TEXT("row"), TEXT("column"), TEXT("rowSpan"), TEXT("columnSpan"),
	TEXT("horizontalAlignment"), TEXT("verticalAlignment")
};
// Valid top-level keys
static const TSet<FString> ValidTopLevelKeys = { TEXT("root"), TEXT("animations") };
// Valid keys in an animation entry
static const TSet<FString> ValidAnimationKeys = { TEXT("name"), TEXT("target"), TEXT("duration"), TEXT("tracks") };
// Valid keys in an opacity track
static const TSet<FString> ValidOpacityTrackKeys = { TEXT("type"), TEXT("from"), TEXT("to") };

bool FWidgetBlueprintJsonParser::Parse(const FString& JsonString, FWidgetBlueprintSpec& OutSpec, FString& OutError)
{
	TSharedPtr<FJsonObject> RootObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		OutError = TEXT("[WidgetBuilder] Failed to parse JSON string");
		return false;
	}

	// Validate top-level keys
	for (const auto& Pair : RootObj->Values)
	{
		if (!ValidTopLevelKeys.Contains(Pair.Key))
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] Unknown top-level key '%s'"), *Pair.Key);
			return false;
		}
	}

	const TSharedPtr<FJsonObject>* RootNodeObj = nullptr;
	if (!RootObj->TryGetObjectField(TEXT("root"), RootNodeObj))
	{
		OutError = TEXT("[WidgetBuilder] Missing required 'root' object");
		return false;
	}

	if (!ParseWidgetNode(*RootNodeObj, OutSpec.Root, TEXT(""), OutError))
	{
		return false;
	}

	// Optional: animations
	const TArray<TSharedPtr<FJsonValue>>* AnimArray = nullptr;
	if (RootObj->TryGetArrayField(TEXT("animations"), AnimArray))
	{
		if (!ParseAnimations(AnimArray, OutSpec.Animations, OutError))
		{
			return false;
		}
	}

	return true;
}

bool FWidgetBlueprintJsonParser::ParseWidgetNode(
	const TSharedPtr<FJsonObject>& NodeObj,
	FWidgetNodeSpec& OutNode,
	const FString& Path,
	FString& OutError)
{
	// Check for unknown keys at node level
	for (const auto& Pair : NodeObj->Values)
	{
		if (!ValidNodeKeys.Contains(Pair.Key))
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Unknown key '%s' at node level"), *Path, *Pair.Key);
			return false;
		}
	}

	// Required: type
	if (!NodeObj->TryGetStringField(TEXT("type"), OutNode.Type))
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Missing required 'type' field"), *Path);
		return false;
	}

	// Required: name
	if (!NodeObj->TryGetStringField(TEXT("name"), OutNode.Name))
	{
		OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Missing required 'name' field"), *Path);
		return false;
	}

	// Build path for this node
	FString NodePath = Path.IsEmpty() ? OutNode.Name : FString::Printf(TEXT("%s.%s"), *Path, *OutNode.Name);

	// Optional: properties
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (NodeObj->TryGetObjectField(TEXT("properties"), PropsObj))
	{
		if (!ParseProperties(*PropsObj, OutNode.Properties, NodePath, OutError))
		{
			return false;
		}
	}

	// Optional: slot
	const TSharedPtr<FJsonObject>* SlotObj = nullptr;
	if (NodeObj->TryGetObjectField(TEXT("slot"), SlotObj))
	{
		OutNode.bHasSlot = true;
		if (!ParseSlotSpec(*SlotObj, OutNode.Slot, NodePath, OutError))
		{
			return false;
		}
	}

	// Optional: children
	const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = nullptr;
	if (NodeObj->TryGetArrayField(TEXT("children"), ChildrenArray))
	{
		for (int32 i = 0; i < ChildrenArray->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>& ChildObj = (*ChildrenArray)[i]->AsObject();
			if (!ChildObj.IsValid())
			{
				OutError = FString::Printf(TEXT("[WidgetBuilder] %s: Child %d is not a valid object"), *NodePath, i);
				return false;
			}

			FWidgetNodeSpec ChildSpec;
			if (!ParseWidgetNode(ChildObj, ChildSpec, NodePath, OutError))
			{
				return false;
			}
			OutNode.Children.Add(MoveTemp(ChildSpec));
		}
	}

	return true;
}

bool FWidgetBlueprintJsonParser::ParseSlotSpec(
	const TSharedPtr<FJsonObject>& SlotObj,
	FWidgetSlotSpec& OutSlot,
	const FString& Path,
	FString& OutError)
{
	// Check for unknown keys
	for (const auto& Pair : SlotObj->Values)
	{
		if (!ValidSlotKeys.Contains(Pair.Key))
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] %s.slot: Unknown key '%s'"), *Path, *Pair.Key);
			return false;
		}
	}

	// position: {x, y}
	const TSharedPtr<FJsonObject>* PosObj = nullptr;
	if (SlotObj->TryGetObjectField(TEXT("position"), PosObj))
	{
		OutSlot.Position.X = (*PosObj)->GetNumberField(TEXT("x"));
		OutSlot.Position.Y = (*PosObj)->GetNumberField(TEXT("y"));
		OutSlot.bHasPosition = true;
	}

	// size: {x, y}
	const TSharedPtr<FJsonObject>* SizeObj = nullptr;
	if (SlotObj->TryGetObjectField(TEXT("size"), SizeObj))
	{
		OutSlot.Size.X = (*SizeObj)->GetNumberField(TEXT("x"));
		OutSlot.Size.Y = (*SizeObj)->GetNumberField(TEXT("y"));
		OutSlot.bHasSize = true;
	}

	// alignment: {x, y}
	const TSharedPtr<FJsonObject>* AlignObj = nullptr;
	if (SlotObj->TryGetObjectField(TEXT("alignment"), AlignObj))
	{
		OutSlot.Alignment.X = (*AlignObj)->GetNumberField(TEXT("x"));
		OutSlot.Alignment.Y = (*AlignObj)->GetNumberField(TEXT("y"));
		OutSlot.bHasAlignment = true;
	}

	// padding: {left, top, right, bottom}
	const TSharedPtr<FJsonObject>* PadObj = nullptr;
	if (SlotObj->TryGetObjectField(TEXT("padding"), PadObj))
	{
		OutSlot.Padding.Left = (*PadObj)->GetNumberField(TEXT("left"));
		OutSlot.Padding.Top = (*PadObj)->GetNumberField(TEXT("top"));
		OutSlot.Padding.Right = (*PadObj)->GetNumberField(TEXT("right"));
		OutSlot.Padding.Bottom = (*PadObj)->GetNumberField(TEXT("bottom"));
		OutSlot.bHasPadding = true;
	}

	// zOrder
	double ZOrder;
	if (SlotObj->TryGetNumberField(TEXT("zOrder"), ZOrder))
	{
		OutSlot.ZOrder = static_cast<int32>(ZOrder);
		OutSlot.bHasZOrder = true;
	}

	// autoSize
	bool AutoSize;
	if (SlotObj->TryGetBoolField(TEXT("autoSize"), AutoSize))
	{
		OutSlot.bAutoSize = AutoSize;
		OutSlot.bHasAutoSize = true;
	}

	// row
	double RowVal;
	if (SlotObj->TryGetNumberField(TEXT("row"), RowVal))
	{
		OutSlot.Row = static_cast<int32>(RowVal);
		OutSlot.bHasRow = true;
	}

	// column
	double ColVal;
	if (SlotObj->TryGetNumberField(TEXT("column"), ColVal))
	{
		OutSlot.Column = static_cast<int32>(ColVal);
		OutSlot.bHasColumn = true;
	}

	// rowSpan
	double RowSpanVal;
	if (SlotObj->TryGetNumberField(TEXT("rowSpan"), RowSpanVal))
	{
		OutSlot.RowSpan = static_cast<int32>(RowSpanVal);
		OutSlot.bHasRowSpan = true;
	}

	// columnSpan
	double ColSpanVal;
	if (SlotObj->TryGetNumberField(TEXT("columnSpan"), ColSpanVal))
	{
		OutSlot.ColumnSpan = static_cast<int32>(ColSpanVal);
		OutSlot.bHasColumnSpan = true;
	}

	// horizontalAlignment
	FString HAStr;
	if (SlotObj->TryGetStringField(TEXT("horizontalAlignment"), HAStr))
	{
		OutSlot.HorizontalAlignment = HAStr;
		OutSlot.bHasHorizontalAlignment = true;
	}

	// verticalAlignment
	FString VAStr;
	if (SlotObj->TryGetStringField(TEXT("verticalAlignment"), VAStr))
	{
		OutSlot.VerticalAlignment = VAStr;
		OutSlot.bHasVerticalAlignment = true;
	}

	return true;
}

bool FWidgetBlueprintJsonParser::ParseProperties(
	const TSharedPtr<FJsonObject>& PropsObj,
	TMap<FString, TSharedPtr<FJsonValue>>& OutProperties,
	const FString& Path,
	FString& OutError)
{
	// Accept all keys in properties -- validator decides which are supported
	for (const auto& Pair : PropsObj->Values)
	{
		OutProperties.Add(Pair.Key, Pair.Value);
	}
	return true;
}

bool FWidgetBlueprintJsonParser::ParseAnimations(
	const TArray<TSharedPtr<FJsonValue>>* AnimArray,
	TArray<FWidgetAnimationSpec>& OutAnimations,
	FString& OutError)
{
	for (int32 i = 0; i < AnimArray->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>& AnimObj = (*AnimArray)[i]->AsObject();
		if (!AnimObj.IsValid())
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: not a valid object"), i);
			return false;
		}

		// Check for unknown keys
		for (const auto& Pair : AnimObj->Values)
		{
			if (!ValidAnimationKeys.Contains(Pair.Key))
			{
				OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: unknown key '%s'"), i, *Pair.Key);
				return false;
			}
		}

		FWidgetAnimationSpec AnimSpec;

		// Required: name
		if (!AnimObj->TryGetStringField(TEXT("name"), AnimSpec.Name))
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: missing required 'name' field"), i);
			return false;
		}

		// Required: target
		if (!AnimObj->TryGetStringField(TEXT("target"), AnimSpec.Target))
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: missing required 'target' field"), i);
			return false;
		}

		// Required: duration
		double DurationVal;
		if (!AnimObj->TryGetNumberField(TEXT("duration"), DurationVal))
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] animations[%d]: missing required 'duration' field"), i);
			return false;
		}
		AnimSpec.Duration = static_cast<float>(DurationVal);

		// Required: tracks
		const TArray<TSharedPtr<FJsonValue>>* TracksArray = nullptr;
		if (!AnimObj->TryGetArrayField(TEXT("tracks"), TracksArray))
		{
			OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s': missing required 'tracks' field"), *AnimSpec.Name);
			return false;
		}

		for (int32 t = 0; t < TracksArray->Num(); ++t)
		{
			const TSharedPtr<FJsonObject>& TrackObj = (*TracksArray)[t]->AsObject();
			if (!TrackObj.IsValid())
			{
				OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: not a valid object"), *AnimSpec.Name, t);
				return false;
			}

			FWidgetAnimationTrackSpec TrackSpec;

			// Required: type
			if (!TrackObj->TryGetStringField(TEXT("type"), TrackSpec.Type))
			{
				OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: missing required 'type' field"), *AnimSpec.Name, t);
				return false;
			}

			// Parse track data based on type
			if (TrackSpec.Type == TEXT("opacity"))
			{
				// Check for unknown keys
				for (const auto& Pair : TrackObj->Values)
				{
					if (!ValidOpacityTrackKeys.Contains(Pair.Key))
					{
						OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: unknown key '%s'"), *AnimSpec.Name, t, *Pair.Key);
						return false;
					}
				}

				// Required: from
				double FromVal;
				if (!TrackObj->TryGetNumberField(TEXT("from"), FromVal))
				{
					OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: opacity track missing 'from'"), *AnimSpec.Name, t);
					return false;
				}
				TrackSpec.FromOpacity = static_cast<float>(FromVal);

				// Required: to
				double ToVal;
				if (!TrackObj->TryGetNumberField(TEXT("to"), ToVal))
				{
					OutError = FString::Printf(TEXT("[WidgetBuilder] Animation '%s'.tracks[%d]: opacity track missing 'to'"), *AnimSpec.Name, t);
					return false;
				}
				TrackSpec.ToOpacity = static_cast<float>(ToVal);

				TrackSpec.bHasOpacityData = true;
			}
			// Unknown track types pass through to validator (which rejects them)

			AnimSpec.Tracks.Add(MoveTemp(TrackSpec));
		}

		OutAnimations.Add(MoveTemp(AnimSpec));
	}

	return true;
}
