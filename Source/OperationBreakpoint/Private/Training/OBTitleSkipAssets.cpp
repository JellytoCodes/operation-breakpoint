#if WITH_EDITOR
#include "Widget/OBTitleWidget.h"
#include "Widget/OBTrainingSkipWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionConstant.h"
#include "MediaTexture.h"
#include "FileMediaSource.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Animation/WidgetAnimation.h"

namespace
{
bool SaveTitleAsset(UObject* Asset)
{
    Asset->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Asset->GetOutermost(),Asset,*FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args);
}
UTextBlock* TitleLabel(UWidgetTree* Tree,FName Name,const TCHAR* Text,int32 FontSize,FLinearColor Color)
{
    auto* Label=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Label->SetText(FText::FromString(Text)); Label->SetColorAndOpacity(Color); Label->SetJustification(ETextJustify::Center);
    auto Font=Label->GetFont(); Font.Size=FontSize;
    if (auto* Face=LoadObject<UObject>(nullptr,TEXT("/Game/Assets/UI/Fonts/Pretendard-Bold_Font.Pretendard-Bold_Font"))) Font.FontObject=Face;
    Label->SetFont(Font); return Label;
}
void CompileTitleWidget(UWidgetBlueprint* BP)
{
    TSet<FName> Names;
    BP->WidgetTree->ForEachWidget([&](UWidget* Widget) {
        Names.Add(Widget->GetFName());
        Widget->bIsVariable=true;
        if (!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName())) BP->OnVariableAdded(Widget->GetFName());
    });
    for (const auto& Animation : BP->Animations)
        if (Animation)
        {
            Names.Add(Animation->GetFName());
            if (!BP->WidgetVariableNameToGuidMap.Contains(Animation->GetFName())) BP->OnVariableAdded(Animation->GetFName());
        }
    for (auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It) if (!Names.Contains(It.Key())) It.RemoveCurrent();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
}
void SetTitleSlots(UWidgetTree* Tree)
{
    if (!Tree) return;
    struct FPlacement { const TCHAR* Name; FVector2D Anchor,Position,Size; };
    for (const auto& P : {
        FPlacement{TEXT("BackgroundVideo"),FVector2D::ZeroVector,FVector2D::ZeroVector,FVector2D::ZeroVector},
        FPlacement{TEXT("LogoImage"),FVector2D(.5,.5),FVector2D(0,-210),FVector2D(300,140)},
        FPlacement{TEXT("TitleImage"),FVector2D(.5,.5),FVector2D(0,-20),FVector2D(760,180)},
        FPlacement{TEXT("PressText"),FVector2D(.5,.84),FVector2D::ZeroVector,FVector2D(700,60)}})
    {
        auto* W=Tree->FindWidget(P.Name); auto* Slot=W ? Cast<UCanvasPanelSlot>(W->Slot) : nullptr;
        if (!Slot) continue;
        Slot->SetAutoSize(false);
        const bool bBackground=FString(P.Name)==TEXT("BackgroundVideo");
        Slot->SetAnchors(bBackground ? FAnchors(0,0,1,1) : FAnchors(P.Anchor.X,P.Anchor.Y));
        Slot->SetAlignment(bBackground ? FVector2D::ZeroVector : FVector2D(.5,.5));
        if (bBackground) Slot->SetOffsets(FMargin(0)); else { Slot->SetPosition(P.Position); Slot->SetSize(P.Size); }
        Slot->SetZOrder(bBackground ? 0 : 1);
    }
}
void ConfigureTitleSkip()
{
    auto Fail=[](const TCHAR* Reason) { UE_LOG(LogTemp,Error,TEXT("TITLE_SKIP_SETUP FAIL %s"),Reason); FPlatformMisc::RequestExitWithStatus(false,1); };
    auto* Title=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/WBP_Title.WBP_Title"));
    auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
    auto* TitleSource=LoadObject<UFileMediaSource>(nullptr,TEXT("/Game/Assets/UI/TitleLoop.TitleLoop"));
    auto* TriggerSource=LoadObject<UFileMediaSource>(nullptr,TEXT("/Game/Assets/UI/TriggerPress.TriggerPress"));
    auto* Mode=LoadObject<UBlueprint>(nullptr,TEXT("/Game/Blueprints/GameMode/BP_OBTitleGameMode.BP_OBTitleGameMode"));
    auto* HUD=LoadObject<UBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/BP_OBHUD.BP_OBHUD"));
    auto* PC=LoadObject<UBlueprint>(nullptr,TEXT("/Game/Blueprints/Player/BP_OBPlayerController.BP_OBPlayerController"));
    if (!Title || !Overlay || !TitleSource || !TriggerSource || !Mode || !HUD || !PC) { Fail(TEXT("Required assets")); return; }
    const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups")/(TEXT("TitleSkip_")+FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S")));
    IFileManager::Get().MakeDirectory(*Backup,true);
    for (UObject* Asset : {static_cast<UObject*>(Title),static_cast<UObject*>(Overlay),static_cast<UObject*>(TitleSource),static_cast<UObject*>(TriggerSource),static_cast<UObject*>(Mode),static_cast<UObject*>(HUD)})
    {
        const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),TEXT(".uasset"));
        if (IFileManager::Get().Copy(*(Backup/FPaths::GetCleanFilename(File)),*File)!=COPY_OK) { Fail(TEXT("Backup")); return; }
    }
    const FString TitleMapFile=FPackageName::LongPackageNameToFilename(TEXT("/Game/Maps/TitleMap"),TEXT(".umap"));
    if (IFileManager::Get().Copy(*(Backup/TEXT("TitleMap.umap")),*TitleMapFile)!=COPY_OK) { Fail(TEXT("TitleMap backup")); return; }
    TitleSource->SetFilePath(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()/TEXT("Movies/TitleLoop.mp4")));
    TriggerSource->SetFilePath(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()/TEXT("Movies/TriggerPress.mp4")));
    if (!TitleSource->Validate() || !TriggerSource->Validate()) { Fail(TEXT("Movies files")); return; }
    auto* Texture=LoadObject<UMediaTexture>(nullptr,TEXT("/Game/Blueprints/Widget/Media/MT_UIVideo.MT_UIVideo"),nullptr,LOAD_NoWarn);
    if (!Texture) { Texture=NewObject<UMediaTexture>(CreatePackage(TEXT("/Game/Blueprints/Widget/Media/MT_UIVideo")),TEXT("MT_UIVideo"),RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Texture); }
    Texture->NewStyleOutput=true;
    auto* Material=LoadObject<UMaterial>(nullptr,TEXT("/Game/Blueprints/Widget/Media/M_UIVideo.M_UIVideo"),nullptr,LOAD_NoWarn);
    if (!Material)
    {
        Material=NewObject<UMaterial>(CreatePackage(TEXT("/Game/Blueprints/Widget/Media/M_UIVideo")),TEXT("M_UIVideo"),RF_Public|RF_Standalone);
        Material->MaterialDomain=MD_UI; Material->BlendMode=BLEND_Translucent;
        auto* Sample=NewObject<UMaterialExpressionTextureSampleParameter2D>(Material); Sample->ParameterName=TEXT("VideoTexture"); Sample->Texture=Texture; Sample->SamplerType=SAMPLERTYPE_Color;
        Material->GetExpressionCollection().AddExpression(Sample); Material->GetEditorOnlyData()->EmissiveColor.Expression=Sample;
        Material->PostEditChange(); FAssetRegistryModule::AssetCreated(Material);
    }
    for (TObjectIterator<UMaterialExpressionTextureSampleParameter2D> It;It;++It)
        if (It->GetOuter()==Material) It->SamplerType=SAMPLERTYPE_Color;
    Material->PostEditChange();
    // Rebuild only the requested title presentation; the old asset contains two images.
    if (Title->WidgetTree) Title->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    Title->WidgetTree=NewObject<UWidgetTree>(Title,TEXT("WidgetTree"),RF_Transactional);
    Title->ParentClass=UOBTitleWidget::StaticClass();
    auto* Canvas=Title->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("TitleCanvas")); Title->WidgetTree->RootWidget=Canvas;
    for (const auto* Name : {TEXT("BackgroundVideo"),TEXT("LogoImage"),TEXT("TitleImage")})
    {
        auto* Image=Title->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Name);
        if (FString(Name)==TEXT("BackgroundVideo")) Image->SetBrushFromMaterial(Material);
        else Image->SetBrushTintColor(FSlateColor(FLinearColor::White));
        Canvas->AddChildToCanvas(Image);
    }
    auto* Press=TitleLabel(Title->WidgetTree,TEXT("PressText"),TEXT("방아쇠를 당겨 시작"),36,FLinearColor::White);
    Press->SetShadowColorAndOpacity(FLinearColor::Black); Press->SetShadowOffset(FVector2D(2,2)); Canvas->AddChildToCanvas(Press);
    SetTitleSlots(Title->WidgetTree);

    const FString SkipPath=TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingSkip");
    auto* Skip=LoadObject<UWidgetBlueprint>(nullptr,*(SkipPath+TEXT(".WBP_TrainingSkip")),nullptr,LOAD_NoWarn);
    if (!Skip)
    {
        auto* Factory=NewObject<UWidgetBlueprintFactory>(); Factory->ParentClass=UOBTrainingSkipWidget::StaticClass();
        Skip=Cast<UWidgetBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(TEXT("WBP_TrainingSkip"),TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components"),UWidgetBlueprint::StaticClass(),Factory));
    }
    if (!Skip) { Fail(TEXT("Skip blueprint")); return; }
    auto* Tree=Skip->WidgetTree.Get();
    if (!Tree->RootWidget)
    {
        auto* Size=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("SkipSize")); Size->SetWidthOverride(240); Size->SetHeightOverride(330); Tree->RootWidget=Size;
        auto* Frame=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("Border")); Frame->SetBrushColor(FLinearColor(.025,.07,.17,1)); Frame->SetPadding(FMargin(4)); Size->SetContent(Frame);
        auto* Stack=Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("SkipStack")); Frame->SetContent(Stack);
        auto* TitleText=TitleLabel(Tree,TEXT("TitleText"),TEXT("기초훈련 건너뛰기"),23,FLinearColor::White);
        Stack->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0,8,0,10));
        auto* White=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("BodyBorder")); White->SetBrushColor(FLinearColor::White); White->SetPadding(FMargin(12,8));
        Stack->AddChildToVerticalBox(White)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Body=Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("BodyStack")); White->SetContent(Body);
        auto* VideoSize=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("VideoSize")); VideoSize->SetWidthOverride(170); VideoSize->SetHeightOverride(170);
        auto* Image=Tree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("TriggerLoopVideo")); Image->SetBrushFromMaterial(Material); VideoSize->SetContent(Image);
        auto* VideoSlot=Body->AddChildToVerticalBox(VideoSize); VideoSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); VideoSlot->SetHorizontalAlignment(HAlign_Center); VideoSlot->SetVerticalAlignment(VAlign_Center);
        auto* ProgressSize=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ProgressSize")); ProgressSize->SetHeightOverride(20);
        auto* Bar=Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("ProgressBar")); Bar->SetFillColorAndOpacity(FLinearColor(1,.3,0,1)); Bar->SetPercent(0); ProgressSize->SetContent(Bar);
        Body->AddChildToVerticalBox(ProgressSize)->SetPadding(FMargin(0,8,0,8));
        auto* BodyText=TitleLabel(Tree,TEXT("BodyText"),TEXT("2초간 방아쇠를 눌러주세요"),14,FLinearColor::Black);
        Body->AddChildToVerticalBox(BodyText)->SetPadding(FMargin(0,2,0,2));
    }
    CompileTitleWidget(Skip);
    if (Skip->Status==BS_Error) { Fail(TEXT("Skip compile")); return; }
    if (!Overlay->WidgetTree->FindWidget(TEXT("TrainingSkip")))
    {
        auto* Root=Cast<UCanvasPanel>(Overlay->WidgetTree->RootWidget);
        if (!Root) { Fail(TEXT("Overlay Canvas")); return; }
        auto* Widget=Overlay->WidgetTree->ConstructWidget<UOBTrainingSkipWidget>(Skip->GeneratedClass.Get(),TEXT("TrainingSkip"));
        auto* Slot=Root->AddChildToCanvas(Widget); Slot->SetPosition(FVector2D(32,32)); Slot->SetSize(FVector2D(240,330)); Slot->SetZOrder(10); Widget->SetVisibility(ESlateVisibility::Collapsed);
    }
    CompileTitleWidget(Title); CompileTitleWidget(Overlay);
    if (Title->Status==BS_Error || Overlay->Status==BS_Error) { Fail(TEXT("Title/overlay compile")); return; }
    auto* TitleClass=Cast<UWidgetBlueprintGeneratedClass>(Title->GeneratedClass);
    SetTitleSlots(Title->WidgetTree); SetTitleSlots(TitleClass ? TitleClass->GetWidgetTreeArchetype() : nullptr);
    auto* SkipClass=Cast<UWidgetBlueprintGeneratedClass>(Skip->GeneratedClass);
    for (auto* SkipTree : {Skip->WidgetTree.Get(),SkipClass ? SkipClass->GetWidgetTreeArchetype() : nullptr})
        if (auto* Label=SkipTree ? Cast<UTextBlock>(SkipTree->FindWidget(TEXT("BodyText"))) : nullptr)
        { auto Font=Label->GetFont(); Font.Size=14; Label->SetFont(Font); }
    auto* OverlayClass=Cast<UWidgetBlueprintGeneratedClass>(Overlay->GeneratedClass);
    for (auto* OverlayTree : {Overlay->WidgetTree.Get(),OverlayClass ? OverlayClass->GetWidgetTreeArchetype() : nullptr})
    {
        auto* Widget=OverlayTree ? OverlayTree->FindWidget(TEXT("TrainingSkip")) : nullptr;
        if (auto* Slot=Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
        { Slot->SetAutoSize(false); Slot->SetSize(FVector2D(240,330)); Slot->SetPosition(FVector2D(32,32)); Slot->SetZOrder(10); }
    }
    auto* ModeDefaults=Cast<AGameModeBase>(Mode->GeneratedClass->GetDefaultObject());
    auto* HUDDefaults=HUD->GeneratedClass->GetDefaultObject();
    auto* TitleProperty=FindFProperty<FClassProperty>(HUDDefaults->GetClass(),TEXT("TitleWidgetClass"));
    if (!ModeDefaults || !TitleProperty) { Fail(TEXT("Title GameMode/HUD binding")); return; }
    ModeDefaults->HUDClass=HUD->GeneratedClass.Get(); ModeDefaults->PlayerControllerClass=PC->GeneratedClass.Get(); ModeDefaults->DefaultPawnClass=nullptr;
    TitleProperty->SetObjectPropertyValue_InContainer(HUDDefaults,Title->GeneratedClass.Get());
    for (UObject* Asset : {static_cast<UObject*>(Texture),static_cast<UObject*>(Material),static_cast<UObject*>(Title),static_cast<UObject*>(Skip),static_cast<UObject*>(Overlay),static_cast<UObject*>(TitleSource),static_cast<UObject*>(TriggerSource),static_cast<UObject*>(Mode),static_cast<UObject*>(HUD)})
        if (!SaveTitleAsset(Asset)) { Fail(TEXT("Save")); return; }
    if (!FEditorFileUtils::LoadMap(TEXT("/Game/Maps/TitleMap"),false,true)) { Fail(TEXT("Load TitleMap")); return; }
    auto* World=GEditor->GetEditorWorldContext().World(); World->GetWorldSettings()->DefaultGameMode=Mode->GeneratedClass.Get();
    if (!FEditorFileUtils::SaveMap(World,TitleMapFile)) { Fail(TEXT("Save TitleMap")); return; }
    UE_LOG(LogTemp,Display,TEXT("TITLE_SKIP_SETUP PASS backup=%s"),*Backup); FPlatformMisc::RequestExitWithStatus(false,0);
}
static FAutoConsoleCommand GConfigureTitleSkip(TEXT("OB.UI.ConfigureTitleSkip"),TEXT("Back up and configure title video, skip widget and portable media sources; exits."),FConsoleCommandDelegate::CreateStatic(&ConfigureTitleSkip));
void ConfigureVideoBlend()
{
    auto* Material=LoadObject<UMaterial>(nullptr,TEXT("/Game/Blueprints/Widget/Media/M_UIVideo.M_UIVideo"));
    bool bOK=Material!=nullptr;
    if (Material)
    {
        const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups")/(TEXT("VideoBlend_")+FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S")));
        IFileManager::Get().MakeDirectory(*Backup,true);
        const FString File=FPackageName::LongPackageNameToFilename(Material->GetOutermost()->GetName(),TEXT(".uasset"));
        bOK=IFileManager::Get().Copy(*(Backup/TEXT("M_UIVideo.uasset")),*File)==COPY_OK;
        if (bOK)
        {
            Material->BlendMode=BLEND_Translucent;
            if (!Material->GetEditorOnlyData()->Opacity.Expression)
            {
                auto* Opacity=NewObject<UMaterialExpressionConstant>(Material); Opacity->R=1;
                Material->GetExpressionCollection().AddExpression(Opacity); Material->GetEditorOnlyData()->Opacity.Expression=Opacity;
            }
            Material->PostEditChange(); bOK=SaveTitleAsset(Material);
        }
    }
    UE_LOG(LogTemp,Display,TEXT("VIDEO_BLEND_SETUP outcome=%s"),bOK ? TEXT("PASS") : TEXT("FAIL"));
    FPlatformMisc::RequestExitWithStatus(false,bOK ? 0 : 1);
}
static FAutoConsoleCommand GConfigureVideoBlend(TEXT("OB.UI.ConfigureVideoBlend"),TEXT("Back up and enable title video opacity blending without changing widget layouts; exits."),FConsoleCommandDelegate::CreateStatic(&ConfigureVideoBlend));
void ConfigureTitlePingPong()
{
    const TCHAR* PackageName=TEXT("/Game/Assets/UI/TitleIdlePingPong");
    const FString File=FPackageName::LongPackageNameToFilename(PackageName,TEXT(".uasset"));
    const FString Movie=FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()/TEXT("Movies/TitleIdlePingPong.mp4"));
    bool bOK=IFileManager::Get().FileExists(*Movie);
    if (bOK && IFileManager::Get().FileExists(*File))
    {
        const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups")/(TEXT("TitlePingPong_")+FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S")));
        IFileManager::Get().MakeDirectory(*Backup,true);
        bOK=IFileManager::Get().Copy(*(Backup/TEXT("TitleIdlePingPong.uasset")),*File)==COPY_OK;
    }
    if (bOK)
    {
        auto* Source=LoadObject<UFileMediaSource>(nullptr,TEXT("/Game/Assets/UI/TitleIdlePingPong.TitleIdlePingPong"),nullptr,LOAD_NoWarn);
        if (!Source)
        {
            Source=NewObject<UFileMediaSource>(CreatePackage(PackageName),TEXT("TitleIdlePingPong"),RF_Public|RF_Standalone);
            FAssetRegistryModule::AssetCreated(Source);
        }
        Source->SetFilePath(Movie);
        bOK=Source->Validate() && SaveTitleAsset(Source);
    }
    UE_LOG(LogTemp,Display,TEXT("TITLE_PING_PONG_SETUP outcome=%s"),bOK ? TEXT("PASS") : TEXT("FAIL"));
    FPlatformMisc::RequestExitWithStatus(false,bOK ? 0 : 1);
}
static FAutoConsoleCommand GConfigureTitlePingPong(TEXT("OB.UI.ConfigureTitlePingPong"),TEXT("Create the portable ping-pong idle media source without changing widget layouts; exits."),FConsoleCommandDelegate::CreateStatic(&ConfigureTitlePingPong));
}
#endif
