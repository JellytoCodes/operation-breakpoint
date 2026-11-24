#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/SizeBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Texture2D.h"
#include "Animation/WidgetAnimation.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "AssetCompilingManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/DateTime.h"
#include "UObject/SavePackage.h"
#include "Components/CanvasPanel.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Training/OBTrainingUIData.h"
#include "Engine/DataTable.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Widget/OBSystemMessageWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Core/OBPlayerController.h"
#include "Widget/OBHUD.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

// Editor-only inspection/application entry points; no gameplay layout overrides.
namespace OBUIReadability
{
const TCHAR* Root=TEXT("/Game/Blueprints/Widget/PlayerOverlay");
TArray<UWidgetBlueprint*> Blueprints()
{
    TArray<UWidgetBlueprint*> Result;
    const TArray<FString> Paths={TEXT("/Game/Blueprints/Widget/WBP_Title"),FString(Root)/TEXT("WBP_PlayerOverlay"),
        FString(Root)/TEXT("Components/WBP_Guide"),FString(Root)/TEXT("Components/WBP_Mission"),
        FString(Root)/TEXT("Components/WBP_Subtitle"),FString(Root)/TEXT("Components/WBP_Timer"),
        FString(Root)/TEXT("Components/WBP_Life"),FString(Root)/TEXT("Components/WBP_TrainingResult"),
        FString(Root)/TEXT("Components/WBP_SystemMessage"),FString(Root)/TEXT("Components/WBP_TrainingSkip")};
    for (const FString& Path:Paths)
        if (auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(Path+TEXT(".")+FPaths::GetBaseFilename(Path)))) Result.Add(BP);
    return Result;
}
TArray<TSharedPtr<FJsonValue>> Vector(double X,double Y)
{ return {MakeShared<FJsonValueNumber>(X),MakeShared<FJsonValueNumber>(Y)}; }
TArray<TSharedPtr<FJsonValue>> Margin(const FMargin& M)
{ return {MakeShared<FJsonValueNumber>(M.Left),MakeShared<FJsonValueNumber>(M.Top),MakeShared<FJsonValueNumber>(M.Right),MakeShared<FJsonValueNumber>(M.Bottom)}; }
void Inspect()
{
    TArray<TSharedPtr<FJsonValue>> Assets;
    for (auto* BP:Blueprints())
    {
        auto Asset=MakeShared<FJsonObject>(); Asset->SetStringField(TEXT("asset"),BP->GetPathName());
        Asset->SetStringField(TEXT("root"),BP->WidgetTree && BP->WidgetTree->RootWidget ? BP->WidgetTree->RootWidget->GetName() : TEXT(""));
        TArray<TSharedPtr<FJsonValue>> Nodes;
        if (BP->WidgetTree) BP->WidgetTree->ForEachWidget([&](UWidget* W) {
            auto Node=MakeShared<FJsonObject>();
            Node->SetStringField(TEXT("name"),W->GetName()); Node->SetStringField(TEXT("class"),W->GetClass()->GetName());
            Node->SetStringField(TEXT("parent"),GetNameSafe(W->GetParent()));
            Node->SetNumberField(TEXT("visibility"),int32(W->GetVisibility())); Node->SetNumberField(TEXT("opacity"),W->GetRenderOpacity());
            Node->SetArrayField(TEXT("render_scale"),Vector(W->GetRenderTransform().Scale.X,W->GetRenderTransform().Scale.Y));
            if (auto* Slot=Cast<UCanvasPanelSlot>(W->Slot))
            {
                const auto A=Slot->GetAnchors();
                Node->SetArrayField(TEXT("anchor_min"),Vector(A.Minimum.X,A.Minimum.Y));
                Node->SetArrayField(TEXT("anchor_max"),Vector(A.Maximum.X,A.Maximum.Y));
                Node->SetArrayField(TEXT("offsets"),Margin(Slot->GetOffsets()));
                Node->SetArrayField(TEXT("alignment"),Vector(Slot->GetAlignment().X,Slot->GetAlignment().Y));
                Node->SetBoolField(TEXT("auto_size"),Slot->GetAutoSize()); Node->SetNumberField(TEXT("z"),Slot->GetZOrder());
            }
            if (auto* Text=Cast<UTextBlock>(W))
            {
                const auto F=Text->GetFont();
                Node->SetStringField(TEXT("text"),Text->GetText().ToString()); Node->SetNumberField(TEXT("font_size"),F.Size);
                Node->SetStringField(TEXT("font"),GetPathNameSafe(F.FontObject)); Node->SetNumberField(TEXT("outline"),F.OutlineSettings.OutlineSize);
                Node->SetStringField(TEXT("color"),Text->GetColorAndOpacity().GetSpecifiedColor().ToString());
                Node->SetNumberField(TEXT("wrap_at"),Text->GetWrapTextAt()); Node->SetBoolField(TEXT("auto_wrap"),Text->GetAutoWrapText());
            }
            if (auto* Box=Cast<USizeBox>(W))
            { Node->SetNumberField(TEXT("width"),Box->GetWidthOverride()); Node->SetNumberField(TEXT("height"),Box->GetHeightOverride()); }
            if (auto* Box=Cast<UScaleBox>(W))
            { Node->SetNumberField(TEXT("stretch"),int32(Box->GetStretch())); Node->SetNumberField(TEXT("direction"),int32(Box->GetStretchDirection())); }
            const FSlateBrush* Brush=nullptr;
            if (auto* Image=Cast<UImage>(W)) Brush=&Image->GetBrush();
            if (auto* Border=Cast<UBorder>(W)) { Brush=&Border->Background; Node->SetArrayField(TEXT("padding"),Margin(Border->GetPadding())); }
            if (Brush)
            {
                Node->SetStringField(TEXT("resource"),GetPathNameSafe(Brush->GetResourceObject()));
                Node->SetArrayField(TEXT("brush_size"),Vector(Brush->GetImageSize().X,Brush->GetImageSize().Y));
                if (auto* Texture=Cast<UTexture2D>(Brush->GetResourceObject()))
                    Node->SetArrayField(TEXT("texture_size"),Vector(Texture->Source.GetSizeX(),Texture->Source.GetSizeY()));
            }
            Nodes.Add(MakeShared<FJsonValueObject>(Node));
        });
        Asset->SetArrayField(TEXT("nodes"),Nodes);
        TArray<TSharedPtr<FJsonValue>> Animations;
        for (const auto& Animation:BP->Animations) if (Animation) Animations.Add(MakeShared<FJsonValueString>(Animation->GetName()));
        Asset->SetArrayField(TEXT("animations"),Animations);
        Assets.Add(MakeShared<FJsonValueObject>(Asset));
    }
    FString Json; const auto Writer=TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Assets,Writer);
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Tests/UIReview"); IFileManager::Get().MakeDirectory(*Folder,true);
    const bool OK=FFileHelper::SaveStringToFile(Json,*(Folder/TEXT("widget_details.json")));
    UE_LOG(LogTemp,Display,TEXT("UI_READABILITY inspect=%d assets=%d"),OK,Assets.Num());
    FPlatformMisc::RequestExitWithStatus(false,OK && Assets.Num()==10 ? 0 : 1);
}
FLinearColor Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }
UCanvasPanelSlot* Place(UCanvasPanel* Canvas,UWidget* Widget,FVector2D Position,FVector2D Size,
    FAnchors Anchors=FAnchors(0,0),FVector2D Alignment=FVector2D::ZeroVector)
{
    Widget->RemoveFromParent(); auto* Slot=Canvas->AddChildToCanvas(Widget);
    Slot->SetAnchors(Anchors); Slot->SetAlignment(Alignment); Slot->SetPosition(Position); Slot->SetSize(Size); Slot->SetAutoSize(false);
    return Slot;
}
void Baseline(UWidgetBlueprint* BP,const TCHAR* Name,float Width,float Height)
{
    auto* Box=CastChecked<USizeBox>(BP->WidgetTree->FindWidget(Name)); Box->SetWidthOverride(Width); Box->SetHeightOverride(Height);
}
UTextBlock* AddLabel(UWidgetBlueprint* BP,const TCHAR* Name,int32 Size,const TCHAR* Hex,const TCHAR* Sample)
{
    auto* Text=Cast<UTextBlock>(BP->WidgetTree->FindWidget(Name));
    if (!Text) Text=BP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    if (auto* Existing=Cast<UTextBlock>(BP->WidgetTree->FindWidget(TEXT("BodyText")))) Text->SetFont(Existing->GetFont());
    auto Font=Text->GetFont(); Font.Size=Size; Font.OutlineSettings.OutlineSize=0;
    if (!Font.FontObject) Font.FontObject=LoadObject<UObject>(nullptr,TEXT("/Game/Assets/UI/Fonts/Pretendard-Bold_Font.Pretendard-Bold_Font"));
    Text->SetFont(Font); Text->SetText(FText::FromString(Sample)); Text->SetColorAndOpacity(Color(Hex));
    Text->SetAutoWrapText(false); Text->SetLineHeightPercentage(1.08f); Text->SetShadowOffset(FVector2D::ZeroVector);
    Text->bIsVariable=true;
    return Text;
}
bool Save(UObject* Object)
{
    Object->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Object->GetPackage(),Object,*FPackageName::LongPackageNameToFilename(Object->GetPackage()->GetName(),TEXT(".uasset")),Args);
}
bool Compile(UWidgetBlueprint* BP)
{
    TSet<FName> Names;
    BP->WidgetTree->ForEachWidget([&](UWidget* W) { Names.Add(W->GetFName()); });
    for (const auto& Animation:BP->Animations) if (Animation) Names.Add(Animation->GetFName());
    for (auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It)
        if (!Names.Contains(It.Key())) It.RemoveCurrent();
    for (FName Name:Names) if (!BP->WidgetVariableNameToGuidMap.Contains(Name)) BP->OnVariableAdded(Name);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
    return BP->Status!=BS_Error && Save(BP);
}
void ApplyLifeProgress()
{
    auto* Life=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Life.WBP_Life"));
    auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
    auto* Full=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Life/HP_Full.HP_Full"));
    auto* Empty=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Life/HP_None.HP_None"));
    if (!Life || !Overlay || !Full || !Empty) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    // Measure the colored interior, excluding the transparent margins and the shared outline.
    TArray64<uint8> FullPixels,EmptyPixels;
    const int32 Width=Full->Source.GetSizeX(),Height=Full->Source.GetSizeY();
    if (!Full->Source.GetMipData(FullPixels,0) || !Empty->Source.GetMipData(EmptyPixels,0)
        || FullPixels.Num()!=int64(Width)*Height*4 || EmptyPixels.Num()!=FullPixels.Num())
    { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    int32 InteriorTop=Height,InteriorBottom=-1;
    for (int32 Y=0;Y<Height;++Y) for (int32 X=0;X<Width;++X)
    {
        const int64 Alpha=(int64(Y)*Width+X)*4+3;
        if (FullPixels[Alpha]>200 && EmptyPixels[Alpha]<10)
        { InteriorTop=FMath::Min(InteriorTop,Y); InteriorBottom=FMath::Max(InteriorBottom,Y); }
    }
    if (InteriorBottom<InteriorTop) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const float FillPadding=64.f*FMath::Min(InteriorTop,Height-1-InteriorBottom)/Height;
    auto* Bar=Cast<UProgressBar>(Life->WidgetTree->FindWidget(TEXT("HealthProgress")));
    if (!Bar)
    {
        auto* Heart=Cast<UImage>(Life->WidgetTree->FindWidget(TEXT("LIFE_01")));
        if (!Heart || !Heart->GetParent()) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
        Bar=Life->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("HealthProgress"));
        // Keep the authored slot, alignment, background frame and number label.
        if (!Heart->GetParent()->ReplaceChild(Heart,Bar)) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
        Heart->Slot=nullptr;
        Heart->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    }
    Bar->bIsVariable=true;
    FSlateBrush FullBrush,EmptyBrush;
    FullBrush.SetResourceObject(Full); EmptyBrush.SetResourceObject(Empty);
    FullBrush.DrawAs=EmptyBrush.DrawAs=ESlateBrushDrawType::Image;
    FullBrush.SetImageSize(FVector2D(64,64)); EmptyBrush.SetImageSize(FVector2D(64,64));
    auto Style=Bar->GetWidgetStyle();
    // SProgressBar takes its desired size from MarqueeImage even when marquee is disabled.
    Style.SetBackgroundImage(EmptyBrush).SetFillImage(FullBrush).SetMarqueeImage(FullBrush).SetEnableFillAnimation(false);
    Bar->SetWidgetStyle(Style); Bar->SetBarFillType(EProgressBarFillType::BottomToTop);
    Bar->SetBarFillStyle(EProgressBarFillStyle::Mask); Bar->SetBorderPadding(FVector2D(0,FillPadding));
    Bar->SetFillColorAndOpacity(FLinearColor::White); Bar->SetIsMarquee(false); Bar->SetPercent(1.f);
    CastChecked<UTextBlock>(Life->WidgetTree->FindWidget(TEXT("TitleText")))->SetText(FText::FromString(TEXT("5 / 5")));
    const bool OK=Compile(Life) && Compile(Overlay);
    UE_LOG(LogTemp,Display,TEXT("UI_LIFE_PROGRESS applied=%d preserved_textures=1 interior=%d..%d fill_padding=%.3f"),OK,InteriorTop,InteriorBottom,FillPadding);
    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);
}
void Apply()
{
    // Preserve the authored artwork, component trees and animation targets. Only edit readability and placement.
    const auto Assets=Blueprints();
    if (Assets.Num()!=10) { UE_LOG(LogTemp,Error,TEXT("UI_READABILITY original widget missing")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups/UIReadability")/FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    IFileManager::Get().MakeDirectory(*Backup,true);
    for (auto* BP:Assets)
    {
        const FString File=FPackageName::LongPackageNameToFilename(BP->GetPackage()->GetName(),TEXT(".uasset"));
        if (IFileManager::Get().Copy(*(Backup/FPaths::GetCleanFilename(File)),*File)!=COPY_OK)
        { UE_LOG(LogTemp,Error,TEXT("UI_READABILITY widget backup failed")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    }
    TMap<FString,UWidgetBlueprint*> Widgets; for (auto* BP:Assets) Widgets.Add(BP->GetName(),BP);
    auto Style=[](UWidgetBlueprint* BP,const TCHAR* Name,int32 Size) {
        auto* Text=CastChecked<UTextBlock>(BP->WidgetTree->FindWidget(Name));
        auto Font=Text->GetFont(); Font.Size=Size; Text->SetFont(Font); Text->SetAutoWrapText(false); return Text;
    };
    auto* Mission=Widgets[TEXT("WBP_Mission")]; Baseline(Mission,TEXT("BaselineSizeBox"),240,350);
    auto* MissionTitle=Style(Mission,TEXT("MissionTitleText"),20);
    if (auto* Slot=Cast<UVerticalBoxSlot>(MissionTitle->Slot)) { Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); Slot->SetPadding(FMargin(0,3,0,14)); }
    CastChecked<UBorder>(Mission->WidgetTree->FindWidget(TEXT("Mission")))->SetPadding(FMargin(12,4,12,12));
    if (auto* Spacer=Cast<USpacer>(Mission->WidgetTree->FindWidget(TEXT("Spacer_78")))) Spacer->SetSize(FVector2D(0,4));
    for (int32 I=1;I<=5;++I)
    {
        auto* Row=Style(Mission,*FString::Printf(TEXT("BodyText_%d"),I),16); Row->SetColorAndOpacity(Color(TEXT("17395C")));
        if (auto* Slot=Cast<UVerticalBoxSlot>(Row->GetParent()->Slot)) { Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); Slot->SetPadding(FMargin(0,0,0,10)); }
    }
    auto* Timer=Widgets[TEXT("WBP_Timer")]; Baseline(Timer,TEXT("BaselineSizeBox"),220,110);
    auto* Zone=Style(Timer,TEXT("TitleText"),11); auto* Time=Style(Timer,TEXT("ValueText"),28);
    // The same title displays Korean training names as well as the Latin ZONE label.
    auto ZoneFont=Zone->GetFont(); ZoneFont.FontObject=LoadObject<UObject>(nullptr,TEXT("/Game/Assets/UI/Fonts/Pretendard-Bold_Font.Pretendard-Bold_Font")); Zone->SetFont(ZoneFont);
    if (auto* Slot=Cast<UVerticalBoxSlot>(Zone->Slot)) { Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); Slot->SetPadding(FMargin(0,0,0,8)); }
    if (auto* Slot=Cast<UVerticalBoxSlot>(Time->Slot)) { Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); Slot->SetPadding(FMargin(0)); }
    auto* Life=Widgets[TEXT("WBP_Life")]; Baseline(Life,TEXT("BaselineSizeBox"),104,104);
    Style(Life,TEXT("TitleText"),14);
    if (auto* Heart=Cast<UImage>(Life->WidgetTree->FindWidget(TEXT("LIFE_01"))))
    { auto HeartBrush=Heart->GetBrush(); HeartBrush.SetImageSize(FVector2D(64,64)); Heart->SetBrush(HeartBrush); }
    auto* Subtitle=Widgets[TEXT("WBP_Subtitle")]; Baseline(Subtitle,TEXT("BaselineSizeBox"),1792,56);
    auto* Script=Style(Subtitle,TEXT("Script"),16); Script->SetColorAndOpacity(Color(TEXT("17395C"))); Script->SetWrapTextAt(1612);
    auto* Speaker=Style(Subtitle,TEXT("Name"),16);
    auto* ScriptSlot=CastChecked<UOverlaySlot>(Script->Slot); ScriptSlot->SetPadding(FMargin(156,0,24,0)); ScriptSlot->SetHorizontalAlignment(HAlign_Fill); ScriptSlot->SetVerticalAlignment(VAlign_Center);
    auto* SpeakerSlot=CastChecked<UOverlaySlot>(Speaker->Slot); SpeakerSlot->SetPadding(FMargin(24,0,0,0)); SpeakerSlot->SetHorizontalAlignment(HAlign_Left); SpeakerSlot->SetVerticalAlignment(VAlign_Center);
    auto* Guide=Widgets[TEXT("WBP_Guide")];
    auto* GuideTitle=Style(Guide,TEXT("TitleText"),30); CastChecked<UCanvasPanelSlot>(GuideTitle->Slot)->SetSize(FVector2D(1180,60));
    auto* GuideCanvas=CastChecked<UCanvasPanel>(Guide->WidgetTree->FindWidget(TEXT("PanelBackground")));
    auto* Body=Style(Guide,TEXT("BodyText"),18); Body->SetWrapTextAt(1000);
    Place(GuideCanvas,Body,FVector2D(80,-36),FVector2D(1000,120),FAnchors(0,1),FVector2D(0,1));
    auto* BodyImage=CastChecked<UImage>(Guide->WidgetTree->FindWidget(TEXT("BodyImage")));
    auto* Fit=Cast<UScaleBox>(Guide->WidgetTree->FindWidget(TEXT("BodyImageFit")));
    if (!Fit) Fit=Guide->WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("BodyImageFit"));
    BodyImage->RemoveFromParent(); Fit->SetStretch(EStretch::ScaleToFit); Fit->SetContent(BodyImage);
    Place(GuideCanvas,Fit,FVector2D(0,-20),FVector2D(1240,480),FAnchors(.5f,.5f),FVector2D(.5f,.5f));
    auto* Continue=AddLabel(Guide,TEXT("ContinueHint"),14,TEXT("000000"),TEXT("방아쇠를 당겨\n계속"));
    Place(GuideCanvas,Continue,FVector2D(1230,-64),FVector2D(150,54),FAnchors(0,1),FVector2D(0,1)); Continue->SetVisibility(ESlateVisibility::Hidden);
    auto* Result=Widgets[TEXT("WBP_TrainingResult")];
    auto* ResultTitle=Style(Result,TEXT("TitleText"),64);
    auto* ResultBody=Style(Result,TEXT("BodyText"),24); ResultBody->SetWrapTextAt(1260);
    auto* ResultOverlay=CastChecked<UOverlay>(ResultBody->GetParent());
    auto* TitleSlot=CastChecked<UOverlaySlot>(ResultTitle->Slot); TitleSlot->SetPadding(FMargin(40,0,40,70)); TitleSlot->SetHorizontalAlignment(HAlign_Fill); TitleSlot->SetVerticalAlignment(VAlign_Center);
    auto* BodySlot=CastChecked<UOverlaySlot>(ResultBody->Slot); BodySlot->SetPadding(FMargin(100,0,100,150)); BodySlot->SetHorizontalAlignment(HAlign_Fill); BodySlot->SetVerticalAlignment(VAlign_Bottom);
    auto* Hint=AddLabel(Result,TEXT("InteractionText"),18,TEXT("17395C"),TEXT("방아쇠 한 번: 다음 스테이지\n방아쇠 길게 (2초): 다시 훈련"));
    Hint->SetColorAndOpacity(ResultBody->GetColorAndOpacity()); Hint->SetJustification(ETextJustify::Center);
    Hint->RemoveFromParent(); auto* HintSlot=ResultOverlay->AddChildToOverlay(Hint); HintSlot->SetPadding(FMargin(100,0,100,50)); HintSlot->SetHorizontalAlignment(HAlign_Fill); HintSlot->SetVerticalAlignment(VAlign_Bottom);
    auto* Message=Widgets[TEXT("WBP_SystemMessage")]; Baseline(Message,TEXT("SizeBox_0"),1000,100);
    auto* MessageText=Style(Message,TEXT("Text_Tutorial"),24); MessageText->SetWrapTextAt(980);
    MessageText->SetShadowColorAndOpacity(FLinearColor(0,0,0,.85f)); MessageText->SetShadowOffset(FVector2D(1,1));
    auto* Skip=Widgets[TEXT("WBP_TrainingSkip")]; Baseline(Skip,TEXT("SkipSize"),240,300); Baseline(Skip,TEXT("VideoSize"),136,136);
    Style(Skip,TEXT("TitleText"),18); auto* SkipBody=Style(Skip,TEXT("BodyText"),14); SkipBody->SetWrapTextAt(220);
    auto* Overlay=Widgets[TEXT("WBP_PlayerOverlay")];
    auto Slot=[&](const TCHAR* Name) { return CastChecked<UCanvasPanelSlot>(Overlay->WidgetTree->FindWidget(Name)->Slot); };
    Slot(TEXT("Timer"))->SetAlignment(FVector2D(.5f,0)); Slot(TEXT("Timer"))->SetPosition(FVector2D(0,32));
    Slot(TEXT("Life"))->SetAlignment(FVector2D(1,0)); Slot(TEXT("Life"))->SetPosition(FVector2D(-40,32));
    Slot(TEXT("Subtitle"))->SetAlignment(FVector2D(.5f,1)); Slot(TEXT("Subtitle"))->SetPosition(FVector2D(0,-24));
    Slot(TEXT("SystemMessage"))->SetSize(FVector2D(1000,100));
    bool OK=true;
    for (auto* BP:Assets) if (BP!=Overlay && BP->GetName()!=TEXT("WBP_Title")) OK=Compile(BP) && OK;
    OK=Compile(Overlay) && OK;
    UE_LOG(LogTemp,Display,TEXT("UI_READABILITY preserved_design=%d widgets=9 textures_modified=0"),OK);
    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);
}
bool Preview(const FString& Mode,FIntPoint Size,int32 Page=0)
{
    auto* Class=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    if (!Class) return false;
    TStrongObjectPtr<UOBPlayerOverlayWidget> Overlay(NewObject<UOBPlayerOverlayWidget>(GetTransientPackage(),Class));
    Overlay->SetDesignerFlags(EWidgetDesignFlags::Designing|EWidgetDesignFlags::ExecutePreConstruct);
    Overlay->DesignerPreview=EOBOverlayPreviewMode::Stage;
    if (!Overlay->Initialize()) return false;
    const auto Slate=Overlay->TakeWidget(); Overlay->SynchronizeProperties();
    for (const TCHAR* Name:{TEXT("Guide"),TEXT("Subtitle"),TEXT("Mission"),TEXT("Timer"),TEXT("Life"),TEXT("TrainingMarkers"),TEXT("TrainingResult"),TEXT("SystemMessage"),TEXT("TrainingSkip")})
        if (auto* W=Overlay->GetWidgetFromName(Name)) W->SetVisibility(ESlateVisibility::Collapsed);
    auto Show=[&](const TCHAR* Name) { auto* W=Overlay->GetWidgetFromName(Name); W->SetVisibility(ESlateVisibility::HitTestInvisible); W->SetRenderOpacity(1.f); return W; };
    if (Mode==TEXT("HUD"))
    {
        auto* Timer=CastChecked<UOBHudPanelWidget>(Show(TEXT("Timer"))); Timer->SetZoneProgress(6,6); Timer->SetRemainingSeconds(90);
        auto* Life=CastChecked<UOBHudPanelWidget>(Show(TEXT("Life"))); Life->SetHealth(40,100);
        auto* Mission=CastChecked<UOBMissionWidget>(Show(TEXT("Mission"))); FOBTrainingResultData Sample; Mission->UpdateTrainingObjectives(Sample,true);
        auto* Caption=CastChecked<UOBSubtitleWidget>(Show(TEXT("Subtitle")));
        Caption->SetDialogue(FText::FromString(TEXT("상황실")),FText::FromString(TEXT("전방에 적과 인질이 함께 있습니다. 사격 전에 대상을 정확히 식별하고, 엄폐물을 이용해 이동하십시오. 아군의 지원 신호를 확인한 뒤 다음 구역으로 진입합니다.")));
    }
    else if (Mode==TEXT("Guide"))
    { auto* Guide=CastChecked<UOBGuideWidget>(Show(TEXT("Guide"))); Guide->SetGuidePage(Page,true); }
    else if (Mode==TEXT("Result") || Mode==TEXT("Ending"))
    {
        auto* Result=CastChecked<UOBTrainingResultWidget>(Show(TEXT("TrainingResult"))); Result->bAutomaticNavigation=false;
        FOBTrainingResultData Sample; Sample.bPrototypeFlow=true; Sample.bScenarioCompleted=Page==3009; Sample.RecordedFireSeconds=75; Sample.RecordedFireLimitSeconds=120;
        if (Mode==TEXT("Ending")) Result->SetStageCompletion(Page==3009);
        else if (Page==3008) Result->SetWarning(Sample); else Result->SetResult(Sample);
    }
    else if (Mode==TEXT("Message")) CastChecked<UOBSystemMessageWidget>(Show(TEXT("SystemMessage")))->SetMessage(FText::FromString(TEXT("기초 훈련을 시작합니다. 교관의 명령에 따라 훈련을 진행합니다.")));
    else if (Mode==TEXT("Skip")) Show(TEXT("TrainingSkip"));
    TSharedRef<SWidget> Scaled=SNew(SDPIScaler).DPIScale(GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size))[Slate];
    // Apply Slate's display gamma once, without a second sRGB render-target conversion.
    FAssetCompilingManager::Get().FinishAllCompilation(); FWidgetRenderer Renderer(true);
    auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,false);
    Renderer.DrawWidget(Target,Scaled,FVector2D(Size),0.f);
    FlushRenderingCommands(); if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    Renderer.DrawWidget(Target,Scaled,FVector2D(Size),0.f); FlushRenderingCommands();
    TArray<FColor> Pixels; FReadSurfaceDataFlags Flags; Flags.SetLinearToGamma(false);
    if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)) return false;
    TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Tests/UIReview/After"); IFileManager::Get().MakeDirectory(*Folder,true);
    const FString Filename=FString::Printf(TEXT("%s_%d_%dx%d.png"),*Mode,Page,Size.X,Size.Y);
    const bool OK=FFileHelper::SaveArrayToFile(PNG,*(Folder/Filename));
    UE_LOG(LogTemp,Display,TEXT("UI_READABILITY preview=%s ok=%d"),*Filename,OK);
    // Check only the visible component for the sample, after a real Slate layout/paint.
    UUserWidget* Panel=Mode==TEXT("HUD") ? Cast<UUserWidget>(Overlay->GetWidgetFromName(TEXT("Subtitle"))) : Mode==TEXT("Guide") ? Cast<UUserWidget>(Overlay->GetWidgetFromName(TEXT("Guide"))) :
        (Mode==TEXT("Result") || Mode==TEXT("Ending")) ? Overlay->GetTrainingResultWidget() : nullptr;
    if (Panel) Panel->WidgetTree->ForEachWidget([&](UWidget* W) {
        if (auto* Text=Cast<UTextBlock>(W); Text && Text->IsVisible() && !Text->GetText().IsEmpty())
        {
            const FVector2D Allocated=Text->GetCachedGeometry().GetLocalSize(),Desired=Text->GetDesiredSize();
            UE_LOG(LogTemp,Display,TEXT("UI_READABILITY text=%s/%s allocated=%s desired=%s"),*Filename,*Text->GetName(),*Allocated.ToString(),*Desired.ToString());
        }
    });
    return OK;
}
void PreviewAll()
{
    bool OK=true;
    for (auto Size:{FIntPoint(1920,1080),FIntPoint(1280,720),FIntPoint(2560,1080)}) OK=Preview(TEXT("HUD"),Size) && OK;
    for (int32 Page=3001;Page<=3007;++Page) OK=Preview(TEXT("Guide"),FIntPoint(1920,1080),Page) && OK;
    for (auto Size:{FIntPoint(1920,1080),FIntPoint(1280,720)})
        for (int32 Page=3008;Page<=3010;++Page) OK=Preview(TEXT("Result"),Size,Page) && OK;
    for (int32 Page:{3009,3010}) OK=Preview(TEXT("Ending"),FIntPoint(1920,1080),Page) && OK;
    OK=Preview(TEXT("Message"),FIntPoint(1920,1080)) && OK; OK=Preview(TEXT("Skip"),FIntPoint(1920,1080)) && OK;
    UE_LOG(LogTemp,Display,TEXT("UI_READABILITY preview_all=%d"),OK); FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);
}
bool PreviewSavedWidget(const TCHAR* Path,const TCHAR* Filename,bool bCursor)
{
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,Path);
    if (!BP || !BP->GeneratedClass) return false;
    TStrongObjectPtr<UUserWidget> Widget(NewObject<UUserWidget>(GetTransientPackage(),BP->GeneratedClass));
    Widget->SetDesignerFlags(EWidgetDesignFlags::Designing|EWidgetDesignFlags::ExecutePreConstruct);
    if (!Widget->Initialize()) return false;
    TSharedRef<SWidget> Slate=Widget->TakeWidget(); Widget->SynchronizeProperties(); Slate->SlatePrepass();
    if (bCursor)
    {
        auto Desired=Widget->GetDesiredSize();
        if (Desired.X<=0 || Desired.Y<=0 || Desired.X>256 || Desired.Y>256) Desired=FVector2D(64,64);
        Slate=SNew(SOverlay)+SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(Desired.X).HeightOverride(Desired.Y)[Slate]];
    }
    const FIntPoint Size(1920,1080); FAssetCompilingManager::Get().FinishAllCompilation();
    FWidgetRenderer Renderer(true); auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,false);
    Renderer.DrawWidget(Target,Slate,FVector2D(Size),0.f); FlushRenderingCommands();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    Renderer.DrawWidget(Target,Slate,FVector2D(Size),0.f); FlushRenderingCommands(); TArray<FColor> Pixels;
    FReadSurfaceDataFlags Flags; Flags.SetLinearToGamma(false);
    if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)) return false;
    TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Tests/UIReview/After"); IFileManager::Get().MakeDirectory(*Folder,true);
    return FFileHelper::SaveArrayToFile(PNG,*(Folder/Filename));
}
void CaptureAdditionalWidgets()
{
    bool OK=Preview(TEXT("Message"),FIntPoint(1920,1080));
    OK=PreviewSavedWidget(TEXT("/Game/Blueprints/Widget/WBP_Crosshair.WBP_Crosshair"),TEXT("Preview_Crosshair_1920.png"),true) && OK;
    OK=PreviewSavedWidget(TEXT("/Game/Blueprints/Widget/WBP_Result.WBP_Result"),TEXT("Preview_SeparateResult_1920.png"),false) && OK;
    UE_LOG(LogTemp,Display,TEXT("UI_SCREENSHOTS additional_widgets=%d"),OK); FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);
}
void CaptionSample()
{
    if (!GEngine) return;
    for (const auto& Context:GEngine->GetWorldContexts())
        if (auto* World=Context.World(); World && World->IsGameWorld())
            if (auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController()); PC && PC->GetOBHUD())
                if (auto* Overlay=PC->GetOBHUD()->GetPlayerOverlayWidget())
                {
                    Overlay->ShowPersistentSubtitle(FText::FromString(TEXT("상황실")),FText::FromString(TEXT("전방에 적과 인질이 함께 있습니다. 사격 전에 대상을 정확히 식별하고, 엄폐물을 이용해 이동하십시오. 아군의 지원 신호를 확인한 뒤 다음 구역으로 진입합니다.")));
                    UE_LOG(LogTemp,Display,TEXT("UI_READABILITY runtime caption sample shown; no asset data changed")); return;
                }
}
void LifeSample(const TArray<FString>& Args)
{
    if (!GEngine || Args.Num()!=1) return;
    for (const auto& Context:GEngine->GetWorldContexts())
        if (auto* World=Context.World(); World && World->IsGameWorld())
            if (auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController()); PC && PC->GetOBHUD())
                if (auto* Overlay=PC->GetOBHUD()->GetPlayerOverlayWidget())
                {
                    const float Lives=FCString::Atof(*Args[0]); Overlay->SetHealth(Lives,5);
                    auto* Life=Cast<UUserWidget>(Overlay->GetWidgetFromName(TEXT("Life")));
                    auto* Bar=Life ? Cast<UProgressBar>(Life->GetWidgetFromName(TEXT("HealthProgress"))) : nullptr;
                    UE_LOG(LogTemp,Display,TEXT("UI_LIFE_PROGRESS sample_lives=%.2f fill=%.2f"),Lives,Bar ? Bar->GetPercent() : -1.f);
                    return;
                }
}
}
static FAutoConsoleCommand GInspectUIReadability(TEXT("OB.UI.ReadabilityInspect"),TEXT("Read current widget layout and resources without saving assets."),FConsoleCommandDelegate::CreateStatic(&OBUIReadability::Inspect));
static FAutoConsoleCommand GApplyUIReadability(TEXT("OB.UI.ApplyReadability"),TEXT("Adjust readability and placement while preserving all authored textures and decoration."),FConsoleCommandDelegate::CreateStatic(&OBUIReadability::Apply));
static FAutoConsoleCommand GPreviewUIReadability(TEXT("OB.UI.PreviewReadability"),TEXT("Render saved UI states and inspect long Korean text at target viewport sizes without saving assets."),FConsoleCommandDelegate::CreateStatic(&OBUIReadability::PreviewAll));
static FAutoConsoleCommand GCaptionUIReadability(TEXT("OB.UI.ReadabilityCaptionSample"),TEXT("Show an editor-only long-caption sample over the running game for readability inspection."),FConsoleCommandDelegate::CreateStatic(&OBUIReadability::CaptionSample));
static FAutoConsoleCommand GCaptureAdditionalUI(TEXT("OB.UI.CaptureAdditionalWidgets"),TEXT("Capture saved cursor, separate result and system-message widgets without modifying assets."),FConsoleCommandDelegate::CreateStatic(&OBUIReadability::CaptureAdditionalWidgets));
static FAutoConsoleCommand GApplyLifeProgress(TEXT("OB.UI.ApplyLifeProgress"),TEXT("Replace the Life image with an editable masked ProgressBar while preserving original artwork and layout."),FConsoleCommandDelegate::CreateStatic(&OBUIReadability::ApplyLifeProgress));
static FAutoConsoleCommand GLifeSample(TEXT("OB.UI.LifeSample"),TEXT("Set a temporary HUD life sample for screenshot inspection without changing gameplay health."),FConsoleCommandWithArgsDelegate::CreateStatic(&OBUIReadability::LifeSample));
#endif
