#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "UnrealClient.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/Facades/CollectionAnchoringFacade.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameModeBase.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "Core/OBPlayerController.h"
#include "Stage/OBStageFlowComponent.h"
#include "World/OBBreachDoor.h"

// Exercises the user-facing console commands and real map reloads, without modifying saved assets.
class FOBQABreachMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;
        Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        struct FState
        {
            int32 Step=0;bool OK=true;double Started=FPlatformTime::Seconds();
            TWeakObjectPtr<UWorld> PreviousWorld;
            FString Log;
            bool bSawBlack=false,bHeldStill=true,bSawFadeIn=false;
            FVector BreachPosition;
            TMap<int32,FVector> WallBefore;
            TSet<int32> WallAnchors;
            double CombatStarted=0;
        };
        const auto State=MakeShared<FState>();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) {
            auto Check=[State](bool Good,const TCHAR* Label) {
                State->OK&=Good;
                State->Log+=FString::Printf(TEXT("%s %s\n"),Good ? TEXT("PASS") : TEXT("FAIL"),Label);
            };
            auto Finish=[State]() {
                State->Log+=State->OK ? TEXT("outcome=PASS\n") : TEXT("outcome=FAIL\n");
                const FString Output=FPaths::ProjectDir()/TEXT("Build/Testing/Verification/Zone6Assets");
                IFileManager::Get().MakeDirectory(*Output,true);
                FFileHelper::SaveStringToFile(State->Log,*(Output/TEXT("qa-breach-pie.txt")));
                UE_LOG(LogTemp,Display,TEXT("QA_BREACH_PIE\n%s"),*State->Log);
                GEditor->RequestEndPlayMap();
                const bool OK=State->OK;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.5f);
            };
            if (FPlatformTime::Seconds()-State->Started>240.)
            { Check(false,TEXT("Console checkpoint reloads and progression meet the deadline"));Finish();return false; }
            auto* World=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=World ? Cast<AOBPlayerController>(World->GetFirstPlayerController()) : nullptr;
            auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (!Player || World->GetTimeSeconds()<.5f || !Player->GetPlayerSplineComponent()->GetRailPath()) return true;
            auto* Flow=Player->GetStageFlowComponent();auto* Rail=Player->GetPlayerSplineComponent();
            auto* Action=Player->GetActionComponent();
            AOBBreachDoor* Door=nullptr;
            for (TActorIterator<AOBBreachDoor> It(World);It;++It) { Door=*It;break; }
            auto Reload=[State,World,PC](const TCHAR* Command,int32 NextStep) {
                State->PreviousWorld=World;State->Step=NextStep;
                PC->ConsoleCommand(Command);
            };
            const bool bNewWorld=World!=State->PreviousWorld.Get();
            if (State->Step==0)
            {
                Check(Flow->GetPhase()==EOBStagePhase::Opening,TEXT("Ordinary PIE initially uses the production opening"));
                Reload(TEXT("QA Breach"),1);return true;
            }
            if (State->Step==1 && bNewWorld && Flow->IsWaitingForBreach())
            {
                if (World->GetTimeSeconds()<1.5f) return true;
                Check(Rail->GetCurrentPointIndex()==85 && Rail->IsAtActionPoint() && !Rail->IsCameraTuningForQA(),
                    TEXT("QA Breach reloads directly into live stop 85, outside camera-tuning mode"));
                Check(Door && !Door->IsBreached() && !Door->IsBreachInProgress(),TEXT("The default checkpoint starts with an intact Chaos door"));
                Check(Flow->IsInputBlocked() && Flow->GetRemainingSeconds()==0.f && Action->GetEncounterEnemies().IsEmpty(),
                    TEXT("Waiting at 85 blocks combat and keeps the hostage clock stopped"));
                Check(FMath::IsNearlyEqual(Player->GetPlayerCamera()->FieldOfView,80.f),TEXT("The checkpoint applies the saved stop lens immediately"));
                Check(Door && Door->FracturedWallAsset,TEXT("The saved breach door references an actual wall fracture collection"));
                if (Door && Door->FracturedWallAsset)
                {
                    const auto Rest=Door->FracturedWallAsset->GetGeometryCollection();
                    const Chaos::Facades::FCollectionAnchoringFacade Anchors(static_cast<const FManagedArrayCollection&>(*Rest));
                    TArray<FTransform3f> Global;GeometryCollectionAlgo::GlobalMatrices(Rest->Transform,Rest->Parent,Global);
                    for (int32 I=0;I<Rest->SimulationType.Num();++I)
                        if (Rest->IsRigid(I))
                        {
                            State->WallBefore.Add(I,Door->GetFrameMesh()->GetComponentTransform().TransformPosition(FVector(Global[I].GetLocation())));
                            if (Anchors.IsAnchored(I)) State->WallAnchors.Add(I);
                        }
                }
                if (!State->OK) { Finish();return false; }
                State->BreachPosition=Player->GetActorLocation();
                PC->ConsoleCommand(TEXT("QA Breach Trigger"));State->Step=2;return true;
            }
            if (State->Step==2 && PC->PlayerCameraManager)
            {
                auto* Camera=PC->PlayerCameraManager.Get();
                State->bSawBlack|=Camera->bEnableFading && Camera->FadeAmount>.95f;
                if (Flow->GetPhase()==EOBStagePhase::Zone6Breach)
                    State->bHeldStill&=Player->GetActorLocation().Equals(State->BreachPosition,.1f) && Rail->GetCurrentPointIndex()==85;
                if (Flow->GetPhase()==EOBStagePhase::Travel)
                {
                    State->bSawFadeIn|=Camera->bEnableFading && Camera->FadeAmount>0.f && Camera->FadeAmount<.99f;
                }
            }
            if (State->Step==2 && Flow->GetPhase()==EOBStagePhase::Combat && Rail->GetCurrentPointIndex()==86)
            {
                Check(Door && Door->IsBreached(),TEXT("The trigger command completes an actual Chaos door break before travel"));
                Check(Action->GetEncounterEnemies().Num()==4 && Flow->GetRemainingSeconds()>9.f && Flow->GetRemainingSeconds()<=10.f,
                    TEXT("The real 86 encounter starts with four combatants and the normal hostage timer"));
                bool Slow=true;
                for (AOBEnemyCharacter* Enemy : Action->GetEncounterEnemies()) Slow&=Enemy && FMath::IsNearlyEqual(Enemy->CustomTimeDilation,1.f);
                Check(Slow,TEXT("Live progression ends intro slow motion before the rescue timer"));
                Check(State->bSawBlack && State->bHeldStill && State->bSawFadeIn,
                    TEXT("The camera blacks out while holding 85, then fades back in during departure"));
                State->Log+=FString::Printf(TEXT("fade_black=%d held_still=%d fade_in_during_departure=%d\n"),State->bSawBlack,State->bHeldStill,State->bSawFadeIn);
                Check(Door && Door->GetFracturedWall()->IsRootBroken(),TEXT("Both the wall and leaf are physically broken before combat"));
                int32 Moved=0,Fixed=0;
                if (Door)
                {
                    auto* Wall=Door->GetFracturedWall();const auto& Current=Wall->GetComponentSpaceTransforms3f();
                    for (const auto& Pair : State->WallBefore)
                        if (Current.IsValidIndex(Pair.Key))
                        {
                            const FVector Now=Wall->GetComponentTransform().TransformPosition(FVector(Current[Pair.Key].GetLocation()));
                            const double Travel=FVector::Distance(Now,Pair.Value);
                            if (State->WallAnchors.Contains(Pair.Key)) { if (Travel<1.) ++Fixed; }
                            else if (Travel>5.) ++Moved;
                        }
                }
                Check(Moved>=2 && Fixed==State->WallAnchors.Num() && Fixed>0,
                    TEXT("Inner wall fragments fly while every authored outer wall anchor stays fixed"));
                State->Log+=FString::Printf(TEXT("wall_moving=%d wall_fixed=%d wall_anchors=%d\n"),Moved,Fixed,State->WallAnchors.Num());
                State->CombatStarted=World->GetTimeSeconds();State->Step=20;return true;
            }
            if (State->Step==20 && World->GetTimeSeconds()-State->CombatStarted>4.2)
            {
                int32 Hostages=0;TSet<FString> Meshes;
                for (TActorIterator<AOBHostageCharacter> It(World);It;++It)
                {
                    auto* Mesh=It->GetMesh();auto* Animation=Mesh->GetSingleNodeInstance();
                    Check(Mesh->IsVisible() && !Mesh->bHiddenInGame && Mesh->GetSkeletalMeshAsset() && Animation
                        && Animation->IsPlaying() && Animation->IsLooping() && Animation->GetCurrentTime()<1.5f,
                        TEXT("Each full-body hostage is visible and continues looping past the four-second clip boundary"));
                    if (Mesh->GetSkeletalMeshAsset()) Meshes.Add(Mesh->GetSkeletalMeshAsset()->GetPathName());
                    ++Hostages;
                }
                Check(Hostages==2 && Meshes.Num()==2,TEXT("The actual saved encounter spawns the two distinct requested hostage meshes"));
                Check(PC->PlayerCameraManager && !PC->PlayerCameraManager->bEnableFading,TEXT("The short breach fade cannot remain over gameplay"));
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Build/Testing/Verification/Zone6Assets/hostages-stage.png"),true,false);
                State->Step=21;return true;
            }
            if (State->Step==21 && World->GetTimeSeconds()-State->CombatStarted>4.5)
            {
                Reload(TEXT("QA Breach 83"),3);return true;
            }
            if (State->Step==3 && bNewWorld && Flow->GetPhase()==EOBStagePhase::Zone6Dialogue)
            {
                Check(Rail->GetCurrentPointIndex()==83 && Flow->IsInputBlocked() && Flow->GetRemainingSeconds()==0.f,
                    TEXT("QA Breach 83 enters the production dialogue hold without traversing earlier zones"));
                Check(Door && !Door->IsBreached() && !Door->IsBreachInProgress(),TEXT("Re-entering 83 restores the previously destroyed door"));
                Check(Action->GetEncounterEnemies().IsEmpty() && FMath::Abs(FMath::FindDeltaAngleDegrees(Player->GetActorRotation().Yaw,90.f))<1.f,
                    TEXT("Dialogue checkpoint applies the corridor facing without spawning the final encounter"));
                PC->ConsoleCommand(TEXT("QA Breach Trigger"));
                Check(Flow->GetPhase()==EOBStagePhase::Zone6Dialogue && Door && !Door->IsBreachInProgress(),
                    TEXT("The test trigger cannot skip the 83 dialogue gate"));
                Reload(TEXT("QA Breach 85"),4);return true;
            }
            if (State->Step==4 && bNewWorld && Flow->IsWaitingForBreach())
            {
                Check(Rail->GetCurrentPointIndex()==85 && Door && !Door->IsBreached() && !Door->IsBreachInProgress(),
                    TEXT("Explicit QA Breach 85 can repeat a clean breach test after another checkpoint"));
                Reload(TEXT("QA Reset"),5);return true;
            }
            if (State->Step==5 && bNewWorld)
            {
                const auto* Mode=World->GetAuthGameMode();
                Check(Mode && UGameplayStatics::ParseOption(Mode->OptionsString,TEXT("OBQABreach")).IsEmpty(),
                    TEXT("QA Reset drops the checkpoint URL option rather than silently re-entering QA"));
                Check(Flow->GetPhase()==EOBStagePhase::Opening && Flow->GetCurrentZone()==0 && Rail->GetCurrentPointIndex()==0,
                    TEXT("QA Reset restores ordinary opening and first-point startup"));
                Finish();return false;
            }
            return true;
        }),.05f);
    }
};
static FAutoConsoleCommand GOBQABreachPIE(TEXT("OB.QA.BreachPIE"),
    TEXT("Checks the live QA Breach console checkpoints, simulated input, repeated reloads and QA Reset. Exits this editor."),
    FConsoleCommandDelegate::CreateStatic(&FOBQABreachMapSmoke::Start));
#endif
