#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "AssetCompilingManager.h"
#include "ContentStreaming.h"
#include "ShaderCompiler.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBModularSoldierComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Core/OBPlayerController.h"
#include "Core/OBGameViewportClient.h"
#include "Stage/OBStageFlowComponent.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "Components/TextBlock.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Slate/WidgetRenderer.h"

class FOBStagePlanMapSmoke
{
    struct FState
    {
        int32 Step=0,ResetIndex=0;
        bool OK=true,Captured=false;
        double Started=FPlatformTime::Seconds(),At=Started;
        TWeakObjectPtr<UWorld> Previous;
        FString Log;
    };
    static FString Folder() { return FPaths::ProjectSavedDir()/TEXT("Tests/StagePlan"); }
    static void Check(const TSharedPtr<FState>& S,bool OK,const TCHAR* What)
    {
        S->OK &= OK;S->Log+=FString::Printf(TEXT("%s %s\n"),OK ? TEXT("PASS") : TEXT("FAIL"),What);
        UE_LOG(LogTemp,Display,TEXT("STAGE_PLAN %s %s"),OK ? TEXT("PASS") : TEXT("FAIL"),What);
    }
    static void Key(AOBPlayerController* PC,FKey Key,bool Pressed)
    {
        PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Key,Pressed ? IE_Pressed : IE_Released,FPlatformTime::Cycles64()));
    }
    static void Result(AOBPlayerCharacter* P,bool Success)
    {
        auto* F=P->GetStageFlowComponent();F->ClearPresentation();F->RestoreSlowMotion();F->RestoreOpeningMovement();
        P->GetPlayerSplineComponent()->SetStageMovementBlocked(true);
        F->Phase=EOBStagePhase::Combat;F->CurrentZone=6;F->StartResultTracking();
        F->StageRecord.bUsedTestInputs=true;
        for (int32 I=0;I<6;++I)
        {
            auto& Z=F->StageRecord.StageZones[I];Z=FOBStageZoneResult();Z.Zone=I+1;Z.bStarted=true;
            if (I<5) { Z.bFinalized=true;Z.Score=80; }
            else { Z.PlannedEnemies=5;Z.PlayerKills=5;Z.KillAssessmentSum=5; }
        }
        F->RemainingSeconds=7;F->ZoneElapsedSeconds=3;
        F->FinishStage(Success);
    }
    static bool Capture(USceneCaptureComponent2D* View,const FString& File)
    {
        View->CaptureScene();FlushRenderingCommands();TArray<FColor> Pixels;
        if (!View->TextureTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)) return false;
        TArray<uint8> PNG;FImageUtils::CompressImageArray(View->TextureTarget->SizeX,View->TextureTarget->SizeY,Pixels,PNG);
        return FFileHelper::SaveArrayToFile(PNG,*File);
    }
    static bool CaptureResult(UOBTrainingResultWidget* Result,const FOBTrainingResultData& Data)
    {
        // Rendering the live Slate subtree at another size would replace its cached viewport geometry.
        auto* Copy=CreateWidget<UOBTrainingResultWidget>(Result->GetOwningPlayer(),Result->GetClass());
        Copy->bAutomaticNavigation=false;Copy->TakeWidget();Copy->SetResult(Data);Copy->SetStageCompletion(Result->IsScenarioPassed());
        FWidgetRenderer Renderer(true);
        auto* Target=Renderer.DrawWidget(Copy->TakeWidget(),FVector2D(1920,1080));
        FlushRenderingCommands();TArray<FColor> Pixels;
        if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)) return false;
        TArray<uint8> PNG;FImageUtils::CompressImageArray(Target->SizeX,Target->SizeY,Pixels,PNG);
        return FFileHelper::SaveArrayToFile(PNG,*(Folder()/TEXT("result-1920x1080.png")));
    }
    static void Appearance(UWorld* W,const TSharedPtr<FState>& S)
    {
        const TCHAR* Names[]={TEXT("Rifleman"),TEXT("RPGman"),TEXT("HostageTaker")};
        const TCHAR* Classes[]={TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"),TEXT("/Game/Blueprints/Enemy/BP_RPGTrooper.BP_RPGTrooper_C"),TEXT("/Game/Blueprints/Enemy/BP_HostageTaker.BP_HostageTaker_C")};
        TArray<USceneCaptureComponent2D*> Views;
        for (int32 I=0;I<3;++I)
        {
            FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* E=W->SpawnActor<AOBEnemyCharacter>(LoadClass<AOBEnemyCharacter>(nullptr,Classes[I]),FVector(2000,30000+I*600,200),FRotator::ZeroRotator,Spawn);
            Check(S,E!=nullptr,TEXT("Production enemy appearance spawns"));if (!E) continue;
            E->GetCharacterMovement()->DisableMovement();E->SetActorHiddenInGame(false);
            auto* Mesh=E->GetMesh();Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Mesh->TickAnimation(.1f,false);Mesh->RefreshBoneTransforms();
            Check(S,E->GetSoldierAppearanceComponent()->GetVisualParts().Num()==1,TEXT("Each role uses its own complete insurgent preset"));
            Check(S,Mesh->GetAnimInstance()!=nullptr,TEXT("Retargeted animation instance initializes"));
            if (I==2)
            {
                const int32 Before=E->GetPatternShotsFired();E->StartEncounterBehavior();E->FireBullet();
                Check(S,E->GetEnemyArchetype()==EOBEnemyArchetype::HostageTaker && E->GetPatternShotsFired()==Before,TEXT("Captor is a distinct nonattacking combatant"));
            }
            auto* View=NewObject<USceneCaptureComponent2D>(E);View->bCaptureEveryFrame=false;View->bCaptureOnMovement=false;
            View->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;View->ShowOnlyActors.Add(E);
            View->CaptureSource=ESceneCaptureSource::SCS_BaseColor;View->FOVAngle=35;
            View->TextureTarget=NewObject<UTextureRenderTarget2D>(View);View->TextureTarget->InitCustomFormat(640,960,PF_B8G8R8A8,false);
            const FVector Target=E->GetActorLocation()+FVector(0,0,0),From=Target+FVector(390,170,0);
            View->SetWorldLocationAndRotation(From,UKismetMathLibrary::FindLookAtRotation(From,Target));View->RegisterComponent();
            for (USkeletalMeshComponent* Part:E->GetSoldierAppearanceComponent()->GetVisualParts()) { Part->UpdateFollowerComponent();Part->RefreshBoneTransforms();Part->PrestreamTextures(10.f,true); }
            E->GetMesh()->PrestreamTextures(10.f,true);E->GetWeaponMesh()->PrestreamTextures(10.f,true);
            Views.Add(View);
        }
        FAssetCompilingManager::Get().FinishAllCompilation();IStreamingManager::Get().StreamAllResources(10.f);FlushRenderingCommands();
        for (auto* View:Views) View->CaptureScene();
        FlushRenderingCommands();if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();FlushRenderingCommands();
        for (int32 I=0;I<Views.Num();++I)
            Check(S,Capture(Views[I],Folder()/(FString(Names[I])+TEXT("-applied.png"))),TEXT("Applied role appearance captured"));
    }
public:
    static void Start(bool AppearanceOnly=false,bool LayoutOnly=false)
    {
        IFileManager::Get().MakeDirectory(*Folder(),true);
        auto S=MakeShared<FState>();
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S,AppearanceOnly,LayoutOnly](float) {
            auto Finish=[&]() {
                const TCHAR* File=AppearanceOnly ? TEXT("appearance-smoke.txt") : LayoutOnly ? TEXT("layout-smoke.txt") : TEXT("runtime-smoke.txt");
                FFileHelper::SaveStringToFile(S->Log+FString::Printf(TEXT("outcome=%s\n"),S->OK ? TEXT("PASS") : TEXT("FAIL")),*(Folder()/File));
                if (GEditor->PlayWorld) GEditor->RequestEndPlayMap();
                const bool OK=S->OK;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) { FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false; }),2.f);
            };
            const double Now=FPlatformTime::Seconds();
            if (Now-S->Started>180) { Check(S,false,TEXT("Runtime navigation deadline"));Finish();return false; }
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (!W || !PC || W->GetTimeSeconds()<.5) return true;
            auto* F=P ? P->GetStageFlowComponent() : nullptr;
            auto* O=PC->GetOBHUD() ? PC->GetOBHUD()->GetPlayerOverlayWidget() : nullptr;
            auto* R=O ? O->GetTrainingResultWidget() : nullptr;
            const FString Map=UGameplayStatics::GetCurrentLevelName(W,true);
            if (S->Step==0 && F)
            {
                Check(S,F->ZoneTimeLimit==60 && F->HostageTimeLimit==10,TEXT("Saved Stage_01 uses 60s normal zones and 10s rescue"));
                if (!LayoutOnly) Appearance(W,S);
                if (AppearanceOnly) { Finish();return false; }
                Result(P,true);S->Step=1;
            }
            else if (S->Step==1 && F && F->GetPhase()==EOBStagePhase::Complete && R)
            {
                Check(S,O->IsTrainingResultVisible(),TEXT("Actual ending restores the interactive result"));
                Check(S,CastChecked<UTextBlock>(R->GetWidgetFromName(TEXT("StageScoreText")))->GetText().ToString()==TEXT("82.08 / 100"),TEXT("End-to-result displays the six-zone average"));
                Check(S,CastChecked<UTextBlock>(R->GetWidgetFromName(TEXT("StageRankText")))->GetText().ToString()==TEXT("A"),TEXT("End-to-result displays rank A"));
                Check(S,CaptureResult(R,F->GetStageRecord()),TEXT("Result artwork renders at full viewport resolution"));
                FScreenshotRequest::RequestScreenshot(Folder()/TEXT("result-applied.png"),true,false);S->At=Now;S->Step=2;
            }
            else if (S->Step==2 && Now-S->At>.5)
            {
                const FGeometry Root=R->GetCachedGeometry();const FVector2D Bounds=Root.GetLocalSize();
                for (const TCHAR* Name:{TEXT("StageTitleText"),TEXT("StageRetryHint"),TEXT("StageMenuHint")})
                {
                    auto* Widget=R->GetWidgetFromName(Name);const FGeometry Geo=Widget->GetCachedGeometry();
                    const FVector2D Min=Root.AbsoluteToLocal(Geo.LocalToAbsolute(FVector2D::ZeroVector));
                    const FVector2D Max=Root.AbsoluteToLocal(Geo.LocalToAbsolute(Geo.GetLocalSize()));
                    Check(S,Min.X>=-1 && Min.Y>=-1 && Max.X<=Bounds.X+1 && Max.Y<=Bounds.Y+1,TEXT("Title and both navigation prompts fit the actual small PIE viewport"));
                    S->Log+=FString::Printf(TEXT("layout %s root=%s min=%s max=%s\n"),Name,*Bounds.ToString(),*Min.ToString(),*Max.ToString());
                }
                if (LayoutOnly) { Finish();return false; }
                S->Previous=W;Key(PC,EKeys::LeftMouseButton,true);Key(PC,EKeys::LeftMouseButton,false);S->Step=3;
            }
            else if (S->Step==3 && W!=S->Previous.Get() && Map==TEXT("Stage_01") && F)
            {
                Check(S,F->GetCurrentZone()==0 && F->GetStageRecord().ShotsFired==0 && F->GetPhase()==EOBStagePhase::Opening,TEXT("Tap reloads Stage_01 with a fresh opening and zero statistics"));
                Result(P,false);S->Step=4;
            }
            else if (S->Step==4 && F && F->GetPhase()==EOBStagePhase::Complete && R)
            {
                Check(S,CastChecked<UTextBlock>(R->GetWidgetFromName(TEXT("StageScoreText")))->GetText().ToString()==TEXT("66.67 / 100"),TEXT("Failed rescue retains prior zones and zeros zone 6"));
                S->Previous=W;Key(PC,EKeys::LeftMouseButton,true);R->AdvanceInteraction(2.f);S->Step=5;
            }
            else if (S->Step==5 && W!=S->Previous.Get() && Map==TEXT("TitleMap"))
            {
                Check(S,PC->GetOBHUD() && PC->GetOBHUD()->IsMenuVisible(),TEXT("Held result trigger reaches the usable main screen"));
                S->Previous=W;PC->NavigateToMap(TEXT("/Game/Maps/Stage_01"));S->Step=6;
            }
            else if (S->Step==6 && W!=S->Previous.Get() && F && Map==TEXT("Stage_01"))
            {
                const EOBStagePhase Phases[]={EOBStagePhase::Opening,EOBStagePhase::Travel,EOBStagePhase::Combat,EOBStagePhase::Zone6Breach,EOBStagePhase::Ending,EOBStagePhase::Complete};
                F->Phase=Phases[S->ResetIndex];PC->SetPause(S->ResetIndex==2);
                auto* Viewport=PC->GetLocalPlayer() ? PC->GetLocalPlayer()->ViewportClient.Get() : nullptr;
                Check(S,Cast<UOBGameViewportClient>(Viewport)!=nullptr,TEXT("Production viewport installs the global reset handler"));
                if (!Viewport) { Finish();return false; }
                S->Previous=W;
                Viewport->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::F5,IE_Pressed,FPlatformTime::Cycles64()));
                S->Step=7;
            }
            else if (S->Step==7 && W!=S->Previous.Get() && Map==TEXT("TitleMap"))
            {
                Check(S,!UGameplayStatics::IsGamePaused(W) && PC->GetOBHUD() && PC->GetOBHUD()->IsMenuVisible(),TEXT("F5 clears active/paused stage state and reopens main screen"));
                if (++S->ResetIndex==6) { Finish();return false; }
                S->Previous=W;PC->NavigateToMap(TEXT("/Game/Maps/Stage_01"));S->Step=6;
            }
            return true;
        }),.1f);
    }
};
static FAutoConsoleCommand GStagePlanSmoke(TEXT("OB.Stage.PlanPIE"),TEXT("Verify saved role appearances, scores, result navigation and F5 in disposable PIE."),FConsoleCommandDelegate::CreateLambda([] { FOBStagePlanMapSmoke::Start(); }));
static FAutoConsoleCommand GStageAppearanceSmoke(TEXT("OB.Stage.AppearancePIE"),TEXT("Capture the three production role appearances after shader compilation."),FConsoleCommandDelegate::CreateLambda([] { FOBStagePlanMapSmoke::Start(true); }));
static FAutoConsoleCommand GStageLayoutSmoke(TEXT("OB.Stage.ResultLayoutPIE"),TEXT("Verify result content and prompt bounds in the actual small PIE viewport."),FConsoleCommandDelegate::CreateLambda([] { FOBStagePlanMapSmoke::Start(false,true); }));
#endif
