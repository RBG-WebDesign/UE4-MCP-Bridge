// Copyright 2026 RareBird Games. All Rights Reserved.
//
// Author a UMaterial graph from one desired-state spec: expression nodes, named
// scalar / vector / texture / static switch parameters, the links between the
// nodes, and the links into BaseColor, EmissiveColor, Roughness, Normal and the
// rest.
//
// -------------------------------------------------------------------------
// Why this exists even though material_instance_build's tail note says it
// could not, and what changed.
//
// The note was right about the evidence and wrong about the conclusion.
// UMaterialEditingLibrary::CreateMaterialExpressionEx
// (MaterialEditingLibrary.cpp:469) really does add to UMaterial::Expressions
// with no Material->Modify(), and ConnectMaterialExpressions (:631) really does
// write through FExpressionInput::Connect the same way. What the note missed is
// that the engine's OWN material editor has the same problem and solves it by
// owning Modify() at the call site: FMaterialEditor::CreateNewMaterialExpression
// (MaterialEditor.cpp:4344) opens an FScopedTransaction, calls
// Material->Modify(), and only then calls CreateMaterialExpressionEx. Modify()
// is the caller's job by design, not a missing feature.
//
// UObject::Modify (Obj.cpp:1206) forwards to SaveToTransactionBuffer
// (UObjectGlobals.cpp:2197), which calls GUndo->SaveObject(Object) - a
// serialized snapshot of the whole object as it is BEFORE the mutation. One
// Modify() on the UMaterial therefore puts its Expressions array and its own
// FExpressionInput members (BaseColor, EmissiveColor, Roughness, Normal, ...)
// into the undo record, and cancelling the transaction restores them.
//
// That covers the material. It does not cover a pre-existing UMaterialExpression
// whose own inputs get rewritten, because those inputs live on the expression,
// not on the material. So this command never rewrites one:
//
//   FULL GRAPH REPLACEMENT. Every expression this command connects is one it
//   created inside the same transaction. The only pre-existing object it
//   mutates is the UMaterial itself.
//
// which is also what makes the command convergent - the spec is the whole graph,
// so a rerun produces the same graph - and it is the same bargain
// blueprint_build and widget_build already make.
//
// The old expressions are dropped from Material->Expressions but deliberately
// NOT marked pending kill, so a cancelled transaction restores an array of
// pointers to objects that are still alive with their inputs untouched.
//
// Modify() returns false when there is no transactor, when the object is not
// RF_Transactional, or when it lives in a script package. Any of those means the
// graph write would land outside the undo record, which is exactly the condition
// that makes a multi-node build unrecoverable. The return value is CHECKED and
// the command refuses before writing anything, because finding 0r in
// docs/CAPABILITY_FINDINGS.md is what a discarded Modify() result costs: a
// rollback that silently did nothing.
//
// Failure is decided by re-reading, never by trusting the undo. On the create
// path the Asset Registry has to agree the asset is gone; on the update path the
// material's structure hash has to come back to what it was before the build.
// -------------------------------------------------------------------------

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Texture.h"
#include "Factories/MaterialFactoryNew.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "MCPBridgeAssetRollback.h"
#include "MCPBridgeMaterialSnapshot.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "RHI.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

using MCPBridgeMaterialSnapshot::EParameterKind;

namespace MCPMaterialBuild
{
    /** The material settings a caller may choose, as small hand-written tables.
        Written out rather than resolved through the UEnum on purpose: the enums
        carry entries this command should not accept (MD_RuntimeVirtualTexture is
        marked Hidden and deprecated in 4.27), and a table is the place to say so
        once. */
    struct FEnumChoice
    {
        const TCHAR* Name;
        int32 Value;
    };

    inline const TArray<FEnumChoice>& DomainChoices()
    {
        static const TArray<FEnumChoice> Choices = {
            { TEXT("Surface"),       MD_Surface },
            { TEXT("DeferredDecal"), MD_DeferredDecal },
            { TEXT("LightFunction"), MD_LightFunction },
            { TEXT("Volume"),        MD_Volume },
            { TEXT("PostProcess"),   MD_PostProcess },
            { TEXT("UI"),            MD_UI },
        };
        return Choices;
    }

    inline const TArray<FEnumChoice>& BlendModeChoices()
    {
        static const TArray<FEnumChoice> Choices = {
            { TEXT("Opaque"),         BLEND_Opaque },
            { TEXT("Masked"),         BLEND_Masked },
            { TEXT("Translucent"),    BLEND_Translucent },
            { TEXT("Additive"),       BLEND_Additive },
            { TEXT("Modulate"),       BLEND_Modulate },
            { TEXT("AlphaComposite"), BLEND_AlphaComposite },
            { TEXT("AlphaHoldout"),   BLEND_AlphaHoldout },
        };
        return Choices;
    }

    inline const TArray<FEnumChoice>& ShadingModelChoices()
    {
        static const TArray<FEnumChoice> Choices = {
            { TEXT("Unlit"),               MSM_Unlit },
            { TEXT("DefaultLit"),          MSM_DefaultLit },
            { TEXT("Subsurface"),          MSM_Subsurface },
            { TEXT("PreintegratedSkin"),   MSM_PreintegratedSkin },
            { TEXT("ClearCoat"),           MSM_ClearCoat },
            { TEXT("SubsurfaceProfile"),   MSM_SubsurfaceProfile },
            { TEXT("TwoSidedFoliage"),     MSM_TwoSidedFoliage },
            { TEXT("Hair"),                MSM_Hair },
            { TEXT("Cloth"),               MSM_Cloth },
            { TEXT("Eye"),                 MSM_Eye },
        };
        return Choices;
    }

    FString ChoiceNames(const TArray<FEnumChoice>& Choices)
    {
        TArray<FString> Names;
        for (const FEnumChoice& Choice : Choices) { Names.Add(Choice.Name); }
        return FString::Join(Names, TEXT(", "));
    }

    /** The name the resolved value came from. The response reports this rather
        than echoing the spec's string back, so what it says is what the material
        actually got, including when the field was absent and the default won. */
    FString ChoiceName(const TArray<FEnumChoice>& Choices, int32 Value)
    {
        for (const FEnumChoice& Choice : Choices)
        {
            if (Choice.Value == Value) { return Choice.Name; }
        }
        return FString::Printf(TEXT("(%d)"), Value);
    }

    /** Read one optional enum-ish string field. Absent keeps the default; a name
        that is not in the table is an error naming every name that is. */
    bool ReadChoice(
        const TSharedPtr<FJsonObject>& Spec,
        const TCHAR* Field,
        const TArray<FEnumChoice>& Choices,
        int32& InOutValue,
        TArray<FString>& OutErrors)
    {
        FString Text;
        if (!Spec->TryGetStringField(Field, Text)) { return true; }
        Text = Text.TrimStartAndEnd();
        if (Text.IsEmpty()) { return true; }
        for (const FEnumChoice& Choice : Choices)
        {
            if (Text.Equals(Choice.Name, ESearchCase::IgnoreCase))
            {
                InOutValue = Choice.Value;
                return true;
            }
        }
        OutErrors.Add(FString::Printf(
            TEXT("%s '%s' is not one of: %s."), Field, *Text, *ChoiceNames(Choices)));
        return false;
    }

    /**
     * The UClass behind an expression `type`.
     *
     * Short names are the ergonomic case ("Multiply" -> MaterialExpressionMultiply)
     * because a caller writing a graph from a prompt should not have to spell
     * engine class names. Whatever the spelling, the result must be a concrete
     * subclass of UMaterialExpression, which is the security boundary: a shader
     * graph node has no side effects outside the material it belongs to, and
     * nothing else can be constructed through this path.
     */
    UClass* ResolveExpressionClass(const FString& Type, FString& OutError)
    {
        const FString Trimmed = Type.TrimStartAndEnd();
        if (Trimmed.IsEmpty())
        {
            OutError = TEXT("type is empty");
            return nullptr;
        }
        TArray<FString> Candidates;
        Candidates.Add(Trimmed);
        if (!Trimmed.StartsWith(TEXT("MaterialExpression")))
        {
            Candidates.Add(TEXT("MaterialExpression") + Trimmed);
        }

        UClass* Found = nullptr;
        for (const FString& Candidate : Candidates)
        {
            // Every UMaterialExpression subclass is compiled into the Engine
            // module and therefore already loaded, so a name lookup is enough
            // and no package has to be loaded to answer.
            UClass* Class = FindObject<UClass>(ANY_PACKAGE, *Candidate);
            if (Class != nullptr) { Found = Class; break; }
        }
        if (Found == nullptr)
        {
            OutError = FString::Printf(
                TEXT("no class named '%s' or 'MaterialExpression%s' exists"), *Trimmed, *Trimmed);
            return nullptr;
        }
        if (!Found->IsChildOf(UMaterialExpression::StaticClass()))
        {
            OutError = FString::Printf(
                TEXT("'%s' is a %s, not a material expression"), *Trimmed, *Found->GetName());
            return nullptr;
        }
        if (Found->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            OutError = FString::Printf(
                TEXT("'%s' is abstract or deprecated and cannot be placed"), *Found->GetName());
            return nullptr;
        }
        return Found;
    }

    /** The UMaterialExpression subclass behind one parameter kind. A switch
        parameter is the StaticSwitch node rather than the bare StaticBool: it
        publishes to GetAllStaticSwitchParameterInfo exactly the same way and it
        also has A and B inputs, so a caller can actually wire it. */
    UClass* ParameterClass(EParameterKind Kind)
    {
        switch (Kind)
        {
            case EParameterKind::Scalar:       return UMaterialExpressionScalarParameter::StaticClass();
            case EParameterKind::Vector:       return UMaterialExpressionVectorParameter::StaticClass();
            case EParameterKind::Texture:      return UMaterialExpressionTextureSampleParameter2D::StaticClass();
            case EParameterKind::StaticSwitch: return UMaterialExpressionStaticSwitchParameter::StaticClass();
            default:                           return nullptr;
        }
    }

    bool ParseParameterKind(const FString& Text, EParameterKind& OutKind)
    {
        if (Text.Equals(TEXT("scalar"), ESearchCase::IgnoreCase)) { OutKind = EParameterKind::Scalar; return true; }
        if (Text.Equals(TEXT("vector"), ESearchCase::IgnoreCase)) { OutKind = EParameterKind::Vector; return true; }
        if (Text.Equals(TEXT("texture"), ESearchCase::IgnoreCase)) { OutKind = EParameterKind::Texture; return true; }
        if (Text.Equals(TEXT("switch"), ESearchCase::IgnoreCase)
            || Text.Equals(TEXT("static_switch"), ESearchCase::IgnoreCase))
        {
            OutKind = EParameterKind::StaticSwitch;
            return true;
        }
        return false;
    }

    /** One node the spec asked for, resolved and validated before anything is
        created. Parameters and free expressions share this type and one id
        namespace, so `outputs: {"EmissiveColor": "GlowStrength"}` addresses a
        parameter node the same way it addresses a Multiply. */
    struct FPlannedNode
    {
        FString Id;
        UClass* Class = nullptr;
        bool bIsParameter = false;
        EParameterKind Kind = EParameterKind::Scalar;
        FName ParameterName;
        FName Group;
        float ScalarDefault = 0.0f;
        FLinearColor VectorDefault = FLinearColor::White;
        bool bSwitchDefault = false;
        UTexture* TextureDefault = nullptr;
        FString TextureDefaultPath;
        TSharedPtr<FJsonObject> Params;
        int32 X = 0;
        int32 Y = 0;
        /** Filled during the build. The engine names expressions itself
            (MaterialExpressionMultiply_0), so the caller's id and the name
            material_inspect will report are different strings and the response
            has to carry both. */
        UMaterialExpression* Created = nullptr;
    };

    struct FPlannedLink
    {
        FString FromId;
        FString FromOutput;
        FString ToId;
        FString ToInput;
        int32 ResolvedToInputIndex = INDEX_NONE;
    };

    struct FPlannedOutput
    {
        EMaterialProperty Property = MP_BaseColor;
        FString PropertyName;
        FString FromId;
        FString FromOutput;
    };

    /** Split "NodeId.PortName" into its two halves. A bare "NodeId" means the
        node's first port, which is what a single-output node such as Multiply
        or a parameter always wants and is the overwhelmingly common case. */
    void SplitPort(const FString& Text, FString& OutId, FString& OutPort)
    {
        const FString Trimmed = Text.TrimStartAndEnd();
        int32 Dot = INDEX_NONE;
        if (Trimmed.FindLastChar(TEXT('.'), Dot))
        {
            OutId = Trimmed.Left(Dot).TrimStartAndEnd();
            OutPort = Trimmed.Mid(Dot + 1).TrimStartAndEnd();
            return;
        }
        OutId = Trimmed;
        OutPort.Empty();
    }

    /** Every input name a node has, for an error message that names the choices
        instead of only the miss. */
    FString InputNames(UMaterialExpression* Expression)
    {
        TArray<FString> Names;
        const TArray<FExpressionInput*> Inputs = Expression->GetInputs();
        for (int32 Index = 0; Index < Inputs.Num(); ++Index)
        {
            const FString Name = Expression->GetInputName(Index).ToString();
            Names.Add(Name.IsEmpty() ? FString::Printf(TEXT("[%d]"), Index) : Name);
        }
        return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
    }

    FString OutputNames(UMaterialExpression* Expression)
    {
        TArray<FString> Names;
        const TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
        for (int32 Index = 0; Index < Outputs.Num(); ++Index)
        {
            const FString Name = Outputs[Index].OutputName.ToString();
            Names.Add(Name.IsEmpty() ? FString::Printf(TEXT("[%d]"), Index) : Name);
        }
        return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
    }

    /** Resolve a port name to an index. An empty name is index 0; a numeric name
        addresses a port directly, which is how the unnamed extra outputs of
        nodes such as TextureSample are reached. Matching is case insensitive
        because "rgb" and "RGB" are the same pin to everyone but strcmp. */
    int32 ResolveOutputIndex(UMaterialExpression* Expression, const FString& Name)
    {
        const TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
        if (Name.IsEmpty()) { return Outputs.Num() > 0 ? 0 : INDEX_NONE; }
        for (int32 Index = 0; Index < Outputs.Num(); ++Index)
        {
            if (Outputs[Index].OutputName.ToString().Equals(Name, ESearchCase::IgnoreCase))
            {
                return Index;
            }
        }
        if (Name.IsNumeric())
        {
            const int32 Index = FCString::Atoi(*Name);
            if (Outputs.IsValidIndex(Index)) { return Index; }
        }
        return INDEX_NONE;
    }

    int32 ResolveInputIndex(UMaterialExpression* Expression, const FString& Name)
    {
        const TArray<FExpressionInput*> Inputs = Expression->GetInputs();
        if (Name.IsEmpty()) { return Inputs.Num() > 0 ? 0 : INDEX_NONE; }
        const FString ResolvedName = Expression->IsA<UMaterialExpressionStaticSwitchParameter>()
            ? (Name.Equals(TEXT("A"), ESearchCase::IgnoreCase) ? TEXT("True")
                : Name.Equals(TEXT("B"), ESearchCase::IgnoreCase) ? TEXT("False") : Name)
            : Name;
        for (int32 Index = 0; Index < Inputs.Num(); ++Index)
        {
            if (Expression->GetInputName(Index).ToString().Equals(ResolvedName, ESearchCase::IgnoreCase))
            {
                return Index;
            }
        }
        if (Name.IsNumeric())
        {
            const int32 Index = FCString::Atoi(*Name);
            if (Inputs.IsValidIndex(Index)) { return Index; }
        }
        return INDEX_NONE;
    }

    /**
     * Write one JSON value into one reflected property of a material expression.
     *
     * Narrower than the component-template marshaller in
     * MCPPuerTSBridgeBlueprint.cpp on purpose, and not a second copy of it: that
     * one has to accept anything a component can hold, this one accepts the
     * handful of types a shader node actually carries and refuses the rest by
     * name. Refusing is the safe default here because a silently skipped node
     * property is a graph that compiles and looks wrong.
     *
     * The one case that gets its own branch is an asset reference. The
     * converter's route for a UObject* is FProperty::ImportText with the parse
     * result discarded, so an unresolvable path leaves the property null and
     * still reports success - the exact failure this command exists to remove.
     */
    bool ApplyNodeProperty(
        UMaterialExpression* Expression,
        const FString& PropertyName,
        const TSharedPtr<FJsonValue>& Value,
        FString& OutError)
    {
        UClass* Class = Expression->GetClass();
        FProperty* Property = Class->FindPropertyByName(FName(*PropertyName));
        if (Property == nullptr)
        {
            TArray<FString> Names;
            for (TFieldIterator<FProperty> It(Class); It && Names.Num() < 24; ++It)
            {
                if (It->HasAnyPropertyFlags(CPF_Edit)) { Names.Add(It->GetName()); }
            }
            OutError = FString::Printf(
                TEXT("%s has no property '%s'. Editable properties: %s."),
                *Class->GetName(), *PropertyName,
                Names.Num() > 0 ? *FString::Join(Names, TEXT(", ")) : TEXT("(none)"));
            return false;
        }
        void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Expression);

        if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
        {
            FString Path;
            if (Value->Type == EJson::Null) { Path.Empty(); }
            else if (!Value->TryGetString(Path))
            {
                OutError = FString::Printf(
                    TEXT("'%s' expects an asset path string, or null to clear."), *PropertyName);
                return false;
            }
            if (Path.IsEmpty() || Path == TEXT("None"))
            {
                ObjectProperty->SetObjectPropertyValue(ValuePtr, nullptr);
                return true;
            }
            UObject* Loaded = LoadObject<UObject>(nullptr, *Path);
            if (Loaded == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("'%s': no asset could be loaded from '%s'."), *PropertyName, *Path);
                return false;
            }
            if (ObjectProperty->PropertyClass != nullptr && !Loaded->IsA(ObjectProperty->PropertyClass))
            {
                OutError = FString::Printf(
                    TEXT("'%s': '%s' is a %s, but the property holds a %s."),
                    *PropertyName, *Path, *Loaded->GetClass()->GetName(),
                    *ObjectProperty->PropertyClass->GetName());
                return false;
            }
            ObjectProperty->SetObjectPropertyValue(ValuePtr, Loaded);
            return true;
        }

        const bool bSupported =
            Property->IsA<FBoolProperty>()
            || Property->IsA<FNumericProperty>()
            || Property->IsA<FEnumProperty>()
            || Property->IsA<FStrProperty>()
            || Property->IsA<FNameProperty>()
            || Property->IsA<FTextProperty>()
            || Property->IsA<FStructProperty>();
        if (!bSupported)
        {
            OutError = FString::Printf(
                TEXT("'%s' is a %s, which this command does not write. Supported: numbers, "
                     "booleans, enums, strings, structs such as {\"r\":1,\"g\":0,\"b\":0}, "
                     "and asset paths."),
                *PropertyName, *Property->GetClass()->GetName());
            return false;
        }
        if (!FJsonObjectConverter::JsonValueToUProperty(Value, Property, ValuePtr))
        {
            OutError = FString::Printf(
                TEXT("'%s' did not accept the value; it is a %s."),
                *PropertyName, *Property->GetCPPType());
            return false;
        }
        return true;
    }
}

using namespace MCPMaterialBuild;

bool UMCPPuerTSBridgeService::BuildMaterialJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Material spec must be a JSON object.");
        return false;
    }

    FString AssetPath;
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    AssetPath = AssetPath.TrimStartAndEnd();
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Materials are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    const bool bSave = !Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        || Spec->GetBoolField(TEXT("save"));
    const bool bTwoSided = Spec->HasTypedField<EJson::Boolean>(TEXT("two_sided"))
        && Spec->GetBoolField(TEXT("two_sided"));

    TArray<FString> Errors;

    int32 Domain = MD_Surface;
    int32 BlendMode = BLEND_Opaque;
    int32 ShadingModel = MSM_DefaultLit;
    ReadChoice(Spec, TEXT("domain"), DomainChoices(), Domain, Errors);
    ReadChoice(Spec, TEXT("blend_mode"), BlendModeChoices(), BlendMode, Errors);
    ReadChoice(Spec, TEXT("shading_model"), ShadingModelChoices(), ShadingModel, Errors);

    // --- Parse the nodes. Parameters first, so their ids are the ones a
    //     connection or an output most naturally names. ---
    TArray<FPlannedNode> Nodes;
    TSet<FString> NodeIds;

    const TArray<TSharedPtr<FJsonValue>>* ParameterArray = nullptr;
    if (Spec->HasField(TEXT("parameters")) && !Spec->TryGetArrayField(TEXT("parameters"), ParameterArray))
    {
        Errors.Add(TEXT("parameters must be an array of objects."));
    }
    if (ParameterArray != nullptr)
    {
        for (int32 Index = 0; Index < ParameterArray->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!(*ParameterArray)[Index].IsValid() || !(*ParameterArray)[Index]->TryGetObject(Object))
            {
                Errors.Add(FString::Printf(TEXT("parameters[%d] must be an object."), Index));
                continue;
            }
            FPlannedNode Node;
            Node.bIsParameter = true;

            FString KindText;
            (*Object)->TryGetStringField(TEXT("kind"), KindText);
            if (!ParseParameterKind(KindText.TrimStartAndEnd(), Node.Kind))
            {
                Errors.Add(FString::Printf(
                    TEXT("parameters[%d].kind '%s' is not one of: scalar, vector, texture, switch."),
                    Index, *KindText));
                continue;
            }
            Node.Class = ParameterClass(Node.Kind);

            FString Name;
            (*Object)->TryGetStringField(TEXT("name"), Name);
            Name = Name.TrimStartAndEnd();
            if (Name.IsEmpty())
            {
                Errors.Add(FString::Printf(TEXT("parameters[%d].name is required."), Index));
                continue;
            }
            Node.ParameterName = FName(*Name);

            FString Id;
            (*Object)->TryGetStringField(TEXT("id"), Id);
            Node.Id = Id.TrimStartAndEnd().IsEmpty() ? Name : Id.TrimStartAndEnd();

            FString Group;
            if ((*Object)->TryGetStringField(TEXT("group"), Group))
            {
                Node.Group = FName(*Group.TrimStartAndEnd());
            }

            const TSharedPtr<FJsonValue> Default = (*Object)->TryGetField(TEXT("default"));
            switch (Node.Kind)
            {
                case EParameterKind::Scalar:
                {
                    double Number = 0.0;
                    if (Default.IsValid() && !Default->TryGetNumber(Number))
                    {
                        Errors.Add(FString::Printf(
                            TEXT("parameters[%d] '%s': a scalar default must be a number."), Index, *Name));
                    }
                    Node.ScalarDefault = static_cast<float>(Number);
                    break;
                }
                case EParameterKind::Vector:
                {
                    const TSharedPtr<FJsonObject>* Color = nullptr;
                    if (Default.IsValid() && Default->TryGetObject(Color) && Color != nullptr)
                    {
                        double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
                        (*Color)->TryGetNumberField(TEXT("r"), R);
                        (*Color)->TryGetNumberField(TEXT("g"), G);
                        (*Color)->TryGetNumberField(TEXT("b"), B);
                        (*Color)->TryGetNumberField(TEXT("a"), A);
                        Node.VectorDefault = FLinearColor(
                            static_cast<float>(R), static_cast<float>(G),
                            static_cast<float>(B), static_cast<float>(A));
                    }
                    else if (Default.IsValid())
                    {
                        Errors.Add(FString::Printf(
                            TEXT("parameters[%d] '%s': a vector default must be an object with r, g, b "
                                 "and optional a."), Index, *Name));
                    }
                    break;
                }
                case EParameterKind::StaticSwitch:
                {
                    bool bValue = false;
                    if (Default.IsValid() && !Default->TryGetBool(bValue))
                    {
                        Errors.Add(FString::Printf(
                            TEXT("parameters[%d] '%s': a switch default must be true or false."),
                            Index, *Name));
                    }
                    Node.bSwitchDefault = bValue;
                    break;
                }
                case EParameterKind::Texture:
                {
                    FString Path;
                    if (!Default.IsValid() || !Default->TryGetString(Path)
                        || Path.TrimStartAndEnd().IsEmpty())
                    {
                        // Not optional, and not pedantry: a texture sample with
                        // no texture is a material that fails to compile, and
                        // the caller would get the compile error instead of this
                        // sentence. puerts_texture_import exists to produce one.
                        Errors.Add(FString::Printf(
                            TEXT("parameters[%d] '%s': a texture parameter needs a default texture "
                                 "asset path. puerts_texture_import can generate one."), Index, *Name));
                        break;
                    }
                    Node.TextureDefaultPath = Path.TrimStartAndEnd();
                    Node.TextureDefault = LoadObject<UTexture>(nullptr, *Node.TextureDefaultPath);
                    if (Node.TextureDefault == nullptr)
                    {
                        Errors.Add(FString::Printf(
                            TEXT("parameters[%d] '%s': no texture could be loaded from '%s'."),
                            Index, *Name, *Node.TextureDefaultPath));
                    }
                    break;
                }
            }

            double PosX = 0.0, PosY = 0.0;
            Node.X = (*Object)->TryGetNumberField(TEXT("x"), PosX) ? static_cast<int32>(PosX) : -640;
            Node.Y = (*Object)->TryGetNumberField(TEXT("y"), PosY)
                ? static_cast<int32>(PosY) : Index * 140 - 200;

            if (NodeIds.Contains(Node.Id))
            {
                Errors.Add(FString::Printf(TEXT("node id '%s' is used more than once."), *Node.Id));
            }
            NodeIds.Add(Node.Id);
            Nodes.Add(Node);
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* ExpressionArray = nullptr;
    if (Spec->HasField(TEXT("expressions")) && !Spec->TryGetArrayField(TEXT("expressions"), ExpressionArray))
    {
        Errors.Add(TEXT("expressions must be an array of objects."));
    }
    if (ExpressionArray != nullptr)
    {
        for (int32 Index = 0; Index < ExpressionArray->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!(*ExpressionArray)[Index].IsValid() || !(*ExpressionArray)[Index]->TryGetObject(Object))
            {
                Errors.Add(FString::Printf(TEXT("expressions[%d] must be an object."), Index));
                continue;
            }
            FPlannedNode Node;

            FString Id;
            (*Object)->TryGetStringField(TEXT("id"), Id);
            Node.Id = Id.TrimStartAndEnd();
            if (Node.Id.IsEmpty())
            {
                Errors.Add(FString::Printf(TEXT("expressions[%d].id is required."), Index));
                continue;
            }

            FString Type;
            (*Object)->TryGetStringField(TEXT("type"), Type);
            FString ClassError;
            Node.Class = ResolveExpressionClass(Type, ClassError);
            if (Node.Class == nullptr)
            {
                Errors.Add(FString::Printf(
                    TEXT("expressions[%d] '%s': %s."), Index, *Node.Id, *ClassError));
                continue;
            }

            const TSharedPtr<FJsonObject>* Params = nullptr;
            if ((*Object)->HasField(TEXT("params")))
            {
                if ((*Object)->TryGetObjectField(TEXT("params"), Params) && Params != nullptr)
                {
                    Node.Params = *Params;
                }
                else
                {
                    Errors.Add(FString::Printf(
                        TEXT("expressions[%d] '%s': params must be an object of property name to value."),
                        Index, *Node.Id));
                }
            }

            double PosX = 0.0, PosY = 0.0;
            Node.X = (*Object)->TryGetNumberField(TEXT("x"), PosX) ? static_cast<int32>(PosX) : -300;
            Node.Y = (*Object)->TryGetNumberField(TEXT("y"), PosY)
                ? static_cast<int32>(PosY) : Index * 140 - 200;

            if (NodeIds.Contains(Node.Id))
            {
                Errors.Add(FString::Printf(TEXT("node id '%s' is used more than once."), *Node.Id));
            }
            NodeIds.Add(Node.Id);
            Nodes.Add(Node);
        }
    }

    auto FindNode = [&Nodes](const FString& Id) -> FPlannedNode*
    {
        for (FPlannedNode& Node : Nodes)
        {
            if (Node.Id == Id) { return &Node; }
        }
        return nullptr;
    };
    auto KnownIds = [&Nodes]() -> FString
    {
        TArray<FString> Ids;
        for (const FPlannedNode& Node : Nodes) { Ids.Add(Node.Id); }
        Ids.Sort();
        return Ids.Num() > 0 ? FString::Join(Ids, TEXT(", ")) : FString(TEXT("(none)"));
    };

    // --- Parse the links. ---
    TArray<FPlannedLink> Links;
    const TArray<TSharedPtr<FJsonValue>>* ConnectionArray = nullptr;
    if (Spec->HasField(TEXT("connections")) && !Spec->TryGetArrayField(TEXT("connections"), ConnectionArray))
    {
        Errors.Add(TEXT("connections must be an array of objects."));
    }
    if (ConnectionArray != nullptr)
    {
        for (int32 Index = 0; Index < ConnectionArray->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!(*ConnectionArray)[Index].IsValid() || !(*ConnectionArray)[Index]->TryGetObject(Object))
            {
                Errors.Add(FString::Printf(TEXT("connections[%d] must be an object."), Index));
                continue;
            }
            FString From, To;
            (*Object)->TryGetStringField(TEXT("from"), From);
            (*Object)->TryGetStringField(TEXT("to"), To);
            FPlannedLink Link;
            SplitPort(From, Link.FromId, Link.FromOutput);
            SplitPort(To, Link.ToId, Link.ToInput);
            if (Link.FromId.IsEmpty() || Link.ToId.IsEmpty())
            {
                Errors.Add(FString::Printf(
                    TEXT("connections[%d] needs from \"nodeId\" or \"nodeId.Output\" and "
                         "to \"nodeId.Input\"."), Index));
                continue;
            }
            if (FindNode(Link.FromId) == nullptr)
            {
                Errors.Add(FString::Printf(
                    TEXT("connections[%d].from names node '%s', which the spec does not declare. "
                         "Declared nodes: %s."), Index, *Link.FromId, *KnownIds()));
            }
            if (FindNode(Link.ToId) == nullptr)
            {
                Errors.Add(FString::Printf(
                    TEXT("connections[%d].to names node '%s', which the spec does not declare. "
                         "Declared nodes: %s."), Index, *Link.ToId, *KnownIds()));
            }
            Links.Add(Link);
        }
    }

    // --- Parse the material outputs. The property table is the inspector's, so
    //     a name accepted here is a name material_inspect reports back. ---
    TArray<FPlannedOutput> Outputs;
    const TSharedPtr<FJsonObject>* OutputMap = nullptr;
    if (Spec->HasField(TEXT("outputs")) && !Spec->TryGetObjectField(TEXT("outputs"), OutputMap))
    {
        Errors.Add(TEXT("outputs must be an object of material property name to node id."));
    }
    if (OutputMap != nullptr)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*OutputMap)->Values)
        {
            const MCPBridgeMaterialSnapshot::FMaterialPropertyEntry* Match = nullptr;
            for (const MCPBridgeMaterialSnapshot::FMaterialPropertyEntry& Entry
                    : MCPBridgeMaterialSnapshot::NamedMaterialProperties())
            {
                if (Pair.Key.Equals(Entry.Name, ESearchCase::IgnoreCase)) { Match = &Entry; break; }
            }
            if (Match == nullptr)
            {
                TArray<FString> Names;
                for (const MCPBridgeMaterialSnapshot::FMaterialPropertyEntry& Entry
                        : MCPBridgeMaterialSnapshot::NamedMaterialProperties())
                {
                    Names.Add(Entry.Name);
                }
                Errors.Add(FString::Printf(
                    TEXT("outputs.%s is not a material property. Supported: %s."),
                    *Pair.Key, *FString::Join(Names, TEXT(", "))));
                continue;
            }
            FString Text;
            if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(Text) || Text.TrimStartAndEnd().IsEmpty())
            {
                Errors.Add(FString::Printf(
                    TEXT("outputs.%s must be a node id, optionally with an output "
                         "(\"NodeId\" or \"NodeId.RGB\")."), *Pair.Key));
                continue;
            }
            FPlannedOutput Output;
            Output.Property = Match->Property;
            Output.PropertyName = Match->Name;
            SplitPort(Text, Output.FromId, Output.FromOutput);
            if (FindNode(Output.FromId) == nullptr)
            {
                Errors.Add(FString::Printf(
                    TEXT("outputs.%s names node '%s', which the spec does not declare. "
                         "Declared nodes: %s."), *Pair.Key, *Output.FromId, *KnownIds()));
            }
            Outputs.Add(Output);
        }
        Outputs.Sort([](const FPlannedOutput& A, const FPlannedOutput& B)
            { return A.PropertyName < B.PropertyName; });
    }

    if (Nodes.Num() == 0)
    {
        Errors.Add(TEXT("A material needs at least one node: give parameters, expressions, or both."));
    }

    // --- Resolve the existing asset before answering a plan, so a plan can say
    //     whether the run would create or replace. ---
    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = AssetPath + TEXT(".") + AssetName;
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData ExistingData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath));
    UMaterial* Material = nullptr;
    if (ExistingData.IsValid())
    {
        Material = Cast<UMaterial>(ExistingData.GetAsset());
        if (Material == nullptr)
        {
            OutError = FString::Printf(
                TEXT("The asset at '%s' is a %s, not a Material. Choose a different asset_path "
                     "rather than overwriting it."),
                *ObjectPath, *ExistingData.AssetClass.ToString());
            return false;
        }
    }
    const bool bCreating = Material == nullptr;

    if (Errors.Num() > 0)
    {
        OutError = FString::Join(Errors, TEXT(" "));
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> PlannedNodes;
    for (const FPlannedNode& Node : Nodes)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("id"), Node.Id);
        Entry->SetStringField(TEXT("class"), Node.Class->GetName());
        Entry->SetBoolField(TEXT("is_parameter"), Node.bIsParameter);
        if (Node.bIsParameter)
        {
            Entry->SetStringField(TEXT("parameter_name"), Node.ParameterName.ToString());
            Entry->SetStringField(TEXT("parameter_kind"),
                MCPBridgeMaterialSnapshot::ParameterKindName(Node.Kind));
        }
        PlannedNodes.Add(MakeShared<FJsonValueObject>(Entry));
    }
    MCPBridgeBlueprintMembers::SortByStringField(PlannedNodes, TEXT("id"));

    auto BuildBaseResult = [&]() -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), AssetPath);
        Result->SetArrayField(TEXT("plan"), PlannedNodes);
        Result->SetNumberField(TEXT("planned_node_count"), Nodes.Num());
        Result->SetNumberField(TEXT("planned_connection_count"), Links.Num());
        Result->SetNumberField(TEXT("planned_output_count"), Outputs.Num());
        return Result;
    };

    if (bPlanOnly)
    {
        TSharedPtr<FJsonObject> Result = BuildBaseResult();
        Result->SetBoolField(TEXT("plan_only"), true);
        Result->SetBoolField(TEXT("would_create"), bCreating);
        Result->SetBoolField(TEXT("would_replace_graph"), !bCreating);
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        OutResultJson = SerializeJson(Result);
        return true;
    }

    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Material authoring requires an active transaction.");
        return false;
    }

    // --- One rollback boundary around the whole request. ---
    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();
    // The graph as it was. This is what decides whether a rollback worked on the
    // update path: the undo record's own word that it replayed is not evidence
    // that the nodes and links came back.
    const FString PreHash = bCreating
        ? FString() : MCPBridgeMaterialSnapshot::StructureHash(Material);

    bool bRollbackSucceeded = false;
    auto FailWithRollback = [&](const FString& Error) -> bool
    {
        // Cancel first: the transaction's undo records point at objects the
        // rollback is about to destroy.
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }
        if (!bCreating && Material != nullptr)
        {
            // Resync the parameter and expression caches the undo just rewound,
            // before Rollback restores the package dirty flag over the top.
            Material->PostEditChange();
        }
        Rollback.Rollback();

        if (bCreating)
        {
            bRollbackSucceeded =
                !AssetRegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath)).IsValid();
            Material = nullptr;
        }
        else
        {
            bRollbackSucceeded = MCPBridgeMaterialSnapshot::StructureHash(Material) == PreHash;
        }

        OutError = bRollbackSucceeded
            ? Error
            : FString::Printf(
                TEXT("%s The rollback did NOT restore the material: re-reading its graph does not "
                     "hash to what it was before the build. Treat '%s' as damaged."),
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
        UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
        Material = Cast<UMaterial>(Factory->FactoryCreateNew(
            UMaterial::StaticClass(),
            Package,
            FName(*AssetName),
            RF_Public | RF_Standalone | RF_Transactional,
            nullptr,
            GWarn));
        if (Material == nullptr)
        {
            return FailWithRollback(TEXT("Unreal could not create the material."));
        }
        Rollback.TrackCreated(Material);
        AssetRegistryModule.Get().AssetCreated(Material);
    }

    // --- The gate. See the file header: this return value decides whether the
    //     rest of this function is recoverable, so it is checked and not
    //     assumed. Nothing has been written to the material yet. ---
    if (!Material->Modify())
    {
        return FailWithRollback(FString::Printf(
            TEXT("UMaterial::Modify() returned false for '%s', so this build's writes would land "
                 "outside the undo record and a failure could not be rolled back. That happens "
                 "when there is no active transactor, when the asset is not RF_Transactional, or "
                 "when it lives in a script package. Refusing before writing anything."),
            *AssetPath));
    }

    TArray<TSharedPtr<FJsonValue>> Warnings;
    if (Material->MaterialGraph != nullptr)
    {
        // ponytail: warn rather than drive UMaterialGraph. Rebuilding the editor
        // graph would put another object graph in the transaction for a case
        // that only matters when a human already has the asset open. Upgrade to
        // MaterialGraph->Modify() + RebuildGraph() if that stops being rare.
        Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
            TEXT("The material editor is open on '%s'. Its graph view is not refreshed by this "
                 "command; close and reopen the asset to see the new nodes."), *AssetPath)));
    }

    Material->MaterialDomain = static_cast<EMaterialDomain>(Domain);
    Material->BlendMode = static_cast<EBlendMode>(BlendMode);
    Material->SetShadingModel(static_cast<EMaterialShadingModel>(ShadingModel));
    Material->TwoSided = bTwoSided;

    // Full replacement, which is what makes a rerun converge. The old
    // expressions are dropped from the array and NOT marked pending kill, so a
    // cancelled transaction restores an array of pointers to live objects whose
    // own inputs were never touched. Anything left unreferenced after a
    // successful build is collected normally and never saved.
    Material->Expressions.Empty(Nodes.Num());
    for (const MCPBridgeMaterialSnapshot::FMaterialPropertyEntry& Entry
            : MCPBridgeMaterialSnapshot::NamedMaterialProperties())
    {
        if (FExpressionInput* Input = Material->GetExpressionInputForProperty(Entry.Property))
        {
            Input->Expression = nullptr;
        }
    }

    // --- Create the nodes. ---
    for (FPlannedNode& Node : Nodes)
    {
        UMaterialExpression* Expression =
            NewObject<UMaterialExpression>(Material, Node.Class, NAME_None, RF_Transactional);
        if (Expression == nullptr)
        {
            return FailWithRollback(FString::Printf(
                TEXT("Unreal could not construct a %s for node '%s'."),
                *Node.Class->GetName(), *Node.Id));
        }
        Expression->Material = Material;
        Expression->MaterialExpressionEditorX = Node.X;
        Expression->MaterialExpressionEditorY = Node.Y;
        Expression->UpdateMaterialExpressionGuid(true, true);

        if (Node.bIsParameter)
        {
            Expression->SetParameterName(Node.ParameterName);
            if (!Node.Group.IsNone())
            {
                if (FNameProperty* GroupProperty =
                        CastField<FNameProperty>(Node.Class->FindPropertyByName(TEXT("Group"))))
                {
                    GroupProperty->SetPropertyValue_InContainer(Expression, Node.Group);
                }
            }
            switch (Node.Kind)
            {
                case EParameterKind::Scalar:
                    Cast<UMaterialExpressionScalarParameter>(Expression)->DefaultValue = Node.ScalarDefault;
                    break;
                case EParameterKind::Vector:
                    Cast<UMaterialExpressionVectorParameter>(Expression)->DefaultValue = Node.VectorDefault;
                    break;
                case EParameterKind::StaticSwitch:
                    Cast<UMaterialExpressionStaticBoolParameter>(Expression)->DefaultValue = Node.bSwitchDefault;
                    break;
                case EParameterKind::Texture:
                {
                    UMaterialExpressionTextureBase* TextureNode =
                        Cast<UMaterialExpressionTextureBase>(Expression);
                    TextureNode->Texture = Node.TextureDefault;
                    // A normal map read through a Color sampler is a compile
                    // error, and the caller did not ask about sampler types.
                    TextureNode->AutoSetSampleType();
                    break;
                }
            }
        }

        if (Node.Params.IsValid())
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Node.Params->Values)
            {
                FString PropertyError;
                if (!ApplyNodeProperty(Expression, Pair.Key, Pair.Value, PropertyError))
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("Node '%s': %s"), *Node.Id, *PropertyError));
                }
            }
            // Params may have set Texture directly on a TextureSample, so the
            // sampler type is resolved after they are applied too.
            if (UMaterialExpressionTextureBase* TextureNode =
                    Cast<UMaterialExpressionTextureBase>(Expression))
            {
                if (TextureNode->Texture != nullptr) { TextureNode->AutoSetSampleType(); }
            }
        }

        Node.Created = Expression;
        Material->Expressions.Add(Expression);
    }

    // --- Link the nodes. Every FExpressionInput written here belongs to an
    //     expression created above, inside this transaction. ---
    for (FPlannedLink& Link : Links)
    {
        FPlannedNode* From = FindNode(Link.FromId);
        FPlannedNode* To = FindNode(Link.ToId);
        const int32 OutputIndex = ResolveOutputIndex(From->Created, Link.FromOutput);
        if (OutputIndex == INDEX_NONE)
        {
            return FailWithRollback(FString::Printf(
                TEXT("Node '%s' (%s) has no output '%s'. Outputs: %s."),
                *Link.FromId, *From->Class->GetName(), *Link.FromOutput,
                *OutputNames(From->Created)));
        }
        const int32 InputIndex = ResolveInputIndex(To->Created, Link.ToInput);
        if (InputIndex == INDEX_NONE)
        {
            return FailWithRollback(FString::Printf(
                TEXT("Node '%s' (%s) has no input '%s'. Inputs: %s."),
                *Link.ToId, *To->Class->GetName(), *Link.ToInput, *InputNames(To->Created)));
        }
        const TArray<FExpressionInput*> Inputs = To->Created->GetInputs();
        FExpressionInput* Input = Inputs.IsValidIndex(InputIndex) ? Inputs[InputIndex] : nullptr;
        if (Input == nullptr)
        {
            return FailWithRollback(FString::Printf(
                TEXT("Node '%s' input '%s' resolved to index %d, which the node does not expose."),
                *Link.ToId, *Link.ToInput, InputIndex));
        }
        Input->Connect(OutputIndex, From->Created);
        Link.ResolvedToInputIndex = InputIndex;
    }

    // --- Link the material's own outputs. ---
    for (const FPlannedOutput& Output : Outputs)
    {
        FPlannedNode* From = FindNode(Output.FromId);
        const int32 OutputIndex = ResolveOutputIndex(From->Created, Output.FromOutput);
        if (OutputIndex == INDEX_NONE)
        {
            return FailWithRollback(FString::Printf(
                TEXT("outputs.%s: node '%s' (%s) has no output '%s'. Outputs: %s."),
                *Output.PropertyName, *Output.FromId, *From->Class->GetName(),
                *Output.FromOutput, *OutputNames(From->Created)));
        }
        FExpressionInput* Input = Material->GetExpressionInputForProperty(Output.Property);
        if (Input == nullptr)
        {
            return FailWithRollback(FString::Printf(
                TEXT("outputs.%s: this material has no input for that property in the current "
                     "domain, blend mode and shading model."), *Output.PropertyName));
        }
        Input->Connect(OutputIndex, From->Created);
    }

    // Rebuilds the parameter list, refreshes any child material instance's
    // parameter names, and recompiles. The engine's own path, rather than a
    // hand-rolled PostEditChange, so a child instance built earlier by
    // material_instance_build does not keep stale parameter names.
    UMaterialEditingLibrary::RecompileMaterial(Material);

    // --- Independent read-back, through the same reader material_inspect uses.
    //     Nothing above is believed until this agrees with it. ---
    TArray<TSharedPtr<FJsonValue>> ReadExpressions;
    TArray<TSharedPtr<FJsonValue>> ReadConnections;
    TArray<TSharedPtr<FJsonValue>> ReadMaterialInputs;
    TArray<TSharedPtr<FJsonValue>> ReadUnsupported;
    MCPBridgeMaterialSnapshot::DescribeGraph(
        Material, ReadExpressions, ReadConnections, ReadMaterialInputs, ReadUnsupported);

    TArray<FString> Mismatches;

    // Every requested link is present, addressed the way the inspector
    // addresses it: source expression object name, target object name, target
    // input index.
    for (const FPlannedLink& Link : Links)
    {
        const FString FromName = MCPBridgeMaterialSnapshot::ExpressionId(FindNode(Link.FromId)->Created);
        const FString ToName = MCPBridgeMaterialSnapshot::ExpressionId(FindNode(Link.ToId)->Created);
        bool bFound = false;
        for (const TSharedPtr<FJsonValue>& Value : ReadConnections)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Object)) { continue; }
            if ((*Object)->GetStringField(TEXT("from_expression_id")) == FromName
                && (*Object)->GetStringField(TEXT("to_expression_id")) == ToName
                && static_cast<int32>((*Object)->GetNumberField(TEXT("to_input_index")))
                    == Link.ResolvedToInputIndex)
            {
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            Mismatches.Add(FString::Printf(
                TEXT("the link %s -> %s.%s is not in the graph after the build."),
                *Link.FromId, *Link.ToId, *Link.ToInput));
        }
    }

    for (const FPlannedOutput& Output : Outputs)
    {
        const FString FromName = MCPBridgeMaterialSnapshot::ExpressionId(FindNode(Output.FromId)->Created);
        bool bFound = false;
        for (const TSharedPtr<FJsonValue>& Value : ReadMaterialInputs)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Object)) { continue; }
            if ((*Object)->GetStringField(TEXT("property")) == Output.PropertyName)
            {
                bFound = (*Object)->GetBoolField(TEXT("connected"))
                    && (*Object)->GetStringField(TEXT("expression_id")) == FromName;
                break;
            }
        }
        if (!bFound)
        {
            Mismatches.Add(FString::Printf(
                TEXT("%s is not driven by '%s' after the build."),
                *Output.PropertyName, *Output.FromId));
        }
    }

    // Every requested parameter is actually published by the material. This is
    // the check that matters most to a caller, because material_instance_build
    // refuses any name the parent does not publish.
    for (const FPlannedNode& Node : Nodes)
    {
        if (!Node.bIsParameter) { continue; }
        if (!MCPBridgeMaterialSnapshot::HasParameter(Material, Node.Kind, Node.ParameterName))
        {
            Mismatches.Add(FString::Printf(
                TEXT("the %s parameter '%s' is not published by the material after the build."),
                MCPBridgeMaterialSnapshot::ParameterKindName(Node.Kind), *Node.ParameterName.ToString()));
        }
    }

    if (Mismatches.Num() > 0)
    {
        return FailWithRollback(FString::Printf(
            TEXT("The material was built but the read-back disagrees: %s"),
            *FString::Join(Mismatches, TEXT(" "))));
    }

    // --- Compile, and report the result rather than assume it. ---
    TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> CompileErrors;
    bool bCompileFinished = false;
    FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIFeatureLevel);
    if (Resource != nullptr)
    {
        // Blocking on purpose: an asynchronous compile that has not finished
        // cannot tell a caller whether the material is valid, and answering
        // "requested" instead of "compiled" is the assumption this command
        // exists to remove.
        Resource->FinishCompilation();

        bCompileFinished = Resource->IsCompilationFinished();
        for (const FString& Error : Resource->GetCompileErrors())
        {
            CompileErrors.Add(MakeShared<FJsonValueString>(Error));
        }
    }
    Compile->SetBoolField(TEXT("resource_found"), Resource != nullptr);
    Compile->SetBoolField(TEXT("finished"), bCompileFinished);
    Compile->SetBoolField(TEXT("succeeded"),
        Resource != nullptr && bCompileFinished && CompileErrors.Num() == 0);
    Compile->SetArrayField(TEXT("errors"), CompileErrors);

    if (CompileErrors.Num() > 0)
    {
        // A master material's compile errors are always this request's fault:
        // unlike an instance, it has no parent to inherit them from.
        return FailWithRollback(FString::Printf(
            TEXT("The material did not compile cleanly: %s"),
            *FString::Join(Resource->GetCompileErrors(), TEXT(" "))));
    }

    // --- Save, only now. ---
    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(AssetPath, SaveError);
        if (!bSaved)
        {
            return FailWithRollback(FString::Printf(
                TEXT("The material was built and verified but could not be saved: %s"), *SaveError));
        }
    }

    Rollback.Commit();

    // The caller's ids and the engine's object names are different strings, and
    // material_inspect reports the engine's. Both are in the response so a
    // caller can correlate a spec it wrote with a graph it reads back.
    TArray<TSharedPtr<FJsonValue>> NodeReport;
    for (const FPlannedNode& Node : Nodes)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("id"), Node.Id);
        Entry->SetStringField(TEXT("expression_id"),
            MCPBridgeMaterialSnapshot::ExpressionId(Node.Created));
        Entry->SetStringField(TEXT("class"), Node.Class->GetName());
        Entry->SetBoolField(TEXT("is_parameter"), Node.bIsParameter);
        if (Node.bIsParameter)
        {
            Entry->SetStringField(TEXT("parameter_name"), Node.ParameterName.ToString());
            Entry->SetStringField(TEXT("parameter_kind"),
                MCPBridgeMaterialSnapshot::ParameterKindName(Node.Kind));
        }
        NodeReport.Add(MakeShared<FJsonValueObject>(Entry));
    }
    MCPBridgeBlueprintMembers::SortByStringField(NodeReport, TEXT("id"));

    TSharedPtr<FJsonObject> Result = BuildBaseResult();
    Result->SetBoolField(TEXT("created"), bCreating);
    Result->SetStringField(TEXT("object_path"), Material->GetPathName());
    Result->SetStringField(TEXT("domain"), ChoiceName(DomainChoices(), Domain));
    Result->SetStringField(TEXT("blend_mode"), ChoiceName(BlendModeChoices(), BlendMode));
    Result->SetStringField(TEXT("shading_model"), ChoiceName(ShadingModelChoices(), ShadingModel));
    Result->SetBoolField(TEXT("two_sided"), bTwoSided);
    Result->SetArrayField(TEXT("nodes"), NodeReport);
    Result->SetNumberField(TEXT("node_count"), Material->Expressions.Num());
    Result->SetNumberField(TEXT("connection_count"), Links.Num());
    Result->SetArrayField(TEXT("parameters_read_back"),
        MCPBridgeMaterialSnapshot::DescribeParameters(Material));
    Result->SetArrayField(TEXT("material_inputs"), ReadMaterialInputs);
    Result->SetObjectField(TEXT("compile"), Compile);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetStringField(TEXT("structure_hash_sha1"),
        MCPBridgeMaterialSnapshot::StructureHash(Material));
    Result->SetArrayField(TEXT("warnings"), Warnings);
    Result->SetObjectField(TEXT("rollback"),
        Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
    Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
    OutResultJson = SerializeJson(Result);
    return true;
}
