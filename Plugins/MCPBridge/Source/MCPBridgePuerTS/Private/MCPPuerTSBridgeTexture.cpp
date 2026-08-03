// Copyright 2026 RareBird Games. All Rights Reserved.
//
// Produce a UTexture2D asset so a texture parameter can actually be set.
//
// The gap this closes: material_instance_build accepts a `textures` map and
// material_build accepts a texture parameter with a default, and until now
// nothing in the catalog could produce a texture to put in either.
// docs/GAP_AUDIT-2026-07-29.md recorded it as "no texture tools at all".
//
// GENERATED, not imported from disk. A solid colour or a checker is enough to
// drive a texture parameter, needs no asset on disk, and is reproducible from
// the spec alone, which is what makes the command convergent: the same spec
// produces byte-identical source data, so a rerun compares equal and writes
// nothing.
//
// File import is deliberately absent rather than half-built. Reading an
// arbitrary path off disk needs an allowed-root policy, and this bridge has
// exactly one such policy today (engine_source_read's engine root) which is for
// reading engine source, not for importing user content. Inventing a second
// filesystem trust root is a security decision that belongs with the integrator,
// not with a lane. A spec that names source_file is refused by name and says so.
//
// ponytail: generation only. Add UTextureFactory binary import when an allowed
// import root exists and is reviewed, and validate against it the way
// engine_source_read validates.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Json.h"
#include "MCPBridgeAssetRollback.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace MCPTextureBuild
{
    /** The compression settings a caller may choose. Named rather than resolved
        through the UEnum because the enum carries a dozen entries that only make
        sense for imported content, and a table is where to say which four this
        command supports. */
    struct FCompressionChoice
    {
        const TCHAR* Name;
        TextureCompressionSettings Value;
        /** What SRGB defaults to for this choice. A normal map or a mask read
            through sRGB is wrong, and a caller asking for "Normalmap" is not
            also asking to think about gamma. */
        bool bDefaultSRGB;
    };

    inline const TArray<FCompressionChoice>& CompressionChoices()
    {
        static const TArray<FCompressionChoice> Choices = {
            { TEXT("Default"),   TC_Default,        true },
            { TEXT("Normalmap"), TC_Normalmap,      false },
            { TEXT("Masks"),     TC_Masks,          false },
            { TEXT("Grayscale"), TC_Grayscale,      false },
        };
        return Choices;
    }

    bool ReadColor(
        const TSharedPtr<FJsonObject>& Spec,
        const TCHAR* Field,
        const FLinearColor& Fallback,
        FLinearColor& OutColor,
        TArray<FString>& OutErrors)
    {
        OutColor = Fallback;
        if (!Spec->HasField(Field)) { return true; }
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!Spec->TryGetObjectField(Field, Object) || Object == nullptr)
        {
            OutErrors.Add(FString::Printf(
                TEXT("%s must be an object with r, g, b and optional a, each 0..1."), Field));
            return false;
        }
        double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
        (*Object)->TryGetNumberField(TEXT("r"), R);
        (*Object)->TryGetNumberField(TEXT("g"), G);
        (*Object)->TryGetNumberField(TEXT("b"), B);
        (*Object)->TryGetNumberField(TEXT("a"), A);
        OutColor = FLinearColor(
            static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
        return true;
    }

    /** The BGRA8 source bytes for the requested pattern, top row first.
        TSF_BGRA8 is the format FImageUtils::CreateTexture2D uses for the same
        job (ImageUtils.cpp), so this stays on the path the editor's own
        colour-buffer textures take. */
    void FillPixels(
        TArray<uint8>& Out,
        int32 Width,
        int32 Height,
        bool bChecker,
        int32 CheckerSize,
        const FColor& A,
        const FColor& B)
    {
        Out.SetNumUninitialized(Width * Height * 4);
        uint8* Write = Out.GetData();
        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                const bool bSecond = bChecker
                    && (((X / CheckerSize) + (Y / CheckerSize)) % 2) == 1;
                const FColor& Color = bSecond ? B : A;
                *Write++ = Color.B;
                *Write++ = Color.G;
                *Write++ = Color.R;
                *Write++ = Color.A;
            }
        }
    }
}

using namespace MCPTextureBuild;

bool UMCPPuerTSBridgeService::ImportTextureJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Texture spec must be a JSON object.");
        return false;
    }

    FString SourceFile;
    if (Spec->TryGetStringField(TEXT("source_file"), SourceFile)
        && !SourceFile.TrimStartAndEnd().IsEmpty())
    {
        OutError = TEXT(
            "source_file is not supported: this command generates a texture and does not read "
            "one off disk. Importing an arbitrary path needs an allowed import root that this "
            "bridge does not define yet, and guessing one would be a filesystem escape. Use "
            "pattern \"solid\" or \"checker\" with color and color_b instead.");
        return false;
    }

    FString AssetPath;
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    AssetPath = AssetPath.TrimStartAndEnd();
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Textures are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    TArray<FString> Errors;

    // --- Size. Powers of two only, and capped: a texture is generated on the
    //     game thread and 8192x8192 would be a 256 MB allocation and a visible
    //     editor stall for a placeholder. ---
    double WidthNumber = 256.0;
    double HeightNumber = 0.0;
    Spec->TryGetNumberField(TEXT("width"), WidthNumber);
    const int32 Width = static_cast<int32>(WidthNumber);
    const int32 Height = Spec->TryGetNumberField(TEXT("height"), HeightNumber)
        ? static_cast<int32>(HeightNumber) : Width;
    auto IsPowerOfTwoInRange = [](int32 Value) -> bool
    {
        return Value >= 4 && Value <= 2048 && (Value & (Value - 1)) == 0;
    };
    if (!IsPowerOfTwoInRange(Width) || !IsPowerOfTwoInRange(Height))
    {
        Errors.Add(FString::Printf(
            TEXT("width and height must each be a power of two between 4 and 2048; got %d x %d."),
            Width, Height));
    }

    // --- Pattern. ---
    FString Pattern;
    Spec->TryGetStringField(TEXT("pattern"), Pattern);
    Pattern = Pattern.TrimStartAndEnd();
    if (Pattern.IsEmpty()) { Pattern = TEXT("solid"); }
    const bool bChecker = Pattern.Equals(TEXT("checker"), ESearchCase::IgnoreCase);
    if (!bChecker && !Pattern.Equals(TEXT("solid"), ESearchCase::IgnoreCase))
    {
        Errors.Add(FString::Printf(
            TEXT("pattern '%s' is not one of: solid, checker."), *Pattern));
    }

    double CheckerNumber = 32.0;
    Spec->TryGetNumberField(TEXT("checker_size"), CheckerNumber);
    const int32 CheckerSize = static_cast<int32>(CheckerNumber);
    if (bChecker && (CheckerSize < 1 || CheckerSize > FMath::Min(Width, Height)))
    {
        Errors.Add(FString::Printf(
            TEXT("checker_size must be between 1 and the smaller of width and height (%d); got %d."),
            FMath::Min(Width, Height), CheckerSize));
    }

    FLinearColor ColorA, ColorB;
    ReadColor(Spec, TEXT("color"), FLinearColor::White, ColorA, Errors);
    ReadColor(Spec, TEXT("color_b"), FLinearColor(0.25f, 0.25f, 0.25f, 1.0f), ColorB, Errors);

    // --- Compression and sRGB. ---
    FString CompressionText;
    Spec->TryGetStringField(TEXT("compression"), CompressionText);
    CompressionText = CompressionText.TrimStartAndEnd();
    if (CompressionText.IsEmpty()) { CompressionText = TEXT("Default"); }
    const FCompressionChoice* Compression = nullptr;
    for (const FCompressionChoice& Choice : CompressionChoices())
    {
        if (CompressionText.Equals(Choice.Name, ESearchCase::IgnoreCase)) { Compression = &Choice; break; }
    }
    if (Compression == nullptr)
    {
        TArray<FString> Names;
        for (const FCompressionChoice& Choice : CompressionChoices()) { Names.Add(Choice.Name); }
        Errors.Add(FString::Printf(
            TEXT("compression '%s' is not one of: %s."),
            *CompressionText, *FString::Join(Names, TEXT(", "))));
    }
    const bool bSRGB = Spec->HasTypedField<EJson::Boolean>(TEXT("srgb"))
        ? Spec->GetBoolField(TEXT("srgb"))
        : (Compression != nullptr ? Compression->bDefaultSRGB : true);
    const bool bSave = !Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        || Spec->GetBoolField(TEXT("save"));
    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));

    if (Errors.Num() > 0)
    {
        OutError = FString::Join(Errors, TEXT(" "));
        return false;
    }

    // --- Resolve the existing asset. ---
    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = AssetPath + TEXT(".") + AssetName;
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData ExistingData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath));
    UTexture2D* Texture = nullptr;
    if (ExistingData.IsValid())
    {
        Texture = Cast<UTexture2D>(ExistingData.GetAsset());
        if (Texture == nullptr)
        {
            OutError = FString::Printf(
                TEXT("The asset at '%s' is a %s, not a Texture2D. Choose a different asset_path "
                     "rather than overwriting it."),
                *ObjectPath, *ExistingData.AssetClass.ToString());
            return false;
        }
    }
    const bool bCreating = Texture == nullptr;

    // sRGB is an encoding of the source bytes, not a flag applied afterwards, so
    // the conversion has to know about it: ToFColor(true) writes sRGB-encoded
    // bytes, ToFColor(false) writes the linear values unchanged.
    const FColor QuantizedA = ColorA.ToFColor(bSRGB);
    const FColor QuantizedB = ColorB.ToFColor(bSRGB);
    TArray<uint8> Pixels;
    FillPixels(Pixels, Width, Height, bChecker, CheckerSize, QuantizedA, QuantizedB);

    // --- Convergence. The same spec produces the same bytes, so an asset that
    //     already carries them is left completely alone: not written, not
    //     dirtied, not saved. ---
    bool bUnchanged = false;
    if (!bCreating)
    {
        const bool bSettingsMatch =
            Texture->Source.IsValid()
            && Texture->Source.GetSizeX() == Width
            && Texture->Source.GetSizeY() == Height
            && Texture->Source.GetFormat() == TSF_BGRA8
            && Texture->SRGB == bSRGB
            && Texture->CompressionSettings == Compression->Value;
        if (bSettingsMatch)
        {
            TArray64<uint8> Existing;
            bUnchanged = Texture->Source.GetMipData(Existing, 0)
                && Existing.Num() == Pixels.Num()
                && FMemory::Memcmp(Existing.GetData(), Pixels.GetData(), Pixels.Num()) == 0;
        }
    }

    auto BuildBaseResult = [&]() -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), AssetPath);
        Result->SetNumberField(TEXT("width"), Width);
        Result->SetNumberField(TEXT("height"), Height);
        Result->SetStringField(TEXT("format"), TEXT("BGRA8"));
        Result->SetStringField(TEXT("pattern"), bChecker ? TEXT("checker") : TEXT("solid"));
        Result->SetStringField(TEXT("compression"), Compression->Name);
        Result->SetBoolField(TEXT("srgb"), bSRGB);
        return Result;
    };

    if (bPlanOnly)
    {
        TSharedPtr<FJsonObject> Result = BuildBaseResult();
        Result->SetBoolField(TEXT("plan_only"), true);
        Result->SetBoolField(TEXT("would_create"), bCreating);
        Result->SetBoolField(TEXT("unchanged"), bUnchanged);
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        OutResultJson = SerializeJson(Result);
        return true;
    }

    if (bUnchanged)
    {
        TSharedPtr<FJsonObject> Result = BuildBaseResult();
        Result->SetBoolField(TEXT("created"), false);
        Result->SetBoolField(TEXT("unchanged"), true);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetStringField(TEXT("object_path"), Texture->GetPathName());
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        OutResultJson = SerializeJson(Result);
        return true;
    }

    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Texture authoring requires an active transaction.");
        return false;
    }

    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    bool bRollbackSucceeded = false;
    auto FailWithRollback = [&](const FString& Error) -> bool
    {
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }
        Rollback.Rollback();
        bRollbackSucceeded = bCreating
            ? !AssetRegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath)).IsValid()
            : Texture != nullptr && Texture->Source.IsValid();
        OutError = bRollbackSucceeded
            ? Error
            : FString::Printf(
                TEXT("%s The rollback did NOT restore the texture. Treat '%s' as damaged."),
                *Error, *AssetPath);
        return false;
    };

    if (bCreating)
    {
        UPackage* Package = CreatePackage(*AssetPath);
        if (Package == nullptr)
        {
            return FailWithRollback(FString::Printf(
                TEXT("Unreal could not create the package '%s'."), *AssetPath));
        }
        Texture = NewObject<UTexture2D>(
            Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
        if (Texture == nullptr)
        {
            return FailWithRollback(TEXT("Unreal could not create the texture."));
        }
        Rollback.TrackCreated(Texture);
        AssetRegistryModule.Get().AssetCreated(Texture);
    }

    // Checked, for the same reason material_build checks it: a Modify() whose
    // false return is discarded is a rollback that silently does nothing
    // (docs/CAPABILITY_FINDINGS.md finding 0r). Nothing has been written yet.
    if (!Texture->Modify())
    {
        return FailWithRollback(FString::Printf(
            TEXT("UTexture2D::Modify() returned false for '%s', so this write would land outside "
                 "the undo record and could not be rolled back. Refusing before writing anything."),
            *AssetPath));
    }

    // One mip, no chain: the platform mip chain is built by PostEditChange from
    // MipGenSettings, and generating a source chain here would only give the
    // compressor bytes it is about to recompute.
    Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, Pixels.GetData());
    Texture->SRGB = bSRGB;
    Texture->CompressionSettings = Compression->Value;
    Texture->CompressionNoAlpha = false;
    Texture->MipGenSettings = TMGS_FromTextureGroup;
    Texture->PostEditChange();
    Texture->MarkPackageDirty();

    // --- Independent read-back. The write is believed only after the source
    //     reports back what was asked for. ---
    TArray<FString> Mismatches;
    if (!Texture->Source.IsValid())
    {
        Mismatches.Add(TEXT("the texture has no valid source data after the write"));
    }
    else
    {
        if (Texture->Source.GetSizeX() != Width || Texture->Source.GetSizeY() != Height)
        {
            Mismatches.Add(FString::Printf(
                TEXT("the source is %d x %d, not %d x %d"),
                Texture->Source.GetSizeX(), Texture->Source.GetSizeY(), Width, Height));
        }
        TArray64<uint8> ReadBack;
        if (!Texture->Source.GetMipData(ReadBack, 0)
            || ReadBack.Num() != Pixels.Num()
            || FMemory::Memcmp(ReadBack.GetData(), Pixels.GetData(), Pixels.Num()) != 0)
        {
            Mismatches.Add(TEXT("the source pixels do not read back as the pattern that was written"));
        }
    }
    if (Texture->SRGB != bSRGB)
    {
        Mismatches.Add(TEXT("SRGB did not take the requested value"));
    }
    if (Texture->CompressionSettings != Compression->Value)
    {
        Mismatches.Add(TEXT("CompressionSettings did not take the requested value"));
    }
    if (Mismatches.Num() > 0)
    {
        return FailWithRollback(FString::Printf(
            TEXT("The texture was written but the read-back disagrees: %s. Nothing was saved and "
                 "the transaction was cancelled."),
            *FString::Join(Mismatches, TEXT(", "))));
    }

    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(AssetPath, SaveError);
        if (!bSaved)
        {
            return FailWithRollback(FString::Printf(
                TEXT("The texture was built and verified but could not be saved: %s"), *SaveError));
        }
    }

    Rollback.Commit();

    TSharedPtr<FJsonObject> Result = BuildBaseResult();
    Result->SetBoolField(TEXT("created"), bCreating);
    Result->SetBoolField(TEXT("unchanged"), false);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetStringField(TEXT("object_path"), Texture->GetPathName());
    Result->SetObjectField(TEXT("rollback"),
        Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
    Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
    OutResultJson = SerializeJson(Result);
    return true;
}
