#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Widget/OBTitleWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBHUD.h"
#include "Widget/OBUIVideo.h"
#include "Core/OBPlayerController.h"
#include "Core/OBQACheatManager.h"
#include "Training/OBTrainingDirector.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "MediaPlayer.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"

// Reads the user's saved Title animation in actual PIE and exercises the real QA
// preview while paused. Saves no Blueprint assets and changes no widget layout.
class FOBTitleGuideMediaSmoke
{
    struct FState
    {
        double Started=FPlatformTime::Seconds(),Mark=0,VideoMark=0;
        int32 Phase=0,OpacityTurns=0;
        float Minimum=1,Maximum=0,LastOpacity=-1;
        double LastVideo=-1;
        bool bRising=false,bVideoAdvanced=false,bVideoLooped=false;
        TWeakObjectPtr<UOBGuideWidget> Guide;
    };
    static bool Finish(bool OK,const TCHAR* Why)
    {
        UE_LOG(LogTemp,Display,TEXT("TITLE_GUIDE_MEDIA outcome=%s reason=%s"),OK?TEXT("PASS"):TEXT("FAIL"),Why);
        if(GEditor && GEditor->PlayWorld)GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float){FPlatformMisc::RequestExitWithStatus(false,OK?0:1);return false;}),1.f);
        return false;
    }
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/TitleMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        auto S=MakeShared<FState>();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){
            const double Now=FPlatformTime::Seconds();
            if(Now-S->Started>90)return Finish(false,TEXT("timeout"));
            auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;
            auto* PC=W?Cast<AOBPlayerController>(W->GetFirstPlayerController()):nullptr;
            if(!PC || !PC->GetOBHUD())return true;
            if(S->Phase<=1)
            {
                auto* Title=PC->GetOBHUD()->GetTitleWidget();
                if(!Title)return true;
                auto* Animation=Title->PressTextFadeInOut.Get();
                if(!Animation || !Title->PressText || !Title->LogoImage || !Title->TitleImage)
                    return Finish(false,TEXT("Saved title animation or named widgets missing"));
                if(S->Phase==0)
                {
                    if(!Title->IsAnimationPlaying(Animation))return Finish(false,TEXT("PressText animation is not playing"));
                    if(!S->Mark)S->Mark=Now;
                    const float Opacity=Title->PressText->GetRenderOpacity()*Title->PressText->GetColorAndOpacity().GetSpecifiedColor().A;
                    S->Minimum=FMath::Min(S->Minimum,Opacity);S->Maximum=FMath::Max(S->Maximum,Opacity);
                    if(S->LastOpacity>=0 && !FMath::IsNearlyEqual(Opacity,S->LastOpacity,.005f))
                    {
                        const bool Rising=Opacity>S->LastOpacity;
                        if(Rising!=S->bRising)++S->OpacityTurns;
                        S->bRising=Rising;
                    }
                    S->LastOpacity=Opacity;
                    const double Duration=Animation->GetEndTime()-Animation->GetStartTime();
                    if(Now-S->Mark<FMath::Max(2.,Duration*2.2) || Title->ClipStates[2]!=UOBTitleWidget::EClipState::Ready)return true;
                    if(S->Maximum-S->Minimum<.2f || S->OpacityTurns<3)
                        return Finish(false,TEXT("Opacity did not blink over multiple cycles"));
                    UE_LOG(LogTemp,Display,TEXT("TITLE_GUIDE_MEDIA blink_bound=1 min=%.3f max=%.3f turns=%d duration=%.3f"),S->Minimum,S->Maximum,S->OpacityTurns,Duration);
                    FPointerEvent Click(0,FVector2D(960,540),FVector2D(960,540),TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
                    Title->NativeOnPreviewMouseButtonDown(FGeometry(),Click);
                    S->Phase=1;S->Mark=Now;
                }
                if(Title->LogoImage->IsVisible() || Title->TitleImage->IsVisible() || Title->PressText->IsVisible())
                    return Finish(false,TEXT("A title overlay remained visible after trigger"));
                if(Now-S->Mark>.2 && Title->IsAnimationPlaying(Animation))
                    return Finish(false,TEXT("Blink animation continued after trigger"));
                if(Now-S->Mark>.3){UE_LOG(LogTemp,Display,TEXT("TITLE_GUIDE_MEDIA trigger_hidden=LogoImage,TitleImage,PressText blink_stopped=1"));S->Phase=2;}
                return true;
            }
            if(S->Phase==2)
            {
                AOBTrainingDirector* D=nullptr;for(TActorIterator<AOBTrainingDirector>It(W);It;++It)D=*It;
                if(!D || !D->IsReadyForQA())return true;
                PC->ConsoleCommand(TEXT("QA UI 3001"),true);
                auto* QA=Cast<UOBQACheatManager>(PC->CheatManager);
                auto* Preview=QA?QA->UIPreview.Get():nullptr;
                S->Guide=Preview?Cast<UOBGuideWidget>(Preview->WidgetTree->FindWidget(TEXT("Guide"))):nullptr;
                if(!S->Guide.IsValid())return Finish(false,TEXT("QA guide not available"));
                S->Phase=3;S->Mark=Now;
            }
            if(S->Phase==3)
            {
                auto* Guide=S->Guide.Get();
                auto* Player=Guide && Guide->TriggerVideo?Guide->TriggerVideo->GetPlayer():nullptr;
                if(!Player || !Guide->TriggerImage->IsVisible())return Finish(false,TEXT("Ready guide did not create visible video"));
                auto* MID=Cast<UMaterialInstanceDynamic>(Guide->TriggerImage->GetBrush().GetResourceObject());
                if(!MID || !MID->Parent || MID->Parent->GetName()!=TEXT("M_UIVideo"))return Finish(false,TEXT("Wrong guide video material"));
                if(!Player->IsPlaying())return true;
                const double Time=Player->GetTime().GetTotalSeconds();
                S->bVideoAdvanced|=S->LastVideo>=0 && Time>S->LastVideo+.01;
                S->bVideoLooped|=S->LastVideo>.1 && Time<S->LastVideo-.1;
                S->LastVideo=Time;
                if(!S->bVideoAdvanced || !S->bVideoLooped || !Player->IsLooping())return true;
                UE_LOG(LogTemp,Display,TEXT("TITLE_GUIDE_MEDIA guide_material=M_UIVideo source=%s paused=%d video_playing=1 looped=1"),*Player->GetUrl(),W->IsPaused());
                Guide->SetGuidePage(3002,false);
                S->Phase=4;S->Mark=Now;
                return true;
            }
            if(S->Phase==4 && Now-S->Mark>.2)
            {
                auto* Guide=S->Guide.Get();
                if(Guide->TriggerImage->IsVisible() || Guide->TriggerVideo->GetPlayer()->IsPlaying())
                    return Finish(false,TEXT("Guide page change did not hide/close the video"));
                PC->ConsoleCommand(TEXT("QA UI Close"),true);
                return Finish(true,TEXT("title animation loops; trigger hides all overlays; guide movie loops and closes on page change"));
            }
            return true;
        }),.05f);
    }
};
static FAutoConsoleCommand GTitleGuideMediaSmoke(TEXT("OB.UI.TitleGuideMediaPIE"),TEXT("Verify the saved title blink, overlay hiding and Guide trigger movie in disposable PIE; exits."),FConsoleCommandDelegate::CreateStatic(&FOBTitleGuideMediaSmoke::Start));
#endif
