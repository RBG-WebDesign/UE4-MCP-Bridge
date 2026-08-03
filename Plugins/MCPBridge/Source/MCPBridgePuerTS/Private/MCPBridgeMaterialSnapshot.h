// Copyright 2026 RareBird Games. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "Json.h"
#include "MCPBridgeBlueprintMembers.h"
#include "MaterialEditingLibrary.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialLayersFunctions.h"
#include "SceneTypes.h"
#include "StaticParameterSet.h"

/**
 * The canonical read of a material, defined once and shared by the two commands
 * that need it: material_inspect, which reports it, and
 * material_instance_build, which compares it before and after a parameter
 * batch.
 *
 * One definition on purpose, for the same reason MCPBridgeBlueprintMembers.h is
 * one definition: a builder that measured its own success with a different
 * reader than the inspector a caller verifies with can report a change the
 * inspector cannot see.
 *
 * The hash comes from MCPBridgeBlueprintMembers::StructureHash rather than from
 * a second SHA1 here, so there is exactly one hashing scheme in this module.
 * Note that it returns LOWERCASE hex, matching graph_inspect's member hash; the
 * Behavior Tree and Widget inspectors spell their own digest uppercase. Compare
 * a hash against another read of the SAME command, never across commands.
 */
namespace MCPBridgeMaterialSnapshot
{
    enum class EParameterKind : uint8
    {
        Scalar,
        Vector,
        Texture,
        StaticSwitch,
    };

    inline const TCHAR* ParameterKindName(EParameterKind Kind)
    {
        switch (Kind)
        {
            case EParameterKind::Scalar:       return TEXT("scalar");
            case EParameterKind::Vector:       return TEXT("vector");
            case EParameterKind::Texture:      return TEXT("texture");
            case EParameterKind::StaticSwitch: return TEXT("static_switch");
            default:                           return TEXT("unknown");
        }
    }

    /** A function rather than an inline variable: inline variables are C++17
        and UE4.27 builds this module as C++14. */
    inline const TArray<EParameterKind>& AllParameterKinds()
    {
        static const TArray<EParameterKind> Kinds = {
            EParameterKind::Scalar,
            EParameterKind::Vector,
            EParameterKind::Texture,
            EParameterKind::StaticSwitch,
        };
        return Kinds;
    }

    /**
     * Every parameter of one kind that the material or its parent chain
     * publishes, canonically ordered.
     *
     * UMaterialInterface::GetAll*ParameterInfo is the single API that answers
     * for a UMaterial and for a UMaterialInstance alike, which is why the
     * per-kind name getters in UMaterialEditingLibrary are not used here: three
     * of the four kinds have one and static switches do not.
     */
    inline TArray<FName> ParameterNames(UMaterialInterface* Material, EParameterKind Kind)
    {
        TArray<FName> Names;
        if (Material == nullptr) { return Names; }

        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        switch (Kind)
        {
            case EParameterKind::Scalar:       Material->GetAllScalarParameterInfo(Infos, Ids); break;
            case EParameterKind::Vector:       Material->GetAllVectorParameterInfo(Infos, Ids); break;
            case EParameterKind::Texture:      Material->GetAllTextureParameterInfo(Infos, Ids); break;
            case EParameterKind::StaticSwitch: Material->GetAllStaticSwitchParameterInfo(Infos, Ids); break;
        }
        for (const FMaterialParameterInfo& Info : Infos)
        {
            Names.AddUnique(Info.Name);
        }
        Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
        return Names;
    }

    /** Does this material publish a parameter of this kind under this name? */
    inline bool HasParameter(UMaterialInterface* Material, EParameterKind Kind, const FName& Name)
    {
        return ParameterNames(Material, Kind).Contains(Name);
    }

    /**
     * The expression GUID behind one parameter.
     *
     * Needed only for static switches, whose override entry in
     * FStaticParameterSet carries the GUID of the parameter expression it
     * targets. GetAll*ParameterInfo returns the ids parallel to the infos,
     * which is the one 4.27 API that hands them over without going through
     * FHashedMaterialParameterInfo.
     */
    inline bool ParameterGuid(
        UMaterialInterface* Material,
        EParameterKind Kind,
        const FName& Name,
        FGuid& OutGuid)
    {
        if (Material == nullptr) { return false; }
        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        switch (Kind)
        {
            case EParameterKind::Scalar:       Material->GetAllScalarParameterInfo(Infos, Ids); break;
            case EParameterKind::Vector:       Material->GetAllVectorParameterInfo(Infos, Ids); break;
            case EParameterKind::Texture:      Material->GetAllTextureParameterInfo(Infos, Ids); break;
            case EParameterKind::StaticSwitch: Material->GetAllStaticSwitchParameterInfo(Infos, Ids); break;
        }
        for (int32 Index = 0; Index < Infos.Num(); ++Index)
        {
            if (Infos[Index].Name == Name && Ids.IsValidIndex(Index))
            {
                OutGuid = Ids[Index];
                return true;
            }
        }
        return false;
    }

    /**
     * The names closest to one a caller got wrong, so a refusal can name the
     * matches rather than only the miss. Substring matching in both directions,
     * case insensitive; no edit distance, because a parameter typo is nearly
     * always a case or prefix difference and a Levenshtein table here would be
     * more code than the failure is worth.
     */
    inline TArray<FString> ClosestParameterNames(
        UMaterialInterface* Material,
        EParameterKind Kind,
        const FName& Requested,
        int32 Limit = 5)
    {
        const FString Needle = Requested.ToString().ToLower();
        TArray<FString> Matches;
        TArray<FString> Others;
        for (const FName& Candidate : ParameterNames(Material, Kind))
        {
            const FString Text = Candidate.ToString();
            const FString Lower = Text.ToLower();
            if (Lower.Contains(Needle) || Needle.Contains(Lower)) { Matches.Add(Text); }
            else { Others.Add(Text); }
        }
        // With nothing similar, the whole (short) list is more useful than an
        // empty suggestion: the caller usually guessed a name that never existed.
        if (Matches.Num() == 0 && Others.Num() <= Limit * 2) { Matches = Others; }
        if (Matches.Num() > Limit) { Matches.SetNum(Limit); }
        return Matches;
    }

    /** True when a material instance overrides this parameter itself, rather
        than inheriting it from its parent. */
    inline bool IsOverridden(UMaterialInstance* Instance, EParameterKind Kind, const FName& Name)
    {
        if (Instance == nullptr) { return false; }
        switch (Kind)
        {
            case EParameterKind::Scalar:
                for (const FScalarParameterValue& Value : Instance->ScalarParameterValues)
                {
                    if (Value.ParameterInfo.Name == Name) { return true; }
                }
                return false;
            case EParameterKind::Vector:
                for (const FVectorParameterValue& Value : Instance->VectorParameterValues)
                {
                    if (Value.ParameterInfo.Name == Name) { return true; }
                }
                return false;
            case EParameterKind::Texture:
                for (const FTextureParameterValue& Value : Instance->TextureParameterValues)
                {
                    if (Value.ParameterInfo.Name == Name) { return true; }
                }
                return false;
            case EParameterKind::StaticSwitch:
                // StaticParameters is private in 4.27; GetStaticParameters is
                // the public accessor and is what the editor itself reads.
                for (const FStaticSwitchParameter& Value : Instance->GetStaticParameters().StaticSwitchParameters)
                {
                    if (Value.ParameterInfo.Name == Name) { return Value.bOverride; }
                }
                return false;
            default:
                return false;
        }
    }

    /**
     * The effective value of one parameter, as the value field of a JSON
     * object: a number for scalar, {r,g,b,a} for vector, an asset path for
     * texture, a bool for a static switch.
     *
     * UMaterialEditingLibrary is the reader for both shapes. Its
     * GetMaterialInstance* functions resolve through the instance's parent
     * chain, and its GetMaterialDefault* functions read the parameter
     * expression's own default, so an inherited value and an authored default
     * are reported the same way and a caller does not have to know which it
     * got.
     */
    inline void SetParameterValueField(
        const TSharedPtr<FJsonObject>& Out,
        UMaterialInterface* Material,
        EParameterKind Kind,
        const FName& Name)
    {
        UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Material);
        UMaterial* AsMaterial = Cast<UMaterial>(Material);
        if (Instance == nullptr && AsMaterial == nullptr)
        {
            // Neither reader applies (a UMaterialInstanceDynamic, which is not
            // an asset and cannot reach this code from an asset path). Say so
            // rather than hand a null to the editing library.
            Out->SetField(TEXT("value"), MakeShared<FJsonValueNull>());
            return;
        }
        switch (Kind)
        {
            case EParameterKind::Scalar:
                Out->SetNumberField(TEXT("value"), Instance != nullptr
                    ? UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(Instance, Name)
                    : UMaterialEditingLibrary::GetMaterialDefaultScalarParameterValue(AsMaterial, Name));
                return;
            case EParameterKind::Vector:
            {
                const FLinearColor Color = Instance != nullptr
                    ? UMaterialEditingLibrary::GetMaterialInstanceVectorParameterValue(Instance, Name)
                    : UMaterialEditingLibrary::GetMaterialDefaultVectorParameterValue(AsMaterial, Name);
                TSharedPtr<FJsonObject> Value = MakeShared<FJsonObject>();
                Value->SetNumberField(TEXT("r"), Color.R);
                Value->SetNumberField(TEXT("g"), Color.G);
                Value->SetNumberField(TEXT("b"), Color.B);
                Value->SetNumberField(TEXT("a"), Color.A);
                Out->SetObjectField(TEXT("value"), Value);
                return;
            }
            case EParameterKind::Texture:
            {
                const UTexture* Texture = Instance != nullptr
                    ? UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(Instance, Name)
                    : UMaterialEditingLibrary::GetMaterialDefaultTextureParameterValue(AsMaterial, Name);
                Out->SetStringField(TEXT("value"), Texture != nullptr ? Texture->GetPathName() : FString());
                return;
            }
            case EParameterKind::StaticSwitch:
                Out->SetBoolField(TEXT("value"), Instance != nullptr
                    ? UMaterialEditingLibrary::GetMaterialInstanceStaticSwitchParameterValue(Instance, Name)
                    : UMaterialEditingLibrary::GetMaterialDefaultStaticSwitchParameterValue(AsMaterial, Name));
                return;
        }
    }

    /** One parameter: name, kind, effective value, and whether an instance
        overrides it. Canonically keyed by "name" so the array sorts. */
    inline TSharedPtr<FJsonObject> DescribeParameter(
        UMaterialInterface* Material,
        EParameterKind Kind,
        const FName& Name)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("name"), Name.ToString());
        Out->SetStringField(TEXT("type"), ParameterKindName(Kind));
        Out->SetBoolField(TEXT("overridden"), IsOverridden(Cast<UMaterialInstance>(Material), Kind, Name));
        SetParameterValueField(Out, Material, Kind, Name);
        return Out;
    }

    /** Every parameter of every kind, ordered by "<type>:<name>" so the array
        is canonical across reads and two kinds cannot collide on one name. */
    inline TArray<TSharedPtr<FJsonValue>> DescribeParameters(UMaterialInterface* Material)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        if (Material == nullptr) { return Out; }
        for (const EParameterKind Kind : AllParameterKinds())
        {
            for (const FName& Name : ParameterNames(Material, Kind))
            {
                TSharedPtr<FJsonObject> Entry = DescribeParameter(Material, Kind, Name);
                Entry->SetStringField(TEXT("id"),
                    FString::Printf(TEXT("%s:%s"), ParameterKindName(Kind), *Name.ToString()));
                Out.Add(MakeShared<FJsonValueObject>(Entry));
            }
        }
        MCPBridgeBlueprintMembers::SortByStringField(Out, TEXT("id"));
        return Out;
    }

    /**
     * A material expression's stable identity.
     *
     * The UObject name is unique inside the material's package and is
     * serialized, so unlike a UMG widget or a Behavior Tree node this does not
     * have to be derived from a traversal path. Reported as identity_kind
     * "observed", the same word graph_inspect uses for a node addressed by its
     * object name and NodeGuid.
     */
    inline FString ExpressionId(const UMaterialExpression* Expression)
    {
        return Expression != nullptr ? Expression->GetName() : FString();
    }

    /** The material property inputs worth reporting: the ones a 4.27 material
        editor actually shows. The hidden entries of EMaterialProperty
        (Lightmass-internal, customized UVs, custom outputs) are left out
        because they are not authored by a caller and would be noise in every
        response. */
    struct FMaterialPropertyEntry
    {
        EMaterialProperty Property;
        const TCHAR* Name;
    };

    inline const TArray<FMaterialPropertyEntry>& NamedMaterialProperties()
    {
        static const TArray<FMaterialPropertyEntry> Properties = {
            { MP_BaseColor,           TEXT("BaseColor") },
            { MP_Metallic,            TEXT("Metallic") },
            { MP_Specular,            TEXT("Specular") },
            { MP_Roughness,           TEXT("Roughness") },
            { MP_Anisotropy,          TEXT("Anisotropy") },
            { MP_EmissiveColor,       TEXT("EmissiveColor") },
            { MP_Opacity,             TEXT("Opacity") },
            { MP_OpacityMask,         TEXT("OpacityMask") },
            { MP_Normal,              TEXT("Normal") },
            { MP_Tangent,             TEXT("Tangent") },
            { MP_WorldPositionOffset, TEXT("WorldPositionOffset") },
            { MP_SubsurfaceColor,     TEXT("SubsurfaceColor") },
            { MP_AmbientOcclusion,    TEXT("AmbientOcclusion") },
            { MP_Refraction,          TEXT("Refraction") },
            { MP_PixelDepthOffset,    TEXT("PixelDepthOffset") },
        };
        return Properties;
    }

    /**
     * The expression graph of a master material: the nodes, the links between
     * them, and the links into the material's own outputs.
     *
     * Read only. Nothing here calls Modify, MarkPackageDirty or a compile.
     * GetInputs and GetInputName are editor-only virtuals, which is fine: this
     * whole module is an editor module.
     */
    inline void DescribeGraph(
        UMaterial* Material,
        TArray<TSharedPtr<FJsonValue>>& OutExpressions,
        TArray<TSharedPtr<FJsonValue>>& OutConnections,
        TArray<TSharedPtr<FJsonValue>>& OutMaterialInputs,
        TArray<TSharedPtr<FJsonValue>>& OutUnsupported)
    {
        if (Material == nullptr) { return; }

        for (int32 Index = 0; Index < Material->Expressions.Num(); ++Index)
        {
            UMaterialExpression* Expression = Material->Expressions[Index];
            if (Expression == nullptr)
            {
                OutUnsupported.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("expressions[%d] is null in the asset"), Index)));
                continue;
            }

            const FString Id = ExpressionId(Expression);
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("id"), Id);
            Entry->SetStringField(TEXT("name"), Expression->GetName());
            Entry->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
            Entry->SetStringField(TEXT("class_path"), Expression->GetClass()->GetPathName());
            Entry->SetNumberField(TEXT("editor_x"), Expression->MaterialExpressionEditorX);
            Entry->SetNumberField(TEXT("editor_y"), Expression->MaterialExpressionEditorY);
            Entry->SetStringField(TEXT("description"), Expression->Desc);
            // Only parameter expressions are guaranteed to carry a generated
            // GUID, so an empty string here means "this node type has none",
            // not "the read failed".
            Entry->SetStringField(TEXT("guid"), Expression->MaterialExpressionGuid.IsValid()
                ? Expression->MaterialExpressionGuid.ToString() : FString());
            if (Expression->HasAParameterName())
            {
                Entry->SetStringField(TEXT("parameter_name"), Expression->GetParameterName().ToString());
            }

            const TArray<FExpressionInput*> Inputs = Expression->GetInputs();
            TArray<TSharedPtr<FJsonValue>> InputEntries;
            for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
            {
                FExpressionInput* Input = Inputs[InputIndex];
                if (Input == nullptr) { continue; }
                const FString InputName = Expression->GetInputName(InputIndex).ToString();

                TSharedPtr<FJsonObject> InputEntry = MakeShared<FJsonObject>();
                InputEntry->SetNumberField(TEXT("index"), InputIndex);
                InputEntry->SetStringField(TEXT("name"), InputName);
                InputEntry->SetBoolField(TEXT("connected"), Input->Expression != nullptr);
                InputEntries.Add(MakeShared<FJsonValueObject>(InputEntry));

                if (Input->Expression == nullptr) { continue; }
                const FString FromId = ExpressionId(Input->Expression);
                TSharedPtr<FJsonObject> Connection = MakeShared<FJsonObject>();
                // The id doubles as the canonical sort key, which is why it
                // carries every field that distinguishes one link from another.
                Connection->SetStringField(TEXT("id"), FString::Printf(TEXT("%s:%d->%s:%d"),
                    *FromId, Input->OutputIndex, *Id, InputIndex));
                Connection->SetStringField(TEXT("from_expression_id"), FromId);
                Connection->SetNumberField(TEXT("from_output_index"), Input->OutputIndex);
                Connection->SetStringField(TEXT("to_expression_id"), Id);
                Connection->SetNumberField(TEXT("to_input_index"), InputIndex);
                Connection->SetStringField(TEXT("to_input_name"), InputName);
                OutConnections.Add(MakeShared<FJsonValueObject>(Connection));
            }
            Entry->SetArrayField(TEXT("inputs"), InputEntries);
            OutExpressions.Add(MakeShared<FJsonValueObject>(Entry));
        }

        for (const FMaterialPropertyEntry& Property : NamedMaterialProperties())
        {
            FExpressionInput* Input = Material->GetExpressionInputForProperty(Property.Property);
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("property"), Property.Name);
            Entry->SetBoolField(TEXT("connected"), Input != nullptr && Input->Expression != nullptr);
            Entry->SetStringField(TEXT("expression_id"),
                Input != nullptr ? ExpressionId(Input->Expression) : FString());
            Entry->SetNumberField(TEXT("output_index"), Input != nullptr ? Input->OutputIndex : 0);
            OutMaterialInputs.Add(MakeShared<FJsonValueObject>(Entry));
        }

        MCPBridgeBlueprintMembers::SortByStringField(OutExpressions, TEXT("id"));
        MCPBridgeBlueprintMembers::SortByStringField(OutConnections, TEXT("id"));
        MCPBridgeBlueprintMembers::SortByStringField(OutMaterialInputs, TEXT("property"));
    }

    /**
     * The canonical structure of a material, for hashing and for comparison.
     *
     * STRUCTURE, so parameter VALUES are excluded on purpose, exactly as the
     * Widget inspector excludes widget properties: retinting an instance must
     * not read as a reshape of the material. What is included is the shape a
     * caller authored - which parameters exist, of what kind, which ones an
     * instance overrides, the expression nodes, and the links.
     *
     * material_instance_build therefore does NOT use this hash to decide
     * whether its writes landed. It re-reads each parameter's value and
     * compares it, which is the check that actually distinguishes a applied
     * write from a dropped one.
     */
    inline TSharedPtr<FJsonObject> BuildSnapshot(UMaterialInterface* Material)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        if (Material == nullptr) { return Out; }

        UMaterial* AsMaterial = Cast<UMaterial>(Material);
        UMaterialInstance* AsInstance = Cast<UMaterialInstance>(Material);
        Out->SetStringField(TEXT("kind"), AsMaterial != nullptr ? TEXT("material") : TEXT("material_instance"));
        Out->SetStringField(TEXT("parent_path"),
            AsInstance != nullptr && AsInstance->Parent != nullptr
                ? AsInstance->Parent->GetPathName() : FString());

        TArray<TSharedPtr<FJsonValue>> Parameters;
        for (const EParameterKind Kind : AllParameterKinds())
        {
            for (const FName& Name : ParameterNames(Material, Kind))
            {
                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("id"),
                    FString::Printf(TEXT("%s:%s"), ParameterKindName(Kind), *Name.ToString()));
                Entry->SetBoolField(TEXT("overridden"), IsOverridden(AsInstance, Kind, Name));
                Parameters.Add(MakeShared<FJsonValueObject>(Entry));
            }
        }
        MCPBridgeBlueprintMembers::SortByStringField(Parameters, TEXT("id"));
        Out->SetArrayField(TEXT("parameters"), Parameters);

        TArray<TSharedPtr<FJsonValue>> Expressions;
        TArray<TSharedPtr<FJsonValue>> Connections;
        TArray<TSharedPtr<FJsonValue>> MaterialInputs;
        TArray<TSharedPtr<FJsonValue>> Unsupported;
        DescribeGraph(AsMaterial, Expressions, Connections, MaterialInputs, Unsupported);

        // Node positions and descriptions are cosmetic; a moved node is not a
        // different graph, so the hashed view carries identity and class only.
        TArray<TSharedPtr<FJsonValue>> ExpressionKeys;
        for (const TSharedPtr<FJsonValue>& Value : Expressions)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Object)) { continue; }
            TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
            Key->SetStringField(TEXT("id"), FString::Printf(TEXT("%s|%s"),
                *(*Object)->GetStringField(TEXT("id")),
                *(*Object)->GetStringField(TEXT("class_path"))));
            ExpressionKeys.Add(MakeShared<FJsonValueObject>(Key));
        }
        MCPBridgeBlueprintMembers::SortByStringField(ExpressionKeys, TEXT("id"));
        Out->SetArrayField(TEXT("expressions"), ExpressionKeys);
        Out->SetArrayField(TEXT("connections"), Connections);
        Out->SetArrayField(TEXT("material_inputs"), MaterialInputs);
        return Out;
    }

    /** SHA-1 over the canonical snapshot, through the one hashing function this
        module has. */
    inline FString StructureHash(UMaterialInterface* Material)
    {
        return MCPBridgeBlueprintMembers::StructureHash(BuildSnapshot(Material));
    }
}
