#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingStatusWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBRadialProgressBar.h"
#include "Widget/OBHUD.h"
#include "Core/OBPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "InputKeyEventArgs.h"

// Real map, saved UMG and native completion/timeout/input paths. Only the aim
// cursor and elapsed hold time are injected; no asset or gameplay defaults saved.
class FOBMarkerWarningSmoke
{
    struct FState { double Started=FPlatformTime::Seconds(),Mark=0; int32 Phase=0; };
    static bool Finish(bool OK,const TCHAR* Why)
    {
        UE_LOG(LogTemp,Display,TEXT("MARKER_WARNING_PIE outcome=%s reason=%s"),OK?TEXT("PASS"):TEXT("FAIL"),Why);
        if(GEditor && GEditor->PlayWorld)GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float){FPlatformMisc::RequestExitWithStatus(false,OK?0:1);return false;}),1.f);
        return false;
    }
    static void Trigger(AOBPlayerController* PC,bool Pressed)
    {
        PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::LeftMouseButton,Pressed?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));
    }
    static bool Capture(UUserWidget* Widget,const TCHAR* Name)
    {
        const FIntPoint Size(1920,1080);const auto Slate=Widget->TakeWidget();
        FWidgetRenderer Renderer(true);Renderer.DrawWidget(Slate,FVector2D(Size));FlushRenderingCommands();
        auto* Target=Renderer.DrawWidget(Slate,FVector2D(Size));TArray<FColor> Pixels;
        if(!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))return false;
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("Tests/MarkerWarning/Preview");IFileManager::Get().MakeDirectory(*Folder,true);
        return FFileHelper::SaveArrayToFile(PNG,*(Folder/Name));
    }
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/TutorialMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);auto S=MakeShared<FState>();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){
            const double Now=FPlatformTime::Seconds();if(Now-S->Started>80)return Finish(false,TEXT("Timeout"));
            auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;
            auto* PC=W?Cast<AOBPlayerController>(W->GetFirstPlayerController()):nullptr;
            if(!PC || !PC->GetOBHUD())return true;
            if(S->Phase==5)
            {
                if(!UWorld::RemovePIEPrefix(W->GetOutermost()->GetName()).EndsWith(TEXT("TitleMap")))return true;
                return Finish(true,TEXT("Half radial fill, green completion, red reset, Warning UI, resume and TitleMap navigation"));
            }
            AOBTrainingDirector* D=nullptr;for(TActorIterator<AOBTrainingDirector> It(W);It;++It)D=*It;
            auto* Overlay=PC->GetOBHUD()->GetPlayerOverlayWidget();
            if(!D || !D->IsReadyForQA() || !Overlay)return true;
            auto* Markers=Overlay->GetTrainingMarkers();auto* Result=Overlay->GetTrainingResultWidget();
            if(!Markers || !Result)return Finish(false,TEXT("Missing saved panels"));
            auto* Target=Cast<UImage>(Markers->WidgetTree->FindWidget(TEXT("AimTarget")));
            auto* Gauge=Cast<UOBRadialProgressBar>(Markers->WidgetTree->FindWidget(TEXT("AimProgress")));
            if(!Target || !Gauge)return Finish(false,TEXT("Missing marker nodes"));
            int32 Width=0,Height=0;PC->GetViewportSize(Width,Height);const FVector2D Size(Width,Height);
            if(Width<=0 || Height<=0)return true;
            if(S->Phase==0)
            {
                PC->ConsoleCommand(TEXT("QA Goto AimHold"),true);D->SetActorTickEnabled(false);
                if(D->AimRadiusPixels!=72.f || D->GroupRadiusCm!=20.f)return Finish(false,TEXT("Saved TutorialMap did not inherit the relaxed radii"));
                D->UpdateAimHold(.1f,D->GetAimTargetPosition()*Size+FVector2D(72.1,0),Size);
                if(D->GetAimHoldProgress()!=0.f)return Finish(false,TEXT("Outside-radius input was accepted"));
                D->UpdateAimHold(D->AimHoldSeconds*.5f,D->GetAimTargetPosition()*Size+FVector2D(71.9,0),Size);
                S->Phase=1;S->Mark=Now;return true;
            }
            if(S->Phase==1 && Now-S->Mark>.15)
            {
                if(!FMath::IsNearlyEqual(Gauge->GetPercent(),.5f) || Target->GetBrush().GetResourceObject()->GetName()!=TEXT("Aim_Red"))return Finish(false,TEXT("Half progress did not reach saved UMG"));
                auto* TargetSlot=CastChecked<UCanvasPanelSlot>(Target->Slot);
                const double VisibleRadius=TargetSlot->GetSize().X*.45*Size.X/Markers->GetCachedGeometry().GetLocalSize().X;
                if(!FMath::IsNearlyEqual(VisibleRadius,72.,.01))return Finish(false,TEXT("Rendered marker radius does not match its enlarged hit area"));
                UE_LOG(LogTemp,Display,TEXT("TRAINING_RELAXED_RADII aim=%.1fpx rendered=%.1fpx zeroing=%.1fcm"),D->AimRadiusPixels,VisibleRadius,D->GroupRadiusCm);
                if(!Capture(Markers,TEXT("Aim_50.png")))return Finish(false,TEXT("Half capture failed"));
                D->UpdateAimHold(D->AimHoldSeconds*.5f,D->GetAimTargetPosition()*Size,Size);
                S->Phase=2;S->Mark=Now;return true;
            }
            if(S->Phase==2 && Now-S->Mark>.05)
            {
                if(Target->GetBrush().GetResourceObject()->GetName()!=TEXT("Aim_Green") || Gauge->IsVisible() || !Markers->HasAimCompletionFeedback())return Finish(false,TEXT("Native completion event did not show green marker"));
                if(!Capture(Markers,TEXT("Aim_100.png")))return Finish(false,TEXT("Green capture failed"));
                S->Phase=3;return true;
            }
            if(S->Phase==3 && Now-S->Mark>.5)
            {
                if(Target->GetBrush().GetResourceObject()->GetName()!=TEXT("Aim_Red") || Gauge->GetPercent()!=0)return Finish(false,TEXT("Next point did not reset to red"));
                if(!Capture(Markers,TEXT("Aim_0.png")))return Finish(false,TEXT("Reset capture failed"));
                D->PrototypeDeadline=W->GetTimeSeconds()-.1;D->TickPrototype();S->Phase=4;S->Mark=Now;return true;
            }
            if(S->Phase==4 && Now-S->Mark>.15)
            {
                if(!D->bPrototypePaused || !Overlay->IsTrainingPauseVisible() || Result->CurrentUIId!=3008 || Overlay->WidgetTree->FindWidget(TEXT("Pause")))return Finish(false,TEXT("Timeout did not use Warning instead of Pause"));
                auto* Border=Cast<UBorder>(Result->WidgetTree->FindWidget(TEXT("ResultBorder")));
                auto* Title=Cast<UTextBlock>(Result->WidgetTree->FindWidget(TEXT("TitleText")));
                auto* Body=Cast<UTextBlock>(Result->WidgetTree->FindWidget(TEXT("BodyText")));
                if(!Border || !Border->Background.GetResourceObject() || Border->Background.GetResourceObject()->GetName()!=TEXT("SubUI_Warning") || !Title || Title->GetText().IsEmpty() || !Body || Body->GetText().IsEmpty())return Finish(false,TEXT("Warning artwork/text missing"));
                if(!Capture(Result,TEXT("Warning.png")))return Finish(false,TEXT("Warning capture failed"));
                Result->PrepareWarningInteraction(false);Trigger(PC,true);Trigger(PC,false);
                if(D->bPrototypePaused || D->GetRemainingSeconds()<=0 || D->AimIndex!=1)return Finish(false,TEXT("Short trigger failed to resume existing progress"));
                D->PrototypeDeadline=W->GetTimeSeconds()-.1;D->TickPrototype();
                Result->PrepareWarningInteraction(false);Trigger(PC,true);Result->AdvanceInteraction(Result->LongPressSeconds);
                S->Phase=5;return true;
            }
            return true;
        }),0.f);
    }
};
static FAutoConsoleCommand GMarkerWarningSmoke(TEXT("OB.UI.MarkerWarningPIE"),TEXT("Verify saved AimHold gauge and Warning pause interactions on TutorialMap."),FConsoleCommandDelegate::CreateStatic(&FOBMarkerWarningSmoke::Start));
#endif
