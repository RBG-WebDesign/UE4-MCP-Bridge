// Copyright 2026 RareBird Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

class FBPFunctionOps
{
public:
    /**
     * Create a user function graph with typed parameters. Returns the graph name on success
     * (matches FunctionName arg unless UE rewrites it for uniqueness — shouldn't happen given
     * the pre-transaction collision check).
     */
    static FString AddFunction(UBlueprint* Blueprint, const FString& FunctionName, const FString& InputsJson, const FString& OutputsJson);

    /** Remove a named user function graph. Returns true on success. */
    static bool RemoveFunction(UBlueprint* Blueprint, const FString& FunctionName);

    /**
     * Converge a function's declared metadata on MetadataJson.
     *
     * MetadataJson is an object; only the keys PRESENT are written, so patching
     * a tooltip does not silently clear a category. Understood keys:
     * category (string), tooltip (string), pure (bool), const (bool),
     * access ("public" | "protected" | "private"). An unknown key is an error
     * rather than a shrug, because a misspelled key that is quietly ignored
     * reads to the caller as a metadata write that succeeded.
     */
    static bool SetFunctionMetadata(UBlueprint* Blueprint, const FString& FunctionName,
        const FString& MetadataJson, FString& OutError);

    /**
     * Converge a function's parameters on the desired arrays.
     *
     * Each element is {name, type: TypeDescriptor, default?: string,
     * rename_from?: string}. An EMPTY json string leaves that direction alone;
     * a present array is authoritative, so a parameter it does not name is
     * removed. Order in the array becomes parameter order.
     *
     * rename_from is the difference between renaming a parameter and replacing
     * it. Without it, "Multiplier" becoming "Scale" is indistinguishable from
     * a remove plus an add, and the caller loses every call site's connection
     * to that pin. With it, the rename goes through
     * UK2Node::RenameUserDefinedPin, which is what the editor's own rename
     * does.
     */
    static bool SetFunctionParameters(UBlueprint* Blueprint, const FString& FunctionName,
        const FString& InputsJson, const FString& OutputsJson, FString& OutError);

    /**
     * Converge a function's local variables on LocalsJson.
     *
     * Elements are {name, type: TypeDescriptor, default?: string}. A present
     * array is authoritative. Locals live on the function entry node, not in
     * Blueprint::NewVariables, so nothing that walks the Blueprint's member
     * variables can reach them.
     */
    static bool SetFunctionLocals(UBlueprint* Blueprint, const FString& FunctionName,
        const FString& LocalsJson, FString& OutError);
};
