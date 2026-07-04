#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprintSpec.h"

class UWidget;

class FWidgetClassRegistry
{
public:
	FWidgetClassRegistry();

	TSubclassOf<UWidget> ResolveWidgetClass(const FString& TypeName) const;
	bool IsSupportedType(const FString& TypeName) const;
	EWidgetCategory GetCategory(const FString& TypeName) const;
	TArray<FString> GetSupportedTypes() const;
	const TArray<FWidgetPropertyDescriptor>* GetSupportedProperties(const FString& TypeName) const;

private:
	struct FWidgetTypeInfo
	{
		TSubclassOf<UWidget> WidgetClass;
		EWidgetCategory Category;
		TArray<FWidgetPropertyDescriptor> SupportedProperties;
	};

	TMap<FString, FWidgetTypeInfo> TypeRegistry;

	void RegisterTypes();
	static void AddCommonProperties(FWidgetTypeInfo& Info);
	void RegisterPanel(const FString& TypeName, TSubclassOf<UWidget> WidgetClass);
	void RegisterContent(const FString& TypeName, TSubclassOf<UWidget> WidgetClass,
		TArray<FWidgetPropertyDescriptor> TypeSpecificProps = {});
	void RegisterLeaf(const FString& TypeName, TSubclassOf<UWidget> WidgetClass,
		TArray<FWidgetPropertyDescriptor> TypeSpecificProps = {});
};
