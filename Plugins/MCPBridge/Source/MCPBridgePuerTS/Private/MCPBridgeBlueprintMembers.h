// Copyright 2026 RareBird Games. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BlueprintInspectorLibrary.h"
#include "Engine/Blueprint.h"
#include "Json.h"
#include "Misc/SecureHash.h"

/**
 * The member half of a Blueprint, read once and shared by the two commands that
 * need it: graph_inspect, which reports it, and blueprint_member_patch, which
 * compares it before and after a batch.
 *
 * There is one definition on purpose. A patch that measured convergence with a
 * different reader than the inspector a caller verifies with would be able to
 * report a change that the inspector cannot see, which is exactly the false
 * positive findings 0g was fixed to remove.
 *
 * The readers themselves are UBlueprintInspectorLibrary's. What this adds is
 * canonical ordering, so two reads of an unchanged Blueprint hash the same, and
 * event dispatchers, which the library could already read and nothing called.
 */
namespace MCPBridgeBlueprintMembers
{
    /** Parse a reader's JSON string. The readers answer with a bare array on
        success and an object holding "error" when they cannot do the job; both
        shapes are returned as-is so an error survives into the response rather
        than being silently dropped. */
    inline TSharedPtr<FJsonValue> ParseReaderJson(const FString& Json)
    {
        TSharedPtr<FJsonValue> Value;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Value) || !Value.IsValid())
        {
            return MakeShared<FJsonValueNull>();
        }
        return Value;
    }

    /** Sort an array of objects by one of their string fields.

        Canonical order is the point. Unreal's own array order for variables,
        components and graphs is the order they happen to be stored in, so a
        reader that passed it through would answer differently after an
        unrelated edit reordered them, and a hash over that answer would move
        without the Blueprint changing. */
    inline void SortByStringField(TArray<TSharedPtr<FJsonValue>>& Values, const TCHAR* Field)
    {
        Values.Sort([Field](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
        {
            const TSharedPtr<FJsonObject>* ObjectA = nullptr;
            const TSharedPtr<FJsonObject>* ObjectB = nullptr;
            const FString KeyA = A.IsValid() && A->TryGetObject(ObjectA)
                ? (*ObjectA)->GetStringField(Field) : FString();
            const FString KeyB = B.IsValid() && B->TryGetObject(ObjectB)
                ? (*ObjectB)->GetStringField(Field) : FString();
            return KeyA < KeyB;
        });
    }

    /** Read one array-returning reader, sorted. Anything that is not an array
        (an error object) is passed through untouched. */
    inline TSharedPtr<FJsonValue> SortedReaderArray(const FString& Json, const TCHAR* Field)
    {
        TSharedPtr<FJsonValue> Value = ParseReaderJson(Json);
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Value.IsValid() && Value->TryGetArray(Array))
        {
            TArray<TSharedPtr<FJsonValue>> Sorted = *Array;
            SortByStringField(Sorted, Field);
            return MakeShared<FJsonValueArray>(Sorted);
        }
        return Value;
    }

    /** The six member collections a Blueprint carries outside its graphs,
        canonically ordered: components, variables, interfaces, functions and
        event dispatchers. Read only; nothing here calls Modify or a compile. */
    inline TSharedPtr<FJsonObject> BuildSnapshot(UBlueprint* Blueprint)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetField(TEXT("components"),
            SortedReaderArray(UBlueprintInspectorLibrary::ListSCSNodes(Blueprint), TEXT("name")));
        Out->SetField(TEXT("variables"),
            SortedReaderArray(UBlueprintInspectorLibrary::ListVariables(Blueprint), TEXT("name")));
        Out->SetField(TEXT("interfaces"),
            SortedReaderArray(UBlueprintInspectorLibrary::ListInterfaces(Blueprint), TEXT("path")));
        Out->SetField(TEXT("functions"),
            SortedReaderArray(UBlueprintInspectorLibrary::ListFunctions(Blueprint), TEXT("name")));
        Out->SetField(TEXT("macros"),
            SortedReaderArray(UBlueprintInspectorLibrary::ListMacros(Blueprint), TEXT("name")));
        Out->SetField(TEXT("event_dispatchers"),
            SortedReaderArray(UBlueprintInspectorLibrary::ListEventDispatchers(Blueprint), TEXT("name")));
        return Out;
    }

    /**
     * SHA-1 over the serialized snapshot, in the same shape and spelling the
     * graph, Behavior Tree and Widget inspectors already use.
     *
     * The arrays are canonically ordered, so the hash is stable across reads.
     * Object KEY order inside each element is the JSON serializer's, which is
     * deterministic for a fixed key set within one binary but is not a
     * cross-version guarantee: compare hashes produced by the same build, and
     * compare the snapshot itself when you need to compare across builds.
     */
    inline FString StructureHash(const TSharedPtr<FJsonObject>& Snapshot)
    {
        if (!Snapshot.IsValid()) { return FString(); }
        FString Serialized;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
        FJsonSerializer::Serialize(Snapshot.ToSharedRef(), Writer);

        uint8 Digest[FSHA1::DigestSize];
        const FTCHARToUTF8 Utf8(*Serialized);
        FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Digest);
        return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
    }
}
