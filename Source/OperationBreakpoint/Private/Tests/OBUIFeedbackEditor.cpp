#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "AssetCompilingManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "FileMediaSource.h"
#include "Training/OBTrainingResultWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Stage/OBStageFlowComponent.h"
#include "Widget/OBHUD.h"
#include "Widget/OBTrainingSkipWidget.h"
#include "Widget/OBUIVideo.h"
#include "MediaPlayer.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
namespace OBUIReadability { bool Compile(UWidgetBlueprint*); UCanvasPanelSlot* Place(UCanvasPanel*,UWidget*,FVector2D,FVector2D,FAnchors,FVector2D); bool Save(UObject*); }
namespace OBUIFeedback
{
const TCHAR* ComponentRoot=TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/");
FLinearColor Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
UWidgetBlueprint* BP(const TCHAR* Name)
{ const FString P=FString(ComponentRoot)+Name; return LoadObject<UWidgetBlueprint>(nullptr,*(P+TEXT(".")+Name)); }
FSlateBrush Brush(const TCHAR* Path,FVector2D Size)
{ FSlateBrush B; B.SetResourceObject(LoadObject<UTexture2D>(nullptr,Path)); B.DrawAs=ESlateBrushDrawType::Image; B.SetImageSize(Size); return B; }
template<class T> T* Node(UWidgetBlueprint* Asset,const TCHAR* Name)
{ auto* W=Cast<T>(Asset->WidgetTree->FindWidget(Name)); if (!W) W=FindObject<T>(Asset->WidgetTree,Name); if (!W) W=Asset->WidgetTree->ConstructWidget<T>(T::StaticClass(),Name); W->bIsVariable=true; return W; }
UTextBlock* Label(UWidgetBlueprint* Asset,const TCHAR* Name,int32 Size,const TCHAR* Hex,const TCHAR* Text=nullptr)
{
    auto* W=Node<UTextBlock>(Asset,Name); auto F=W->GetFont(); F.Size=Size; F.OutlineSettings.OutlineSize=0;
    F.FontObject=LoadObject<UObject>(nullptr,TEXT("/Game/Assets/UI/Fonts/Pretendard-Bold_Font.Pretendard-Bold_Font"));
    W->SetFont(F); W->SetColorAndOpacity(Color(Hex)); W->SetShadowOffset(FVector2D::ZeroVector); W->SetAutoWrapText(false);
    W->SetLineHeightPercentage(1.08f); if (Text) W->SetText(FText::FromString(Text)); return W;
}
UCanvasPanelSlot* Put(UCanvasPanel* C,UWidget* W,float X,float Y,float Width,float Height)
{ return OBUIReadability::Place(C,W,FVector2D(X,Y),FVector2D(Width,Height),FAnchors(0,0),FVector2D::ZeroVector); }
void Retire(UWidget* Old,UWidget* Replacement)
{
    if (!Old || Old==Replacement) return;
    if (auto* Panel=Cast<UPanelWidget>(Old)) for (auto* Child:Panel->GetAllChildren()) Retire(Child,nullptr);
    Old->RemoveFromParent(); Old->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
}
UProgressBar* HoldBar(UWidgetBlueprint* Asset,const TCHAR* Name,const FSlateBrush& Fill)
{
    auto* Bar=Node<UProgressBar>(Asset,Name); FProgressBarStyle Style;
    Style.BackgroundImage.DrawAs=ESlateBrushDrawType::NoDrawType;
    Style.FillImage=Fill; Style.MarqueeImage.DrawAs=ESlateBrushDrawType::NoDrawType; Style.EnableFillAnimation=false;
    Bar->SetWidgetStyle(Style); Bar->SetBarFillStyle(EProgressBarFillStyle::Mask); Bar->SetBarFillType(EProgressBarFillType::LeftToRight);
    Bar->SetBorderPadding(FVector2D::ZeroVector); Bar->SetFillColorAndOpacity(Color(TEXT("B3D0E7"))); Bar->SetPercent(0);
    Bar->SetVisibility(ESlateVisibility::HitTestInvisible); return Bar;
}
void StageResultLayout(UWidgetBlueprint* Asset,UCanvasPanel* Root)
{
    auto* Fit=Node<UScaleBox>(Asset,TEXT("StageResultPanel")); Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::DownOnly);
    Fit->SetVisibility(ESlateVisibility::Collapsed);
    auto* FitSlot=Root->AddChildToCanvas(Fit);FitSlot->SetAnchors(FAnchors(0,0,1,1));FitSlot->SetOffsets(FMargin(24));
    auto* Size=Node<USizeBox>(Asset,TEXT("StageResultSize")); Size->SetWidthOverride(800); Size->SetHeightOverride(1320); Fit->SetContent(Size);
    auto* C=Node<UCanvasPanel>(Asset,TEXT("StageResultContent")); Size->SetContent(C);
    auto* Art=Node<UImage>(Asset,TEXT("StageResultArtwork")); Art->SetBrush(Brush(TEXT("/Game/Assets/UI/StageResult_Frame.StageResult_Frame"),FVector2D(800,1320))); Put(C,Art,0,0,800,1320);
    auto Text=[&](const TCHAR* Name,const TCHAR* Copy,int32 Font,const TCHAR* Hex,float X,float Y,float W,float H,ETextJustify::Type Justify=ETextJustify::Center,bool Numeric=false)
    {
        auto* T=Label(Asset,Name,Font,Hex,Copy); T->SetJustification(Justify);
        if (Numeric) { auto F=T->GetFont(); F.FontObject=LoadObject<UObject>(nullptr,TEXT("/Game/Assets/UI/Fonts/Tomorrow-ExtraBold_Font.Tomorrow-ExtraBold_Font")); T->SetFont(F); }
        Put(C,T,X,Y,W,H)->SetZOrder(2); return T;
    };
    Text(TEXT("StageTitleText"),TEXT("훈련 종료"),48,TEXT("FFFFFF"),120,53,560,83);
    Text(TEXT("StageSubtitleText"),TEXT("훈련 결과"),36,TEXT("133966"),280,156,240,66);
    Text(TEXT("StageRankText"),TEXT("—"),144,TEXT("FFFFFF"),245,282,310,208);
    Text(TEXT("StageRankLabel"),TEXT("Rank"),44,TEXT("FFFFFF"),265,487,270,75,ETextJustify::Center,true);
    const TCHAR* Labels[]={TEXT("평가 점수"),TEXT("명중률"),TEXT("소요시간")};
    for (int32 I=0;I<3;++I)
        Text(*FString::Printf(TEXT("StageMetricLabel%d"),I),Labels[I],36,TEXT("133966"),120,674+I*71,255,59,ETextJustify::Left);
    Text(TEXT("StageScoreText"),TEXT("— / 100"),36,TEXT("133966"),415,675,273,59,ETextJustify::Right,true);
    Text(TEXT("StageAccuracyText"),TEXT("—"),36,TEXT("133966"),415,746,273,59,ETextJustify::Right,true);
    Text(TEXT("StageTimeText"),TEXT("00:00"),36,TEXT("133966"),415,817,273,59,ETextJustify::Right,true);
    Text(TEXT("StageReportText"),TEXT("훈련 상세 내역은\n자동 저장됩니다."),36,TEXT("133966"),318,940,425,122,ETextJustify::Left);
    Text(TEXT("StageRetryText"),TEXT("재훈련"),38,TEXT("FFFFFF"),205,1138,155,64);
    Text(TEXT("StageRetryHint"),TEXT("짧게 방아쇠 당기기"),15,TEXT("FFFFFF"),205,1195,155,30);
    Text(TEXT("StageMenuText"),TEXT("메인화면"),36,TEXT("133966"),525,1138,185,64);
    Text(TEXT("StageMenuHint"),TEXT("방아쇠 2초 길게 당기기"),14,TEXT("133966"),525,1195,185,29);
    auto* Bar=HoldBar(Asset,TEXT("StageMenuHoldProgress"),Brush(TEXT("/Game/Assets/UI/StageResult_Hold.StageResult_Hold"),FVector2D(291,111)));
    Put(C,Bar,424.5f,1125.5f,291,111)->SetZOrder(1);
    auto* Home=Node<UImage>(Asset,TEXT("StageMenuIcon"));
    Home->SetBrush(Brush(TEXT("/Game/Assets/UI/StageResult_HomeIcon.StageResult_HomeIcon"),FVector2D(70,70)));
    Put(C,Home,454,1145,70,70)->SetZOrder(2);
}
void ResultLayout(UWidgetBlueprint* Asset)
{
    auto* C=CastChecked<UCanvasPanel>(Asset->WidgetTree->RootWidget);
    auto* Frame=CastChecked<UBorder>(Asset->WidgetTree->FindWidget(TEXT("ResultBorder")));
    auto* Old=Frame->GetContent(); auto* Inner=Node<UCanvasPanel>(Asset,TEXT("ResultContent"));
    Frame->SetContent(Inner); Frame->SetPadding(FMargin(0)); Frame->SetBrushColor(FLinearColor::White);
    OBUIReadability::Place(C,Frame,FVector2D(0,-64),FVector2D(1518,755),FAnchors(.5f,.5f),FVector2D(.5f,.5f));
    Retire(Asset->WidgetTree->FindWidget(TEXT("ResultHeaderText")),nullptr);
    auto* Title=Label(Asset,TEXT("TitleText"),84,TEXT("0F2F5A")); Title->SetJustification(ETextJustify::Center); Put(Inner,Title,100,270,1318,145);
    auto* Body=Label(Asset,TEXT("BodyText"),28,TEXT("0F2F5A")); Body->SetJustification(ETextJustify::Center); Body->SetWrapTextAt(1260); Put(Inner,Body,100,455,1318,115);
    auto* Hint=Label(Asset,TEXT("InteractionText"),20,TEXT("0F2F5A")); Hint->SetJustification(ETextJustify::Center); Hint->SetWrapTextAt(1260); Put(Inner,Hint,100,620,1318,75);
    Retire(Old,Inner);
    auto* Actions=Node<UCanvasPanel>(Asset,TEXT("ResultActions")); Actions->SetVisibility(ESlateVisibility::HitTestInvisible);
    OBUIReadability::Place(C,Actions,FVector2D(-504,342),FVector2D(1008,102),FAnchors(.5f,.5f),FVector2D::ZeroVector);
    for (int32 I=0;I<2;++I)
    {
        const float X=I*508.f; auto* Image=Node<UImage>(Asset,I ? TEXT("RetryActionImage") : TEXT("NextActionImage"));
        Image->SetBrush(Brush(I ? TEXT("/Game/Assets/UI/Training_UI/Sub_UI/Button_Click_UI.Button_Click_UI") : TEXT("/Game/Assets/UI/Training_UI/Sub_UI/Button_Press_UI.Button_Press_UI"),FVector2D(500,102)));
        Image->SetColorAndOpacity(FLinearColor::White); Put(Actions,Image,X,0,500,102);
        if (I)
        {
            auto* Bar=HoldBar(Asset,TEXT("RetryHoldProgress"),Brush(TEXT("/Game/Assets/UI/Training_UI/Sub_UI/Button_Click_UI.Button_Click_UI"),FVector2D(500,102)));
            Put(Actions,Bar,X+3,3,494,96)->SetZOrder(1);
        }
        auto* Text=Label(Asset,I ? TEXT("RetryActionText") : TEXT("NextActionText"),32,I ? TEXT("0F2F5A") : TEXT("FFFFFF"),I ? TEXT("재훈련") : TEXT("미션 이동"));
        Text->SetJustification(ETextJustify::Center); Put(Actions,Text,X+14,7,472,60)->SetZOrder(2);
        auto* Small=Label(Asset,I ? TEXT("RetryActionHint") : TEXT("NextActionHint"),14,I ? TEXT("0F2F5A") : TEXT("FFFFFF"));
        Small->SetJustification(ETextJustify::Center); Put(Actions,Small,X+14,70,472,28)->SetZOrder(2);
    }
    StageResultLayout(Asset,C);
}
void ApplyStageResults()
{
    bool OK=true;
    for (const auto* Name:{TEXT("StageResult_Frame"),TEXT("StageResult_Hold"),TEXT("StageResult_HomeIcon")})
    {
        auto* Import=NewObject<UAssetImportTask>(); Import->Filename=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Support/Submission/Assets/Art/UI")/(FString(Name)+TEXT(".png")));
        Import->DestinationPath=TEXT("/Game/Assets/UI"); Import->DestinationName=Name; Import->bAutomated=true; Import->bReplaceExisting=true; Import->bSave=true;
        FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks({Import});
        auto* T=LoadObject<UTexture2D>(nullptr,*(FString(TEXT("/Game/Assets/UI/"))+Name+TEXT(".")+Name));
        if (!T) { OK=false; continue; }
        T->CompressionSettings=TC_EditorIcon; T->LODGroup=TEXTUREGROUP_UI; T->SRGB=true; T->NeverStream=true; T->PostEditChange(); OK=OBUIReadability::Save(T) && OK;
    }
    auto* Asset=BP(TEXT("WBP_TrainingResult")); ResultLayout(Asset); OK=OBUIReadability::Compile(Asset) && OK;
    auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay")); OK=OBUIReadability::Compile(Overlay) && OK;
    UE_LOG(LogTemp,Display,TEXT("STAGE_RESULTS applied=%d"),OK); FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);
}
void GuideLayout(UWidgetBlueprint* Asset)
{
    auto* C=CastChecked<UCanvasPanel>(Asset->WidgetTree->FindWidget(TEXT("PanelBackground")));
    auto* Title=Label(Asset,TEXT("TitleText"),32,TEXT("FFFFFF")); Title->SetJustification(ETextJustify::Center); Put(C,Title,80,22,1240,68);
    auto* Fit=CastChecked<UScaleBox>(Asset->WidgetTree->FindWidget(TEXT("BodyImageFit"))); Fit->SetStretch(EStretch::ScaleToFit); Put(C,Fit,70,118,1260,480);
    auto* Edge=Node<UBorder>(Asset,TEXT("GuideFooterBoundary")); Edge->SetBrushColor(Color(TEXT("B5C0CC"))); Edge->SetPadding(FMargin(1)); Put(C,Edge,48,646,1304,92);
    auto* Footer=Node<UHorizontalBox>(Asset,TEXT("GuideFooterRow")); Edge->SetContent(Footer);
    auto* Copy=Node<UBorder>(Asset,TEXT("GuideCopyArea")); Copy->SetBrushColor(FLinearColor::White); Copy->SetPadding(FMargin(20,0)); Copy->SetVerticalAlignment(VAlign_Center);
    auto* Body=Label(Asset,TEXT("BodyText"),20,TEXT("202A36")); Body->SetWrapTextAt(990); Body->SetJustification(ETextJustify::Left); Body->RemoveFromParent(); Copy->SetContent(Body);
    Copy->RemoveFromParent(); auto* CopySlot=Footer->AddChildToHorizontalBox(Copy); CopySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* Prompt=Node<USizeBox>(Asset,TEXT("AdvancePrompt")); Prompt->SetWidthOverride(268); Prompt->SetHeightOverride(90); Prompt->RemoveFromParent(); Footer->AddChildToHorizontalBox(Prompt);
    auto* Blue=Node<UBorder>(Asset,TEXT("GuidePromptBackground")); Blue->SetBrush(Brush(TEXT("/Game/Assets/UI/Training_UI/Main__UI/CompleteImg/triger_box.triger_box"),FVector2D(278,88))); Blue->SetBrushColor(FLinearColor::White); Blue->SetPadding(FMargin(8,0)); Prompt->SetContent(Blue);
    auto* PromptRow=Node<UHorizontalBox>(Asset,TEXT("GuidePromptRow")); Blue->SetContent(PromptRow);
    auto* Hint=Label(Asset,TEXT("ContinueHint"),16,TEXT("FFFFFF"),TEXT("방아쇠를 당기세요")); Hint->SetJustification(ETextJustify::Center); Hint->RemoveFromParent();
    auto* HintSlot=PromptRow->AddChildToHorizontalBox(Hint); HintSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); HintSlot->SetVerticalAlignment(VAlign_Center);
    auto* Trigger=CastChecked<UImage>(Asset->WidgetTree->FindWidget(TEXT("TriggerImage"))); Trigger->RemoveFromParent(); auto B=Trigger->GetBrush(); B.SetImageSize(FVector2D(64,64)); Trigger->SetBrush(B);
    auto* TriggerSlot=PromptRow->AddChildToHorizontalBox(Trigger); TriggerSlot->SetVerticalAlignment(VAlign_Center); TriggerSlot->SetPadding(FMargin(4,0,0,0));
}
void MissionLayout(UWidgetBlueprint* Asset)
{
    auto* Size=CastChecked<USizeBox>(Asset->WidgetTree->RootWidget); Size->SetWidthOverride(280); Size->SetHeightOverride(408);
    auto* Frame=CastChecked<UBorder>(Asset->WidgetTree->FindWidget(TEXT("Mission"))); auto* Old=Frame->GetContent(); auto* C=Node<UCanvasPanel>(Asset,TEXT("MissionContent")); Frame->SetContent(C); Frame->SetPadding(FMargin(0)); Frame->SetBrushColor(FLinearColor::White);
    auto* Title=Label(Asset,TEXT("MissionTitleText"),22,TEXT("FFFFFF"),TEXT("훈련 목표")); Title->SetJustification(ETextJustify::Center); Put(C,Title,10,9,260,55);
    const TCHAR* Names[]={TEXT("총기 확인"),TEXT("조준 훈련"),TEXT("엄폐 훈련"),TEXT("재장전 훈련"),TEXT("영점 사격")};
    for (int32 I=1;I<=5;++I)
    {
        auto* Text=Label(Asset,*FString::Printf(TEXT("BodyText_%d"),I),18,TEXT("15385D"),Names[I-1]);
        auto* Row=CastChecked<UOverlay>(Text->GetParent()); Put(C,Row,12,76+(I-1)*48,256,40);
        auto* TextSlot=CastChecked<UOverlaySlot>(Text->Slot); TextSlot->SetPadding(FMargin(0,0,62,0)); TextSlot->SetHorizontalAlignment(HAlign_Fill); TextSlot->SetVerticalAlignment(VAlign_Center);
        auto* Strike=CastChecked<UImage>(Asset->WidgetTree->FindWidget(*FString::Printf(TEXT("Strike_%d"),I))); auto B=Strike->GetBrush(); B.SetImageSize(FVector2D(142,2)); B.TintColor=FLinearColor::White; Strike->SetBrush(B);
        auto* StrikeSlot=CastChecked<UOverlaySlot>(Strike->Slot); StrikeSlot->SetHorizontalAlignment(HAlign_Left); StrikeSlot->SetVerticalAlignment(VAlign_Center); StrikeSlot->SetPadding(FMargin(0)); Strike->SetVisibility(ESlateVisibility::Hidden);
        auto* Status=Label(Asset,*FString::Printf(TEXT("OutcomeText_%d"),I),16,TEXT("218555"),TEXT("")); Status->SetJustification(ETextJustify::Right); Status->RemoveFromParent();
        auto* StatusSlot=Row->AddChildToOverlay(Status); StatusSlot->SetHorizontalAlignment(HAlign_Right); StatusSlot->SetVerticalAlignment(VAlign_Center); StatusSlot->SetPadding(FMargin(0));
    }
    Retire(Old,C);
}
void TimerLayout(UWidgetBlueprint* Asset)
{
    auto* Size=CastChecked<USizeBox>(Asset->WidgetTree->RootWidget); Size->SetWidthOverride(260); Size->SetHeightOverride(130);
    auto* Frame=CastChecked<UBorder>(Asset->WidgetTree->FindWidget(TEXT("Border_Timer"))); auto* Old=Frame->GetContent(); auto* C=Node<UCanvasPanel>(Asset,TEXT("TimerContent")); Frame->SetContent(C); Frame->SetPadding(FMargin(0)); Frame->SetBrushColor(FLinearColor::White);
    auto* Title=Label(Asset,TEXT("TitleText"),11,TEXT("FFFFFF"),TEXT("ZONE 1 / 6")); Title->SetJustification(ETextJustify::Center); Put(C,Title,65,7,130,25);
    auto* Value=Label(Asset,TEXT("ValueText"),32,TEXT("0F2F5A"),TEXT("00 : 00")); auto Font=Value->GetFont(); Font.FontObject=LoadObject<UObject>(nullptr,TEXT("/Game/Assets/UI/Fonts/Tomorrow-ExtraBold_Font.Tomorrow-ExtraBold_Font")); Value->SetFont(Font);
    Value->SetJustification(ETextJustify::Center); Put(C,Value,10,45,240,64); Retire(Old,C);
}
void Apply()
{
    auto* Import=NewObject<UAssetImportTask>(); Import->Filename=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Support/Submission/Assets/Art/UI/TriggerPress_Poster.png"));
    Import->DestinationPath=TEXT("/Game/Assets/UI"); Import->DestinationName=TEXT("TriggerPress_Poster"); Import->bAutomated=true; Import->bReplaceExisting=true; Import->bSave=true;
    FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks({Import});
    auto* Poster=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/TriggerPress_Poster.TriggerPress_Poster"));
    if (!Poster) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Poster->CompressionSettings=TC_EditorIcon; Poster->LODGroup=TEXTUREGROUP_UI; Poster->SRGB=true; Poster->NeverStream=true; Poster->PostEditChange(); OBUIReadability::Save(Poster);
    auto* Result=BP(TEXT("WBP_TrainingResult")); auto* Guide=BP(TEXT("WBP_Guide")); auto* Mission=BP(TEXT("WBP_Mission")); auto* Timer=BP(TEXT("WBP_Timer")); auto* Skip=BP(TEXT("WBP_TrainingSkip"));
    ResultLayout(Result); GuideLayout(Guide); MissionLayout(Mission); TimerLayout(Timer);
    auto* SkipSize=CastChecked<USizeBox>(Skip->WidgetTree->RootWidget); SkipSize->SetWidthOverride(260); SkipSize->SetHeightOverride(320);
    Label(Skip,TEXT("TitleText"),18,TEXT("FFFFFF")); Label(Skip,TEXT("BodyText"),14,TEXT("0F2F5A"));
    CastChecked<UBorder>(Skip->WidgetTree->FindWidget(TEXT("Border")))->SetBrushColor(Color(TEXT("163B65")));
    auto* Video=CastChecked<UImage>(Skip->WidgetTree->FindWidget(TEXT("TriggerLoopVideo"))); Video->SetBrushFromTexture(Poster); Video->SetColorAndOpacity(FLinearColor::White);
    bool OK=true; for (auto* Asset:{Result,Guide,Mission,Timer,Skip}) OK=OBUIReadability::Compile(Asset) && OK;
    auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay")); OK=OBUIReadability::Compile(Overlay) && OK;
    UE_LOG(LogTemp,Display,TEXT("UI_FEEDBACK applied=%d widgets=6 original_textures_preserved=1"),OK); FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);
}
void Inspect()
{
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Tests/UIReview/Feedback/Assets"); IFileManager::Get().MakeDirectory(*Folder,true);
    for (const auto* Path:{TEXT("/Game/Assets/UI/Training_UI/Sub_UI/Button_Click_UI.Button_Click_UI"),TEXT("/Game/Assets/UI/Training_UI/Sub_UI/Button_Press_UI.Button_Press_UI"),
        TEXT("/Game/Assets/UI/Training_UI/Main__UI/CompleteImg/triger_box.triger_box"),TEXT("/Game/Assets/UI/Training_UI/Sub_UI/SubUI_Pass.SubUI_Pass"),
        TEXT("/Game/Assets/UI/Training_UI/Quest/Mission_bg.Mission_bg"),TEXT("/Game/Assets/UI/Zone_Timer_Border.Zone_Timer_Border")})
    {
        auto* T=LoadObject<UTexture2D>(nullptr,Path); if (!T) continue;
        const int32 X=T->Source.GetSizeX(),Y=T->Source.GetSizeY(); TArray64<uint8> Raw; TArray64<uint8> PNG;
        if (T->Source.GetMipData(Raw,0) && Raw.Num()==int64(X)*Y*4)
        {
            TArray<FColor> Pixels; Pixels.SetNumUninitialized(X*Y); FMemory::Memcpy(Pixels.GetData(),Raw.GetData(),Raw.Num());
            FImageUtils::PNGCompressImageArray(X,Y,Pixels,PNG); FFileHelper::SaveArrayToFile(PNG,*(Folder/(T->GetName()+TEXT(".png"))));
        }
        UE_LOG(LogTemp,Display,TEXT("UI_FEEDBACK texture=%s size=%dx%d srgb=%d"),Path,X,Y,T->SRGB);
    }
    auto* Media=LoadObject<UFileMediaSource>(nullptr,TEXT("/Game/Assets/UI/TriggerPress.TriggerPress"));
    UE_LOG(LogTemp,Display,TEXT("UI_FEEDBACK trigger_file=%s"),Media ? *Media->GetFilePath() : TEXT("MISSING"));
    FPlatformMisc::RequestExitWithStatus(false,0);
}
void MissionSample()
{
    auto* Asset=BP(TEXT("WBP_Mission"));
    auto* Mission=NewObject<UOBMissionWidget>(GetTransientPackage(),Asset->GeneratedClass);
    Mission->SetDesignerFlags(EWidgetDesignFlags::Designing); Mission->Initialize(); const auto Slate=Mission->TakeWidget();
    FOBTrainingResultData Data;
    for (int32 I=0;I<2;++I) { FOBTrainingStepResult S; S.Step=I ? EOBTrainingStep::AimHold : EOBTrainingStep::WeaponCheck; S.Outcome=I ? EOBTrainingOutcome::Passed : EOBTrainingOutcome::Failed; Data.Steps.Add(S); }
    Mission->UpdateTrainingObjectives(Data,true);
    FAssetCompilingManager::Get().FinishAllCompilation();
    FWidgetRenderer Renderer(true); auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(280,408),TF_Bilinear,false);
    Renderer.DrawWidget(Target,Slate,FVector2D(280,408),0.f); FlushRenderingCommands();
    Renderer.DrawWidget(Target,Slate,FVector2D(280,408),0.f); FlushRenderingCommands();
    TArray<FColor> Pixels; FReadSurfaceDataFlags Flags; Flags.SetLinearToGamma(false);
    bool OK=Target && Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
    if (OK) { TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(280,408,Pixels,PNG); const auto Folder=FPaths::ProjectSavedDir()/TEXT("Tests/UIReview/Feedback/Captures"); IFileManager::Get().MakeDirectory(*Folder,true); OK=FFileHelper::SaveArrayToFile(PNG,*(Folder/TEXT("Mission_PassFail_Preview.png"))); }
    UE_LOG(LogTemp,Display,TEXT("UI_FEEDBACK mission_sample=%d"),OK); FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);
}
void SkipCheck(const TArray<FString>& Args)
{
    for (const auto& Context:GEngine->GetWorldContexts())
        if (auto* World=Context.World(); World && World->IsGameWorld())
            if (auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController()); PC && PC->GetOBHUD())
                if (auto* Overlay=PC->GetOBHUD()->GetPlayerOverlayWidget())
                    if (auto* Skip=Cast<UOBTrainingSkipWidget>(Overlay->GetWidgetFromName(TEXT("TrainingSkip"))))
                    {
                        if (Args.Contains(TEXT("reopen"))) { Skip->SetAvailable(false); Skip->SetAvailable(true); }
                        auto* Image=Cast<UImage>(Skip->GetWidgetFromName(TEXT("TriggerLoopVideo")));
                        UOBUIVideo* Video=nullptr;
                        ForEachObjectWithOuter(Skip,[&](UObject* O){if (auto* V=Cast<UOBUIVideo>(O)) Video=V;},false);
                        auto* Player=Video ? Video->GetPlayer() : nullptr;
                        UE_LOG(LogTemp,Display,TEXT("UI_FEEDBACK skip available=%d playing=%d ready=%d material=%d time=%.3f"),Skip->IsAvailable(),Player && Player->IsPlaying(),Video && Video->IsReadyToDisplay(),Image && Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject()),Player ? Player->GetTime().GetTotalSeconds() : -1);
                        return;
                    }
}
void MissionRuntimeSample()
{
    for (const auto& Context:GEngine->GetWorldContexts())
        if (auto* World=Context.World(); World && World->IsGameWorld())
            if (auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController()); PC && PC->GetOBHUD())
                if (auto* Overlay=PC->GetOBHUD()->GetPlayerOverlayWidget())
                    if (auto* Mission=Cast<UOBMissionWidget>(Overlay->GetWidgetFromName(TEXT("Mission"))))
                    {
                        Mission->SetTrainingDirector(nullptr);
                        FOBTrainingResultData Data;
                        for (int32 I=0;I<2;++I) { FOBTrainingStepResult S; S.Step=I ? EOBTrainingStep::AimHold : EOBTrainingStep::WeaponCheck; S.Outcome=I ? EOBTrainingOutcome::Passed : EOBTrainingOutcome::Failed; Data.Steps.Add(S); }
                        Mission->UpdateTrainingObjectives(Data,true);
                        auto* Frame=Cast<UBorder>(Mission->GetWidgetFromName(TEXT("Mission")));
                        auto* Line=Cast<UImage>(Mission->GetWidgetFromName(TEXT("Strike_1")));
                        UE_LOG(LogTemp,Display,TEXT("UI_FEEDBACK mission_runtime frame=%s brush_tint=%s image_tint=%s"),Frame ? *GetNameSafe(Frame->Background.GetResourceObject()) : TEXT("NONE"),Line ? *Line->GetBrush().TintColor.GetSpecifiedColor().ToString() : TEXT(""),Line ? *Line->GetColorAndOpacity().ToString() : TEXT(""));
                        return;
                    }
}
void ApplyTrainingBlur()
{
    auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
    auto* C=Overlay ? Cast<UCanvasPanel>(Overlay->WidgetTree->RootWidget) : nullptr;
    if (!C) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto* Blur=Node<UBackgroundBlur>(Overlay,TEXT("TrainingBackgroundBlur"));
    Blur->SetPadding(FMargin(0)); Blur->SetBlurStrength(3.f); Blur->SetApplyAlphaToBlur(false);
    Blur->SetOverrideAutoRadiusCalculation(false); Blur->SetVisibility(ESlateVisibility::Collapsed);
    auto* Slot=OBUIReadability::Place(C,Blur,FVector2D::ZeroVector,FVector2D::ZeroVector,FAnchors(0,0,1,1),FVector2D::ZeroVector);
    Slot->SetOffsets(FMargin(0)); Slot->SetZOrder(-100);
    const bool OK=OBUIReadability::Compile(Overlay);
    UE_LOG(LogTemp,Display,TEXT("UI_BLUR applied=%d strength=3 layer=-100"),OK); FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);
}
void ResultHoldSample(const TArray<FString>& Args)
{
    for (const auto& Context:GEngine->GetWorldContexts())
        if (auto* W=Context.World(); W && W->IsGameWorld())
            if (auto* PC=Cast<AOBPlayerController>(W->GetFirstPlayerController()))
            {
                const bool bStage=Args.Num()>0 && Args[0]==TEXT("stage");
                if (bStage)
                    if (auto* Player=Cast<AOBPlayerCharacter>(PC->GetPawn())) Player->GetStageFlowComponent()->SuspendForQA();
                UOBPlayerOverlayWidget* Overlay=nullptr;
                // QA UI owns a separate visible overlay; the live HUD is hidden during that preview.
                for (TObjectIterator<UOBPlayerOverlayWidget> It;It;++It)
                    if (It->GetWorld()==W && It->IsInViewport() && It->IsVisible()) { Overlay=*It; break; }
                if (Overlay)
                    if (auto* Result=Overlay->GetTrainingResultWidget())
                    {
                        Result->bAutomaticNavigation=false;
                        if (bStage)
                        {
                            FOBTrainingResultData Data; Data.bFinished=true; Data.bPrototypeFlow=true;
                            Overlay->ShowTrainingResult(Data); Result->SetStageCompletion(Args.Num()<3 || Args[2]!=TEXT("fail"));
                        }
                        const float Seconds=Args.Num()>1 ? FMath::Clamp(FCString::Atof(*Args[1]),0.f,2.f) : 0.f;
                        if (Seconds>0)
                        {
                            Result->HandleTrigger(false); Result->HandleTrigger(true); Result->AdvanceInteraction(Seconds);
                            // Freeze at the sampled percent for deterministic screen capture.
                            Result->SetDesignerFlags(EWidgetDesignFlags::Designing);
                        }
                        UE_LOG(LogTemp,Display,TEXT("STAGE_RESULTS capture_sample stage=%d hold=%.1f"),bStage,Seconds); return;
                    }
            }
}
}
static FAutoConsoleCommand GFeedbackInspect(TEXT("OB.UI.FeedbackInspect"),TEXT("Inspect existing UI artwork and trigger media without changing assets."),FConsoleCommandDelegate::CreateStatic(&OBUIFeedback::Inspect));
static FAutoConsoleCommand GFeedbackApply(TEXT("OB.UI.ApplyFeedback"),TEXT("Apply the supplied UI feedback using original artwork and editable widget components."),FConsoleCommandDelegate::CreateStatic(&OBUIFeedback::Apply));
static FAutoConsoleCommand GFeedbackMission(TEXT("OB.UI.FeedbackMissionSample"),TEXT("Render a labeled mixed-outcome mission preview without changing gameplay results."),FConsoleCommandDelegate::CreateStatic(&OBUIFeedback::MissionSample));
static FAutoConsoleCommand GFeedbackSkip(TEXT("OB.UI.FeedbackSkipCheck"),TEXT("Inspect trigger media playback; optional reopen exercises close/open of the same widget."),FConsoleCommandWithArgsDelegate::CreateStatic(&OBUIFeedback::SkipCheck));
static FAutoConsoleCommand GFeedbackMissionRuntime(TEXT("OB.UI.FeedbackMissionRuntime"),TEXT("Temporarily show mixed mission outcomes in the running HUD, without changing gameplay result data."),FConsoleCommandDelegate::CreateStatic(&OBUIFeedback::MissionRuntimeSample));
static FAutoConsoleCommand GTrainingBlur(TEXT("OB.UI.ApplyTrainingBlur"),TEXT("Add a fullscreen BackgroundBlur beneath the saved training HUD, preserving foreground layout."),FConsoleCommandDelegate::CreateStatic(&OBUIFeedback::ApplyTrainingBlur));
static FAutoConsoleCommand GStageResults(TEXT("OB.UI.ApplyStageResults"),TEXT("Apply neutral stage report and hold indicators, preserving tutorial stamp artwork."),FConsoleCommandDelegate::CreateStatic(&OBUIFeedback::ApplyStageResults));
static FAutoConsoleCommand GResultHoldSample(TEXT("OB.UI.ResultHoldSample"),TEXT("Freeze a held-input sample in the running HUD for QA capture: tutorial|stage seconds [fail]."),FConsoleCommandWithArgsDelegate::CreateStatic(&OBUIFeedback::ResultHoldSample));
#endif
