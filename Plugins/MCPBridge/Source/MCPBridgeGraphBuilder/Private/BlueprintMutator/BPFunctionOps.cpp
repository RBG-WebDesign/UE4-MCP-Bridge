// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPFunctionOps.h"
#include "BPMutatorHelpers.h"
#include "BPGLogCategories.h"
#include "BlueprintInspector/BPTypeDescriptor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "BPFunctionOps"

namespace
{
    struct FParamSpec
    {
        FString Name;
        FEdGraphPinType Type;
    };

    /**
     * Parse `[{name, type: TypeDescriptor}]` JSON array into a typed spec list.
     * Returns true on success even if the array is empty. Logs at Warning and
     * returns false on any malformed element.
     */
    bool ParseParamArray(const FString& Json, TArray<FParamSpec>& OutParams, const TCHAR* Ctx)
    {
        OutParams.Reset();
        if (Json.IsEmpty() || Json == TEXT("[]")) return true;

        TSharedPtr<FJsonValue> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: failed to parse params JSON: %s"), Ctx, *Json);
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
        if (!Root->TryGetArray(ArrPtr))
        {
            UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: expected JSON array, got %s"), Ctx, *Json);
            return false;
        }

        for (const TSharedPtr<FJsonValue>& Elem : *ArrPtr)
        {
            const TSharedPtr<FJsonObject>* Obj = nullptr;
            if (!Elem->TryGetObject(Obj))
            {
                UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: expected param object"), Ctx);
                return false;
            }

            FParamSpec Spec;
            if (!(*Obj)->TryGetStringField(TEXT("name"), Spec.Name) || Spec.Name.IsEmpty())
            {
                UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: param missing 'name'"), Ctx);
                return false;
            }

            const TSharedPtr<FJsonObject>* TypeObj = nullptr;
            if (!(*Obj)->TryGetObjectField(TEXT("type"), TypeObj))
            {
                UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: param '%s' missing 'type'"), Ctx, *Spec.Name);
                return false;
            }

            FString TypeJsonStr;
            const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TypeJsonStr);
            FJsonSerializer::Serialize(TypeObj->ToSharedRef(), Writer);

            Spec.Type = FBPTypeDescriptor::FromJsonString(TypeJsonStr);
            if (Spec.Type.PinCategory.IsNone())
            {
                UE_LOG(LogBlueprintMutator, Warning, TEXT("%s: param '%s' type failed to parse: %s"), Ctx, *Spec.Name, *TypeJsonStr);
                return false;
            }
            OutParams.Add(MoveTemp(Spec));
        }
        return true;
    }

    /** Find a function graph by name — returns nullptr if not present. Case-sensitive compare on FName. */
    UEdGraph* FindFunctionGraph(UBlueprint* Blueprint, const FString& Name)
    {
        const FName N(*Name);
        for (UEdGraph* G : Blueprint->FunctionGraphs)
        {
            if (G && G->GetFName() == N) return G;
        }
        return nullptr;
    }

    /** The entry and result terminator nodes of a function graph. Result is
        legitimately null for a function with no outputs; entry never is on a
        well-formed graph, and a null entry is reported as an error by callers
        rather than worked around. */
    void FindTerminators(UEdGraph* Graph, UK2Node_FunctionEntry*& OutEntry, UK2Node_FunctionResult*& OutResult)
    {
        OutEntry = nullptr;
        OutResult = nullptr;
        if (!Graph) { return; }
        for (UEdGraphNode* N : Graph->Nodes)
        {
            if (!OutEntry)  OutEntry  = Cast<UK2Node_FunctionEntry>(N);
            if (!OutResult) OutResult = Cast<UK2Node_FunctionResult>(N);
        }
    }

    /** Resolve graph + entry node in one step, with the error text callers would
        otherwise each write their own version of. */
    bool ResolveFunction(UBlueprint* Blueprint, const FString& FunctionName, const TCHAR* Ctx,
        UEdGraph*& OutGraph, UK2Node_FunctionEntry*& OutEntry, UK2Node_FunctionResult*& OutResult,
        FString& OutError)
    {
        OutGraph = nullptr;
        OutEntry = nullptr;
        OutResult = nullptr;
        if (!Blueprint)
        {
            OutError = FString::Printf(TEXT("%s: null blueprint"), Ctx);
            return false;
        }
        OutGraph = FindFunctionGraph(Blueprint, FunctionName);
        if (!OutGraph)
        {
            OutError = FString::Printf(TEXT("%s: function '%s' not found"), Ctx, *FunctionName);
            return false;
        }
        FindTerminators(OutGraph, OutEntry, OutResult);
        if (!OutEntry)
        {
            OutError = FString::Printf(
                TEXT("%s: function '%s' has no entry node, so it has no signature to change"),
                Ctx, *FunctionName);
            return false;
        }
        return true;
    }

    /** Parse a JSON object out of text, reporting the failure rather than
        returning an empty object that reads as "the caller asked for nothing". */
    bool ParseJsonObject(const FString& Json, const TCHAR* Ctx, TSharedPtr<FJsonObject>& Out, FString& OutError)
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Out) || !Out.IsValid())
        {
            OutError = FString::Printf(TEXT("%s: expected a JSON object, got: %s"), Ctx, *Json);
            return false;
        }
        return true;
    }

    /**
     * Is this a pin category UE4.27 actually has?
     *
     * FBPTypeDescriptor::FromJson copies the category string into an FName
     * without checking it, which is right for a reader echoing back what a pin
     * already holds and wrong for a writer taking one from a caller: a typo
     * became a pin of a type that does not exist, the pin was created, and the
     * Blueprint compiled. This is the check that turns that into a refusal.
     */
    bool IsKnownPinCategory(const FName& Category)
    {
        static const TSet<FName> Known = {
            UEdGraphSchema_K2::PC_Boolean, UEdGraphSchema_K2::PC_Byte,
            UEdGraphSchema_K2::PC_Class, UEdGraphSchema_K2::PC_SoftClass,
            UEdGraphSchema_K2::PC_Int, UEdGraphSchema_K2::PC_Int64,
            UEdGraphSchema_K2::PC_Float, UEdGraphSchema_K2::PC_Name,
            UEdGraphSchema_K2::PC_Delegate, UEdGraphSchema_K2::PC_MCDelegate,
            UEdGraphSchema_K2::PC_Object, UEdGraphSchema_K2::PC_Interface,
            UEdGraphSchema_K2::PC_SoftObject, UEdGraphSchema_K2::PC_String,
            UEdGraphSchema_K2::PC_Text, UEdGraphSchema_K2::PC_Struct,
            UEdGraphSchema_K2::PC_Enum, UEdGraphSchema_K2::PC_FieldPath,
        };
        return Known.Contains(Category);
    }

    FString KnownPinCategoryList()
    {
        TArray<FString> Names = {
            UEdGraphSchema_K2::PC_Boolean.ToString(), UEdGraphSchema_K2::PC_Byte.ToString(),
            UEdGraphSchema_K2::PC_Class.ToString(), UEdGraphSchema_K2::PC_SoftClass.ToString(),
            UEdGraphSchema_K2::PC_Int.ToString(), UEdGraphSchema_K2::PC_Int64.ToString(),
            UEdGraphSchema_K2::PC_Float.ToString(), UEdGraphSchema_K2::PC_Name.ToString(),
            UEdGraphSchema_K2::PC_Delegate.ToString(), UEdGraphSchema_K2::PC_MCDelegate.ToString(),
            UEdGraphSchema_K2::PC_Object.ToString(), UEdGraphSchema_K2::PC_Interface.ToString(),
            UEdGraphSchema_K2::PC_SoftObject.ToString(), UEdGraphSchema_K2::PC_String.ToString(),
            UEdGraphSchema_K2::PC_Text.ToString(), UEdGraphSchema_K2::PC_Struct.ToString(),
            UEdGraphSchema_K2::PC_Enum.ToString(), UEdGraphSchema_K2::PC_FieldPath.ToString(),
        };
        Names.Sort();
        return FString::Join(Names, TEXT(", "));
    }

    /** One parameter of a desired signature. */
    struct FDesiredParam
    {
        FString Name;
        FString RenameFrom;
        FEdGraphPinType Type;
        bool bHasDefault = false;
        FString Default;
    };

    /** Parse `[{name, type, default?, rename_from?}]`. Reports the reason on
        failure; a signature the caller half-described is refused, never
        half-applied. */
    bool ParseDesiredParams(const FString& Json, const TCHAR* Ctx,
        TArray<FDesiredParam>& Out, FString& OutError)
    {
        Out.Reset();
        if (Json.IsEmpty()) { return true; }

        TSharedPtr<FJsonValue> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            OutError = FString::Printf(TEXT("%s: failed to parse JSON: %s"), Ctx, *Json);
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (!Root->TryGetArray(Array))
        {
            OutError = FString::Printf(TEXT("%s: expected a JSON array, got: %s"), Ctx, *Json);
            return false;
        }

        TSet<FString> SeenNames;
        for (const TSharedPtr<FJsonValue>& Element : *Array)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Element->TryGetObject(Object))
            {
                OutError = FString::Printf(TEXT("%s: every element must be an object"), Ctx);
                return false;
            }

            FDesiredParam Param;
            if (!(*Object)->TryGetStringField(TEXT("name"), Param.Name) || Param.Name.IsEmpty())
            {
                OutError = FString::Printf(TEXT("%s: a parameter is missing 'name'"), Ctx);
                return false;
            }
            if (SeenNames.Contains(Param.Name))
            {
                OutError = FString::Printf(
                    TEXT("%s: parameter '%s' is listed twice, so there is no single desired signature"),
                    Ctx, *Param.Name);
                return false;
            }
            SeenNames.Add(Param.Name);
            (*Object)->TryGetStringField(TEXT("rename_from"), Param.RenameFrom);

            const TSharedPtr<FJsonObject>* TypeObject = nullptr;
            if (!(*Object)->TryGetObjectField(TEXT("type"), TypeObject))
            {
                OutError = FString::Printf(
                    TEXT("%s: parameter '%s' is missing a 'type' descriptor"), Ctx, *Param.Name);
                return false;
            }
            FString TypeJson;
            const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TypeJson);
            FJsonSerializer::Serialize(TypeObject->ToSharedRef(), Writer);
            Param.Type = FBPTypeDescriptor::FromJsonString(TypeJson);
            if (Param.Type.PinCategory.IsNone())
            {
                OutError = FString::Printf(
                    TEXT("%s: parameter '%s' has a type this build cannot resolve: %s"),
                    Ctx, *Param.Name, *TypeJson);
                return false;
            }
            if (!IsKnownPinCategory(Param.Type.PinCategory))
            {
                OutError = FString::Printf(
                    TEXT("%s: parameter '%s' names the type category '%s', which UE4.27 does not have. "
                         "Categories: %s."),
                    Ctx, *Param.Name, *Param.Type.PinCategory.ToString(), *KnownPinCategoryList());
                return false;
            }

            FString DefaultValue;
            if ((*Object)->TryGetStringField(TEXT("default"), DefaultValue))
            {
                Param.bHasDefault = true;
                Param.Default = DefaultValue;
            }
            Out.Add(MoveTemp(Param));
        }
        return true;
    }

    /**
     * Every node whose pins have to be renamed alongside a parameter.
     *
     * A call site's pins are NOT rebuilt from the function until the Blueprint
     * recompiles, and reconstruction matches old pins to new ones BY NAME. So a
     * call site still holding a pin called "Multiplier" when the function now
     * declares "Scale" loses that pin's connections, silently. The editor
     * renames the call sites too, through this same helper
     * (BlueprintDetailsCustomization.cpp:4417-4452); this is that gather step.
     */
    struct FRenameTargetCollector : public FBasePinChangeHelper
    {
        TArray<UK2Node*> Nodes;
        virtual void EditCallSite(UK2Node_CallFunction* CallSite, UBlueprint*) override
        {
            // The cast is not decoration. TArray<UK2Node*>::AddUnique takes
            // UK2Node*&&, and binding a UK2Node_CallFunction* to it needs a
            // conversion MSVC will not make during overload resolution. A unity
            // build hid this for months; Sinfeld builds adaptive non-unity, so
            // the file compiles alone and C2664 surfaces. The two overrides
            // below already cast for the same reason.
            if (CallSite != nullptr) { Nodes.AddUnique((UK2Node*)CallSite); }
        }
        virtual void EditMacroInstance(UK2Node_MacroInstance* MacroInstance, UBlueprint*) override
        {
            if (MacroInstance != nullptr) { Nodes.AddUnique((UK2Node*)MacroInstance); }
        }
        virtual void EditCompositeTunnelNode(UK2Node_Tunnel* TunnelNode) override
        {
            if (TunnelNode != nullptr) { Nodes.AddUnique((UK2Node*)TunnelNode); }
        }
    };

    /** Find a user-defined pin by name on a terminator node. */
    TSharedPtr<FUserPinInfo> FindUserPin(UK2Node_EditablePinBase* Node, const FString& Name)
    {
        if (!Node) { return nullptr; }
        const FName Wanted(*Name);
        for (const TSharedPtr<FUserPinInfo>& Info : Node->UserDefinedPins)
        {
            if (Info.IsValid() && Info->PinName == Wanted) { return Info; }
        }
        return nullptr;
    }

    /**
     * Converge one terminator node's user-defined pins on the desired list.
     *
     * Order matters: renames first, so a rename is never mistaken for a remove
     * plus an add; then removals; then additions; then types, defaults and
     * finally parameter order.
     */
    bool ConvergePins(UBlueprint* Blueprint, UEdGraph* Graph, UK2Node_EditablePinBase* Node,
        const TArray<FDesiredParam>& Desired, EEdGraphPinDirection Direction,
        const TCHAR* Ctx, bool& bOutChanged, FString& OutError)
    {
        if (!Node)
        {
            OutError = FString::Printf(
                TEXT("%s: this function has no node to carry those parameters. A function with "
                     "outputs needs a result node; one is created with the outputs."), Ctx);
            return false;
        }

        Node->Modify();

        for (const FDesiredParam& Param : Desired)
        {
            if (Param.RenameFrom.IsEmpty() || Param.RenameFrom == Param.Name) { continue; }
            if (FindUserPin(Node, Param.Name).IsValid())
            {
                // The destination name is already taken. Renaming onto it would
                // either fail or collide into a uniquified name; either way the
                // caller's stated intent cannot be met, so say which one.
                OutError = FString::Printf(
                    TEXT("%s: cannot rename '%s' to '%s' because a parameter named '%s' already exists"),
                    Ctx, *Param.RenameFrom, *Param.Name, *Param.Name);
                return false;
            }
            if (!FindUserPin(Node, Param.RenameFrom).IsValid())
            {
                OutError = FString::Printf(
                    TEXT("%s: cannot rename '%s' to '%s' because no parameter named '%s' exists"),
                    Ctx, *Param.RenameFrom, *Param.Name, *Param.RenameFrom);
                return false;
            }

            const FName OldName(*Param.RenameFrom);
            const FName NewName(*Param.Name);

            // Gather the call sites BEFORE renaming, then test every node, then
            // rename every node, then fix the terminator's own FUserPinInfo.
            // That order is the editor's, and each step exists for a reason a
            // shorter version gets wrong:
            //
            //   * RenameUserDefinedPin renames a GRAPH PIN and nothing else. It
            //     never touches UserDefinedPins (K2Node.cpp:1416-1445), so
            //     skipping the last step leaves the declared parameter still
            //     called Multiplier while its pin says Scale - and the next
            //     "add the parameter Scale" then creates a second pin, which
            //     the engine uniquifies to Scale1.
            //   * A call site not renamed here keeps a pin named Multiplier,
            //     and reconstruction after the recompile matches pins by name,
            //     so that pin's connections are dropped without a word.
            FRenameTargetCollector Collector;
            Collector.Nodes.Add(Node);
            Collector.Broadcast(Blueprint, Node, Graph);

            for (UK2Node* Target : Collector.Nodes)
            {
                if (Target == nullptr) { continue; }
                if (Target->RenameUserDefinedPin(OldName, NewName, /*bTest=*/true)
                    == ERenamePinResult::ERenamePinResult_NameCollision)
                {
                    OutError = FString::Printf(
                        TEXT("%s: renaming '%s' to '%s' collides with an existing pin on %s"),
                        Ctx, *Param.RenameFrom, *Param.Name, *Target->GetName());
                    return false;
                }
            }
            for (UK2Node* Target : Collector.Nodes)
            {
                if (Target == nullptr) { continue; }
                Target->Modify();
                Target->RenameUserDefinedPin(OldName, NewName, /*bTest=*/false);
            }

            TSharedPtr<FUserPinInfo>* Declared = Node->UserDefinedPins.FindByPredicate(
                [&OldName](const TSharedPtr<FUserPinInfo>& Info)
                {
                    return Info.IsValid() && Info->PinName == OldName;
                });
            if (Declared == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("%s: '%s' was renamed on the graph but its declaration could not be found "
                         "to rename with it"), Ctx, *Param.RenameFrom);
                return false;
            }
            (*Declared)->PinName = NewName;
            bOutChanged = true;
        }

        TSet<FName> DesiredNames;
        for (const FDesiredParam& Param : Desired) { DesiredNames.Add(FName(*Param.Name)); }

        // Copy first: RemoveUserDefinedPin mutates the array being walked.
        TArray<TSharedPtr<FUserPinInfo>> Existing = Node->UserDefinedPins;
        for (const TSharedPtr<FUserPinInfo>& Info : Existing)
        {
            if (!Info.IsValid() || DesiredNames.Contains(Info->PinName)) { continue; }
            Node->RemoveUserDefinedPin(Info);
            bOutChanged = true;
        }

        for (const FDesiredParam& Param : Desired)
        {
            TSharedPtr<FUserPinInfo> Info = FindUserPin(Node, Param.Name);
            if (!Info.IsValid())
            {
                FText PinError;
                if (!Node->CanCreateUserDefinedPin(Param.Type, Direction, PinError))
                {
                    OutError = FString::Printf(TEXT("%s: parameter '%s' cannot be created here: %s"),
                        Ctx, *Param.Name, *PinError.ToString());
                    return false;
                }
                UEdGraphPin* Created = Node->CreateUserDefinedPin(
                    FName(*Param.Name), Param.Type, Direction, /*bUseUniqueName=*/true);
                if (!Created)
                {
                    OutError = FString::Printf(TEXT("%s: creating parameter '%s' failed"), Ctx, *Param.Name);
                    return false;
                }
                // bUseUniqueName is true so the engine never silently overwrites
                // an existing pin, which means it can hand back a DIFFERENT name.
                // A caller told a parameter was created must get the parameter it
                // asked for, so a uniquified name is a refusal, not a result.
                if (Created->PinName != FName(*Param.Name))
                {
                    OutError = FString::Printf(
                        TEXT("%s: parameter '%s' collided with an existing pin and the engine named it "
                             "'%s' instead"), Ctx, *Param.Name, *Created->PinName.ToString());
                    return false;
                }
                Info = FindUserPin(Node, Param.Name);
                bOutChanged = true;
            }
            else if (Info->PinType != Param.Type)
            {
                FText PinError;
                if (!Node->CanCreateUserDefinedPin(Param.Type, Direction, PinError))
                {
                    OutError = FString::Printf(TEXT("%s: parameter '%s' cannot take that type: %s"),
                        Ctx, *Param.Name, *PinError.ToString());
                    return false;
                }
                Info->PinType = Param.Type;
                bOutChanged = true;
            }

            if (Param.bHasDefault && Info.IsValid() && Info->PinDefaultValue != Param.Default)
            {
                if (!Node->ModifyUserDefinedPinDefaultValue(Info, Param.Default))
                {
                    OutError = FString::Printf(
                        TEXT("%s: '%s' is not a value parameter '%s' can hold as a default"),
                        Ctx, *Param.Default, *Param.Name);
                    return false;
                }
                bOutChanged = true;
            }
        }

        // Parameter ORDER is part of the signature: it is the order call sites
        // show their pins in. Reorder to match the desired list rather than
        // leaving it as whatever order the adds and removes happened to leave.
        TArray<TSharedPtr<FUserPinInfo>> Ordered;
        Ordered.Reserve(Desired.Num());
        for (const FDesiredParam& Param : Desired)
        {
            if (TSharedPtr<FUserPinInfo> Info = FindUserPin(Node, Param.Name)) { Ordered.Add(Info); }
        }
        if (Ordered.Num() == Node->UserDefinedPins.Num() && Ordered != Node->UserDefinedPins)
        {
            Node->UserDefinedPins = Ordered;
            bOutChanged = true;
        }

        Node->ReconstructNode();
        return true;
    }
}

FString FBPFunctionOps::AddFunction(UBlueprint* Blueprint, const FString& FunctionName, const FString& InputsJson, const FString& OutputsJson)
{
    if (!Blueprint)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("AddFunction: null blueprint"));
        return FString();
    }
    if (FunctionName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("AddFunction: empty function name"));
        return FString();
    }
    if (FindFunctionGraph(Blueprint, FunctionName))
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("AddFunction: function '%s' already exists"), *FunctionName);
        return FString();
    }

    TArray<FParamSpec> Inputs, Outputs;
    if (!ParseParamArray(InputsJson,  Inputs,  TEXT("AddFunction.Inputs")))  return FString();
    if (!ParseParamArray(OutputsJson, Outputs, TEXT("AddFunction.Outputs"))) return FString();

    FString ResultGraphName;
    const bool bOk = FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("AddFunction", "Soul Juice: Add Function"),
        [&]() -> bool
        {
            // 1. Create the graph shell.
            UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
                Blueprint, FName(*FunctionName),
                UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            if (!NewGraph)
            {
                UE_LOG(LogBlueprintMutator, Error, TEXT("AddFunction: CreateNewGraph failed for '%s'"), *FunctionName);
                return false;
            }

            // 2. Seed with entry (and result if outputs) via the engine's function-graph factory.
            //    The template param is the signature-source type (UClass or UFunction). We pass
            //    nullptr as a UClass* (parameter-less seeding); user-defined pins are added
            //    explicitly below so Type Descriptor rules apply uniformly.
            FBlueprintEditorUtils::AddFunctionGraph<UClass>(
                Blueprint, NewGraph, /*bIsUserCreated=*/true, /*SignatureFromClass=*/(UClass*)nullptr);

            // 3. Locate the auto-placed entry + (optional) result nodes.
            UK2Node_FunctionEntry*  EntryNode  = nullptr;
            UK2Node_FunctionResult* ResultNode = nullptr;
            for (UEdGraphNode* N : NewGraph->Nodes)
            {
                if (!EntryNode)  EntryNode  = Cast<UK2Node_FunctionEntry>(N);
                if (!ResultNode) ResultNode = Cast<UK2Node_FunctionResult>(N);
            }
            if (!EntryNode)
            {
                UE_LOG(LogBlueprintMutator, Error, TEXT("AddFunction: entry node missing after AddFunctionGraph"));
                return false;
            }

            // 4. Inputs → entry-node user-defined pins (EGPD_Output: entry emits INTO the body).
            for (const FParamSpec& P : Inputs)
            {
                UEdGraphPin* Pin = EntryNode->CreateUserDefinedPin(
                    FName(*P.Name), P.Type, EGPD_Output, /*bUseUniqueName=*/true);
                if (!Pin)
                {
                    UE_LOG(LogBlueprintMutator, Warning,
                        TEXT("AddFunction: CreateUserDefinedPin failed for input '%s'"), *P.Name);
                }
            }

            // 5. Outputs → result-node user-defined pins (EGPD_Input: result consumes FROM the body).
            //    AddFunctionGraph only spawns a result node if outputs were declared up-front;
            //    since we passed none, we have to spawn it ourselves when Outputs.Num() > 0.
            if (Outputs.Num() > 0)
            {
                if (!ResultNode)
                {
                    FGraphNodeCreator<UK2Node_FunctionResult> Creator(*NewGraph);
                    ResultNode = Creator.CreateNode();
                    ResultNode->NodePosX = EntryNode->NodePosX + 400;
                    ResultNode->NodePosY = EntryNode->NodePosY;
                    // FunctionReference on the result node mirrors the entry's — engine pattern.
                    ResultNode->FunctionReference = EntryNode->FunctionReference;
                    Creator.Finalize();
                }
                for (const FParamSpec& P : Outputs)
                {
                    UEdGraphPin* Pin = ResultNode->CreateUserDefinedPin(
                        FName(*P.Name), P.Type, EGPD_Input, /*bUseUniqueName=*/true);
                    if (!Pin)
                    {
                        UE_LOG(LogBlueprintMutator, Warning,
                            TEXT("AddFunction: CreateUserDefinedPin failed for output '%s'"), *P.Name);
                    }
                }
            }

            ResultGraphName = NewGraph->GetName();
            return true;
        });

    return bOk ? ResultGraphName : FString();
}

bool FBPFunctionOps::RemoveFunction(UBlueprint* Blueprint, const FString& FunctionName)
{
    if (!Blueprint)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveFunction: null blueprint"));
        return false;
    }
    if (FunctionName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("RemoveFunction: empty function name"));
        return false;
    }

    UEdGraph* Graph = FindFunctionGraph(Blueprint, FunctionName);
    if (!Graph)
    {
        UE_LOG(LogBlueprintMutator, Warning,
            TEXT("RemoveFunction: function '%s' not found"), *FunctionName);
        return false;
    }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("RemoveFunction", "Soul Juice: Remove Function"),
        [&]() -> bool
        {
            FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::Recompile);
            return true;
        });
}

bool FBPFunctionOps::RenameFunction(UBlueprint* Blueprint, const FString& OldName,
    const FString& NewName, FString& OutError)
{
    UEdGraph* Graph = FindFunctionGraph(Blueprint, OldName);
    if (!Graph) { OutError = FString::Printf(TEXT("Function '%s' not found."), *OldName); return false; }
    if (NewName.IsEmpty()) { OutError = TEXT("New function name is empty."); return false; }
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Existing : Graphs)
    {
        if (Existing && Existing != Graph && Existing->GetFName() == FName(*NewName))
        {
            OutError = FString::Printf(TEXT("Graph '%s' already exists."), *NewName);
            return false;
        }
    }
    return FBPMutatorHelpers::RunMutation(Blueprint,
        LOCTEXT("RenameFunction", "MCP Bridge: Rename Function"), [&]()
        {
            FBlueprintEditorUtils::RenameGraph(Graph, NewName);
            if (Graph->GetName() != NewName)
            {
                OutError = FString::Printf(TEXT("UE4 refused function rename to '%s'."), *NewName);
                return false;
            }
            return true;
        });
}
bool FBPFunctionOps::SetFunctionMetadata(UBlueprint* Blueprint, const FString& FunctionName,
    const FString& MetadataJson, FString& OutError)
{
    static const TCHAR* const Ctx = TEXT("SetFunctionMetadata");

    UEdGraph* Graph = nullptr;
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    if (!ResolveFunction(Blueprint, FunctionName, Ctx, Graph, Entry, Result, OutError)) { return false; }

    TSharedPtr<FJsonObject> Metadata;
    if (!ParseJsonObject(MetadataJson, Ctx, Metadata, OutError)) { return false; }

    // Every key is checked before anything is written. A misspelled key that is
    // ignored reads to the caller as a metadata write that succeeded.
    static const TCHAR* const KnownKeys[] = {
        TEXT("category"), TEXT("tooltip"), TEXT("pure"), TEXT("const"), TEXT("access") };
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Metadata->Values)
    {
        bool bKnown = false;
        for (const TCHAR* Key : KnownKeys) { if (Pair.Key == Key) { bKnown = true; break; } }
        if (!bKnown)
        {
            TArray<FString> Names;
            for (const TCHAR* Key : KnownKeys) { Names.Add(Key); }
            OutError = FString::Printf(TEXT("%s: '%s' is not a metadata key this command writes. "
                "Supported: %s."), Ctx, *Pair.Key, *FString::Join(Names, TEXT(", ")));
            return false;
        }
    }

    FString Access;
    if (Metadata->TryGetStringField(TEXT("access"), Access)
        && Access != TEXT("public") && Access != TEXT("protected") && Access != TEXT("private"))
    {
        OutError = FString::Printf(
            TEXT("%s: access must be public, protected or private; got '%s'"), Ctx, *Access);
        return false;
    }

    return FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("SetFunctionMetadata", "Soul Juice: Set Function Metadata"),
        [&]() -> bool
        {
            Entry->Modify();

            FString Category;
            if (Metadata->TryGetStringField(TEXT("category"), Category))
            {
                Entry->MetaData.Category = FText::FromString(Category);
            }
            FString ToolTip;
            if (Metadata->TryGetStringField(TEXT("tooltip"), ToolTip))
            {
                Entry->MetaData.ToolTip = FText::FromString(ToolTip);
            }

            // Flags go through Add/ClearExtraFlags rather than a recomputed
            // SetExtraFlags, so a flag this command does not own is left alone.
            // The editor's own pure toggle does the same thing by XOR
            // (BlueprintDetailsCustomization.cpp:5000); it is stated here as
            // set-to-value because a desired-state caller says what it wants,
            // not what to invert.
            bool bPure = false;
            if (Metadata->TryGetBoolField(TEXT("pure"), bPure))
            {
                bPure ? Entry->AddExtraFlags(FUNC_BlueprintPure)
                      : Entry->ClearExtraFlags(FUNC_BlueprintPure);
            }
            bool bConst = false;
            if (Metadata->TryGetBoolField(TEXT("const"), bConst))
            {
                bConst ? Entry->AddExtraFlags(FUNC_Const) : Entry->ClearExtraFlags(FUNC_Const);
            }
            if (!Access.IsEmpty())
            {
                Entry->ClearExtraFlags(FUNC_Public | FUNC_Protected | FUNC_Private);
                if (Access == TEXT("public"))    { Entry->AddExtraFlags(FUNC_Public); }
                if (Access == TEXT("protected")) { Entry->AddExtraFlags(FUNC_Protected); }
                if (Access == TEXT("private"))   { Entry->AddExtraFlags(FUNC_Private); }
            }

            // Pure changes what pins the terminators carry (a pure function has
            // no exec pins), so the nodes are rebuilt rather than left showing a
            // signature the recompiled function will not have.
            Entry->ReconstructNode();
            if (Result) { Result->ReconstructNode(); }
            return true;
        });
}

bool FBPFunctionOps::SetFunctionParameters(UBlueprint* Blueprint, const FString& FunctionName,
    const FString& InputsJson, const FString& OutputsJson, FString& OutError)
{
    static const TCHAR* const Ctx = TEXT("SetFunctionParameters");

    UEdGraph* Graph = nullptr;
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    if (!ResolveFunction(Blueprint, FunctionName, Ctx, Graph, Entry, Result, OutError)) { return false; }

    TArray<FDesiredParam> Inputs;
    TArray<FDesiredParam> Outputs;
    if (!ParseDesiredParams(InputsJson,  TEXT("SetFunctionParameters.inputs"),  Inputs,  OutError)) { return false; }
    if (!ParseDesiredParams(OutputsJson, TEXT("SetFunctionParameters.outputs"), Outputs, OutError)) { return false; }

    const bool bWantInputs  = !InputsJson.IsEmpty();
    const bool bWantOutputs = !OutputsJson.IsEmpty();

    bool bChanged = false;
    FString MutationError;
    const bool bOk = FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("SetFunctionParameters", "Soul Juice: Set Function Parameters"),
        [&]() -> bool
        {
            if (bWantInputs)
            {
                // Entry pins are EGPD_Output: the entry node emits the arguments
                // INTO the body.
                if (!ConvergePins(Blueprint, Graph, Entry, Inputs, EGPD_Output,
                        TEXT("SetFunctionParameters.inputs"), bChanged, MutationError))
                {
                    return false;
                }
            }
            if (bWantOutputs)
            {
                if (Result == nullptr && Outputs.Num() > 0)
                {
                    // A function declared without outputs has no result node.
                    // Create it the way AddFunction does, mirroring the entry's
                    // function reference, rather than refusing a legitimate
                    // "this function now returns something".
                    FGraphNodeCreator<UK2Node_FunctionResult> Creator(*Graph);
                    Result = Creator.CreateNode();
                    Result->NodePosX = Entry->NodePosX + 400;
                    Result->NodePosY = Entry->NodePosY;
                    Result->FunctionReference = Entry->FunctionReference;
                    Creator.Finalize();
                    bChanged = true;
                }
                if (Result != nullptr)
                {
                    if (!ConvergePins(Blueprint, Graph, Result, Outputs, EGPD_Input,
                            TEXT("SetFunctionParameters.outputs"), bChanged, MutationError))
                    {
                        return false;
                    }
                }
            }
            return true;
        });

    if (!bOk)
    {
        OutError = MutationError.IsEmpty()
            ? FString::Printf(TEXT("%s: the mutation failed; see LogBlueprintMutator"), Ctx)
            : MutationError;
        return false;
    }
    return true;
}

bool FBPFunctionOps::SetFunctionLocals(UBlueprint* Blueprint, const FString& FunctionName,
    const FString& LocalsJson, FString& OutError)
{
    static const TCHAR* const Ctx = TEXT("SetFunctionLocals");

    UEdGraph* Graph = nullptr;
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    if (!ResolveFunction(Blueprint, FunctionName, Ctx, Graph, Entry, Result, OutError)) { return false; }

    // Locals reuse the parameter parser: same {name, type, default} shape, and
    // one parser means one answer about what a type descriptor means.
    TArray<FDesiredParam> Desired;
    if (!ParseDesiredParams(LocalsJson, TEXT("SetFunctionLocals"), Desired, OutError)) { return false; }
    for (const FDesiredParam& Local : Desired)
    {
        if (!Local.RenameFrom.IsEmpty())
        {
            OutError = FString::Printf(
                TEXT("%s: local '%s' asks for rename_from, which this command does not implement for "
                     "locals. Renaming a local is FBlueprintEditorUtils::RenameLocalVariable and it "
                     "is not wired up here; remove and re-add the local if the rename is intended."),
                Ctx, *Local.Name);
            return false;
        }
    }

    FString MutationError;
    const bool bOk = FBPMutatorHelpers::RunMutation(
        Blueprint,
        LOCTEXT("SetFunctionLocals", "Soul Juice: Set Function Locals"),
        [&]() -> bool
        {
            Entry->Modify();

            TSet<FName> DesiredNames;
            for (const FDesiredParam& Local : Desired) { DesiredNames.Add(FName(*Local.Name)); }

            // Remove first, so a local being replaced by one of the same name
            // with a different type does not collide with itself.
            for (int32 Index = Entry->LocalVariables.Num() - 1; Index >= 0; --Index)
            {
                if (!DesiredNames.Contains(Entry->LocalVariables[Index].VarName))
                {
                    // Directly, not through FBlueprintEditorUtils::RemoveLocalVariable:
                    // that overload needs the local's scope UStruct, which only
                    // exists once the skeleton class has compiled the function,
                    // and a local added and removed inside one batch never gets
                    // one. Any get/set node left referring to the removed local
                    // fails the compile below, and the command's rollback
                    // boundary puts the whole batch back.
                    Entry->LocalVariables.RemoveAt(Index);
                }
            }

            for (const FDesiredParam& Local : Desired)
            {
                FBPVariableDescription* Existing = nullptr;
                for (FBPVariableDescription& Candidate : Entry->LocalVariables)
                {
                    if (Candidate.VarName == FName(*Local.Name)) { Existing = &Candidate; break; }
                }

                if (Existing == nullptr)
                {
                    if (!FBlueprintEditorUtils::AddLocalVariable(
                            Blueprint, Graph, FName(*Local.Name), Local.Type,
                            Local.bHasDefault ? Local.Default : FString()))
                    {
                        MutationError = FString::Printf(
                            TEXT("%s: the engine refused to add local '%s'"), Ctx, *Local.Name);
                        return false;
                    }
                    continue;
                }

                if (Existing->VarType != Local.Type) { Existing->VarType = Local.Type; }
                if (Local.bHasDefault && Existing->DefaultValue != Local.Default)
                {
                    Existing->DefaultValue = Local.Default;
                }
            }
            return true;
        });

    if (!bOk)
    {
        OutError = MutationError.IsEmpty()
            ? FString::Printf(TEXT("%s: the mutation failed; see LogBlueprintMutator"), Ctx)
            : MutationError;
        return false;
    }
    return true;
}

#undef LOCTEXT_NAMESPACE
