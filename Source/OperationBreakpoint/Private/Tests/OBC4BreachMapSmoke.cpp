#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "World/OBBreachCharge.h"
#include "World/OBBreachDoor.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OSCManager.h"
#include "OSCAddress.h"
#include "OSCClient.h"
#include "OSCMessage.h"
#include "OSCServer.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Training/OBTrainingStatusWidget.h"
#include "Components/TextBlock.h"
#include "Input/OBDeviceSettingsSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "AI/OBAIController.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllyNavigationComponent.h"
#include "World/OBEncounterSpawner.h"
#include "World/OBSquadRendezvous.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "StateTree.h"
#include "UObject/StrongObjectPtr.h"

class FOBC4BreachMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        struct FState {
            int32 Step=0,Hand=1;bool OK=true,SawBlack=false,IntactBeforeBlack=true,SawPhysics=false;
            double Began=FPlatformTime::Seconds(),At=0,LastPacket=0;uint64 FirstPhysicsFrame=0;
            FVector2D FirstScreen=FVector2D::ZeroVector;FString Log;
            bool EntryChecked=false,SawEntryMotion=false;float MaxEntrySpeed=0;
            FString EntrySamples=TEXT("time,distance,speed_limit,vx,vy,vz,x,y,z\n");
            double PreviousEntryTime=0;FVector PreviousEntryLocation=FVector::ZeroVector;
            TStrongObjectPtr<UOSCClient> Client;
        };
        auto S=MakeShared<FState>();FParse::Value(FCommandLine::Get(),TEXT("C4Hand="),S->Hand);
        const FString Out=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("C4F7Test")) ? FString(TEXT("Tests/C4Breach/F7")) : FString::Printf(TEXT("Tests/C4Breach/PIE-%s"),S->Hand==1 ? TEXT("Left") : TEXT("Right"))));
        IFileManager::Get().MakeDirectory(*Out,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S,Out](float) {
            auto Check=[&](bool OK,const FString& Message) { S->OK &= OK;S->Log+=(OK ? TEXT("PASS ") : TEXT("FAIL "))+Message+TEXT("\n");FFileHelper::SaveStringToFile(S->Log,*(Out/TEXT("progress.txt"))); };
            auto Finish=[&]() {
                S->Log+=FString::Printf(TEXT("outcome=%s hand=%d\n"),S->OK ? TEXT("PASS") : TEXT("FAIL"),S->Hand);
                FFileHelper::SaveStringToFile(S->Log,*(Out/TEXT("verification.txt")));UE_LOG(LogTemp,Display,TEXT("C4_BREACH_PIE\n%s"),*S->Log);
                FFileHelper::SaveStringToFile(S->EntrySamples,*(Out/TEXT("entry-motion.csv")));
                GEditor->RequestEndPlayMap();const bool OK=S->OK;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.25f);
            };
            if (FPlatformTime::Seconds()-S->Began>100) { Check(false,TEXT("C4 breach scenario deadline"));Finish();return false; }
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;auto* PC=W ? W->GetFirstPlayerController() : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;if (!P || W->GetTimeSeconds()<.8f || !P->GetPlayerSplineComponent()->GetRailPath()) return true;
            auto* F=P->GetStageFlowComponent();auto* R=P->GetPlayerSplineComponent();auto* M=W->GetGameInstance()->GetSubsystem<UOBMediaPipeOSCSubsystem>();
            const double Now=FPlatformTime::Seconds();
            auto Capture=[&](const TCHAR* Name,bool IncludeUI=true) {
                auto* View=W->GetGameViewport()->Viewport;TArray<FColor> Pixels;TArray64<uint8> PNG;FIntVector Size;
                auto Window=W->GetGameViewport()->GetWindow();
                bool Captured=IncludeUI && Window && FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(),Pixels,Size);
                if (!Captured && View) { Captured=View->ReadPixels(Pixels);const FIntPoint Dimensions=View->GetSizeXY();Size=FIntVector(Dimensions.X,Dimensions.Y,0); }
                if (!Captured || Pixels.IsEmpty()) { Check(false,TEXT("Actual PIE viewport capture is available"));return; }
                FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
                FFileHelper::SaveArrayToFile(PNG,*(Out/Name));
            };
            if (S->Step==0)
            {
                if (FParse::Param(FCommandLine::Get(),TEXT("C4F7Test")))
                    S->Client.Reset(UOSCManager::CreateOSCClient(TEXT("127.0.0.1"),8000,TEXT("F7C4Loopback"),W));
                else if (M && M->OSCServer) M->OSCServer->Stop();
                F->FinishOpening();F->ClearPresentation();F->RestoreOpeningMovement();F->Phase=EOBStagePhase::Travel;F->StageRecord.bUsedTestInputs=true;P->SetCanBeDamaged(false);
                FString Error;Check(R->StartAtStageStop(85,Error),TEXT("Actual saved breach stop and its authored door activate"));
                Check(F->IsWaitingForBreach() && F->GetBreachCharge() && F->GetBreachCharge()->GetChargeMesh()->GetStaticMesh(),TEXT("The real C4 mesh is loaded for placement"));
                FOSCMessage Legacy;UOSCManager::SetOSCMessageAddress(Legacy,FOSCAddress(TEXT("/motion/breach")));UOSCManager::AddBool(Legacy,true);
                if (M) M->HandleOSCMessage(Legacy,TEXT("127.0.0.1"),8000);
                Check(!F->bBreachRequested,TEXT("Legacy two-hand recognition cannot bypass C4 placement"));
                if (FParse::Param(FCommandLine::Get(),TEXT("Zone6RevisionCheck")))
                {
                    int32 Guards=0,Far=0;bool Facing=true;TMap<AActor*,int32> FarPerHostage;
                    for (TActorIterator<AOBEncounterSpawner> It(W);It;++It) if (It->GetPreparedPointIndex()==86 && It->bParticipatesInCombat)
                    {
                        auto* Enemy=It->GetSpawnedEnemy();auto* AI=Enemy ? Cast<AOBAIController>(Enemy->GetController()) : nullptr;
                        auto* Target=AI ? AI->GetZone6FacingTarget() : nullptr;
                        S->Log+=FString::Printf(TEXT("guard_before name=%s tree=%s running=%d target=%s far=%d yaw=%.2f\n"),*GetNameSafe(Enemy),*GetNameSafe(AI ? AI->GetActiveCombatTree() : nullptr),AI && AI->IsCombatTreeRunning(),*GetNameSafe(Target),AI && AI->IsZone6FarGuard(),Enemy ? Enemy->GetActorRotation().Yaw : 0.f);
                        Facing &= AI && AI->IsCombatTreeRunning() && GetNameSafe(AI->GetActiveCombatTree())==TEXT("ST_Zone6_HostageGuard") && Target;
                        if (Target) Facing &= FVector::DotProduct(Enemy->GetActorForwardVector(),(Target->GetActorLocation()-Enemy->GetActorLocation()).GetSafeNormal2D())>.98f;
                        ++Guards;if (AI && AI->IsZone6FarGuard()) { ++Far;++FarPerHostage.FindOrAdd(Target); }
                    }
                    Check(Guards==4 && Facing,TEXT("All four Zone6 guards run the dedicated StateTree and face their assigned hostage before breach"));
                    Check(Far==2 && FarPerHostage.Num()==2 && FarPerHostage.CreateConstIterator().Value()==1,TEXT("One far guard is selected for each of the two hostages"));
                }
                if (FParse::Param(FCommandLine::Get(),TEXT("C4F7Test")))
                {
                    auto* Settings=W->GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>();
                    Settings->Toggle(2);Settings->StartC4Test();
                    Check(Settings->IsC4Test() && UGameplayStatics::IsGamePaused(W),TEXT("F7 opens an isolated C4 test while the breach world stays paused"));
                }
                S->Step=1;S->At=Now;return true;
            }
            auto* Door=F->ActiveBreachDoor.Get();auto* Charge=F->GetBreachCharge();
            if (!Door && S->Step<7) { Check(false,TEXT("Authored breach door remains valid"));Finish();return false; }
            auto Send=[&](FVector2D UV,bool Tracked) {
                if (!M || Now-S->LastPacket<1./15.) return;
                S->LastPacket=Now;
                FOSCMessage Msg;UOSCManager::SetOSCMessageAddress(Msg,FOSCAddress(TEXT("/mediapipe/breach/guide")));
                const bool Left=S->Hand==1;
                for (float V:TArray<float>{1,640,480,1,1,1,.68125f,.28125f,.74875f,.36875f,.25125f,.28125f,.31875f,.36875f,
                    Left ? float(UV.X) : .25f,Left ? float(UV.Y) : .9f,Tracked ? .9f : 0.f,
                    Left ? .25f : float(UV.X),Left ? .9f : float(UV.Y),Tracked ? .9f : 0.f,0,0,0,0}) UOSCManager::AddFloat(Msg,V);
                if (S->Client.IsValid()) S->Client->SendOSCMessage(Msg);
                else M->HandleOSCMessage(Msg,TEXT("127.0.0.1"),8000);
            };
            if (FParse::Param(FCommandLine::Get(),TEXT("C4F7Test")))
            {
                auto* Settings=W->GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>();
                Send(S->Step==3 ? Settings->GetC4Test().GetTargetCenter() : FVector2D(.25,.4),true);
                if (Now-S->At<.7) return true;
                const auto& Test=Settings->GetC4Test();
                if (S->Step<=2)
                {
                    if ((!Test.bTracked || Test.Hand!=S->Hand) && Now-S->At<5.) return true;
                    Check(Test.bTracked && Test.Hand==S->Hand && !Test.bAttached,TEXT("F7 C4 follows the higher wrist while both hands remain visible"));
                    Capture(S->Step==1 ? TEXT("F7-Left.png") : TEXT("F7-Right.png"));
                    S->Hand=2;++S->Step;S->At=Now;return true;
                }
                if (!Test.bAttached && Now-S->At<5.) return true;
                Check(Test.bAttached && Test.Progress==1,TEXT("The F7 central C4 target completes using the same Stage_01 placement tracker"));
                Check(!F->bBreachRequested && !Door->IsBreached() && UGameplayStatics::IsGamePaused(W),TEXT("F7 test success never plants gameplay C4 or breaks the real door"));
                Capture(TEXT("F7-Attached.png"));
                Settings->StartC4Test();Check(!Settings->GetC4Test().bAttached,TEXT("C4 test can be reset without closing F7"));
                Settings->Command(TEXT("1"));Check(!Settings->IsC4Test(),TEXT("The original tutorial two-hand test remains selectable"));
                Settings->Close();Check(!UGameplayStatics::IsGamePaused(W),TEXT("Closing F7 resumes the original breach world"));
                Finish();return false;
            }
            if (S->Step==1)
            {
                Send(FVector2D(.25,.5),true);
                if (Now-S->At<.6) return true;
                if ((!Charge || Charge->IsHidden() || !F->GetC4Placement().bTracked || F->GetC4Placement().Hand!=S->Hand
                    || !F->GetC4Placement().HandUV.Equals(FVector2D(.25,.5),.001)) && Now-S->At<6.) return true;
                FVector2D Screen;const bool Projected=Charge && PC->ProjectWorldLocationToScreen(Charge->GetChargeMesh()->Bounds.Origin,Screen);
                int32 X=0,Y=0;PC->GetViewportSize(X,Y);
                const FVector2D Expected=FOBBreachGuideLayout::MapPoint(FVector2D(.25,.5),FOBBreachGuideLayout::CameraRect(FVector2D(X,Y),FVector2D(640,480)));
                Check(Charge && !Charge->IsHidden() && F->GetC4Placement().Hand==S->Hand,TEXT("A single selected hand visibly carries C4"));
                Check(Projected && FVector2D::Distance(Screen,Expected)<5.f,TEXT("Actual 3D C4 center projects onto the post-mirror hand position"));
                auto* Markers=F->GetOverlay() ? F->GetOverlay()->GetTrainingMarkers() : nullptr;
                auto* Label=Markers ? Cast<UTextBlock>(Markers->GetWidgetFromName(TEXT("LeftHandLabel"))) : nullptr;
                auto* Area=Markers ? Markers->GetWidgetFromName(TEXT("LeftHandArea")) : nullptr;
                auto* Other=Markers ? Markers->GetWidgetFromName(TEXT("RightHandArea")) : nullptr;
                Check(Label && Label->GetText().ToString()==TEXT("C4 부착 위치") && Area && Area->IsVisible() && Area->GetCachedGeometry().GetLocalSize().X>0 && Other && !Other->IsVisible(),TEXT("Actual authored HUD renders one central C4 target, not the tutorial's two hand targets"));
                Check(Markers && !Markers->GetWidgetFromName(TEXT("BreachProgress")),TEXT("The breach progress bar is absent from the actual HUD tree"));
                S->FirstScreen=Screen;Capture(TEXT("Carrying-C4.png"));
                S->Step=2;S->At=FPlatformTime::Seconds();return true;
            }
            if (S->Step==2)
            {
                Send(FVector2D(.75,.53),true);if (Now-S->At<.5) return true;
                if ((!F->GetC4Placement().bTracked || !F->GetC4Placement().HandUV.Equals(FVector2D(.75,.53),.001)) && Now-S->At<6.) return true;
                FVector2D Screen;if (Charge) PC->ProjectWorldLocationToScreen(Charge->GetChargeMesh()->Bounds.Origin,Screen);
                Check(FVector2D::Distance(Screen,S->FirstScreen)>200 && !F->bBreachRequested,TEXT("Moving the hand moves the real C4 without attaching outside the target"));
                S->Step=3;S->At=Now;return true;
            }
            if (S->Step==3)
            {
                Send(F->GetC4Placement().GetTargetCenter(),false);if (Now-S->At<.4) return true;
                if (F->GetC4Placement().bTracked && Now-S->At<6.) return true;
                Check(Charge && Charge->IsHidden() && F->GetC4Placement().Progress==0 && !F->bBreachRequested,TEXT("Tracking loss hides C4 and cannot attach it"));
                S->Step=4;S->At=Now;return true;
            }
            if (S->Step==4)
            {
                Send(F->GetC4Placement().GetTargetCenter(),true);if (!F->bBreachRequested) return true;
                Check(Charge && Charge->GetRootComponent()->GetAttachParent()==Door->GetIntactDoorMesh(),TEXT("Central placement attaches the actual C4 mesh to the door"));
                FVector2D Screen;int32 X=0,Y=0;PC->GetViewportSize(X,Y);
                const FVector2D Target=FOBBreachGuideLayout::MapPoint(F->GetC4Placement().GetTargetCenter(),FOBBreachGuideLayout::CameraRect(FVector2D(X,Y),FVector2D(640,480)));
                Check(Charge && PC->ProjectWorldLocationToScreen(Charge->GetChargeMesh()->Bounds.Origin,Screen) && FVector2D::Distance(Screen,Target)<15.f,TEXT("The planted C4 stays at the raised target instead of snapping down the door"));
                Check(!Door->IsBreachInProgress() && !Door->IsBreached() && Door->GetIntactDoorMesh()->IsVisible(),TEXT("Door remains intact immediately after C4 attachment"));
                Capture(TEXT("Attached-C4.png"));S->Step=5;return true;
            }
            if (S->Step==5)
            {
                if (FParse::Param(FCommandLine::Get(),TEXT("Zone6RevisionCheck")) && F->GetPhase()==EOBStagePhase::Travel && R->GetCurrentPointIndex()==85)
                {
                    // Compare rendered frame positions. RailVelocity also includes the
                    // correction of CharacterMovement gravity before the rail tick.
                    const double SampleTime=W->GetTimeSeconds();
                    if (S->PreviousEntryTime>0 && SampleTime>S->PreviousEntryTime)
                    {
                        const float Speed=FVector::Distance(P->GetActorLocation(),S->PreviousEntryLocation)/(SampleTime-S->PreviousEntryTime);
                        S->MaxEntrySpeed=FMath::Max(S->MaxEntrySpeed,Speed);S->SawEntryMotion |= Speed>1.f;
                    }
                    S->PreviousEntryLocation=P->GetActorLocation();S->PreviousEntryTime=SampleTime;
                    const FVector V=R->GetRailVelocity(),L=P->GetActorLocation();
                    S->EntrySamples+=FString::Printf(TEXT("%.4f,%.4f,%.1f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n"),W->GetTimeSeconds(),R->GetDistanceAlongRoute(),F->GetTravelMoveSpeed(500.f),V.X,V.Y,V.Z,L.X,L.Y,L.Z);
                    if (!S->EntryChecked)
                    {
                        Check(Door->IsPassageClear(),TEXT("Door leaf fragments and their collision are removed before visible entry"));
                        Check(F->GetTravelMoveSpeed(500.f)==300.f && F->IsInputBlocked(),TEXT("Breach entry uses 300 cm/s and retains the slow-motion input hold"));
                        int32 Allies=0;bool Capped=true;
                        for (TActorIterator<AOBSquadRendezvous> It(W);It;++It) if (It->DepartAfterZone==6)
                            for (const auto& Member:It->Members) { ++Allies;Capped &= It->MoveSpeedLimit==300.f; }
                        Check(Allies==2 && Capped,TEXT("Both Zone6 entry allies retain formation pacing under a 300 cm/s cap"));
                        S->EntryChecked=true;
                    }
                }
                const bool Black=PC->PlayerCameraManager && PC->PlayerCameraManager->FadeAmount>=.999f;
                if (!Black && !S->SawPhysics) S->IntactBeforeBlack &= !Door->IsBreachInProgress() && !Door->IsBreached() && Door->GetIntactDoorMesh()->IsVisible();
                if (Black && !S->SawBlack)
                {
                    TArray<FColor> Pixels;auto* View=W->GetGameViewport()->Viewport;
                    bool RenderedBlack=View && View->ReadPixels(Pixels) && !Pixels.IsEmpty();
                    for (int32 I=0;I<Pixels.Num();I+=37) RenderedBlack &= Pixels[I].R<8 && Pixels[I].G<8 && Pixels[I].B<8;
                    if (RenderedBlack) { S->SawBlack=true;Capture(TEXT("Full-Black.png"),false); }
                }
                if (F->bBreachPhysicsStarted && !S->SawPhysics)
                {
                    S->SawPhysics=true;S->FirstPhysicsFrame=GFrameCounter;
                    Check(Black && F->BreachBlackFrame<GFrameCounter && S->IntactBeforeBlack,TEXT("Actual Chaos destruction starts after a fully black frame, never during fade-out"));
                }
                if (F->GetPhase()!=EOBStagePhase::Zone6Intro) return true;
                Check(S->SawBlack && S->SawPhysics && F->bBreachCompleted && Door->IsBreached(),TEXT("Rendered blackout, real Chaos root break and Zone6 rescue entry all complete"));
                Check(S->IntactBeforeBlack && R->GetCurrentPointIndex()==86,TEXT("Normal rail travel reaches the rescue scene only after the new C4 gate"));
                Capture(TEXT("After-Breach.png"));
                S->Step=6;S->At=Now;return true;
            }
            if (S->Step==6 && Now-S->At>(FParse::Param(FCommandLine::Get(),TEXT("Zone6RevisionCheck")) ? 2.8 : .3))
            {
                if (FParse::Param(FCommandLine::Get(),TEXT("Zone6RevisionCheck")))
                {
                    Check(S->EntryChecked && S->SawEntryMotion && S->MaxEntrySpeed<=300.5f,TEXT("Actual player entry movement stays at or below 300 cm/s"));
                    S->Log+=FString::Printf(TEXT("max_entry_speed_cm_s=%.3f\n"),S->MaxEntrySpeed);
                    int32 Far=0,Near=0;bool Facing=true;
                    for (TActorIterator<AOBEncounterSpawner> It(W);It;++It) if (It->GetPreparedPointIndex()==86 && It->bParticipatesInCombat)
                    {
                        auto* Enemy=It->GetSpawnedEnemy();auto* AI=Enemy ? Cast<AOBAIController>(Enemy->GetController()) : nullptr;
                        auto* Target=AI ? AI->GetZone6FacingTarget() : nullptr;
                        Facing &= AI && Target && Enemy;
                        if (AI && Target)
                        {
                            const bool PlayerTarget=Target==P;
                            S->Log+=FString::Printf(TEXT("guard_after name=%s target=%s far=%d yaw=%.2f dot=%.4f\n"),*GetNameSafe(Enemy),*GetNameSafe(Target),AI->IsZone6FarGuard(),Enemy->GetActorRotation().Yaw,FVector::DotProduct(Enemy->GetActorForwardVector(),(Target->GetActorLocation()-Enemy->GetActorLocation()).GetSafeNormal2D()));
                            Facing &= PlayerTarget==AI->IsZone6FarGuard();
                            Facing &= FVector::DotProduct(Enemy->GetActorForwardVector(),(Target->GetActorLocation()-Enemy->GetActorLocation()).GetSafeNormal2D())>.95f;
                            if (PlayerTarget) ++Far;else ++Near;
                        }
                    }
                    Check(Facing && Far==2 && Near==2,TEXT("During slow motion the two far guards turn to the player while the two close guards keep facing their hostages"));
                    Capture(TEXT("Zone6-GuardFacing.png"));
                }
                Finish();return false;
            }
            return true;
        }),0.f);
    }
};
static FAutoConsoleCommand GOBC4BreachMapSmoke(TEXT("OB.Stage.C4BreachPIE"),TEXT("Check single-hand C4 placement and real destruction only after full black."),FConsoleCommandDelegate::CreateStatic(&FOBC4BreachMapSmoke::Start));
#endif
