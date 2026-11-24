#include "Training/OBTrainingStatusWidget.h"
#include "Training/OBTrainingDirector.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "Engine/GameInstance.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBRadialProgressBar.h"
#include "Engine/Texture2D.h"
#include "Character/OBPlayerCharacter.h"
#include "Stage/OBStageFlowComponent.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
void Visible(UWidget* Widget,bool bShow) { if (Widget) Widget->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
void Place(UWidget* Widget,FVector2D Position,FVector2D Size,FVector2D Alignment=FVector2D::ZeroVector,bool bResize=true)
{
    if (auto* Slot=Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
    { Slot->SetAnchors(FAnchors(0,0)); Slot->SetAlignment(Alignment); Slot->SetPosition(Position); Slot->SetAutoSize(false); if (bResize) Slot->SetSize(Size); }
}
}
void UOBTrainingStatusWidget::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    if (IsDesignTime())
    {
        const FVector2D Size=GetCachedGeometry().GetLocalSize();
        UpdateDesignerMarkers(Size.X>0 && Size.Y>0 ? Size : FVector2D(1920,1080));
    }
}
#if WITH_EDITOR
void UOBTrainingStatusWidget::OnSelectedByDesigner()
{
    Super::OnSelectedByDesigner();
    if (auto* Tree=GetTypedOuter<UWidgetTree>())
        if (auto* Overlay=Cast<UOBPlayerOverlayWidget>(Tree->GetOuter())) Overlay->PreviewSelectedPanel(EOBOverlayElement::Markers,true);
}
void UOBTrainingStatusWidget::OnDeselectedByDesigner()
{
    Super::OnDeselectedByDesigner();
    if (auto* Tree=GetTypedOuter<UWidgetTree>())
        if (auto* Overlay=Cast<UOBPlayerOverlayWidget>(Tree->GetOuter())) Overlay->PreviewSelectedPanel(EOBOverlayElement::Markers,false);
}
#endif
void UOBTrainingStatusWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    // The green marker is only shown briefly. Load it before the exercise so
    // its first display cannot be replaced by Slate's pending-texture brush.
    LoadedAimIdle=AimIdleTexture.LoadSynchronous();
    LoadedAimSuccess=AimSuccessTexture.LoadSynchronous();
    Visible(AimLayer,false); Visible(BreachLayer,false); Visible(BreachText,false);
}
void UOBTrainingStatusWidget::SetDirector(AOBTrainingDirector* InDirector)
{
    if (Director) Director->OnProgressChanged.RemoveDynamic(this,&ThisClass::HandleAimCompleted);
    Director=InDirector; AimCompletionUntil=0;
    if (Director) Director->OnProgressChanged.AddUniqueDynamic(this,&ThisClass::HandleAimCompleted);
}
void UOBTrainingStatusWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (Director) Director->OnProgressChanged.AddUniqueDynamic(this,&ThisClass::HandleAimCompleted);
}
void UOBTrainingStatusWidget::NativeDestruct()
{
    if (Director) Director->OnProgressChanged.RemoveDynamic(this,&ThisClass::HandleAimCompleted);
    AimCompletionUntil=0;
    Super::NativeDestruct();
}
void UOBTrainingStatusWidget::HandleAimCompleted(int32 Count,int32 Required)
{
    if (!Director || Director->CurrentStep!=EOBTrainingStep::AimHold || Count<=0 || Required<=0) return;
    if (!Director->AimPositions.IsValidIndex(Count-1)) return;
    CompletedAimUV=Director->AimPositions[Count-1];
    AimCompletionUntil=FPlatformTime::Seconds()+FMath::Max(0.f,AimCompletionFeedbackSeconds);
}
bool UOBTrainingStatusWidget::HasAimCompletionFeedback() const
{ return Director && !Director->bPrototypePaused && !Director->GetTrainingResult().bFinished && FPlatformTime::Seconds()<AimCompletionUntil; }
void UOBTrainingStatusWidget::BuildDefaultLayout()
{
    // Editor asset migration only. Gameplay renders the authored UMG tree.
    if (!WidgetTree) WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"),RF_Transient);
    auto* Canvas=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if (!Canvas && WidgetTree->RootWidget) return;
    if (!Canvas) { Canvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("MarkerCanvas")); WidgetTree->RootWidget=Canvas; }
    auto Layer=[&](const TCHAR* Name) {
        auto* Panel=Cast<UCanvasPanel>(WidgetTree->FindWidget(Name));
        if (!Panel)
        {
            Panel=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),Name);
            auto* WidgetSlot=Canvas->AddChildToCanvas(Panel); WidgetSlot->SetAnchors(FAnchors(0,0,1,1)); WidgetSlot->SetOffsets(FMargin(0));
        }
        return Panel;
    };
    const FString Art=TEXT("/Game/Blueprints/Widget/PlayerOverlay/Art/Markers/");
    auto Image=[&](const TCHAR* Name,const TCHAR* MaterialName,FLinearColor Color) {
        auto* Result=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Name);
        const FString Path=Art+MaterialName+TEXT(".")+MaterialName;
        if (auto* Material=LoadObject<UMaterialInterface>(nullptr,*Path)) Result->SetBrushFromMaterial(Material);
        Result->SetColorAndOpacity(Color); return Result;
    };
    auto Label=[&](const TCHAR* Name,const TCHAR* Text,int32 Size,FLinearColor Color) {
        auto* Result=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        Result->SetText(FText::FromString(Text)); Result->SetAutoWrapText(false);
        auto Font=Result->GetFont(); Font.Size=Size; Result->SetFont(Font);
        Result->SetColorAndOpacity(Color); Result->SetShadowColorAndOpacity(FLinearColor::Black); Result->SetShadowOffset(FVector2D(2,2)); return Result;
    };
    const bool bAddAim=!WidgetTree->FindWidget(TEXT("AimLayer"));
    AimLayer=Layer(TEXT("AimLayer"));
    if (bAddAim)
    {
        AimTarget=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("AimTarget"));
        AimTarget->SetBrushFromTexture(AimIdleTexture.LoadSynchronous(),false);
        AimLayer->AddChildToCanvas(AimTarget); Place(AimTarget,FVector2D(960,540),FVector2D(60/.45,60/.45),FVector2D(.5,.5));
        auto* Gauge=WidgetTree->ConstructWidget<UOBRadialProgressBar>(UOBRadialProgressBar::StaticClass(),TEXT("AimProgress"));
        AimProgress=Gauge;
        FProgressBarStyle Style; Style.BackgroundImage.DrawAs=ESlateBrushDrawType::NoDrawType;
        Style.FillImage.SetResourceObject(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Art/Markers/M_TrainingAimGauge.M_TrainingAimGauge")));
        Style.FillImage.DrawAs=ESlateBrushDrawType::Image; Style.FillImage.TintColor=FLinearColor::White;
        Gauge->SetWidgetStyle(Style); Gauge->SetFillColorAndOpacity(FLinearColor::White);
        Gauge->SetBarFillStyle(EProgressBarFillStyle::Mask); Gauge->SetRadialPercent(0);
        AimLayer->AddChildToCanvas(AimProgress); Place(AimProgress,FVector2D(960,540),FVector2D(60/.45,60/.45),FVector2D(.5,.5));
    }
    const bool bAddBreach=!WidgetTree->FindWidget(TEXT("BreachLayer"));
    BreachLayer=Layer(TEXT("BreachLayer"));
    if (bAddBreach)
    {
        CameraFrame=Image(TEXT("CameraFrame"),TEXT("MI_TrainingMarkerCameraFrame"),FLinearColor(.5f,.5f,.5f,.6f));
        BreachLayer->AddChildToCanvas(CameraFrame); Place(CameraFrame,FVector2D::ZeroVector,FVector2D(1920,1080));
        CameraLabel=Label(TEXT("CameraLabel"),TEXT("카메라 기준 브리칭 안내"),GuideLabelFontSize,FLinearColor::White);
        BreachLayer->AddChildToCanvas(CameraLabel); Place(CameraLabel,FVector2D(16,16),FVector2D(400,30));
        auto Hand=[&](bool bLeft) {
            const FString Prefix=bLeft ? TEXT("Left") : TEXT("Right");
            const auto Color=bLeft ? LeftHandColor : RightHandColor;
            auto* Area=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),FName(*(Prefix+TEXT("HandArea"))));
            BreachLayer->AddChildToCanvas(Area); Place(Area,FVector2D(400,400),FVector2D(220,180));
            auto Add=[&](UOverlay* Parent,UWidget* Child) { auto* WidgetSlot=Parent->AddChildToOverlay(Child); WidgetSlot->SetHorizontalAlignment(HAlign_Fill); WidgetSlot->SetVerticalAlignment(VAlign_Fill); };
            Add(Area,Image(*(Prefix+TEXT("HandOutline")),TEXT("MI_TrainingMarkerFrameOutline"),FLinearColor::Black));
            auto* Target=Image(*(Prefix+TEXT("HandTarget")),TEXT("M_TrainingMarkerFrame"),Color); Add(Area,Target);
            auto* Success=Image(*(Prefix+TEXT("HandSuccess")),TEXT("M_TrainingMarkerFrame"),SuccessColor); Add(Area,Success);
            auto* Wrist=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),FName(*(Prefix+TEXT("Wrist"))));
            BreachLayer->AddChildToCanvas(Wrist); Place(Wrist,FVector2D(500,500),FVector2D(WristMarkerRadius*2,WristMarkerRadius*2),FVector2D(.5,.5));
            Add(Wrist,Image(*(Prefix+TEXT("WristOutline")),TEXT("MI_TrainingMarkerRingOutline"),FLinearColor::Black));
            auto* WristTarget=Image(*(Prefix+TEXT("WristTarget")),TEXT("M_TrainingMarkerRing"),Color); Add(Wrist,WristTarget);
            auto* WristSuccess=Image(*(Prefix+TEXT("WristSuccess")),TEXT("M_TrainingMarkerRing"),SuccessColor); Add(Wrist,WristSuccess);
            auto* Text=Label(*(Prefix+TEXT("HandLabel")),bLeft ? TEXT("왼손 손목") : TEXT("오른손 손목"),GuideLabelFontSize,Color);
            BreachLayer->AddChildToCanvas(Text); Place(Text,FVector2D(400,370),FVector2D(240,30));
            if (bLeft) { LeftHandArea=Area; LeftHandTarget=Target; LeftHandSuccess=Success; LeftWrist=Wrist; LeftWristTarget=WristTarget; LeftWristSuccess=WristSuccess; LeftHandLabel=Text; }
            else { RightHandArea=Area; RightHandTarget=Target; RightHandSuccess=Success; RightWrist=Wrist; RightWristTarget=WristTarget; RightWristSuccess=WristSuccess; RightHandLabel=Text; }
        };
        Hand(true); Hand(false);
    }
    if (auto* Legacy=WidgetTree->FindWidget(TEXT("BreachProgress"))) WidgetTree->RemoveWidget(Legacy);
    BreachText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("BreachText")));
    if (!BreachText)
    {
        BreachText=Label(TEXT("BreachText"),TEXT("양손 손목을 표시 영역에 넣고 유지하세요."),22,FLinearColor::White);
        BreachText->SetJustification(ETextJustify::Center);
        auto* WidgetSlot=Canvas->AddChildToCanvas(BreachText); WidgetSlot->SetAnchors(FAnchors(.15f,.65f,.85f,.65f)); WidgetSlot->SetOffsets(FMargin(0,0,0,90));
    }
    // Keep persistent brushes pointing at material assets; dynamic instances belong to preview/game instances.
    Visible(AimLayer,true); Visible(BreachLayer,false); Visible(BreachText,false);
}
void UOBTrainingStatusWidget::UpdateAimMarkers(FVector2D Size,FVector2D Viewport,FVector2D TargetUV,float RadiusPixels,float HoldProgress)
{
    Visible(BreachLayer,false); Visible(BreachText,false);
    const bool bValid=Size.X>0 && Size.Y>0 && Viewport.X>0 && Viewport.Y>0 && FMath::IsFinite(RadiusPixels) && RadiusPixels>0;
    Visible(AimLayer,bValid); if (!bValid) return;
    const FVector2D Center=Size*TargetUV;
    const double PixelScale=Size.X/Viewport.X, Radius=RadiusPixels*PixelScale;
    const FVector2D TargetSize(Radius/.45,Radius/.45);
    const float Value=FMath::IsFinite(HoldProgress) ? FMath::Clamp(HoldProgress,0.f,1.f) : 0.f;
    const bool Complete=Value>=1.f;
    Visible(AimOutline,false); Visible(AimProgressOutline,false);
    Place(AimTarget,Center,TargetSize,FVector2D(.5,.5)); Place(AimProgress,Center,TargetSize,FVector2D(.5,.5));
    auto* Texture=Complete ? LoadedAimSuccess.Get() : LoadedAimIdle.Get();
    if(!Texture) Texture=Complete ? AimSuccessTexture.LoadSynchronous() : AimIdleTexture.LoadSynchronous();
    if (AimTarget && AimTarget->GetBrush().GetResourceObject()!=Texture) AimTarget->SetBrushFromTexture(Texture,false);
    if (auto* Gauge=Cast<UOBRadialProgressBar>(AimProgress)) Gauge->SetRadialPercent(Value);
    Visible(AimProgress,!Complete && Value>0.f);
}
void UOBTrainingStatusWidget::UpdateBreachMarkers(FVector2D Size,const FOBBreachGuideState& State,const FText& Message)
{
    if (bUsingC4Brushes)
    {
        if (LeftHandTarget) LeftHandTarget->SetBrush(TrainingTargetBrush);
        if (LeftHandSuccess) { LeftHandSuccess->SetBrush(TrainingSuccessBrush);LeftHandSuccess->SetColorAndOpacity(SuccessColor); }
        bUsingC4Brushes=false;
    }
    Visible(LeftHandArea,true);Visible(RightHandArea,true);Visible(LeftHandLabel,true);Visible(RightHandLabel,true);
    if (LeftHandLabel) LeftHandLabel->SetText(FText::FromString(TEXT("왼손 손목")));
    if (LeftHandLabel) LeftHandLabel->SetColorAndOpacity(FSlateColor(LeftHandColor));
    if (RightHandLabel) RightHandLabel->SetText(FText::FromString(TEXT("오른손 손목")));
    if (LeftHandTarget) LeftHandTarget->SetColorAndOpacity(LeftHandColor);
    Visible(AimLayer,false);
    const FBox2D Camera=FOBBreachGuideLayout::CameraRect(Size,State.CameraSize);
    const bool bValid=Camera.GetSize().X>0 && Camera.GetSize().Y>0;
    Visible(BreachLayer,bValid); Visible(BreachText,bValid); if (!bValid) return;
    Place(CameraFrame,Camera.Min,Camera.GetSize());
    Place(CameraLabel,Camera.Min+FVector2D(16,16),FVector2D::ZeroVector,FVector2D::ZeroVector,false);
    const bool bActive=State.bFresh && State.bBreachMode && State.bPlayerTracked;
    auto Hand=[&](UOverlay* Area,UImage* Target,UImage* Success,UOverlay* Wrist,UImage* WristTarget,UImage* WristSuccess,UTextBlock* Label,
        FVector2D Min,FVector2D Max,FVector2D WristUV,float WristVisibility,bool bInside) {
        const FBox2D Box(FOBBreachGuideLayout::MapPoint(Min,Camera),FOBBreachGuideLayout::MapPoint(Max,Camera));
        Place(Area,Box.Min,Box.GetSize()); if (Area) Area->SetRenderOpacity(bActive ? 1.f : .35f);
        Visible(Target,!(bActive && bInside)); Visible(Success,bActive && bInside);
        Place(Label,Box.Min-FVector2D(0,30),FVector2D::ZeroVector,FVector2D::ZeroVector,false);
        const bool bTracked=bActive && WristVisibility>0 && WristUV.X>=0 && WristUV.X<=1 && WristUV.Y>=0 && WristUV.Y<=1;
        Visible(Wrist,bTracked);
        if (bTracked) Place(Wrist,FOBBreachGuideLayout::MapPoint(WristUV,Camera),FVector2D::ZeroVector,FVector2D(.5,.5),false);
        Visible(WristTarget,!bInside); Visible(WristSuccess,bInside);
    };
    Hand(LeftHandArea,LeftHandTarget,LeftHandSuccess,LeftWrist,LeftWristTarget,LeftWristSuccess,LeftHandLabel,State.LeftMin,State.LeftMax,State.LeftWrist,State.LeftVisibility,State.bLeftInside);
    Hand(RightHandArea,RightHandTarget,RightHandSuccess,RightWrist,RightWristTarget,RightWristSuccess,RightHandLabel,State.RightMin,State.RightMax,State.RightWrist,State.RightVisibility,State.bRightInside);
    if (BreachText) BreachText->SetText(Message);
}
void UOBTrainingStatusWidget::UpdateC4Markers(FVector2D Size,const FOBBreachGuideState& State,const FOBC4PlacementTracker& Placement,const FText& Message)
{
    if (!bUsingC4Brushes)
    {
        if (LeftHandTarget) { TrainingTargetBrush=LeftHandTarget->GetBrush();LeftHandTarget->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent,0.f,FLinearColor(1.f,.8f,.05f),GuideLineWidth)); }
        if (LeftHandSuccess) { TrainingSuccessBrush=LeftHandSuccess->GetBrush();LeftHandSuccess->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent,0.f,SuccessColor,GuideLineWidth));LeftHandSuccess->SetColorAndOpacity(FLinearColor::White); }
        bUsingC4Brushes=true;
    }
    Visible(AimLayer,false);Visible(BreachLayer,true);Visible(BreachText,true);
    const FBox2D Camera=FOBBreachGuideLayout::CameraRect(Size,State.CameraSize);
    Place(CameraFrame,Camera.Min,Camera.GetSize());
    Place(CameraLabel,Camera.Min+FVector2D(16,16),FVector2D::ZeroVector,FVector2D::ZeroVector,false);
    if (CameraLabel) CameraLabel->SetText(FText::FromString(TEXT("C4 부착")));
    Visible(LeftHandArea,true);Visible(RightHandArea,false);Visible(RightHandLabel,false);
    Visible(LeftWrist,false);Visible(RightWrist,false);Visible(LeftHandLabel,true);
    const FVector2D Min=FOBBreachGuideLayout::MapPoint(Placement.TargetMin,Camera),Max=FOBBreachGuideLayout::MapPoint(Placement.TargetMax,Camera);
    Place(LeftHandArea,Min,Max-Min);if (LeftHandArea) LeftHandArea->SetRenderOpacity(Placement.bTracked ? 1.f : .5f);
    Place(LeftHandLabel,Min-FVector2D(0,30),FVector2D::ZeroVector,FVector2D::ZeroVector,false);
    if (LeftHandLabel) { LeftHandLabel->SetText(FText::FromString(TEXT("C4 부착 위치")));LeftHandLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White)); }
    if (LeftHandTarget) LeftHandTarget->SetColorAndOpacity(FLinearColor::White);
    Visible(LeftHandTarget,Placement.Progress<=0);Visible(LeftHandSuccess,Placement.Progress>0);
    if (BreachText) BreachText->SetText(Message);
}
void UOBTrainingStatusWidget::UpdateDesignerMarkers(FVector2D Size)
{
    if (DesignerPreview==EOBTrainingMarkerPreview::Aim) { UpdateAimMarkers(Size,Size,FVector2D(.5,.5),60,.5); return; }
    FOBBreachGuideState State; State.CameraSize=FVector2D(1280,720);
    State.bFresh=State.bBreachMode=State.bPlayerTracked=true;
    State.LeftWrist=(State.LeftMin+State.LeftMax)*.5; State.RightWrist=(State.RightMin+State.RightMax)*.5;
    State.LeftVisibility=State.RightVisibility=1; State.bLeftInside=true; State.bRightInside=false; State.Progress=.5;
    UpdateBreachMarkers(Size,State,FText::FromString(TEXT("양손 손목을 표시 영역에 넣고 유지하세요.")));
}
void UOBTrainingStatusWidget::NativeTick(const FGeometry& Geometry,float Seconds)
{
    Super::NativeTick(Geometry,Seconds);
    const FVector2D Size=Geometry.GetLocalSize();
    if (IsDesignTime()) { UpdateDesignerMarkers(Size); return; }
    const auto* Player=GetOwningPlayer() ? Cast<AOBPlayerCharacter>(GetOwningPlayer()->GetPawn()) : nullptr;
    const auto* Stage=Player ? Player->GetStageFlowComponent() : nullptr;
    if (!Director && Stage && Stage->IsWaitingForBreach() && !Stage->HasRequestedBreach())
    {
        const auto* Motion=GetGameInstance() ? GetGameInstance()->GetSubsystem<UOBMediaPipeOSCSubsystem>() : nullptr;
        const auto State=Motion ? Motion->GetBreachGuideState() : FOBBreachGuideState();
        const auto& Placement=Stage->GetC4Placement();
        FString Message=TEXT("왼손 또는 오른손을 보여주세요.");
        if (Placement.bTracked) Message=FString::Printf(TEXT("%s의 C4를 중앙 부착 위치로 가져다 대세요."),Placement.Hand==1 ? TEXT("왼손") : TEXT("오른손"));
        UpdateC4Markers(Size,State,Placement,FText::FromString(Message));
        return;
    }
    if (!Director || !GetOwningPlayer() || Director->bPrototypePaused || Director->GetTrainingResult().bFinished)
    { Visible(AimLayer,false); Visible(BreachLayer,false); Visible(BreachText,false); return; }
    if (HasAimCompletionFeedback() || Director->CurrentStep==EOBTrainingStep::AimHold)
    {
        int32 W=0,H=0; GetOwningPlayer()->GetViewportSize(W,H);
        const bool Completed=HasAimCompletionFeedback();
        UpdateAimMarkers(Size,FVector2D(W,H),Completed ? CompletedAimUV : Director->GetAimTargetPosition(),Director->AimRadiusPixels,Completed ? 1.f : Director->GetAimHoldProgress()); return;
    }
    if (Director->CurrentStep!=EOBTrainingStep::Breach)
    { Visible(AimLayer,false); Visible(BreachLayer,false); Visible(BreachText,false); return; }
    const auto* Motion=GetGameInstance() ? GetGameInstance()->GetSubsystem<UOBMediaPipeOSCSubsystem>() : nullptr;
    const auto State=Motion ? Motion->GetBreachGuideState() : FOBBreachGuideState();
    if (CameraLabel) CameraLabel->SetText(FText::FromString(TEXT("카메라 기준 브리칭 안내")));
    FString Message;
    if (Director->HasAcceptedBreach()) Message=TEXT("브리칭 진행 중…");
    else if (!State.bFresh) Message=TEXT("카메라 연결을 기다리고 있습니다.");
    else if (!State.bBreachMode) Message=TEXT("카메라 창에서 브리칭 인식 모드(1)를 선택해주세요.");
    else if (!State.bPlayerTracked) Message=TEXT("카메라에 상체와 양손이 보이도록 서주세요.");
    else if (Director->NeedsBreachRelease()) Message=TEXT("손을 영역에서 뺐다가 다시 넣어주세요.");
    else if (State.LeftVisibility<=0 || State.RightVisibility<=0) Message=TEXT("양손 손목이 카메라에 보이도록 해주세요.");
    else Message=TEXT("양손 손목을 표시 영역에 넣고 유지하세요.");
    UpdateBreachMarkers(Size,State,FText::FromString(Message));
}
