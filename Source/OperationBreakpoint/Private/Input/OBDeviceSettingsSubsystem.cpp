#include "Input/OBDeviceSettingsSubsystem.h"
#include "Input/OBOpenFireSubsystem.h"
#include "Input/OBOpenFireTypes.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "Core/OBPlayerController.h"
#include "Core/OBGameInstance.h"
#include "Core/OBWeaponFeedback.h"
#include "Character/OBPlayerCharacter.h"
#include "Stage/OBStageFlowComponent.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "MediaPlayer.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "Blueprint/UserWidget.h"
#include "UObject/UObjectIterator.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"

namespace
{
const FName GunFeedbackTag(TEXT("OBDeviceSettingsFeedback"));
FText Txt(const FString& S) { return FText::FromString(S); }
FString LiveFile(const TCHAR* Name) { return UOBDeviceSettingsSubsystem::RuntimeDir()/FString(Name); }
bool Fresh(const FString& Path,double Seconds=2.)
{
    const auto Time=IFileManager::Get().GetTimeStamp(*Path);
    return Time!=FDateTime::MinValue() && (FDateTime::UtcNow()-Time).GetTotalSeconds()<Seconds;
}
bool GunBridgeIsActive()
{
    const FString Path=LiveFile(TEXT("gun.ini"));
    if (!Fresh(Path)) return false;
    FConfigFile Live; Live.Read(Path);
    int32 Running=0;
    return Live.GetInt(TEXT("Live"),TEXT("bridge_running"),Running) && Running!=0;
}
FString ConfigText(const FString& Section,const FString& Key,const FString& Fallback)
{
    FConfigFile Config; Config.Read(UOBDeviceSettingsSubsystem::SettingsPath());
    FString Value;
    return Config.GetString(*Section,*Key,Value) ? Value : Fallback;
}

class FOBDeviceSettingsKeys : public IInputProcessor
{
    TWeakObjectPtr<UOBDeviceSettingsSubsystem> Owner;
public:
    explicit FOBDeviceSettingsKeys(UOBDeviceSettingsSubsystem* In) : Owner(In) {}
    virtual void Tick(const float,FSlateApplication&,TSharedRef<ICursor>) override {}
    bool OwnsInput() const
    {
        if (!Owner.IsValid() || !Owner->GetWorld() || !Owner->GetWorld()->IsGameWorld()) return false;
        auto* GI=Owner->GetGameInstance(); auto* View=GI ? GI->GetGameViewportClient() : nullptr;
        return View && View->Viewport && View->Viewport->IsForegroundWindow();
    }
    virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& E) override
    {
        if (!OwnsInput()) return false;
        const auto Key=E.GetKey();
        if (Key==EKeys::F6 || Key==EKeys::F7)
        { if (!E.IsRepeat()) Owner->Toggle(Key==EKeys::F6?1:2); return true; }
        // Operator shortcuts also work when a Slate panel owns focus.
        if (Key==EKeys::F9 || Key==EKeys::F10 || Key==EKeys::F11)
        {
            if (!E.IsRepeat())
                if (auto* PC=Cast<AOBPlayerController>(Owner->GetGameInstance()->GetFirstLocalPlayerController()))
                    PC->HandleOperatorShortcut(Key, IE_Pressed);
            return true;
        }
        if (!Owner->IsOpen()) return false;
        if (Key==EKeys::F5)
        {
            if (E.IsRepeat()) return true;
            Owner->Close();
            if (auto* PC=Cast<AOBPlayerController>(Owner->GetGameInstance()->GetFirstLocalPlayerController())) PC->ReturnToMainMenu();
            return true;
        }
        if (Key==EKeys::Escape) { Owner->Close(); return true; }
        if (Owner->GetMode()==1 && Key==EKeys::R) { if (!E.IsRepeat()) Owner->Reload(); return true; }
        // Let the modal Slate controls own keyboard navigation and text input.
        return false;
    }
    virtual bool HandleKeyUpEvent(FSlateApplication&,const FKeyEvent& E) override
    { return OwnsInput() && (E.GetKey()==EKeys::F6 || E.GetKey()==EKeys::F7 || E.GetKey()==EKeys::F9 || E.GetKey()==EKeys::F10 || E.GetKey()==EKeys::F11); }
};

// The existing aim-test level is a fixed flat target board. Render that board
// directly in Slate so its grid and hit marks work identically in Shipping,
// without traveling away from (or reloading) the current training world.
class SOBDeviceTarget : public SCompoundWidget
{
    TWeakObjectPtr<UOBDeviceSettingsSubsystem> Owner;
    mutable FSlateBrush CameraBrush;
public:
    SLATE_BEGIN_ARGS(SOBDeviceTarget) {} SLATE_ARGUMENT(UOBDeviceSettingsSubsystem*,Owner) SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Owner=Args._Owner; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(960,540); }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& E) override
    {
        if (Owner.IsValid() && Owner->GetMode()==1 && !UOBOpenFireSubsystem::IsRequested())
        {
            if (E.GetEffectingButton()==EKeys::LeftMouseButton) Owner->Shoot();
            if (E.GetEffectingButton()==EKeys::RightMouseButton) Owner->Reload();
        }
        return FReply::Handled();
    }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool) const override
    {
        if (!Owner.IsValid()) return Layer;
        const FVector2D Size=G.GetLocalSize();
        auto Box=[&](FVector2D P,FVector2D S,FLinearColor C) {
            FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(S,FSlateLayoutTransform(P)),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,C);
        };
        auto Line=[&](FVector2D A,FVector2D B,FLinearColor C,float Width=1) {
            TArray<FVector2D> Points{A,B};
            FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,C,true,Width);
        };
        Box(FVector2D::ZeroVector,Size,FLinearColor(.018f,.035f,.06f));
        if (Owner->GetMode()==2)
        {
            const float Left=FMath::Min(380.f,(float)Size.X);
            const FVector2D Available(Size.X-Left,Size.Y);
            const FVector2D ImageSize(960,540);
            const float Scale=FMath::Min(Available.X/ImageSize.X,Available.Y/ImageSize.Y);
            const FVector2D Extent=ImageSize*Scale;
            const FVector2D Origin=FVector2D(Left,0)+(Available-Extent)*.5;
            if (auto* Texture=Owner->GetPreview())
            {
                CameraBrush.SetResourceObject(Texture); CameraBrush.ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY());
                FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(Extent,FSlateLayoutTransform(Origin)),&CameraBrush);
            }
            if (Owner->IsC4Test())
            {
                ++Layer;
                const auto& Test=Owner->GetC4Test();
                const FLinearColor Color=Test.bAttached ? FLinearColor(.2f,1,.4f) : FLinearColor(1,.8f,.1f);
                const FVector2D A=Origin+Test.TargetMin*Extent,B=Origin+Test.TargetMax*Extent;
                Line(A,FVector2D(B.X,A.Y),Color,3);Line(FVector2D(B.X,A.Y),B,Color,3);
                Line(B,FVector2D(A.X,B.Y),Color,3);Line(FVector2D(A.X,B.Y),A,Color,3);
                auto Text=[&](FVector2D At,const FString& Value,int32 FontSize,FLinearColor Tint) {
                    FSlateDrawElement::MakeText(Out,Layer,G.ToPaintGeometry(FVector2D(300,35),FSlateLayoutTransform(At)),Txt(Value),FCoreStyle::GetDefaultFontStyle("Bold",FontSize),ESlateDrawEffect::None,Tint);
                };
                Text(A-FVector2D(0,30),TEXT("C4 부착 위치"),17,Color);
                if (Test.bTracked || Test.bAttached)
                {
                    const FVector2D Center=Origin+(Test.bAttached ? Test.GetTargetCenter() : Test.HandUV)*Extent;
                    Box(Center-FVector2D(27,18),FVector2D(54,36),FLinearColor(.12f,.22f,.13f));
                    ++Layer;Text(Center-FVector2D(16,13),TEXT("C4"),17,FLinearColor::White);
                }
                Text(Origin+FVector2D(16,Extent.Y-50),Test.bAttached ? TEXT("C4 부착 성공") : TEXT("한 손을 올려 중앙 영역으로 가져다 대세요"),17,Color);
            }
            return Layer+1;
        }
        const FLinearColor Grid(.11f,.18f,.25f),Accent(.2f,.9f,.65f);
        for (int32 I=1;I<24;++I) Line(FVector2D(Size.X*I/24,0),FVector2D(Size.X*I/24,Size.Y),Grid);
        for (int32 I=1;I<14;++I) Line(FVector2D(0,Size.Y*I/14),FVector2D(Size.X,Size.Y*I/14),Grid);
        Line(FVector2D(Size.X*.5,0),FVector2D(Size.X*.5,Size.Y),FLinearColor(.3f,.4f,.5f));
        Line(FVector2D(0,Size.Y*.5),FVector2D(Size.X,Size.Y*.5),FLinearColor(.3f,.4f,.5f));
        for (const FVector2D& UV:Owner->GetMarks())
        {
            Box(UV*Size-FVector2D(4,4),FVector2D(8,8),FLinearColor(.3f,.22f,.12f));
            Box(UV*Size-FVector2D(2,2),FVector2D(4,4),FLinearColor(.003f,.003f,.003f));
        }
        const float Radius=Owner->GetTargetRadius(Size);
        const FSlateRoundedBoxBrush TargetBrush(FLinearColor::White,Radius);
        ++Layer;
        FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(Radius*2),FSlateLayoutTransform(Owner->GetTargetCenter(Size)-FVector2D(Radius))),
            &TargetBrush,ESlateDrawEffect::None,FLinearColor(.95f,.035f,.045f));
        const float X=Size.X/Size.Y>1.4?.3:.2;
        const FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle("Regular",11);
        for (int32 I=0;I<4;++I)
        {
            const FVector2D P(Size.X*((I%2)==0?X:1-X),I<2?4:Size.Y-4);
            Box(P-FVector2D(5,4),FVector2D(10,8),FLinearColor(1,.35f,.4f));
            FSlateDrawElement::MakeText(Out,Layer,G.ToPaintGeometry(FVector2D(150,20),FSlateLayoutTransform(P+FVector2D(9,I<2?5:-23))),Txt(FString::Printf(TEXT("IR LED %d · %d%%"),I+1,FMath::RoundToInt(100*((I%2)==0?X:1-X)))),Font,ESlateDrawEffect::None,FLinearColor(1,.7f,.7f));
        }
        FVector2D UV;
        if (Owner->GetAim(UV))
        {
            const FVector2D P=UV*Size;
            ++Layer;
            Line(P-FVector2D(13,0),P+FVector2D(13,0),Accent,2);
            Line(P-FVector2D(0,13),P+FVector2D(0,13),Accent,2);
        }
        return Layer+1;
    }
};

TSharedRef<SWidget> Number(UOBDeviceSettingsSubsystem* Owner,const FString& Label,const FString& Section,const FString& Key,double Default,double Low,double High,double Step,double DisplayScale=1,const FString& Help=FString(),bool bLive=false)
{
    return SNew(SHorizontalBox)
        .ToolTipText(Txt(Help))
        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).AutoWrapText(true).Text(Txt(Label))]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,2)[SNew(SBox).WidthOverride(100)[
            SNew(SSpinBox<double>).MinValue(Low*DisplayScale).MaxValue(High*DisplayScale).Delta(Step*DisplayScale)
            .BroadcastValueChangesPerKey(bLive)
            .Value_Lambda([Owner,Section,Key,Default,DisplayScale]{ return Owner->ReadNumber(Section==TEXT("__gun__")?Owner->GunSection():Section,Key,Default)*DisplayScale; })
            .OnValueChanged_Lambda([Owner,Section,Key,DisplayScale,bLive](double Value){if(bLive) Owner->WriteNumber(Section==TEXT("__gun__")?Owner->GunSection():Section,Key,Value/DisplayScale);})
            .OnValueCommitted_Lambda([Owner,Section,Key,DisplayScale](double Value,ETextCommit::Type){Owner->WriteNumber(Section==TEXT("__gun__")?Owner->GunSection():Section,Key,Value/DisplayScale);})
            .OnEndSliderMovement_Lambda([Owner,Section,Key,DisplayScale](double Value){Owner->WriteNumber(Section==TEXT("__gun__")?Owner->GunSection():Section,Key,Value/DisplayScale);})]];
}
TSharedRef<SWidget> Button(const FString& Label,TFunction<void()> Action)
{
    return SNew(SButton).ContentPadding(FMargin(10,7)).Text(Txt(Label)).OnClicked_Lambda([Action]{Action();return FReply::Handled();});
}
TSharedRef<SWidget> DeviceHint(const FString& Text)
{
    return SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(.72f,.82f,.91f)).Text(Txt(Text));
}
class SOBDeviceModal : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SOBDeviceModal) {} SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
    void Construct(const FArguments& Args) { ChildSlot[Args._Content.Widget]; }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent&) override { return FReply::Handled(); }
    virtual FReply OnKeyUp(const FGeometry&,const FKeyEvent&) override { return FReply::Handled(); }
};
TSharedRef<SWidget> MakeScreen(UOBDeviceSettingsSubsystem* Owner)
{
    const bool Gun=Owner->GetMode()==1;
    auto Panel=SNew(SVerticalBox);
    auto Advanced=SNew(SVerticalBox);
    Panel->AddSlot().AutoHeight().Padding(0,0,0,12)[
        SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.065f,.11f,.16f)).Padding(10)[
            SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))
            .Text_Lambda([Owner]{return Txt(Owner->Status());})]];
    if (Gun)
    {
        Panel->AddSlot().AutoHeight().Padding(0,0,0,8)[DeviceHint(TEXT("빨간 표적을 조준하고 방아쇠를 당겨 보세요. 맞히면 표적이 이동합니다."))];
        Panel->AddSlot().AutoHeight()[SNew(STextBlock).Text_Lambda([Owner]{return Txt(FString::Printf(TEXT("남은 탄약  %d / %d"),Owner->GetAmmo(),UOBDeviceSettingsSubsystem::MagazineCapacity));})];
        Panel->AddSlot().AutoHeight().Padding(0,8)[Button(TEXT("재장전 (R)"),[Owner]{Owner->Reload();})];
        const FString Section=TEXT("__gun__");
        Advanced->AddSlot().AutoHeight()[Number(Owner,TEXT("조준 좌우 이동 (%)"),Section,TEXT("x_shift"),0,-.5,.5,.001,100,TEXT("변경 즉시 적용됩니다. 화면 가로 길이에 대한 이동량이며 +는 오른쪽, -는 왼쪽입니다."),true)];
        Advanced->AddSlot().AutoHeight()[Number(Owner,TEXT("조준 상하 미세 조정 (mm)"),Section,TEXT("trim_mm"),0,-500,500,2,1,TEXT("기본 상하 이동에 더해집니다. +는 위, -는 아래입니다. 건의 조정 다이얼과 같은 값입니다."))];
        Advanced->AddSlot().AutoHeight()[Number(Owner,TEXT("기본 상하 이동 (mm)"),Section,TEXT("base_up_mm"),50,-200,200,1,1,TEXT("기준이 되는 상하 이동량입니다. 여기에 미세 조정값을 더해 조준 위치를 옮깁니다."))];
        Advanced->AddSlot().AutoHeight()[Number(Owner,TEXT("화면의 실제 세로 길이 (mm)"),Section,TEXT("height_mm"),700,100,3000,1,1,TEXT("TV·모니터에서 영상이 표시되는 부분의 실제 세로 길이입니다. 해상도의 픽셀 수가 아닙니다."))];
        Advanced->AddSlot().AutoHeight().Padding(0,6)[DeviceHint(TEXT("조준점이 계속 같은 방향으로 어긋날 때 조정하세요. 화면 가장자리의 표식은 IR LED 설치 위치입니다."))];
    }
    else
    {
        Panel->AddSlot().AutoHeight().Padding(0,0,0,8)[DeviceHint(TEXT("영상의 플레이어 영역 안에서 똑바로 서 주세요. 높이 측정이 끝나면 앉았다 일어나 보세요."))];
        Panel->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("서 있는 높이 다시 측정"),[Owner]{Owner->Command(TEXT("r"));})];
        Panel->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("C4 부착 테스트"),[Owner]{Owner->StartC4Test();})];
        Panel->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("훈련장 양손 테스트"),[Owner]{Owner->Command(TEXT("1"));})];
        Advanced->AddSlot().AutoHeight().Padding(0,6)[Button(TEXT("플레이할 사람 다시 찾기"),[Owner]{Owner->Command(TEXT("p"));})];
        Advanced->AddSlot().AutoHeight()[Number(Owner,TEXT("몸 인식 엄격도 (%)"),TEXT("Camera"),TEXT("minimum_visibility"),.6,.1,1,.05,100,TEXT("자세 판단에 사용할 머리·어깨 등의 인식 점수를 정합니다. 점수가 이 값보다 낮으면 해당 위치를 사용하지 않습니다."))];
        Advanced->AddSlot().AutoHeight().Padding(0,3,0,8)[DeviceHint(TEXT("높을수록 가려진 부위를 제외합니다. 인식이 자주 끊기면 낮춰 보세요."))];
        const FString EnterHelp=TEXT("선 자세의 어깨~골반 길이가 100%입니다. 어깨와 머리가 각각 설정한 양 이상 내려가면 앉기로 판단합니다.");
        const FString ExitHelp=TEXT("선 자세에서 내려간 양이 설정값 이하로 줄어들면 서기로 돌아갑니다. 머리가 보여야 하며, 어깨도 보이는 경우 두 기준을 모두 확인합니다.");
        Advanced->AddSlot().AutoHeight()[Number(Owner,TEXT("앉기 · 어깨 내려감 (%)"),TEXT("Camera"),TEXT("crouch_shoulder_enter_ratio"),.45,.1,1,.05,100,EnterHelp)];
        Advanced->AddSlot().AutoHeight()[Number(Owner,TEXT("앉기 · 머리 내려감 (%)"),TEXT("Camera"),TEXT("crouch_head_enter_ratio"),.4,.1,1,.05,100,EnterHelp)];
        Advanced->AddSlot().AutoHeight()[Number(Owner,TEXT("서기 · 어깨 내려감 (%)"),TEXT("Camera"),TEXT("crouch_shoulder_exit_ratio"),.25,.05,.9,.05,100,ExitHelp)];
        Advanced->AddSlot().AutoHeight()[Number(Owner,TEXT("서기 · 머리 내려감 (%)"),TEXT("Camera"),TEXT("crouch_head_exit_ratio"),.2,.05,.9,.05,100,ExitHelp)];
        Advanced->AddSlot().AutoHeight().Padding(0,8)[DeviceHint(TEXT("앉기 수치를 낮추면 조금만 앉아도 인식합니다. 서기 수치를 높이면 덜 일어나도 복귀합니다."))];
    }
    Advanced->AddSlot().AutoHeight().Padding(0,6)[DeviceHint(TEXT("숫자를 바꾸면 Enter 또는 다른 곳을 클릭할 때 저장됩니다."))];
    Panel->AddSlot().AutoHeight().Padding(0,10,0,0)[Button(Gun?TEXT("건컨트롤러 다시 연결"):TEXT("카메라 다시 연결"),[Owner]{Owner->Reconnect();})];
    Panel->AddSlot().AutoHeight().Padding(0,12,0,0)[
        SNew(SExpandableArea).AreaTitle(Txt(Gun?TEXT("조준 세부 설정"):TEXT("자세 인식 세부 설정")))
        .InitiallyCollapsed(true).AllowAnimatedTransition(false).Padding(FMargin(0,4)).BodyContent()[Advanced]];
    return SNew(SOBDeviceModal)[SNew(SOverlay)
        +SOverlay::Slot()[SNew(SOBDeviceTarget).Owner(Owner)]
        +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(16,34)[
            SNew(SBox).WidthOverride(Gun?315:345).MaxDesiredHeight(650)[
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025f,.04f,.065f,.96f)).Padding(14)[
                    SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[
                        SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",19))
                        .Text(Txt(Gun?TEXT("F6  건컨트롤러 확인"):TEXT("F7  카메라 확인")))]
                    +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Panel]]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[
                        Button(Gun?TEXT("돌아가기 (F6 / Esc)"):TEXT("돌아가기 (F7 / Esc)"),[Owner]{Owner->Close();})]]]]];
}
}

FString UOBDeviceSettingsSubsystem::SettingsPath()
{
    const FString Override=FPlatformMisc::GetEnvironmentVariable(TEXT("OB_DEVICE_SETTINGS"));
    if (!Override.IsEmpty()) return Override;
    return FString(FPlatformProcess::UserSettingsDir())/TEXT("OperationBreakpoint/DeviceCalibration.ini");
}
FString UOBDeviceSettingsSubsystem::RuntimeDir()
{
    const FString Folder=FPaths::GetPath(SettingsPath())/TEXT("DeviceRuntime");
    IFileManager::Get().MakeDirectory(*Folder,true); return Folder;
}
void UOBDeviceSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UOBOpenFireSubsystem>();
    Collection.InitializeDependency<UOBMediaPipeOSCSubsystem>();
    if (FSlateApplication::IsInitialized())
    {
        InputProcessor=MakeShared<FOBDeviceSettingsKeys>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor,0);
    }
    if (auto* Gun=GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>()) Gun->OnInputReceived.AddUObject(this,&ThisClass::HandleGun);
    if (FParse::Param(FCommandLine::Get(), TEXT("OBMediaPipe")))
    {
        FString ProfilePath;
        if (UOBGameInstance::ReadLearnerProfileArgument(FCommandLine::Get(), ProfilePath))
            CameraSessionDir = FPaths::GetPath(ProfilePath);
        StartHelper(2);
    }
}
void UOBDeviceSettingsSubsystem::Deinitialize()
{
    Close(); StopHelpers();
    if (auto* Gun=GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>()) Gun->OnInputReceived.RemoveAll(this);
    if (FSlateApplication::IsInitialized() && InputProcessor) FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
    InputProcessor.Reset(); Super::Deinitialize();
}
bool UOBDeviceSettingsSubsystem::IsTickable() const { return !IsTemplate() && Mode!=0; }
void UOBDeviceSettingsSubsystem::Toggle(int32 Device)
{
    if (Device!=1 && Device!=2) return;
    if (Mode==Device) { Close(); return; }
    auto* PC=GetGameInstance()->GetFirstLocalPlayerController();
    auto* View=GetGameInstance()->GetGameViewportClient();
    if (!PC || !View) return;
    if (!Mode)
    {
        OpenedRealTime=GetWorld()->GetRealTimeSeconds();
        PausedController=PC; bWasPaused=UGameplayStatics::IsGamePaused(this); bWasCursor=PC->bShowMouseCursor;
        PreviousFocus=FSlateApplication::Get().GetUserFocusedWidget(0);
        if (auto* OB=Cast<AOBPlayerController>(PC)) OB->CancelOpenFireInput();
        PC->FlushPressedKeys(); PC->SetIgnoreMoveInput(true); PC->SetIgnoreLookInput(true);
        UGameplayStatics::SetGamePaused(this,true);
    }
    if (Screen) View->RemoveViewportWidgetContent(Screen.ToSharedRef());
    if (bC4Test) Command(TEXT("1"));
    if (Mode==2) SetCameraPreviewRequested(false);
    Mode=Device; bTriggerNeedsRelease=true; HelperError.Empty();
    if (Device==1) SpawnTarget();
    SuspendPresentation();
    if (Device==1) PrepareGunAudio();
    if (Device==1) if (auto* Gun=GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>()) Gun->EnableRuntimeInput();
    StartHelper(Device);
    if (Device==2) SetCameraPreviewRequested(true);
    PC->bShowMouseCursor=true;
    Screen=MakeScreen(this); Screen->ForceVolatile(true); View->AddViewportWidgetContent(Screen.ToSharedRef(),10000);
    FSlateApplication::Get().SetUserFocus(0,Screen);
    UE_LOG(LogTemp,Display,TEXT("DEVICE_SETTINGS_OPEN mode=%d paused=%d path=%s"),Mode,bWasPaused,*SettingsPath());
}
void UOBDeviceSettingsSubsystem::Close()
{
    if (!Mode) return;
    if (bC4Test) Command(TEXT("1"));
    if (Mode==2) SetCameraPreviewRequested(false);
    if (auto* View=GetGameInstance()->GetGameViewportClient(); View && Screen) View->RemoveViewportWidgetContent(Screen.ToSharedRef());
    Screen.Reset(); Mode=0; bTriggerNeedsRelease=true;
    if (auto* PC=PausedController.Get())
    {
        if (auto* Player=Cast<AOBPlayerCharacter>(PC->GetPawn()))
            Player->GetStageFlowComponent()->ExcludeDeviceSettingsTime(PC->GetWorld()->GetRealTimeSeconds()-OpenedRealTime);
        PC->FlushPressedKeys(); PC->SetIgnoreMoveInput(false); PC->SetIgnoreLookInput(false); PC->bShowMouseCursor=bWasCursor;
        if (auto* OB=Cast<AOBPlayerController>(PC)) OB->CancelOpenFireInput();
        if (!bWasPaused) UGameplayStatics::SetGamePaused(PC,false);
        if (APawn* Pawn=PC->GetPawn())
            if (auto* Pose=Pawn->FindComponentByClass<UOBMediaPipeInputComponent>()) Pose->RefreshPoseState();
    }
    if (FSlateApplication::IsInitialized())
    {
        if (auto Focus=PreviousFocus.Pin()) FSlateApplication::Get().SetUserFocus(0,Focus);
        else FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
    for (const auto& Entry:SuspendedMedia) if (auto* Media=Entry.Key.Get(); Media && Media->IsPaused()) Media->SetRate(Entry.Value);
    for (const auto& Weak:SuspendedAudio) if (auto* Audio=Weak.Get(); Audio && Audio->GetPlayState()==EAudioComponentPlayState::Paused) Audio->SetPaused(false);
    for (const auto& Weak:GunFeedbackAudio) if (auto* Audio=Weak.Get()) Audio->Stop();
    GunFeedbackAudio.Reset();
    SuspendedMedia.Reset(); SuspendedAudio.Reset();
    PausedController.Reset(); Preview=nullptr;
    PreviewFrameId.Empty();
    PreviewStatsStart=0; PreviewUploads=0; PreviewUploadSeconds=0; LastPreviewPoll=0;
    UE_LOG(LogTemp,Display,TEXT("DEVICE_SETTINGS_CLOSED restored_pause=%d"),bWasPaused);
}
bool UOBDeviceSettingsSubsystem::GetAim(FVector2D& UV) const
{
    if (auto* Gun=GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>(); Gun && Gun->GetAimPosition(UV)) return true;
    if (UOBOpenFireSubsystem::IsRequested()) return false;
    if (auto* PC=GetGameInstance()->GetFirstLocalPlayerController())
    { int32 W,H; PC->GetViewportSize(W,H); float X,Y; if (W>1 && H>1 && PC->GetMousePosition(X,Y)) { UV=FVector2D(X/(W-1),Y/(H-1)); return true; } }
    return false;
}
void UOBDeviceSettingsSubsystem::Shoot()
{
    FVector2D UV;
    if (Mode!=1 || !GetAim(UV) || UV.X<0 || UV.X>1 || UV.Y<0 || UV.Y>1) return;
    if (Ammo<=0) { PlayGunFeedback(false,true); return; }
    --Ammo;
    if (Marks.Num()<MaxBulletMarks) Marks.Add(UV); else { Marks[NextMark]=UV; NextMark=(NextMark+1)%MaxBulletMarks; }
    FVector2D Size=Screen.IsValid() ? Screen->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
    if (Size.X<=1 || Size.Y<=1)
        if (auto* View=GetGameInstance()->GetGameViewportClient(); View && View->Viewport) Size=FVector2D(View->Viewport->GetSizeXY());
    if (Size.X>1 && Size.Y>1 && FVector2D::DistSquared(UV*Size,GetTargetCenter(Size))<=FMath::Square(GetTargetRadius(Size))) SpawnTarget();
    PlayGunFeedback(false);
}
FVector2D UOBDeviceSettingsSubsystem::GetTargetCenter(FVector2D Size) const
{
    const float Radius=GetTargetRadius(Size);
    // Keep the complete circle clear of the settings panel and the LED guides.
    const float Right=FMath::Max(Radius,Size.X-Radius-16.f);
    const float Left=FMath::Min(355.f+Radius,Right);
    const float Top=FMath::Min(48.f+Radius,Size.Y*.5f);
    const float Bottom=FMath::Max(Top,Size.Y-48.f-Radius);
    return FVector2D(FMath::Lerp(Left,Right,TargetPosition.X),FMath::Lerp(Top,Bottom,TargetPosition.Y));
}
void UOBDeviceSettingsSubsystem::SpawnTarget()
{
    const FVector2D Previous=TargetPosition;
    for (int32 I=0;I<16;++I)
    {
        TargetPosition=FVector2D(FMath::FRand(),FMath::FRand());
        if (FVector2D::DistSquared(Previous,TargetPosition)>.09f) return;
    }
    TargetPosition=FVector2D(Previous.X<.5 ? 1 : 0,Previous.Y<.5 ? 1 : 0);
}
void UOBDeviceSettingsSubsystem::Reload()
{
    if (Mode!=1) return;
    PlayGunFeedback(true);
    Ammo=MagazineCapacity;
}
void UOBDeviceSettingsSubsystem::PrepareGunAudio()
{
    // Do disk loading/stream priming when F6 opens, never in the trigger/reload path.
    if (!GunFireSound) GunFireSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Assets/FX/SFX/PlaySound/Shooting.Shooting"));
    if (!GunEmptySound) GunEmptySound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Assets/FX/SFX/PlaySound/Shooting_EmptyMagazine.Shooting_EmptyMagazine"));
    if (!GunReloadSound) GunReloadSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Assets/FX/SFX/PlaySound/Reload.Reload"));
    for (USoundBase* Sound:{GunFireSound.Get(),GunEmptySound.Get(),GunReloadSound.Get()})
        OBWeaponFeedback::Prepare(Sound);
}
void UOBDeviceSettingsSubsystem::PlayGunFeedback(bool bReload,bool bEmpty)
{
    USoundBase* Sound=bReload?GunReloadSound.Get():bEmpty?GunEmptySound.Get():GunFireSound.Get();
    if (auto* Audio=UGameplayStatics::CreateSound2D(this,Sound))
    {
        // UI audio plays while the training world is paused. Mark it before
        // playback so SuspendPresentation only pauses the underlying game.
        Audio->ComponentTags.Add(GunFeedbackTag);
        GunFeedbackAudio.RemoveAll([](const auto& Weak){return !Weak.IsValid();});
        GunFeedbackAudio.Add(Audio);
        const float StartSeconds=bReload?OBWeaponFeedback::ReloadStartTime(Sound):0.f;
        Audio->Play(StartSeconds);
        UE_LOG(LogTemp,Display,TEXT("DEVICE_SETTINGS_AUDIO cue=%s ui=%d world_paused=%d start_seconds=%.3f"),bReload?TEXT("Reload"):bEmpty?TEXT("Empty"):TEXT("Fire"),Audio->bIsUISound,UGameplayStatics::IsGamePaused(this),StartSeconds);
    }
}
void UOBDeviceSettingsSubsystem::HandleGun(const FOBOpenFireEvents& Events)
{
    if (!Mode) return;
    auto* View=GetGameInstance()->GetGameViewportClient();
    if (!View || !View->Viewport || !View->Viewport->IsForegroundWindow()) { bTriggerNeedsRelease=true; return; }
    if (bTriggerNeedsRelease) { bTriggerNeedsRelease=Events.bInteractionHeld; return; }
    if (Mode==1) { if (Events.bTriggerPressed) Shoot(); if (Events.bReloadPressed) Reload(); }
}
void UOBDeviceSettingsSubsystem::SuspendPresentation()
{
    // Media and UI audio have their own clocks, outside world pause.
    for (TObjectIterator<UMediaPlayer> It;It;++It)
    {
        auto* Widget=It->GetTypedOuter<UUserWidget>();
        if (Widget && Widget->GetWorld()==GetWorld() && It->IsPlaying())
        {
            const float Rate=It->GetRate();
            if (It->Pause()) SuspendedMedia.Add(*It,Rate);
        }
    }
    for (TObjectIterator<UAudioComponent> It;It;++It)
        if (It->GetWorld()==GetWorld() && !It->ComponentHasTag(GunFeedbackTag) && It->GetPlayState()==EAudioComponentPlayState::Playing)
        { It->SetPaused(true); SuspendedAudio.AddUnique(*It); }
}
void UOBDeviceSettingsSubsystem::Tick(float)
{
    if (!GetWorld() || PausedController.Get()!=GetGameInstance()->GetFirstLocalPlayerController()) { Close(); return; }
    if (Mode==2 && bC4Test)
        if (auto* Motion=GetGameInstance()->GetSubsystem<UOBMediaPipeOSCSubsystem>())
            C4Test.Update(Motion->GetBreachGuideState(),Motion->GetBreachGuideSampleTime());
    const double Now=FPlatformTime::Seconds();
    if (Screen) Screen->Invalidate(EInvalidateWidgetReason::Paint);
    if (Now-LastPoll>=.1)
    {
        LastPoll=Now;
        if (Screen) Screen->Invalidate(EInvalidateWidgetReason::Layout);
        SuspendPresentation();
    }
    // Camera uploads must not inherit the controls/status 10Hz polling limit.
    // 60Hz polling leaves headroom for the camera's normal 30fps cadence.
    if (Mode==2 && Now-LastPreviewPoll>=1./60.)
    {
        LastPreviewPoll=Now;
        if (!PreviewStatsStart) PreviewStatsStart=Now;
        const FString Path=LiveFile(TEXT("camera.bmp"));
        if (!Fresh(LiveFile(TEXT("camera.ini")))) { Preview=nullptr; return; }
        // File-manager timestamps can have only one-second precision on Windows.
        // Use the producer's session/frame identifier, not mtime, for new frames.
        FConfigFile CameraState; CameraState.Read(LiveFile(TEXT("camera.ini")));
        FString FrameId; CameraState.GetString(TEXT("Live"),TEXT("frame_id"),FrameId);
        if (!FrameId.IsEmpty() && FrameId!=PreviewFrameId)
        {
            const double UploadStarted=FPlatformTime::Seconds();
            TArray<uint8> Bytes;
            if (FFileHelper::LoadFileToArray(Bytes,*Path))
            {
                auto& Images=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
                auto Decoder=Images.CreateImageWrapper(EImageFormat::BMP);
                TArray64<uint8> Pixels;
                if (Decoder && Decoder->SetCompressed(Bytes.GetData(),Bytes.Num())
                    && Decoder->GetWidth()==960 && Decoder->GetHeight()==540 && Decoder->GetRaw(ERGBFormat::BGRA,8,Pixels))
                {
                    if (!Preview)
                    {
                        Preview=UTexture2D::CreateTransient(960,540,PF_B8G8R8A8);
                        Preview->SRGB=true; Preview->UpdateResource();
                    }
                    // Reuse one texture; only the newest frame is uploaded.
                    auto* Data=static_cast<uint8*>(FMemory::Malloc(Pixels.Num()));
                    FMemory::Memcpy(Data,Pixels.GetData(),Pixels.Num());
                    Preview->UpdateTextureRegions(0,1,new FUpdateTextureRegion2D(0,0,0,0,960,540),960*4,4,Data,
                        [](uint8* Source,const FUpdateTextureRegion2D* Region) {FMemory::Free(Source);delete Region;});
                    PreviewFrameId=FrameId;
                    ++PreviewUploads;
                    PreviewUploadSeconds+=FPlatformTime::Seconds()-UploadStarted;
                }
            }
        }
        if (Now-PreviewStatsStart>=5.)
        {
            FConfigFile Live; Live.Read(LiveFile(TEXT("camera.ini")));
            double SourceFPS=0; Live.GetDouble(TEXT("Live"),TEXT("source_fps"),SourceFPS);
            UE_LOG(LogTemp,Display,TEXT("DEVICE_CAMERA_PREVIEW source_fps=%.2f uploaded_fps=%.2f read_decode_ms=%.2f"),
                SourceFPS,PreviewUploads/(Now-PreviewStatsStart),PreviewUploads?1000*PreviewUploadSeconds/PreviewUploads:0.);
            PreviewStatsStart=Now; PreviewUploads=0; PreviewUploadSeconds=0;
        }
    }
}
FString UOBDeviceSettingsSubsystem::Status() const
{
    if (!HelperError.IsEmpty()) return HelperError;
    FProcHandle Process=Mode==1?GunProcess:CameraProcess;
    FConfigFile Live; const FString Path=LiveFile(Mode==1?TEXT("gun.ini"):TEXT("camera.ini"));
    const bool bLive = Fresh(Path);
    if ((Mode==1 ? !GunBridgeIsActive() : !bLive) && Process.IsValid() && !FPlatformProcess::IsProcRunning(Process))
        return Mode==1 ? TEXT("건컨트롤러 연결 끊김\nUSB를 확인한 뒤 다시 연결하세요.")
            : TEXT("카메라 연결 끊김\nUSB 연결이나 카메라를 사용 중인 앱을 확인한 뒤 다시 연결하세요.");
    if (!bLive) return Mode==1 ? TEXT("건컨트롤러 연결 대기\nUSB 연결을 확인해 주세요.")
        : TEXT("카메라 영상 대기\n영상이 들어오면 오른쪽에 표시됩니다.");
    Live.Read(Path);
    int32 A=0,B=0;
    if (Mode==1)
    {
        Live.GetInt(TEXT("Live"),TEXT("connected"),A); Live.GetInt(TEXT("Live"),TEXT("aim_valid"),B);
        if (!A)
        {
            int32 Running=0;
            Live.GetInt(TEXT("Live"),TEXT("bridge_running"),Running);
            return Running ? TEXT("건컨트롤러 다시 연결 중\nUSB 연결을 확인해 주세요.")
                : TEXT("건컨트롤러 연결 대기\nUSB 연결을 확인해 주세요.");
        }
        return B ? TEXT("연결됨 · 조준 입력 정상") : TEXT("연결됨 · 조준점 찾는 중\n화면 중앙을 겨눠 보세요. 조준점이 계속 안 보이면 IR LED를 확인해 주세요.");
    }
    const auto* Motion = GetGameInstance()->GetSubsystem<UOBMediaPipeOSCSubsystem>();
    if (!Motion || !Motion->IsBridgeConnected())
        return TEXT("영상 연결됨 · 자세 입력 대기\n잠시 뒤에도 그대로면 카메라를 다시 연결해 주세요.");
    Live.GetInt(TEXT("Live"),TEXT("player"),A); Live.GetInt(TEXT("Live"),TEXT("calibrated"),B);
    if (bC4Test)
        return FString::Printf(TEXT("C4 테스트 · %s\n선택된 손: %s"),C4Test.bAttached?TEXT("부착 성공"):C4Test.bTracked?TEXT("손 추적 중"):TEXT("손 인식 대기"),
            C4Test.Hand==1?TEXT("왼손"):C4Test.Hand==2?TEXT("오른손"):TEXT("없음"));
    if (!A) return TEXT("카메라 연결됨 · 플레이어 찾는 중\n영상 속 플레이어 영역 안에 서 주세요.");
    if (!B)
    {
        float Progress=0.f;
        Live.GetFloat(TEXT("Live"),TEXT("progress"),Progress);
        return FString::Printf(TEXT("서 있는 높이 측정 중 (%d%%)\n측정이 끝날 때까지 똑바로 서 주세요."),
            FMath::RoundToInt(FMath::Clamp(Progress,0.f,1.f)*100.f));
    }
    return FString::Printf(TEXT("높이 측정 완료 · %s\n양손 동작 %s"),
        Motion->IsCrouching()?TEXT("앉음"):TEXT("서 있음"), Motion->IsBreachRecognized()?TEXT("감지됨"):TEXT("대기"));
}
void UOBDeviceSettingsSubsystem::Command(const FString& Key)
{
    if (Key==TEXT("1")) { bC4Test=false;C4Test.Reset(FPlatformTime::Seconds()); }
    FFileHelper::SaveStringToFile(Key,*(RuntimeDir()/FString::Printf(TEXT("camera-%s.cmd"),*FGuid::NewGuid().ToString(EGuidFormats::Digits))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
void UOBDeviceSettingsSubsystem::StartC4Test()
{
    if (Mode!=2) return;
    C4Test.Reset(FPlatformTime::Seconds());bC4Test=true;
    Command(TEXT("c4"));
}
double UOBDeviceSettingsSubsystem::ReadNumber(const FString& Section,const FString& Key,double Default) const
{
    double Value; const FString Text=ConfigText(Section,Key,TEXT(""));
    return LexTryParseString(Value,*Text) && FMath::IsFinite(Value)?Value:Default;
}
bool UOBDeviceSettingsSubsystem::WriteNumber(const FString& Section,const FString& Key,double Value)
{
    if (!FMath::IsFinite(Value)) return false;
    const FString Path=SettingsPath(),Lock=FPaths::ChangeExtension(Path,TEXT("lock"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    TUniquePtr<FArchive> Guard(IFileManager::Get().CreateFileWriter(*Lock,FILEWRITE_NoReplaceExisting|FILEWRITE_Silent));
    if (!Guard) { HelperError=TEXT("설정 저장 중입니다. 잠시 뒤 다시 적용하세요."); return false; }
    // FConfigFile normalizes quoted JSON objects into Unreal config tokens.
    // Patch only this scalar line; camera ROIs and other device values stay intact.
    FString Existing; FFileHelper::LoadFileToString(Existing,*Path);
    TArray<FString> Lines; Existing.ParseIntoArrayLines(Lines,false);
    const FString Header=TEXT("[")+Section+TEXT("]"),Entry=Key+TEXT("=")+FString::SanitizeFloat(Value);
    bool InSection=false,FoundSection=false,Written=false;
    for (int32 I=0;I<Lines.Num();++I)
    {
        const FString Trimmed=Lines[I].TrimStartAndEnd();
        if (Trimmed.StartsWith(TEXT("[")) && Trimmed.EndsWith(TEXT("]")))
        {
            if (InSection && !Written) { Lines.Insert(Entry,I++); Written=true; }
            InSection=Trimmed==Header; FoundSection|=InSection;
        }
        else if (InSection)
        {
            FString Name,Ignored;
            if (Trimmed.Split(TEXT("="),&Name,&Ignored) && Name.TrimStartAndEnd()==Key)
            { Lines[I]=Entry; Written=true; }
        }
    }
    if (!Written)
    {
        if (!FoundSection) { Lines.Add(TEXT("")); Lines.Add(Header); }
        Lines.Add(Entry);
    }
    const FString Temp=Path+TEXT(".")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".tmp");
    const bool Saved=FFileHelper::SaveStringToFile(FString::Join(Lines,TEXT("\n"))+TEXT("\n"),*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        && IFileManager::Get().Move(*Path,*Temp,true,true);
    Guard.Reset(); IFileManager::Get().Delete(*Lock,false,true);
    HelperError=Saved?FString():TEXT("설정 파일을 저장하지 못했습니다.");
    return Saved;
}
FString UOBDeviceSettingsSubsystem::GunSection() const
{
    const FString Display=ConfigText(TEXT("Gun"),TEXT("section_id"),TEXT(""));
    return TEXT("Gun:")+Display;
}
void UOBDeviceSettingsSubsystem::SetCameraPreviewRequested(bool bRequested) const
{
    if (CameraSessionDir.IsEmpty()) return;
    const FString Path = CameraSessionDir / TEXT("mediapipe.preview");
    if (bRequested)
    {
        IFileManager::Get().MakeDirectory(*CameraSessionDir, true);
        FFileHelper::SaveStringToFile(TEXT("preview"), *Path);
    }
    else if (IFileManager::Get().FileExists(*Path))
    {
        IFileManager::Get().Delete(*Path);
    }
}
void UOBDeviceSettingsSubsystem::Reconnect()
{
    HelperError.Empty();
    if (Mode==1 && (GunBridgeIsActive() || (GunProcess.IsValid() && FPlatformProcess::IsProcRunning(GunProcess))))
    {
        const FString Path=RuntimeDir()/FString::Printf(TEXT("gun-%s.cmd"),*FGuid::NewGuid().ToString(EGuidFormats::Digits));
        const FString Pending=Path+TEXT(".tmp");
        if (!FFileHelper::SaveStringToFile(TEXT("reconnect"),*Pending,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            || !IFileManager::Get().Move(*Path,*Pending))
        {
            HelperError=TEXT("건컨트롤러 다시 연결 요청을 보내지 못했습니다.");
        }
        return;
    }
    StartHelper(Mode);
    if (Mode==2) SetCameraPreviewRequested(true);
}
void UOBDeviceSettingsSubsystem::StartHelper(int32 Device)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (FParse::Param(FCommandLine::Get(),TEXT("OBDeviceSettingsNoHelpers"))) return;
#endif
    const bool Gun=Device==1;
    if (Gun && GunBridgeIsActive()) return;
    FProcHandle& Process=Gun?GunProcess:CameraProcess;
    if (Process.IsValid() && FPlatformProcess::IsProcRunning(Process)) return;
    if (Process.IsValid()) FPlatformProcess::CloseProc(Process);
    FString Exe,Args;
    FString& Stop=Gun?GunStop:CameraStop;
    if (Gun)
    {
        const FString ReconnectId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Stop=RuntimeDir()/(ReconnectId+TEXT(".stop"));
        const FString Port=ConfigText(TEXT("Gun"),TEXT("port"),TEXT("AUTO"));
        const FString Display=ConfigText(TEXT("Gun"),TEXT("display"),TEXT("PRIMARY"));
        FString ProfilePath;
        const FString LogRoot=UOBGameInstance::ReadLearnerProfileArgument(FCommandLine::Get(),ProfilePath)
            ? FPaths::GetPath(ProfilePath) : RuntimeDir()/TEXT("GunLogs");
        const FString LogDir=LogRoot/(TEXT("IR-Reconnect-")+ReconnectId);
        Args=FString::Printf(TEXT("--bridge-worker --port \"%s\" --display \"%s\" --udp-port %d --log-dir \"%s\" --stop-file \"%s\""),*Port,*Display,(int32)ReadNumber(TEXT("Gun"),TEXT("udp_port"),9011),*LogDir,*Stop);
        Exe=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../OB.exe"));
    }
    else
    {
        if (CameraSessionDir.IsEmpty())
            CameraSessionDir = RuntimeDir() / (TEXT("Camera-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
        const FString Ready = CameraSessionDir / TEXT("mediapipe.ready.json");
        IFileManager::Get().Delete(*Ready, false, true);
        Stop = CameraSessionDir / TEXT("mediapipe.stop");
        Exe=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../MediaPipe/MediaPipe.exe"));
        Args=FString::Printf(TEXT("--embedded --ready-file \"%s\" --stop-file \"%s\" --session-token %s --owner-pid %u --log-file \"%s\" --preview-file \"%s\""),
            *Ready, *Stop, *FGuid::NewGuid().ToString(EGuidFormats::Digits), FPlatformProcess::GetCurrentProcessId(),
            *(CameraSessionDir / TEXT("MediaPipe.log")), *(CameraSessionDir / TEXT("mediapipe.preview")));
    }
    if (!IFileManager::Get().FileExists(*Exe))
    {
        const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        const FString Python=Root/TEXT("Support/Testing/Packaging/.venv/Scripts/python.exe");
        if (!IFileManager::Get().FileExists(*Python)) { HelperError=TEXT("장치 실행 파일을 찾지 못했습니다."); return; }
        if (Gun) { Args=TEXT("\"")+Root/TEXT("Support/Submission/OpenFire/package_launcher.py")+TEXT("\" ")+Args; }
        else { Args=TEXT("\"")+Root/TEXT("Support/Testing/Packaging/mediapipe_entry.py")+TEXT("\" ")+Args; }
        Exe=Python;
    }
    Process=FPlatformProcess::CreateProc(*Exe,*Args,false,true,true,nullptr,0,nullptr,nullptr);
    if (!Process.IsValid()) HelperError=TEXT("장치 프로그램을 시작하지 못했습니다.");
}
void UOBDeviceSettingsSubsystem::StopHelpers()
{
    for (const FString& Stop:{GunStop,CameraStop}) if (!Stop.IsEmpty()) FFileHelper::SaveStringToFile(TEXT("stop"),*Stop);
    if (GunProcess.IsValid()) FPlatformProcess::CloseProc(GunProcess);
    if (CameraProcess.IsValid()) FPlatformProcess::CloseProc(CameraProcess);
}
