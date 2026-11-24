#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "InputKeyEventArgs.h"
#include "Components/SplineComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/OBQACheatManager.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "World/OBRailPath.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBEncounterSpawner.h"
#include "Character/OBEnemyCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBQACameraTuningTest,"OperationBreakpoint.QA.ZoneCameraTuning",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBQACameraTuningTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,TEXT("UEDPIE_92_Stage_01"),CreatePackage(TEXT("/Game/Maps/UEDPIE_92_Stage_01")));
    W->StreamingLevelsPrefix=TEXT("UEDPIE_92_");
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT { W->DestroyWorld(false);GEngine->DestroyWorldContext(W); };
    auto* PC=W->SpawnActor<AOBPlayerController>();auto* Player=W->SpawnActor<AOBPlayerCharacter>();PC->Possess(Player);
    auto* Cheats=NewObject<UOBQACheatManager>(PC);
    FindFProperty<FObjectPropertyBase>(APlayerController::StaticClass(),TEXT("CheatManager"))->SetObjectPropertyValue_InContainer(PC,Cheats);
    auto* Rail=W->SpawnActor<AOBRailPath>();auto* Curve=Rail->GetSpline();Curve->ClearSplinePoints(false);
    for (int32 I=0;I<=86;++I) Curve->AddSplinePoint(FVector(I*100,0,I*5),ESplineCoordinateSpace::World,false);
    Curve->UpdateSpline();
    auto& Stops=*FindFProperty<FMapProperty>(AOBRailPath::StaticClass(),TEXT("StopPointSettings"))
        ->ContainerPtrToValuePtr<TMap<int32,FOBRailStopSettings>>(Rail);
    Stops.Empty();for (int32 I : {7,19,44,64,72,86}) Stops.Add(I);
    Stops[19].bOverrideCameraView=true;Stops[19].CameraFOV=50;Stops[19].CameraRotationOffset=FRotator(-10,5,2);
    Stops[7].bOverrideFacingYaw=true;Stops[7].FacingYaw=45;
    AOBEncounterTrigger* FirstTrigger=nullptr;
    for (int32 Point : {7,19,44,64,72,86})
    {
        auto* Trigger=W->SpawnActor<AOBEncounterTrigger>();Trigger->PointIndex=Point;
        if (Point==7) FirstTrigger=Trigger;
    }
    auto* Spawner=W->SpawnActor<AOBEncounterSpawner>();Spawner->SetActorLocation(FVector(900,300,0));
    Spawner->bMoveToStagingPoint=false;Spawner->bOverrideEnemyBehavior=true;Spawner->EnemyBehavior.bStartStateTreeAfterReaction=false;
    FindFProperty<FClassProperty>(AOBEncounterSpawner::StaticClass(),TEXT("EnemyClass"))->SetObjectPropertyValue_InContainer(Spawner,AOBEnemyCharacter::StaticClass());
    FirstTrigger->Spawners.Add(Spawner);
    const auto Authored=Stops;
    auto* C=Player->GetPlayerSplineComponent();auto* Camera=Player->GetPlayerCamera();auto* Action=Player->GetActionComponent();
    C->PlayerCharacter=Player;C->PlayerCamera=Camera;C->RailPath=Rail;
    C->FirstPersonCameraLocation=Camera->GetRelativeLocation();C->FirstPersonCameraRotation=Camera->GetRelativeRotation();
    FindFProperty<FObjectPropertyBase>(UOBActionComponent::StaticClass(),TEXT("PlayerSplineComponent"))->SetObjectPropertyValue_InContainer(Action,C);
    Action->SetExternalActionState(true);
    C->OnActionPointReached.AddLambda([Action](int32){Action->SetExternalActionState(true);});
    auto* Movement=Player->GetCharacterMovement();const auto SavedMode=Movement->MovementMode;
    const bool SavedDamageable=Player->CanBeDamaged(),SavedAdvance=Action->bAutoAdvanceOnEnemyClear;
    FString Reply;
    TestFalse(TEXT("Hotkeys are inactive during ordinary gameplay"),Cheats->HandleCameraTuningKey(EKeys::One,true));
    for (const TCHAR* Command : {TEXT("Zone"),TEXT("Zone 0"),TEXT("Zone 7"),TEXT("Zone 1.5"),TEXT("Zone 1 2"),TEXT("Camera Bad")})
        TestFalse(TEXT("Malformed camera commands cannot start tuning"),Cheats->ExecuteCommand(Command,Reply));
    TestTrue(TEXT("Help explains zone selection and four keys"),Cheats->ExecuteCommand(TEXT("Help"),Reply)
        && Reply.Contains(TEXT("QA Zone")) && Reply.Contains(TEXT("1/2")) && Reply.Contains(TEXT("QA Camera End")));
    TestTrue(TEXT("Zone 1 enters tuning in a PIE-prefixed Stage_01"),Cheats->ExecuteCommand(TEXT("Zone 1"),Reply));
    TestTrue(TEXT("Zone command spawns a fresh enemy"),IsValid(Spawner->GetSpawnedEnemy()));
    TWeakObjectPtr<AOBEnemyCharacter> FirstSpawn=Spawner->GetSpawnedEnemy();
    TestFalse(TEXT("Zone selection alone does not enable adjustment keys"),Cheats->HandleCameraTuningKey(EKeys::One,true));
    TestFalse(TEXT("Native adjustments are blocked before Start"),C->AdjustCameraForQA(1,0));
    TestTrue(TEXT("Explicit Start enables adjustment"),Cheats->ExecuteCommand(TEXT("Camera Start"),Reply) && C->IsCameraEditingForQA());
    TestTrue(TEXT("Tuning holds the action point without gravity or damage"),C->IsAtActionPoint()
        && Movement->MovementMode==MOVE_None && !Player->CanBeDamaged() && !Action->bAutoAdvanceOnEnemyClear);
    FVector Feet=Player->GetActorLocation();Feet.Z-=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    TestTrue(TEXT("The command teleports to the authored stop XYZ"),Feet.Equals(Curve->GetLocationAtSplinePoint(7,ESplineCoordinateSpace::World),.01));
    TestTrue(TEXT("The command applies this stop's authored combat yaw"),FMath::IsNearlyEqual(Player->GetActorRotation().Yaw,45.f,.01f));
    auto Key=[&](FKey K,EInputEvent Event=IE_Pressed) {
        return PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,Event,FPlatformTime::Cycles64()));
    };
    TestTrue(TEXT("Controller key 1 immediately increases FOV"),Key(EKeys::One) && Camera->FieldOfView==71.f);
    TestTrue(TEXT("Controller key 2 immediately decreases FOV"),Key(EKeys::Two) && Camera->FieldOfView==70.f);
    TestTrue(TEXT("Held-key repeat adjusts once per repeat event"),Key(EKeys::One,IE_Repeat) && Camera->FieldOfView==71.f);
    TestTrue(TEXT("Release is consumed without a second adjustment"),Key(EKeys::One,IE_Released) && Camera->FieldOfView==71.f);
    TestTrue(TEXT("Controller key 3 raises pitch"),Key(EKeys::Three) && FMath::IsNearlyEqual(Camera->GetRelativeRotation().Pitch,1.0,.01));
    TestTrue(TEXT("Controller key 4 lowers pitch"),Key(EKeys::Four) && Camera->GetRelativeRotation().Equals(C->FirstPersonCameraRotation,.01));
    TestTrue(TEXT("Keys 5/6 adjust yaw in both directions"),Key(EKeys::Five) && FMath::IsNearlyEqual(Camera->GetRelativeRotation().Yaw,1.0,.01)
        && Key(EKeys::Six) && FMath::IsNearlyZero(Camera->GetRelativeRotation().Yaw,.01));
    TestTrue(TEXT("Keys 7/8 adjust roll in both directions"),Key(EKeys::Seven) && FMath::IsNearlyEqual(Camera->GetRelativeRotation().Roll,1.0,.01)
        && Key(EKeys::Eight) && FMath::IsNearlyZero(Camera->GetRelativeRotation().Roll,.01));
    TestFalse(TEXT("Unassigned keys are not consumed by the tuner"),Cheats->HandleCameraTuningKey(EKeys::Nine,true));
    const FVector HeldLocation=Player->GetActorLocation();
    C->StartMoveToNextPoint();Action->CompleteAction();C->TickComponent(2,LEVELTICK_All,&C->PrimaryComponentTick);
    TestTrue(TEXT("Neither travel nor action completion can leave the selected stop"),C->IsAtActionPoint()
        && Action->GetActionState()==EOBActionState::Action && Player->GetActorLocation().Equals(HeldLocation));
    TestEqual(TEXT("Tick does not overwrite the live FOV"),Camera->FieldOfView,71.f);
    PC->SetTrainingAmmo(7,30);PC->CompleteReload();
    TestEqual(TEXT("Gameplay reload input is blocked while tuning"),PC->GetCurrentAmmo(),7);
    TestTrue(TEXT("Final output contains reproducible stop settings"),Cheats->ExecuteCommand(TEXT("Camera"),Reply)
        && Reply.Contains(TEXT("Zone=1 Point=7")) && Reply.Contains(TEXT("CameraFOV=71.00")) && Reply.Contains(TEXT("CameraRotationOffset=(Pitch=")));
    C->AdjustCameraForQA(10000,10000);
    TestTrue(TEXT("FOV and pitch respect upper limits"),Camera->FieldOfView==170.f && FMath::IsNearlyEqual(Camera->GetRelativeRotation().Pitch,85.0,.01));
    C->AdjustCameraForQA(-10000,-10000);
    TestTrue(TEXT("FOV and pitch respect lower limits"),Camera->FieldOfView==5.f && FMath::IsNearlyEqual(Camera->GetRelativeRotation().Pitch,-85.0,.01));
    TestTrue(TEXT("Switching zones starts from that stop's authored view"),Cheats->ExecuteCommand(TEXT("Zone 2"),Reply)
        && Camera->FieldOfView==50.f && Camera->GetRelativeRotation().Equals(FRotator(-10,5,2),.01));
    TestTrue(TEXT("Switching zones destroys the previous zone's actors"),!FirstSpawn.IsValid() && !Spawner->IsPrepared());
    TestFalse(TEXT("Switching zones disables adjustment until a new Start"),C->IsCameraEditingForQA());
    TestTrue(TEXT("Start can enable the next zone"),Cheats->ExecuteCommand(TEXT("Camera Start"),Reply));
    Key(EKeys::Three);
    TestTrue(TEXT("Pitch hotkeys preserve authored yaw and roll"),Camera->GetRelativeRotation().Equals(FRotator(-9,5,2),.01));
    TestTrue(TEXT("Returning to a tuned zone restores its session preview"),Cheats->ExecuteCommand(TEXT("Zone 1"),Reply) && Camera->FieldOfView==5.f);
    TWeakObjectPtr<AOBEnemyCharacter> SecondSpawn=Spawner->GetSpawnedEnemy();
    TestTrue(TEXT("Returning to the same zone creates a new actor"),SecondSpawn.IsValid() && SecondSpawn!=FirstSpawn);
    const FVector BeforeReject=Player->GetActorLocation();
    TestFalse(TEXT("Invalid zone does not disrupt an active tuner"),Cheats->ExecuteCommand(TEXT("Zone 9"),Reply));
    TestTrue(TEXT("Rejected zone preserves preview and location"),Camera->FieldOfView==5.f && Player->GetActorLocation().Equals(BeforeReject));
    TestTrue(TEXT("End logs the last preview before restoring gameplay"),Cheats->ExecuteCommand(TEXT("Camera End"),Reply)
        && Reply.Contains(TEXT("CameraFOV=5.00")) && !C->IsCameraTuningForQA());
    TestTrue(TEXT("End destroys, rather than damages, the zone actors and resets spawners"),!SecondSpawn.IsValid()
        && Spawner->GetSpawnedEnemy()==nullptr && !Spawner->IsPrepared() && !Action->HasEncounterEnemies());
    TestFalse(TEXT("Start requires a new Zone after End"),Cheats->ExecuteCommand(TEXT("Camera Start"),Reply));
    TestTrue(TEXT("End restores movement, damage and automatic progression"),Movement->MovementMode==SavedMode
        && Player->CanBeDamaged()==SavedDamageable && Action->bAutoAdvanceOnEnemyClear==SavedAdvance);
    TestTrue(TEXT("End restores authored camera values and releases hotkeys"),Camera->FieldOfView==70.f
        && !Cheats->HandleCameraTuningKey(EKeys::One,true));
    Player->SetCanBeDamaged(false);Action->bAutoAdvanceOnEnemyClear=false;Movement->SetMovementMode(MOVE_Flying);
    TestTrue(TEXT("A second session can enter Zone6 without a result popup"),Cheats->ExecuteCommand(TEXT("Zone 6"),Reply) && C->GetCurrentPointIndex()==86);
    Cheats->HandlePreviewOwnerEndPlay(PC,EEndPlayReason::EndPlayInEditor);
    TestTrue(TEXT("Cleanup preserves pre-existing god mode and disabled auto advance"),!C->IsCameraTuningForQA()
        && !Player->CanBeDamaged() && !Action->bAutoAdvanceOnEnemyClear && Movement->MovementMode==MOVE_Flying);
    for (const auto& Entry : Authored)
        TestTrue(TEXT("Preview never changes the authored StopPointSettings"),Stops[Entry.Key].bOverrideCameraView==Entry.Value.bOverrideCameraView
            && Stops[Entry.Key].CameraFOV==Entry.Value.CameraFOV && Stops[Entry.Key].CameraRotationOffset.Equals(Entry.Value.CameraRotationOffset));
    auto* Other=UWorld::CreateWorld(EWorldType::Game,false,TEXT("NotStage_01"),CreatePackage(TEXT("/Game/Maps/NotStage_01")));
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Other);
    auto* OtherPC=Other->SpawnActor<AOBPlayerController>();auto* OtherCheats=NewObject<UOBQACheatManager>(OtherPC);
    TestFalse(TEXT("Other maps reject the stage command"),OtherCheats->ExecuteCommand(TEXT("Zone 1"),Reply));
    TestTrue(TEXT("Wrong-map feedback identifies invalid access"),Reply.Contains(TEXT("잘못된 접근")) && Reply.Contains(TEXT("Stage_01")));
    Other->DestroyWorld(false);GEngine->DestroyWorldContext(Other);
    return !HasAnyErrors();
}
#endif
