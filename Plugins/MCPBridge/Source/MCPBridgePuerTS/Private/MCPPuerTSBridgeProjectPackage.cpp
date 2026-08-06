#include "MCPPuerTSBridgeService.h"

#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Json.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
    bool HasCommandProcessorMeta(const FString& Value)
    {
        const TCHAR* Tokens[] = { TEXT("\""), TEXT("%"), TEXT("!"), TEXT("&"),
            TEXT("|"), TEXT("<"), TEXT(">"), TEXT("^"), TEXT("\r"), TEXT("\n") };
        for (const TCHAR* Token : Tokens)
        {
            if (Value.Contains(Token)) { return true; }
        }
        return false;
    }

    bool ResolveArchiveDirectory(const FString& Requested, FString& OutPath, FString& OutError)
    {
        FString Candidate = Requested.IsEmpty()
            ? FPaths::ProjectSavedDir() / TEXT("MCPPuerTSBridge/Packages/Development")
            : Requested;
        if (FPaths::IsRelative(Candidate)) { Candidate = FPaths::ProjectDir() / Candidate; }
        OutPath = FPaths::ConvertRelativePathToFull(Candidate);
        FPaths::NormalizeDirectoryName(OutPath);
        FPaths::CollapseRelativeDirectories(OutPath);
        FString SavedRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
        FPaths::NormalizeDirectoryName(SavedRoot);
        FPaths::CollapseRelativeDirectories(SavedRoot);
        SavedRoot += TEXT("/");
        if (!OutPath.StartsWith(SavedRoot, ESearchCase::IgnoreCase) || HasCommandProcessorMeta(OutPath))
        {
            OutError = TEXT("output_directory must resolve inside this project's Saved directory.");
            return false;
        }
        return true;
    }

    bool HasDirtyPackages(TArray<FString>& OutNames)
    {
        TArray<UPackage*> Dirty;
        FEditorFileUtils::GetDirtyWorldPackages(Dirty);
        FEditorFileUtils::GetDirtyContentPackages(Dirty);
        for (UPackage* Package : Dirty)
        {
            if (Package != nullptr) { OutNames.AddUnique(Package->GetName()); }
        }
        OutNames.Sort();
        return OutNames.Num() > 0;
    }
}

bool UMCPPuerTSBridgeService::PackageProjectJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError)
{
    TSharedPtr<FJsonObject> Request;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RequestJson), Request)
        || !Request.IsValid())
    {
        OutError = TEXT("Project package request must be a JSON object.");
        return false;
    }
    if (GEditor != nullptr && GEditor->PlayWorld != nullptr)
    {
        OutError = TEXT("Project packaging is refused during Play In Editor.");
        return false;
    }

    FString Target = TEXT("Win64");
    FString Configuration = TEXT("Development");
    FString RequestedOutput;
    bool bPlanOnly = false;
    Request->TryGetStringField(TEXT("target"), Target);
    Request->TryGetStringField(TEXT("configuration"), Configuration);
    Request->TryGetStringField(TEXT("output_directory"), RequestedOutput);
    Request->TryGetBoolField(TEXT("plan_only"), bPlanOnly);
    if (Target != TEXT("Win64"))
    {
        OutError = TEXT("target must be Win64.");
        return false;
    }
    if (Configuration != TEXT("Development") && Configuration != TEXT("Shipping"))
    {
        OutError = TEXT("configuration must be Development or Shipping.");
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* MapValues = nullptr;
    if (!Request->TryGetArrayField(TEXT("maps"), MapValues)
        || MapValues == nullptr || MapValues->Num() == 0 || MapValues->Num() > 32)
    {
        OutError = TEXT("maps must contain 1 to 32 saved /Game map package names.");
        return false;
    }
    TArray<FString> Maps;
    for (const TSharedPtr<FJsonValue>& Value : *MapValues)
    {
        FString Map;
        if (!Value.IsValid() || !Value->TryGetString(Map)
            || !FPackageName::IsValidLongPackageName(Map, true) || !Map.StartsWith(TEXT("/Game/"))
            || Map.Contains(TEXT("\"")))
        {
            OutError = TEXT("Every map must be a /Game package name without command-line characters.");
            return false;
        }
        FString Filename;
        if (!FPackageName::TryConvertLongPackageNameToFilename(
                Map, Filename, FPackageName::GetMapPackageExtension())
            || !FPaths::FileExists(Filename))
        {
            OutError = FString::Printf(TEXT("Map '%s' is not a saved .umap."), *Map);
            return false;
        }
        Maps.AddUnique(Map);
    }

    FString OutputDirectory;
    if (RequestedOutput.IsEmpty())
    {
        RequestedOutput = TEXT("Saved/MCPPuerTSBridge/Packages/") + Configuration;
    }
    if (!ResolveArchiveDirectory(RequestedOutput, OutputDirectory, OutError)) { return false; }
    FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeDirectoryName(ProjectRoot);
    ProjectRoot += TEXT("/");
    if (!ProjectFile.EndsWith(TEXT(".uproject"), ESearchCase::IgnoreCase)
        || !ProjectFile.StartsWith(ProjectRoot, ESearchCase::IgnoreCase)
        || !FPaths::FileExists(ProjectFile) || HasCommandProcessorMeta(ProjectFile))
    {
        OutError = TEXT("The served project's .uproject path is missing or outside ProjectDir.");
        return false;
    }
    FString RunUAT = FPaths::ConvertRelativePathToFull(
        FPaths::EngineDir() / TEXT("Build/BatchFiles/RunUAT.bat"));
    if (!FPaths::FileExists(RunUAT) || HasCommandProcessorMeta(RunUAT))
    {
        OutError = TEXT("The fixed Engine/Build/BatchFiles/RunUAT.bat path is unavailable.");
        return false;
    }

    TArray<FString> DirtyNames;
    if (HasDirtyPackages(DirtyNames))
    {
        OutError = TEXT("Save all dirty world and content packages before packaging. Dirty: ")
            + FString::Join(DirtyNames, TEXT(", "));
        return false;
    }
    for (const FBridgeJob& Existing : Jobs)
    {
        if (Existing.Kind == EBridgeJobKind::ProjectPackage && Existing.State == TEXT("running"))
        {
            OutError = TEXT("A project package job is already running in this editor session.");
            return false;
        }
    }

    TSharedPtr<FJsonObject> StartResult = MakeShared<FJsonObject>();
    StartResult->SetStringField(TEXT("target"), Target);
    StartResult->SetStringField(TEXT("configuration"), Configuration);
    StartResult->SetStringField(TEXT("output_directory"), OutputDirectory);
    TArray<TSharedPtr<FJsonValue>> MapResult;
    for (const FString& Map : Maps) { MapResult.Add(MakeShared<FJsonValueString>(Map)); }
    StartResult->SetArrayField(TEXT("maps"), MapResult);
    if (bPlanOnly)
    {
        StartResult->SetStringField(TEXT("status"), TEXT("planned"));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
        FJsonSerializer::Serialize(StartResult.ToSharedRef(), Writer);
        return true;
    }

    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    FString MapArgument = FString::Join(Maps, TEXT("+"));
    FString Arguments = FString::Printf(
        TEXT("/d /s /c \"\"%s\" BuildCookRun -project=\"%s\" -nop4 -targetplatform=Win64 ")
        TEXT("-clientconfig=%s -build -cook -stage -pak -package -archive -archivedirectory=\"%s\" ")
        TEXT("-map=%s -unattended -utf8output\""),
        *RunUAT, *ProjectFile, *Configuration, *OutputDirectory, *MapArgument);
    FString CmdExe = FPlatformMisc::GetEnvironmentVariable(TEXT("COMSPEC"));
    if (!FPaths::FileExists(CmdExe) || HasCommandProcessorMeta(CmdExe)
        || !FPaths::GetCleanFilename(CmdExe).Equals(TEXT("cmd.exe"), ESearchCase::IgnoreCase))
    {
        OutError = TEXT("The Windows command processor path is unavailable.");
        return false;
    }
    void* ReadPipe = nullptr;
    void* WritePipe = nullptr;
    if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
    {
        OutError = TEXT("Could not create the UAT output pipe.");
        return false;
    }
    uint32 ProcessId = 0;
    FProcHandle Process = FPlatformProcess::CreateProc(
        *CmdExe, *Arguments, true, true, true, &ProcessId, 0, *FPaths::ProjectDir(), WritePipe);
    FPlatformProcess::ClosePipe(nullptr, WritePipe);
    if (!Process.IsValid())
    {
        FPlatformProcess::ClosePipe(ReadPipe, nullptr);
        OutError = TEXT("Could not start the fixed RunUAT BuildCookRun child process.");
        return false;
    }

    StartResult->SetStringField(TEXT("status"), TEXT("started"));
    StartResult->SetNumberField(TEXT("process_id"), ProcessId);
    const FString JobId = RegisterJob(EBridgeJobKind::ProjectPackage,
        TEXT("project_package_start"), TEXT("UAT BuildCookRun child process"), StartResult);
    FBridgeJob& Job = Jobs.Last();
    Job.ProcessHandle = Process;
    Job.ProcessReadPipe = ReadPipe;
    Job.ProcessId = ProcessId;
    Job.OutputDirectory = OutputDirectory;
    StartResult->SetStringField(TEXT("job_id"), JobId);
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResultJson);
    FJsonSerializer::Serialize(StartResult.ToSharedRef(), Writer);
    return true;
}
