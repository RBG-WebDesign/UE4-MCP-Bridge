#include "MCPPuerTSBridgeService.h"

#include "Editor.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Json.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

bool UMCPPuerTSBridgeService::CaptureViewportJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    if (GEditor == nullptr || GEditor->PlayWorld != nullptr)
    {
        OutError = TEXT("Viewport capture requires the editor outside PIE.");
        return false;
    }

    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Viewport capture request must be valid JSON.");
        return false;
    }

    FString Filename = TEXT("puerts_viewport.png");
    Request->TryGetStringField(TEXT("filename"), Filename);
    Filename = FPaths::GetCleanFilename(Filename);
    if (Filename.IsEmpty()) Filename = TEXT("puerts_viewport.png");
    if (!Filename.EndsWith(TEXT(".png"))) Filename += TEXT(".png");

    TSet<FString> Requested;
    const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
    if (Request->TryGetArrayField(TEXT("actors"), Names) && Names != nullptr)
    {
        if (Names->Num() > 200)
        {
            OutError = TEXT("Viewport capture is limited to 200 actors.");
            return false;
        }
        for (const TSharedPtr<FJsonValue>& Name : *Names)
        {
            FString Value;
            if (!Name.IsValid() || !Name->TryGetString(Value) || Value.IsEmpty())
            {
                OutError = TEXT("actors must contain non-empty strings.");
                return false;
            }
            Requested.Add(Value);
        }
    }

    TArray<AActor*> Actors;
    for (AActor* Actor : GetLevelActors())
    {
        if (Actor == nullptr) continue;
        const bool bDefaultPhysicsActor = Requested.Num() == 0 && Actor->Tags.Contains(TEXT("MCPPhysics"));
        const bool bRequested = Requested.Contains(Actor->GetName()) || Requested.Contains(Actor->GetActorLabel());
        if (bDefaultPhysicsActor || bRequested) Actors.Add(Actor);
    }
    // Naming actors means "frame these first". Naming none means "capture what
    // the viewport is looking at", which is what the AGENTS.md feedback loop
    // asks for after every spatial operation and what this used to refuse: an
    // empty level, or a level with nothing MCPPhysics-tagged in it, failed the
    // screenshot rather than taking one. Only a request that named actors and
    // found none is an error, because that caller was told about actors that
    // are not there.
    if (Actors.Num() == 0 && Requested.Num() > 0)
    {
        TArray<FString> Asked = Requested.Array();
        Asked.Sort();
        OutError = FString::Printf(
            TEXT("No actor in the level matches any of the %d name(s) requested for viewport "
                 "capture: %s. Names are matched against an actor's name and its label."),
            Requested.Num(), *FString::Join(Asked, TEXT(", ")));
        return false;
    }

    if (Actors.Num() > 0)
    {
        GEditor->MoveViewportCamerasToActor(Actors, true);
    }
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/MCPBridge"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString OutputPath = Directory / Filename;
    FViewport* Viewport = GEditor->GetActiveViewport();
    if (Viewport == nullptr)
    {
        OutError = TEXT("No active editor viewport is available for capture.");
        return false;
    }
    GEditor->RedrawLevelEditingViewports();
    Viewport->Draw();
    const FIntPoint Size = Viewport->GetSizeXY();
    if (Size.X <= 0 || Size.Y <= 0)
    {
        OutError = TEXT("The active editor viewport has no drawable size.");
        return false;
    }

    TArray<FColor> Pixels;
    if (!Viewport->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
    {
        OutError = TEXT("Unable to read pixels from the active editor viewport.");
        return false;
    }
    for (FColor& Pixel : Pixels)
    {
        Pixel.A = 255;
    }
    TArray<uint8> Png;
    FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, Png);
    if (Png.Num() == 0 || !FFileHelper::SaveArrayToFile(Png, *OutputPath))
    {
        OutError = TEXT("Unable to write the viewport PNG.");
        return false;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("filepath"), OutputPath);
    Result->SetNumberField(TEXT("actor_count"), Actors.Num());
    Result->SetNumberField(TEXT("width"), Size.X);
    Result->SetNumberField(TEXT("height"), Size.Y);
    Result->SetNumberField(TEXT("file_size_bytes"), Png.Num());
    Result->SetStringField(TEXT("capture_method"), TEXT("active_viewport_read_pixels"));
    OutResultJson = SerializeJson(Result);
    return true;
}
