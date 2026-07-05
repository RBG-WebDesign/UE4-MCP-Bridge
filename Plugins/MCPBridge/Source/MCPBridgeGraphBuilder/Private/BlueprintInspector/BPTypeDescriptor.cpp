// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPTypeDescriptor.h"
#include "BPGLogCategories.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Class.h"

namespace
{
    FString ContainerToString(EPinContainerType Container)
    {
        switch (Container)
        {
            case EPinContainerType::Array: return TEXT("Array");
            case EPinContainerType::Set:   return TEXT("Set");
            case EPinContainerType::Map:   return TEXT("Map");
            default: return TEXT("None");
        }
    }

    EPinContainerType ContainerFromString(const FString& Str)
    {
        if (Str == TEXT("Array")) return EPinContainerType::Array;
        if (Str == TEXT("Set"))   return EPinContainerType::Set;
        if (Str == TEXT("Map"))   return EPinContainerType::Map;
        return EPinContainerType::None;
    }

    FString ObjectPathForPinType(const FEdGraphPinType& Pt)
    {
        if (UObject* Obj = Pt.PinSubCategoryObject.Get())
        {
            return Obj->GetPathName();
        }
        return FString();
    }
}

TSharedPtr<FJsonObject> FBPTypeDescriptor::ToJson(const FEdGraphPinType& PinType)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("category"), PinType.PinCategory.ToString());
    Root->SetStringField(TEXT("sub_category"), PinType.PinSubCategory.ToString());
    Root->SetStringField(TEXT("sub_category_object"), ObjectPathForPinType(PinType));
    Root->SetStringField(TEXT("container_type"), ContainerToString(PinType.ContainerType));
    Root->SetBoolField(TEXT("is_reference"), PinType.bIsReference);

    if (PinType.ContainerType == EPinContainerType::Map)
    {
        FEdGraphPinType ValueType;
        ValueType.PinCategory = PinType.PinValueType.TerminalCategory;
        ValueType.PinSubCategory = PinType.PinValueType.TerminalSubCategory;
        ValueType.PinSubCategoryObject = PinType.PinValueType.TerminalSubCategoryObject;
        Root->SetObjectField(TEXT("value_type"), ToJson(ValueType));
    }
    else
    {
        Root->SetField(TEXT("value_type"), MakeShared<FJsonValueNull>());
    }
    return Root;
}

bool FBPTypeDescriptor::FromJson(const TSharedPtr<FJsonObject>& Json, FEdGraphPinType& OutPinType)
{
    if (!Json.IsValid()) return false;

    FString Category;
    if (!Json->TryGetStringField(TEXT("category"), Category))
    {
        UE_LOG(LogBlueprintInspector, Warning,
               TEXT("FBPTypeDescriptor::FromJson: missing 'category' field"));
        return false;
    }
    OutPinType.PinCategory = FName(*Category);

    FString SubCategory;
    if (Json->TryGetStringField(TEXT("sub_category"), SubCategory))
    {
        OutPinType.PinSubCategory = FName(*SubCategory);
    }

    FString ContainerStr;
    if (Json->TryGetStringField(TEXT("container_type"), ContainerStr))
    {
        OutPinType.ContainerType = ContainerFromString(ContainerStr);
    }

    bool bIsRef = false;
    if (Json->TryGetBoolField(TEXT("is_reference"), bIsRef))
    {
        OutPinType.bIsReference = bIsRef;
    }

    FString ObjPath;
    if (Json->TryGetStringField(TEXT("sub_category_object"), ObjPath) && !ObjPath.IsEmpty())
    {
        UObject* Resolved = FindObject<UObject>(nullptr, *ObjPath);
        if (!Resolved)
        {
            Resolved = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjPath);
        }
        if (!Resolved)
        {
            UE_LOG(LogBlueprintInspector, Warning,
                   TEXT("FBPTypeDescriptor::FromJson: failed to resolve sub_category_object '%s'"),
                   *ObjPath);
        }
        OutPinType.PinSubCategoryObject = Resolved;
    }

    if (OutPinType.ContainerType == EPinContainerType::Map)
    {
        const TSharedPtr<FJsonObject>* ValueTypeJson = nullptr;
        if (Json->TryGetObjectField(TEXT("value_type"), ValueTypeJson) && ValueTypeJson && (*ValueTypeJson).IsValid())
        {
            FEdGraphPinType Inner;
            if (FromJson(*ValueTypeJson, Inner))
            {
                OutPinType.PinValueType.TerminalCategory = Inner.PinCategory;
                OutPinType.PinValueType.TerminalSubCategory = Inner.PinSubCategory;
                OutPinType.PinValueType.TerminalSubCategoryObject = Inner.PinSubCategoryObject;
            }
        }
    }
    return true;
}

FString FBPTypeDescriptor::ToJsonString(const FEdGraphPinType& PinType)
{
    FString Out;
    TSharedPtr<FJsonObject> Obj = ToJson(PinType);
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    return Out;
}

FEdGraphPinType FBPTypeDescriptor::FromJsonString(const FString& JsonString)
{
    FEdGraphPinType Result;
    TSharedPtr<FJsonObject> Obj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
    {
        FromJson(Obj, Result);
    }
    return Result;
}
