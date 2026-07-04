#include "BPNodeFactory_Internals.h"
#include "BPGLogCategories.h"

namespace BPNodeFactoryInternal
{
    bool ParseJsonConfig(const FString& ConfigJson, TSharedPtr<FJsonObject>& OutObj)
    {
        if (ConfigJson.IsEmpty()) { OutObj = MakeShared<FJsonObject>(); return true; }
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConfigJson);
        if (!FJsonSerializer::Deserialize(Reader, OutObj) || !OutObj.IsValid())
        {
            UE_LOG(LogBlueprintMutator, Warning,
                TEXT("BPNodeFactory: failed to parse ConfigJson: %s"), *ConfigJson);
            return false;
        }
        return true;
    }
}
