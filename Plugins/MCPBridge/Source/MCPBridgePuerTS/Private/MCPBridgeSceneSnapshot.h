// Copyright 2026 RareBird Games. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/TriggerBase.h"
#include "Engine/TriggerVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerStart.h"
#include "Json.h"
#include "Misc/SecureHash.h"

/**
 * The canonical read of an editor level, defined once and shared by the two
 * commands that need it: scene_inspect, which reports it, and scene_batch,
 * which compares it before and after a batch.
 *
 * There is one definition on purpose, for the same reason
 * MCPBridgeBlueprintMembers has one: a batch that measured convergence with a
 * different reader than the inspector a caller verifies with could report a
 * change the inspector cannot see.
 *
 * The shape and the spelling follow the existing inspectors rather than
 * inventing a second vocabulary: structure_hash_sha1 and structure_hash_basis
 * mean here what they mean in graph_inspect, behavior_tree_inspect and
 * widget_inspect, and identity_kind says whether an identity is observed or
 * derived.
 *
 * Actor identity is OBSERVED: an actor's object name is unique within its level
 * and does not drift, unlike its label, which a user renames freely and which
 * is not unique at all. So the object name is the id, the label is reported
 * beside it as data, and a selector that addresses by label has to survive
 * matching more than one actor.
 */
namespace MCPBridgeScene
{
    /** Fixed precision, so two reads of an unmoved actor produce the same text.
        Raw float printing lets the last bits of a transform decide a hash, and a
        hash that moves without the level changing is worse than no hash. Values
        within half of the last printed digit of zero are printed as zero, so a
        negative zero and a positive zero are the same actor. */
    inline FString FormatScalar(double Value)
    {
        return FString::Printf(TEXT("%.3f"), FMath::IsNearlyZero(Value, 0.0005) ? 0.0 : Value);
    }

    inline TSharedPtr<FJsonObject> VectorJson(const FVector& Value)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetNumberField(TEXT("x"), Value.X);
        Out->SetNumberField(TEXT("y"), Value.Y);
        Out->SetNumberField(TEXT("z"), Value.Z);
        return Out;
    }

    inline TSharedPtr<FJsonObject> RotatorJson(const FRotator& Value)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetNumberField(TEXT("pitch"), Value.Pitch);
        Out->SetNumberField(TEXT("yaw"), Value.Yaw);
        Out->SetNumberField(TEXT("roll"), Value.Roll);
        return Out;
    }

    /** A trigger volume for the purposes of the PlayerStart rule in AGENTS.md.
        ATriggerBase covers TriggerBox, TriggerSphere and TriggerCapsule;
        ATriggerVolume is the brush-based one and derives from AVolume instead,
        so neither test implies the other. */
    inline bool IsTriggerVolume(const AActor* Actor)
    {
        return Actor != nullptr
            && (Actor->IsA(ATriggerBase::StaticClass()) || Actor->IsA(ATriggerVolume::StaticClass()));
    }

    /** Every PlayerStart in the level, by name and world location. Read rather
        than assumed: the rule is about where the player actually appears. */
    inline TArray<TPair<FString, FVector>> PlayerStarts(UWorld* World)
    {
        TArray<TPair<FString, FVector>> Out;
        if (World == nullptr) { return Out; }
        for (TActorIterator<APlayerStart> It(World); It; ++It)
        {
            APlayerStart* Start = *It;
            if (IsValid(Start) && !Start->IsPendingKill())
            {
                Out.Add(TPair<FString, FVector>(Start->GetName(), Start->GetActorLocation()));
            }
        }
        Out.Sort([](const TPair<FString, FVector>& A, const TPair<FString, FVector>& B)
        {
            return A.Key < B.Key;
        });
        return Out;
    }

    /** Every actor of the current editor level, in canonical order.
        Sorted by object name, so the array describes the level rather than the
        order Unreal happens to iterate it in. */
    inline TArray<AActor*> SortedLevelActors(UWorld* World)
    {
        TArray<AActor*> Actors;
        if (World == nullptr) { return Actors; }
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (IsValid(Actor) && !Actor->IsPendingKill())
            {
                Actors.Add(Actor);
            }
        }
        Actors.Sort([](const AActor& A, const AActor& B) { return A.GetName() < B.GetName(); });
        return Actors;
    }

    /** The components of one actor in canonical order, by object name. */
    inline TArray<UActorComponent*> SortedComponents(const AActor* Actor)
    {
        TArray<UActorComponent*> Out;
        if (Actor == nullptr) { return Out; }
        for (UActorComponent* Component : Actor->GetComponents())
        {
            if (IsValid(Component) && !Component->IsPendingKill())
            {
                Out.Add(Component);
            }
        }
        Out.Sort([](const UActorComponent& A, const UActorComponent& B)
        {
            return A.GetName() < B.GetName();
        });
        return Out;
    }

    inline TSharedPtr<FJsonObject> ComponentJson(UActorComponent* Component)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("name"), Component->GetName());
        Out->SetStringField(TEXT("class"), Component->GetClass()->GetName());
        Out->SetStringField(TEXT("class_path"), Component->GetClass()->GetPathName());
        const USceneComponent* Scene = Cast<USceneComponent>(Component);
        if (Scene != nullptr)
        {
            Out->SetObjectField(TEXT("relative_location"), VectorJson(Scene->GetRelativeLocation()));
            Out->SetObjectField(TEXT("relative_rotation"), RotatorJson(Scene->GetRelativeRotation()));
            Out->SetObjectField(TEXT("relative_scale"), VectorJson(Scene->GetRelativeScale3D()));
            const USceneComponent* Parent = Scene->GetAttachParent();
            Out->SetStringField(TEXT("attach_parent"), Parent != nullptr ? Parent->GetName() : FString());
            Out->SetStringField(TEXT("mobility"),
                Scene->Mobility == EComponentMobility::Static ? TEXT("Static")
                : Scene->Mobility == EComponentMobility::Stationary ? TEXT("Stationary")
                : TEXT("Movable"));
        }
        return Out;
    }

    /** One actor as the inspector reports it. Transforms are WORLD transforms,
        which is what a caller who is placing things means and what scene_batch
        writes, so the read and the write speak the same coordinate space. */
    inline TSharedPtr<FJsonObject> ActorJson(AActor* Actor)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("id"), Actor->GetName());
        Out->SetStringField(TEXT("identity_kind"), TEXT("observed"));
        Out->SetStringField(TEXT("path"), Actor->GetPathName());
        Out->SetStringField(TEXT("label"), Actor->GetActorLabel());
        Out->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
        Out->SetStringField(TEXT("class_path"), Actor->GetClass()->GetPathName());
        Out->SetStringField(TEXT("folder"), Actor->GetFolderPath().ToString());
        Out->SetBoolField(TEXT("is_trigger_volume"), IsTriggerVolume(Actor));

        TArray<TSharedPtr<FJsonValue>> Tags;
        for (const FName& Tag : Actor->Tags) { Tags.Add(MakeShared<FJsonValueString>(Tag.ToString())); }
        Out->SetArrayField(TEXT("tags"), Tags);

        TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
        Transform->SetObjectField(TEXT("location"), VectorJson(Actor->GetActorLocation()));
        Transform->SetObjectField(TEXT("rotation"), RotatorJson(Actor->GetActorRotation()));
        Transform->SetObjectField(TEXT("scale"), VectorJson(Actor->GetActorScale3D()));
        Out->SetObjectField(TEXT("transform"), Transform);

        FVector Origin = FVector::ZeroVector;
        FVector Extent = FVector::ZeroVector;
        Actor->GetActorBounds(false, Origin, Extent);
        TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
        Bounds->SetObjectField(TEXT("origin"), VectorJson(Origin));
        Bounds->SetObjectField(TEXT("extent"), VectorJson(Extent));
        Out->SetObjectField(TEXT("bounds"), Bounds);

        const AActor* AttachParent = Actor->GetAttachParentActor();
        Out->SetStringField(TEXT("attach_parent"), AttachParent != nullptr ? AttachParent->GetName() : FString());
        Out->SetStringField(TEXT("attach_socket"), Actor->GetAttachParentSocketName().ToString());

        TArray<TSharedPtr<FJsonValue>> Components;
        for (UActorComponent* Component : SortedComponents(Actor))
        {
            Components.Add(MakeShared<FJsonValueObject>(ComponentJson(Component)));
        }
        Out->SetArrayField(TEXT("components"), Components);
        return Out;
    }

    /**
     * The one line that stands for an actor in the hash.
     *
     * Bounds are deliberately absent: they are derived from the transform and
     * from asset content, so including them would make an unrelated mesh reimport
     * read as a scene change. Everything the batch can author is present, so a
     * batch that reports a change and moves nothing is caught by the hash.
     */
    inline FString StructuralKeyOf(AActor* Actor)
    {
        TArray<FString> TagParts;
        for (const FName& Tag : Actor->Tags) { TagParts.Add(Tag.ToString()); }

        TArray<FString> ComponentParts;
        for (UActorComponent* Component : SortedComponents(Actor))
        {
            const USceneComponent* Scene = Cast<USceneComponent>(Component);
            const FVector RelLocation = Scene != nullptr ? Scene->GetRelativeLocation() : FVector::ZeroVector;
            const FRotator RelRotation = Scene != nullptr ? Scene->GetRelativeRotation() : FRotator::ZeroRotator;
            const FVector RelScale = Scene != nullptr ? Scene->GetRelativeScale3D() : FVector::OneVector;
            const USceneComponent* Parent = Scene != nullptr ? Scene->GetAttachParent() : nullptr;
            ComponentParts.Add(FString::Printf(TEXT("%s:%s:%s:%s,%s,%s:%s,%s,%s:%s,%s,%s"),
                *Component->GetName(), *Component->GetClass()->GetPathName(),
                Parent != nullptr ? *Parent->GetName() : TEXT(""),
                *FormatScalar(RelLocation.X), *FormatScalar(RelLocation.Y), *FormatScalar(RelLocation.Z),
                *FormatScalar(RelRotation.Pitch), *FormatScalar(RelRotation.Yaw), *FormatScalar(RelRotation.Roll),
                *FormatScalar(RelScale.X), *FormatScalar(RelScale.Y), *FormatScalar(RelScale.Z)));
        }

        const FVector Location = Actor->GetActorLocation();
        const FRotator Rotation = Actor->GetActorRotation();
        const FVector Scale = Actor->GetActorScale3D();
        const AActor* AttachParent = Actor->GetAttachParentActor();
        return FString::Printf(TEXT("%s|%s|%s|%s|[%s]|%s,%s,%s|%s,%s,%s|%s,%s,%s|%s@%s|[%s]"),
            *Actor->GetName(), *Actor->GetClass()->GetPathName(), *Actor->GetActorLabel(),
            *Actor->GetFolderPath().ToString(), *FString::Join(TagParts, TEXT(",")),
            *FormatScalar(Location.X), *FormatScalar(Location.Y), *FormatScalar(Location.Z),
            *FormatScalar(Rotation.Pitch), *FormatScalar(Rotation.Yaw), *FormatScalar(Rotation.Roll),
            *FormatScalar(Scale.X), *FormatScalar(Scale.Y), *FormatScalar(Scale.Z),
            AttachParent != nullptr ? *AttachParent->GetName() : TEXT(""),
            *Actor->GetAttachParentSocketName().ToString(),
            *FString::Join(ComponentParts, TEXT(";")));
    }

    /**
     * SHA-1 over the canonical text, in the same shape and spelling the graph,
     * Behavior Tree, Widget and Blueprint member inspectors already use.
     *
     * Always computed over the WHOLE level, never over a filtered subset. A hash
     * of a filter is a hash of the request, and comparing two of them before and
     * after a batch would report convergence for a level the batch also changed
     * somewhere the filter did not look.
     */
    inline FString StructureHash(UWorld* World)
    {
        TArray<FString> Lines;
        for (AActor* Actor : SortedLevelActors(World)) { Lines.Add(StructuralKeyOf(Actor)); }
        const FString Canonical = FString::Printf(TEXT("actors:\n%s\n"), *FString::Join(Lines, TEXT("\n")));

        uint8 Digest[FSHA1::DigestSize];
        const FTCHARToUTF8 Utf8(*Canonical);
        FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Digest);
        return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
    }

    inline const TCHAR* StructureHashBasis()
    {
        return TEXT("per actor: object name, class path, label, folder, tags, world location, "
                    "rotation and scale to three decimals, attach parent and socket, and every "
                    "component's name, class path, attach parent and relative transform; sorted "
                    "by object name. Bounds and reflected property values are deliberately "
                    "excluded: bounds are derived from asset content, and property values are "
                    "verified per operation rather than folded into one number");
    }

    /** The level package the editor world currently has loaded, as a package
        path. Empty when there is no world, which is a refusal rather than an
        empty success. */
    inline FString LevelPackagePath(UWorld* World)
    {
        return World != nullptr ? World->GetOutermost()->GetName() : FString();
    }

    inline UWorld* EditorWorld()
    {
        return GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    }
}
