// Copyright 2026 RareBird Games. All Rights Reserved.

#include "CanonFontLibrary.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "UObject/UnrealType.h"

bool UCanonFontLibrary::ConfigureRuntimeFont(
	UFont* Font,
	const TArray<UFontFace*>& Faces,
	const TArray<FName>& TypefaceNames,
	int32 DefaultFaceIndex,
	FString& OutError)
{
	OutError.Reset();

	if (!Font)
	{
		OutError = TEXT("Font is null.");
		return false;
	}
	if (Faces.Num() == 0 || Faces.Num() != TypefaceNames.Num())
	{
		OutError = TEXT("Faces and TypefaceNames must be non-empty arrays with matching lengths.");
		return false;
	}
	if (!Faces.IsValidIndex(DefaultFaceIndex) || !Faces[DefaultFaceIndex])
	{
		OutError = TEXT("DefaultFaceIndex does not identify a valid FontFace.");
		return false;
	}

	FStructProperty* CompositeProperty = FindFProperty<FStructProperty>(UFont::StaticClass(), TEXT("CompositeFont"));
	if (!CompositeProperty)
	{
		OutError = TEXT("UFont.CompositeFont reflection property was not found.");
		return false;
	}

	FCompositeFont* CompositeFont = CompositeProperty->ContainerPtrToValuePtr<FCompositeFont>(Font);
	if (!CompositeFont)
	{
		OutError = TEXT("UFont.CompositeFont could not be accessed.");
		return false;
	}

	Font->Modify();
	Font->FontCacheType = EFontCacheType::Runtime;
	CompositeFont->DefaultTypeface.Fonts.Reset();
	CompositeFont->SubTypefaces.Reset();

	for (int32 Index = 0; Index < Faces.Num(); ++Index)
	{
		if (!Faces[Index] || TypefaceNames[Index].IsNone())
		{
			OutError = FString::Printf(TEXT("Invalid face or typeface name at index %d."), Index);
			return false;
		}

		FTypefaceEntry Entry(TypefaceNames[Index]);
		Entry.Font = FFontData(Faces[Index]);
		CompositeFont->DefaultTypeface.Fonts.Add(MoveTemp(Entry));
	}

	FTypefaceEntry DefaultEntry(TEXT("Default"));
	DefaultEntry.Font = FFontData(Faces[DefaultFaceIndex]);
	CompositeFont->DefaultTypeface.Fonts.Insert(MoveTemp(DefaultEntry), 0);

#if WITH_EDITORONLY_DATA
	CompositeFont->MakeDirty();
#endif
	Font->PostEditChange();
	Font->MarkPackageDirty();
	return true;
}
