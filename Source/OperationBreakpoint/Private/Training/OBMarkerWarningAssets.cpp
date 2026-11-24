#if WITH_EDITOR
#include "Training/OBTrainingStatusWidget.h"
#include "Widget/OBRadialProgressBar.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ObjectTools.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/DateTime.h"
#include "UObject/SavePackage.h"
#include "Animation/WidgetAnimation.h"
#include "AssetCompilingManager.h"

namespace
{
bool SaveMarkerAsset(UObject* Object)
{
    Object->MarkPackageDirty();
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Object->GetOutermost(),Object,*FPackageName::LongPackageNameToFilename(Object->GetOutermost()->GetName(),TEXT(".uasset")),Args);
}
UMaterial* CreateAimGaugeMaterial(UTexture2D* Gauge)
{
    const TCHAR* Path=TEXT("/Game/Blueprints/Widget/PlayerOverlay/Art/Markers/M_TrainingAimGauge");
    if(auto* Existing=LoadObject<UMaterial>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Art/Markers/M_TrainingAimGauge.M_TrainingAimGauge"),nullptr,LOAD_NoWarn))return Existing;
    auto* Material=NewObject<UMaterial>(CreatePackage(Path),TEXT("M_TrainingAimGauge"),RF_Public|RF_Standalone);
    Material->MaterialDomain=MD_UI;Material->BlendMode=BLEND_Translucent;
    auto Add=[&](UMaterialExpression* E){Material->GetExpressionCollection().AddExpression(E);};
    auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(Material);Add(UV);
    auto* Texture=NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);Add(Texture);
    Texture->ParameterName=TEXT("GaugeTexture");Texture->Texture=Gauge;Texture->Coordinates.Expression=UV;
    auto* Progress=NewObject<UMaterialExpressionScalarParameter>(Material);Add(Progress);Progress->ParameterName=TEXT("Progress");Progress->DefaultValue=.5f;
    auto* Mask=NewObject<UMaterialExpressionCustom>(Material);Add(Mask);Mask->OutputType=CMOT_Float1;
    Mask->Description=TEXT("Clockwise Gauge_Green reveal from twelve o'clock");
    Mask->Code=TEXT("if(Progress<=0)return 0; if(Progress>=1)return Alpha; float2 p=UV-0.5; float angle=frac(atan2(p.x,-p.y)/6.28318530718+1); return Alpha*step(angle,saturate(Progress));");
    FCustomInput A;A.InputName=TEXT("UV");A.Input.Expression=UV;Mask->Inputs.Add(A);
    FCustomInput B;B.InputName=TEXT("Progress");B.Input.Expression=Progress;Mask->Inputs.Add(B);
    FCustomInput C;C.InputName=TEXT("Alpha");C.Input.Expression=Texture;C.Input.OutputIndex=4;Mask->Inputs.Add(C);
    Material->GetEditorOnlyData()->EmissiveColor.Expression=Texture;
    Material->GetEditorOnlyData()->Opacity.Expression=Mask;
    Material->PostEditChange();FAssetRegistryModule::AssetCreated(Material);
    return SaveMarkerAsset(Material)?Material:nullptr;
}
void PrepareMarkerBlueprint(UWidgetBlueprint* BP)
{
    TSet<FName> Names;
    BP->WidgetTree->ForEachWidget([&](UWidget* W){W->bIsVariable=true;Names.Add(W->GetFName());});
    for(const auto& Animation:BP->Animations)if(Animation)Names.Add(Animation->GetFName());
    for(auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It)if(!Names.Contains(It.Key()))It.RemoveCurrent();
    for(FName Name:Names)if(!BP->WidgetVariableNameToGuidMap.Contains(Name))BP->OnVariableAdded(Name);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    for(auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It)if(!Names.Contains(It.Key()))It.RemoveCurrent();
    FKismetEditorUtilities::CompileBlueprint(BP);
}
void ApplyAimBrushes(UWidgetTree* Tree,UTexture2D* Red,UMaterial* Material)
{
    if(!Tree)return;
    if(auto* Target=Cast<UImage>(Tree->FindWidget(TEXT("AimTarget")))){Target->SetBrushFromTexture(Red,false);Target->SetColorAndOpacity(FLinearColor::White);}
    if(auto* Bar=Cast<UOBRadialProgressBar>(Tree->FindWidget(TEXT("AimProgress"))))
    {
        auto Style=Bar->GetWidgetStyle();Style.BackgroundImage.DrawAs=ESlateBrushDrawType::NoDrawType;
        Style.FillImage.SetResourceObject(Material);Style.FillImage.DrawAs=ESlateBrushDrawType::Image;Style.FillImage.Margin=FMargin(0);Style.FillImage.TintColor=FLinearColor::White;
        Bar->SetWidgetStyle(Style);Bar->SetFillColorAndOpacity(FLinearColor::White);Bar->SetBorderPadding(FVector2D::ZeroVector);
        Bar->SetBarFillStyle(EProgressBarFillStyle::Mask);Bar->SetRadialPercent(0);
    }
}
void ConfigureMarkerWarning()
{
    auto Fail=[](const TCHAR* Why){UE_LOG(LogTemp,Error,TEXT("MARKER_WARNING_ASSETS FAIL %s"),Why);FPlatformMisc::RequestExitWithStatus(false,1);};
    const FString Folder=TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/");
    auto* Marker=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+TEXT("WBP_TrainingMarkers.WBP_TrainingMarkers")));
    auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
    auto* Pause=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+TEXT("WBP_Pause.WBP_Pause")),nullptr,LOAD_NoWarn);
    auto* Red=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Training_UI/Aim/Aim_Red.Aim_Red"));
    auto* Green=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Training_UI/Aim/Aim_Green.Aim_Green"));
    auto* Gauge=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Training_UI/Aim/Gauge_Green.Gauge_Green"));
    if(!Marker || !Overlay || !Marker->WidgetTree || !Overlay->WidgetTree || !Red || !Green || !Gauge){Fail(TEXT("Required assets"));return;}
    const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups")/(TEXT("MarkerWarning_")+FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S")));
    IFileManager::Get().MakeDirectory(*Backup,true);
    for(UObject* Asset:{static_cast<UObject*>(Marker),static_cast<UObject*>(Overlay),static_cast<UObject*>(Pause)})
    {
        if(!Asset)continue;
        const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),TEXT(".uasset"));
        if(IFileManager::Get().Copy(*(Backup/FPaths::GetCleanFilename(File)),*File)!=COPY_OK){Fail(TEXT("Backup"));return;}
    }
    for(auto* Texture:{Red,Green,Gauge})
    {
        const FString File=FPackageName::LongPackageNameToFilename(Texture->GetOutermost()->GetName(),TEXT(".uasset"));
        if(IFileManager::Get().Copy(*(Backup/FPaths::GetCleanFilename(File)),*File)!=COPY_OK){Fail(TEXT("Texture backup"));return;}
        UE_LOG(LogTemp,Display,TEXT("MARKER_TEXTURE name=%s previous_group=%d previous_vt=%d"),*Texture->GetName(),int32(Texture->LODGroup),Texture->VirtualTextureStreaming);
        Texture->Modify();Texture->VirtualTextureStreaming=false;Texture->NeverStream=true;
        Texture->LODGroup=TEXTUREGROUP_UI;Texture->MipGenSettings=TMGS_NoMipmaps;Texture->PostEditChange();
    }
    FAssetCompilingManager::Get().FinishAllCompilation();
    for(auto* Texture:{Red,Green,Gauge})if(!SaveMarkerAsset(Texture)){Fail(TEXT("UI texture save"));return;}
    auto* Material=CreateAimGaugeMaterial(Gauge);if(!Material){Fail(TEXT("Gauge material"));return;}
    auto Remove=[&](UWidgetBlueprint* BP,const TCHAR* Name)
    {
        if(auto* Old=BP->WidgetTree->FindWidget(Name)){BP->WidgetTree->RemoveWidget(Old);Old->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);}
    };
    auto* Target=Cast<UImage>(Marker->WidgetTree->FindWidget(TEXT("AimTarget")));
    auto* Layer=Cast<UCanvasPanel>(Marker->WidgetTree->FindWidget(TEXT("AimLayer")));
    auto* TargetSlot=Target?Cast<UCanvasPanelSlot>(Target->Slot):nullptr;
    if(!Layer || !TargetSlot){Fail(TEXT("Aim source hierarchy"));return;}
    const FAnchorData Layout=TargetSlot->GetLayout();
    const int32 ZOrder=TargetSlot->GetZOrder();
    if(!Cast<UOBRadialProgressBar>(Marker->WidgetTree->FindWidget(TEXT("AimProgress"))))
    {
        Remove(Marker,TEXT("AimProgress"));
        auto* Bar=Marker->WidgetTree->ConstructWidget<UOBRadialProgressBar>(UOBRadialProgressBar::StaticClass(),TEXT("AimProgress"));
        auto* Slot=Layer->AddChildToCanvas(Bar);Slot->SetLayout(Layout);Slot->SetAutoSize(false);Slot->SetZOrder(ZOrder+1);
    }
    Remove(Marker,TEXT("AimOutline"));Remove(Marker,TEXT("AimProgressOutline"));
    ApplyAimBrushes(Marker->WidgetTree,Red,Material);
    Remove(Overlay,TEXT("Pause"));
    PrepareMarkerBlueprint(Marker);PrepareMarkerBlueprint(Overlay);
    if(Marker->Status==BS_Error || Overlay->Status==BS_Error){Fail(TEXT("Blueprint compilation; backups retained"));return;}
    if(auto* Class=Cast<UWidgetBlueprintGeneratedClass>(Marker->GeneratedClass))
    {
        auto* Defaults=CastChecked<UOBTrainingStatusWidget>(Class->GetDefaultObject());Defaults->AimIdleTexture=Red;Defaults->AimSuccessTexture=Green;
        ApplyAimBrushes(Class->GetWidgetTreeArchetype(),Red,Material);
    }
    if(auto* Class=Cast<UWidgetBlueprintGeneratedClass>(Overlay->GeneratedClass))CastChecked<UOBPlayerOverlayWidget>(Class->GetDefaultObject())->TrainingElements&=~2;
    if(!SaveMarkerAsset(Marker) || !SaveMarkerAsset(Overlay)){Fail(TEXT("Save widgets"));return;}
    // Delete in a fresh editor process, after old Blueprint reinstancing/undo
    // references have gone away. No forced deletion of unrelated references.
    UE_LOG(LogTemp,Display,TEXT("MARKER_WARNING_ASSETS PASS radial_progress=1 pause_detached=1 backup=%s"),*Backup);
    FPlatformMisc::RequestExitWithStatus(false,0);
}
static FAutoConsoleCommand GMarkerWarningAssets(TEXT("OB.UI.ConfigureMarkerWarning"),TEXT("Back up and migrate AimHold artwork/radial bar; remove obsolete Pause WBP after detaching it."),FConsoleCommandDelegate::CreateStatic(&ConfigureMarkerWarning));
static FAutoConsoleCommand GDeleteLegacyPause(TEXT("OB.UI.DeleteLegacyPause"),TEXT("Delete detached WBP_Pause in a fresh editor process; checks remaining asset references."),FConsoleCommandDelegate::CreateLambda([](){
    const FName Package(TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Pause"));
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();Registry.SearchAllAssets(true);
    TArray<FName> References;Registry.GetReferencers(Package,References);
    References.Remove(Package);
    if(!References.IsEmpty())
    {
        for(FName Ref:References)UE_LOG(LogTemp,Error,TEXT("LEGACY_PAUSE_DELETE referenced_by=%s"),*Ref.ToString());
        FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    auto* Pause=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Pause.WBP_Pause"),nullptr,LOAD_NoWarn);
    const bool OK=!Pause || ObjectTools::DeleteObjects({Pause},false)==1;
    UE_LOG(LogTemp,Display,TEXT("LEGACY_PAUSE_DELETE outcome=%s"),OK?TEXT("PASS"):TEXT("FAIL"));
    FPlatformMisc::RequestExitWithStatus(false,OK?0:1);
}));
}
#endif
