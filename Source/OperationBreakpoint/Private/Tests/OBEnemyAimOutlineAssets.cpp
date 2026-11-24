#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetCompilingManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace
{
void CreateEnemyAimOutlineMaterial()
{
    const TCHAR* PackagePath=TEXT("/Game/Assets/Materials/M_PP_AimOutline");
    auto* Material=LoadObject<UMaterial>(nullptr,TEXT("/Game/Assets/Materials/M_PP_AimOutline.M_PP_AimOutline"),nullptr,LOAD_NoWarn);
    if (!Material) Material=NewObject<UMaterial>(CreatePackage(PackagePath),TEXT("M_PP_AimOutline"),RF_Public|RF_Standalone);
    Material->GetExpressionCollection().Empty();
    Material->MaterialDomain=MD_PostProcess;
    Material->BlendableLocation=BL_SceneColorAfterTonemapping;
    auto Add=[&](UMaterialExpression* E) { Material->GetExpressionCollection().AddExpression(E); };
    auto* Scene=NewObject<UMaterialExpressionSceneTexture>(Material);Add(Scene);Scene->SceneTextureId=PPI_PostProcessInput0;
    auto* Stencil=NewObject<UMaterialExpressionSceneTexture>(Material);Add(Stencil);Stencil->SceneTextureId=PPI_CustomStencil;
    auto* Depth=NewObject<UMaterialExpressionSceneTexture>(Material);Add(Depth);Depth->SceneTextureId=PPI_SceneDepth;
    auto* CustomDepth=NewObject<UMaterialExpressionSceneTexture>(Material);Add(CustomDepth);CustomDepth->SceneTextureId=PPI_CustomDepth;
    auto* Debug=NewObject<UMaterialExpressionScalarParameter>(Material);Add(Debug);Debug->ParameterName=TEXT("DebugMode");Debug->DefaultValue=0;
    auto* Outline=NewObject<UMaterialExpressionCustom>(Material);Add(Outline);
    Outline->OutputType=CMOT_Float3;
    Outline->Description=TEXT("Two-pixel aim silhouette: enemy 253 orange, hostage 252 green");
    for (const auto& Entry:TArray<TPair<FName,UMaterialExpression*>>{{TEXT("SceneColor"),Scene},{TEXT("Stencil"),Stencil},{TEXT("Depth"),Depth},{TEXT("CustomDepth"),CustomDepth},{TEXT("DebugMode"),Debug}})
    { FCustomInput Input;Input.InputName=Entry.Key;Input.Input.Expression=Entry.Value;Outline->Inputs.Add(Input); }
    Outline->Code=FString::Printf(TEXT(
        "if(DebugMode>1.5)return float3(1,0.55,0.035);\n"
        "if(DebugMode>0.5)return float3(Stencil.r/255,Depth.r/10000,CustomDepth.r/10000);\n"
        "float2 uv=GetDefaultSceneTextureUV(Parameters,%d);\n"
        "float2 pixel=GetSceneTextureBufferSize(%d).zw*2.0;\n"
        "float2 center=1-step(0.5,abs(Stencil.r-float2(253,252)));\n"
        "float2 edge=0;\n"
        "[unroll] for(int y=-1;y<=1;y++) { [unroll] for(int x=-1;x<=1;x++) {\n"
        "float2 p=ClampSceneTextureUV(uv+float2(x,y)*pixel,%d);\n"
        "float s=SceneTextureLookup(p,%d,false).r;\n"
        "float cd=SceneTextureLookup(p,%d,false).r;\n"
        "float d=SceneTextureLookup(p,%d,false).r;\n"
        "edge=max(edge,(1-step(0.5,abs(s-float2(253,252))))*step(cd,d+2));\n"
        "}}\n"
        "float2 mask=edge*(1-center);\n"
        "float3 color=mask.y>mask.x ? float3(0.1,1,0.25) : float3(1,0.55,0.035);\n"
        "return lerp(SceneColor.rgb,color,saturate(max(mask.x,mask.y)));\n"),
        int32(PPI_CustomStencil),int32(PPI_CustomStencil),int32(PPI_CustomStencil),int32(PPI_CustomStencil),int32(PPI_CustomDepth),int32(PPI_SceneDepth));
    Material->GetEditorOnlyData()->EmissiveColor.Expression=Outline;
    Material->PostEditChange();FAssetRegistryModule::AssetCreated(Material);
    FAssetCompilingManager::Get().FinishAllCompilation();
    Material->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    const bool Saved=UPackage::SavePackage(Material->GetOutermost(),Material,*FPackageName::LongPackageNameToFilename(PackagePath,TEXT(".uasset")),Args);
    UE_LOG(LogTemp,Display,TEXT("AIM_OUTLINE_ASSET saved=%d"),Saved);
}
}
static FAutoConsoleCommand GOBCreateEnemyAimOutlineMaterial(TEXT("OB.Editor.CreateAimOutline"),TEXT("Create the character Custom Stencil outline material."),FConsoleCommandDelegate::CreateStatic(&CreateEnemyAimOutlineMaterial));
#endif
