#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "TimerManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "UObject/UnrealType.h"
#include "InputKeyEventArgs.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/OBPlayerController.h"
#include "Core/OBQACheatManager.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "World/OBRailPath.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBEncounterSpawner.h"
#include "Character/OBEnemyCharacter.h"
#include "Widget/OBHUD.h"
#include "Stage/OBStageFlowComponent.h"

// Disposable verification editor only. Does not save or modify authored stop settings.
class FOBQACameraTuningMapSmoke
{
public:
    static void Start()
    {
        if (!GEditor) return;
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (W && W->GetTimeSeconds()>.3f && Player && Player->GetPlayerSplineComponent()->GetRailPath()
                && Cast<UOBQACheatManager>(PC->CheatManager)) { Run(W,PC);return false; }
            if (FPlatformTime::Seconds()-Started>90) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.1f);
    }
private:
    static FString AuthoredStops(AOBRailPath* Rail)
    {
        FString Text;auto* P=FindFProperty<FMapProperty>(AOBRailPath::StaticClass(),TEXT("StopPointSettings"));
        P->ExportText_InContainer(0,Text,Rail,Rail,Rail,PPF_None);return Text;
    }
    static TArray<TWeakObjectPtr<AOBEnemyCharacter>> ZoneActors(UWorld* W,int32 Point)
    {
        TArray<TWeakObjectPtr<AOBEnemyCharacter>> Actors;
        for (TActorIterator<AOBEncounterTrigger> It(W);It;++It) if (It->GetPointIndex()==Point)
            for (AOBEncounterSpawner* S : It->Spawners) if (IsValid(S) && IsValid(S->GetSpawnedEnemy())) Actors.Add(S->GetSpawnedEnemy());
        return Actors;
    }
    static void Run(UWorld* W,AOBPlayerController* PC)
    {
        struct FState
        {
            int32 Zone=1,Phase=0,Cycles=0,Destroyed=0;bool bOK=true,bDamageable=true,bAdvance=true;
            uint8 MovementMode=0;
            FVector Location;
            FString OriginalStops,Lines;
            FTimerHandle Timer;
            TArray<TWeakObjectPtr<AOBEnemyCharacter>> Actors;
        };
        auto S=MakeShared<FState>();auto* Player=CastChecked<AOBPlayerCharacter>(PC->GetPawn());
        Player->GetStageFlowComponent()->SuspendForQA();
        auto* C=Player->GetPlayerSplineComponent();
        S->OriginalStops=AuthoredStops(C->GetRailPath());S->bDamageable=Player->CanBeDamaged();
        S->bAdvance=Player->GetActionComponent()->bAutoAdvanceOnEnemyClear;S->MovementMode=Player->GetCharacterMovement()->MovementMode;
        TWeakObjectPtr<UWorld> WeakWorld=W;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,WeakWorld]() {
            auto* World=WeakWorld.Get();if (!World) return;
            auto* PC=CastChecked<AOBPlayerController>(World->GetFirstPlayerController());
            auto* Player=CastChecked<AOBPlayerCharacter>(PC->GetPawn());auto* C=Player->GetPlayerSplineComponent();
            auto* Cheats=CastChecked<UOBQACheatManager>(PC->CheatManager);FString Reply;
            const int32 Points[]={7,19,44,64,72,86},Counts[]={5,7,7,5,8,6};
            if (S->Zone>6)
            {
                S->bOK &= AuthoredStops(C->GetRailPath())==S->OriginalStops && S->Cycles==12 && S->Destroyed==76;
                FString Report=FString::Printf(TEXT("outcome=%s zones=6 spawn_cycles=%d destroyed=%d\n"),S->bOK ? TEXT("PASS") : TEXT("FAIL"),S->Cycles,S->Destroyed)+S->Lines;
                FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/TEXT("Build/Testing/Verification/QACameraLifecycle/runtime.txt")));
                UE_LOG(LogTemp,Display,TEXT("QA_CAMERA_TUNING_PIE %s"),*Report);
                World->GetTimerManager().ClearTimer(S->Timer);GEditor->RequestEndPlayMap();
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK=S->bOK](float) {
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),1.f);
                return;
            }
            auto EndAndCheck=[&]() {
                PC->ConsoleCommand(TEXT("QA Camera End"),true);
                S->bOK &= !C->IsCameraTuningForQA() && !C->IsCameraEditingForQA() && ZoneActors(World,Points[S->Zone-1]).IsEmpty()
                    && !Player->GetActionComponent()->HasEncounterEnemies();
                for (auto Enemy : S->Actors) S->bOK &= !Enemy.IsValid();
                S->Destroyed+=S->Actors.Num();
                S->bOK &= Player->CanBeDamaged()==S->bDamageable && Player->GetActionComponent()->bAutoAdvanceOnEnemyClear==S->bAdvance
                    && Player->GetCharacterMovement()->MovementMode==S->MovementMode;
            };
            if (S->Phase==1)
            {
                S->bOK &= Player->GetActorLocation().Equals(S->Location,.1) && C->IsCameraEditingForQA()
                    && !Player->CanBeDamaged() && !Player->GetActionComponent()->bAutoAdvanceOnEnemyClear;
                EndAndCheck();S->Phase=2;return;
            }
            S->bDamageable=Player->CanBeDamaged();S->bAdvance=Player->GetActionComponent()->bAutoAdvanceOnEnemyClear;
            S->MovementMode=Player->GetCharacterMovement()->MovementMode;
            // Use the same console Exec dispatcher and InputKey path as a tester.
            PC->ConsoleCommand(FString::Printf(TEXT("QA Zone %d"),S->Zone),true);
            S->bOK &= C->IsCameraTuningForQA() && C->IsAtActionPoint() && C->GetCurrentPointIndex()==Points[S->Zone-1];
            S->bOK &= !C->IsCameraEditingForQA() && !Cheats->HandleCameraTuningKey(EKeys::One,true);
            S->Actors=ZoneActors(World,Points[S->Zone-1]);++S->Cycles;
            S->bOK &= S->Actors.Num()==Counts[S->Zone-1];
            PC->ConsoleCommand(TEXT("QA Camera Start"),true);S->bOK &= C->IsCameraEditingForQA();
            if (S->Phase==2)
            {
                EndAndCheck();UE_LOG(LogTemp,Display,TEXT("QA_CAMERA_RESPAWN zone=%d count=%d ok=%d"),S->Zone,S->Actors.Num(),S->bOK);
                ++S->Zone;S->Phase=0;return;
            }
            if (S->Zone<=6)
            {
                const float FOVs[]={112,90,115,81,79,111},Pitches[]={23,17,16,0,0,-1},Yaws[]={-20,10,3,5,0,-4};
                const auto* Stop=C->GetRailPath()->FindStopSettings(Points[S->Zone-1]);
                S->bOK &= Stop && Stop->bOverrideCameraView && Stop->CameraFOV==FOVs[S->Zone-1]
                    && Stop->CameraRotationOffset.Equals(FRotator(Pitches[S->Zone-1],Yaws[S->Zone-1],0),.01);
            }
            auto* Camera=Player->GetPlayerCamera();const float StartFOV=Camera->FieldOfView;
            const float StartPitch=Camera->GetRelativeRotation().Pitch;
            auto Key=[&](FKey Key) {
                const auto Device=FInputDeviceId::CreateFromInternalId(0);
                bool OK=PC->InputKey(FInputKeyEventArgs(World->GetGameViewport()->Viewport,Device,Key,IE_Pressed,FPlatformTime::Cycles64()));
                OK &= PC->InputKey(FInputKeyEventArgs(World->GetGameViewport()->Viewport,Device,Key,IE_Released,FPlatformTime::Cycles64()));
                return OK;
            };
            S->bOK &= Key(EKeys::One) && FMath::IsNearlyEqual(Camera->FieldOfView,StartFOV+1,.01f);
            S->bOK &= Key(EKeys::Two) && FMath::IsNearlyEqual(Camera->FieldOfView,StartFOV,.01f);
            S->bOK &= Key(EKeys::Three) && FMath::IsNearlyEqual(float(Camera->GetRelativeRotation().Pitch),StartPitch+1,.01f);
            S->bOK &= Key(EKeys::Four) && FMath::IsNearlyEqual(float(Camera->GetRelativeRotation().Pitch),StartPitch,.01f);
            const float Yaw=Camera->GetRelativeRotation().Yaw,Roll=Camera->GetRelativeRotation().Roll;
            S->bOK &= Key(EKeys::Five) && FMath::IsNearlyEqual(float(Camera->GetRelativeRotation().Yaw),Yaw+1,.01f);
            S->bOK &= Key(EKeys::Six) && FMath::IsNearlyEqual(float(Camera->GetRelativeRotation().Yaw),Yaw,.01f);
            S->bOK &= Key(EKeys::Seven) && FMath::IsNearlyEqual(float(Camera->GetRelativeRotation().Roll),Roll+1,.01f);
            S->bOK &= Key(EKeys::Eight) && FMath::IsNearlyEqual(float(Camera->GetRelativeRotation().Roll),Roll,.01f);
            S->bOK &= Key(EKeys::One) && Key(EKeys::Four);
            FMinimalViewInfo View;Player->CalcCamera(1.f/60,View);
            S->bOK &= FMath::IsNearlyEqual(View.FOV,StartFOV+1,.01f) && Player->GetWeaponMesh()->bOwnerNoSee
                && (!PC->GetOBHUD() || !PC->GetOBHUD()->IsMenuVisible());
            S->bOK &= Cheats->ExecuteCommand(TEXT("Camera"),Reply) && Reply.Contains(TEXT("CameraRotationOffset="));
            S->Lines+=Reply+TEXT("\n");S->Location=Player->GetActorLocation();S->Phase=1;
            S->bOK &= AuthoredStops(C->GetRailPath())==S->OriginalStops;
            UE_LOG(LogTemp,Display,TEXT("QA_CAMERA_ZONE zone=%d fov=%.2f pitch=%.2f ok=%d"),S->Zone,View.FOV,Camera->GetRelativeRotation().Pitch,S->bOK);
        }),1.f,true);
    }
};
static FAutoConsoleCommand GOBQACameraTuningPIE(TEXT("OB.QA.CameraTuningPIE"),
    TEXT("Verify QA Zone 1..6 and camera keys in a disposable editor, then exit without saving."),
    FConsoleCommandDelegate::CreateStatic(&FOBQACameraTuningMapSmoke::Start));
#endif
