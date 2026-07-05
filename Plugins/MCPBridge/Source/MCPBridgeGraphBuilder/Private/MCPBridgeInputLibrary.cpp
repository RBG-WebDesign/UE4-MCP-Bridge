#include "MCPBridgeInputLibrary.h"

#include "GameFramework/InputSettings.h"
#include "InputCoreTypes.h"

namespace
{
	FString ResolveKey(const FString& KeyName, FKey& OutKey)
	{
		OutKey = FKey(*KeyName);
		if (!OutKey.IsValid())
		{
			return FString::Printf(TEXT("Invalid key name: '%s'"), *KeyName);
		}
		return FString();
	}

	UInputSettings* Settings()
	{
		return UInputSettings::GetInputSettings();
	}
}

FString UMCPBridgeInputLibrary::AddActionMapping(const FString& ActionName, const FString& KeyName,
	bool bShift, bool bCtrl, bool bAlt, bool bCmd)
{
	if (ActionName.IsEmpty())
	{
		return TEXT("ActionName is empty");
	}
	FKey Key;
	const FString KeyError = ResolveKey(KeyName, Key);
	if (!KeyError.IsEmpty())
	{
		return KeyError;
	}

	UInputSettings* InputSettings = Settings();
	if (!InputSettings)
	{
		return TEXT("InputSettings unavailable");
	}

	FInputActionKeyMapping Mapping(*ActionName, Key, bShift, bCtrl, bAlt, bCmd);

	// Reject exact duplicates so repeated calls stay idempotent.
	TArray<FInputActionKeyMapping> Existing;
	InputSettings->GetActionMappingByName(*ActionName, Existing);
	if (Existing.Contains(Mapping))
	{
		return FString();
	}

	InputSettings->AddActionMapping(Mapping, /*bForceRebuildKeymaps*/ true);
	InputSettings->SaveKeyMappings();
	return FString();
}

FString UMCPBridgeInputLibrary::RemoveActionMapping(const FString& ActionName, const FString& KeyName)
{
	UInputSettings* InputSettings = Settings();
	if (!InputSettings)
	{
		return TEXT("InputSettings unavailable");
	}

	TArray<FInputActionKeyMapping> Existing;
	InputSettings->GetActionMappingByName(*ActionName, Existing);
	if (Existing.Num() == 0)
	{
		return FString::Printf(TEXT("No action mapping named '%s'"), *ActionName);
	}

	int32 Removed = 0;
	for (const FInputActionKeyMapping& Mapping : Existing)
	{
		if (KeyName.IsEmpty() || Mapping.Key.ToString() == KeyName)
		{
			InputSettings->RemoveActionMapping(Mapping, /*bForceRebuildKeymaps*/ true);
			++Removed;
		}
	}
	if (Removed == 0)
	{
		return FString::Printf(TEXT("Action '%s' has no mapping for key '%s'"), *ActionName, *KeyName);
	}
	InputSettings->SaveKeyMappings();
	return FString();
}

FString UMCPBridgeInputLibrary::AddAxisMapping(const FString& AxisName, const FString& KeyName, float Scale)
{
	if (AxisName.IsEmpty())
	{
		return TEXT("AxisName is empty");
	}
	FKey Key;
	const FString KeyError = ResolveKey(KeyName, Key);
	if (!KeyError.IsEmpty())
	{
		return KeyError;
	}

	UInputSettings* InputSettings = Settings();
	if (!InputSettings)
	{
		return TEXT("InputSettings unavailable");
	}

	FInputAxisKeyMapping Mapping(*AxisName, Key, Scale);

	TArray<FInputAxisKeyMapping> Existing;
	InputSettings->GetAxisMappingByName(*AxisName, Existing);
	if (Existing.Contains(Mapping))
	{
		return FString();
	}

	InputSettings->AddAxisMapping(Mapping, /*bForceRebuildKeymaps*/ true);
	InputSettings->SaveKeyMappings();
	return FString();
}

FString UMCPBridgeInputLibrary::RemoveAxisMapping(const FString& AxisName, const FString& KeyName)
{
	UInputSettings* InputSettings = Settings();
	if (!InputSettings)
	{
		return TEXT("InputSettings unavailable");
	}

	TArray<FInputAxisKeyMapping> Existing;
	InputSettings->GetAxisMappingByName(*AxisName, Existing);
	if (Existing.Num() == 0)
	{
		return FString::Printf(TEXT("No axis mapping named '%s'"), *AxisName);
	}

	int32 Removed = 0;
	for (const FInputAxisKeyMapping& Mapping : Existing)
	{
		if (KeyName.IsEmpty() || Mapping.Key.ToString() == KeyName)
		{
			InputSettings->RemoveAxisMapping(Mapping, /*bForceRebuildKeymaps*/ true);
			++Removed;
		}
	}
	if (Removed == 0)
	{
		return FString::Printf(TEXT("Axis '%s' has no mapping for key '%s'"), *AxisName, *KeyName);
	}
	InputSettings->SaveKeyMappings();
	return FString();
}

bool UMCPBridgeInputLibrary::IsValidKeyName(const FString& KeyName)
{
	return FKey(*KeyName).IsValid();
}
