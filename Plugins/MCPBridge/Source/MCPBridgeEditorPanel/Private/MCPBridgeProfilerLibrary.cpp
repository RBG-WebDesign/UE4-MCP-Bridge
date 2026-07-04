#include "MCPBridgeProfilerLibrary.h"

#include "HAL/FileManager.h"
#include "Json.h"
#include "Misc/Paths.h"

FString UMCPBridgeProfilerLibrary::SummarizeTraceFile(const FString& TracePath)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    const bool bExists = !TracePath.IsEmpty() && FPaths::FileExists(TracePath);
    Root->SetStringField(TEXT("path"), TracePath);
    Root->SetBoolField(TEXT("exists"), bExists);
    Root->SetBoolField(TEXT("parsed"), false);
    Root->SetStringField(TEXT("parser"), TEXT("capture_only"));

    if (bExists)
    {
        Root->SetNumberField(TEXT("size_bytes"), static_cast<double>(IFileManager::Get().FileSize(*TracePath)));
        Root->SetStringField(TEXT("note"), TEXT("Trace file was found. Deep CPU scope parsing is reserved for the TraceServices-backed parser."));
    }
    else
    {
        Root->SetStringField(TEXT("note"), TEXT("Trace file was not found."));
    }

    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    return Output;
}

