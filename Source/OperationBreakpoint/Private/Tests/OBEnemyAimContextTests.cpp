#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/OBEnemyAnimInstance.h"
#include "AI/OBAIController.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OBPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Stage/OBStageFlowComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBEnemyAimContextTest, "OperationBreakpoint.Combat.EnemyAimContext",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBEnemyAimContextTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase")); World->SetGameMode(URL);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* PC = World->SpawnActor<AOBPlayerController>(); World->AddController(PC);
    auto* Player = World->SpawnActor<AOBPlayerCharacter>(FVector(1000, 500, 400), FRotator::ZeroRotator); PC->Possess(Player);
    auto* Enemy = World->SpawnActor<AOBEnemyCharacter>();
    auto* EnemyClass = LoadClass<AOBEnemyCharacter>(nullptr, TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    if (!TestNotNull(TEXT("Existing enemy weapon reference loads"), EnemyClass)) return false;
    auto* RifleMesh = EnemyClass->GetDefaultObject<AOBEnemyCharacter>()->GetWeaponMesh()->GetSkeletalMeshAsset();
    if (!TestNotNull(TEXT("An actual rifle mesh is available for the aim fixture"), RifleMesh)) return false;
    Enemy->GetWeaponMesh()->SetSkeletalMeshAsset(RifleMesh);
    auto* AI = World->SpawnActor<AOBAIController>(); AI->Possess(Enemy);
    auto* Anim = NewObject<UOBEnemyAnimInstance>(Enemy->GetMesh()); Anim->NativeInitializeAnimation();
    auto Update = [&](float Delta = .25f) { Anim->UpdateEnemyAimContext(Delta); };
    auto CheckOff = [&](const TCHAR* Label) {
        Update();
        TestTrue(Label, !Anim->GetEnemyAimContext().bActive && Anim->AimAlpha == 0.f
            && Anim->AimPitch == 0.f && Anim->AimYaw == 0.f);
    };
    Enemy->SetCombatState(EOBEnemyCombatState::PreparingAttack);
    CheckOff(TEXT("A combat state cannot aim before AI staging has completed"));
    Enemy->EncounterBehavior.EntryMode = EOBEnemyEntryMode::WaitAtSpawn;
    AI->PrepareEncounterEntry(Enemy->GetActorTransform(), FTransform::Identity);
    Enemy->SetCombatState(EOBEnemyCombatState::PreparingAttack);
    Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Update(.01f);
    TestTrue(TEXT("Stationary staged combatant captures the player as an active target"), Anim->GetEnemyAimContext().bActive);
    TestTrue(TEXT("Initial aim blends instead of snapping to full weight"), Anim->AimAlpha > 0.f && Anim->AimAlpha < 1.f);
    const float InitialPitch = Anim->AimPitch;
    const float InitialYaw = Anim->AimYaw;
    Update();
    const FOBEnemyAimContext RightHigh = Anim->GetEnemyAimContext();
    TestTrue(TEXT("A target on the right and above produces positive body-relative yaw and pitch"), Anim->AimYaw > 0.f && Anim->AimPitch > 0.f);
    TestTrue(TEXT("Angular interpolation advances toward the target"), Anim->AimPitch > InitialPitch && Anim->AimYaw > InitialYaw);
    TestTrue(TEXT("The raw aim target is the same direct target used by enemy fire"), RightHigh.RawTargetWorld.Equals(Enemy->GetDirectAimTarget(*Player), .001f));
    TestTrue(TEXT("The exported mesh-space target round-trips to the world target"),
        RightHigh.MeshToWorld.TransformPosition(RightHigh.TargetComponentSpace).Equals(RightHigh.TargetWorld, .001f));
    const FVector FromAngles = RightHigh.OriginWorld + FRotator(Anim->AimPitch, Anim->AimYaw, 0).Vector()
        * FVector::Distance(RightHigh.OriginWorld, RightHigh.RawTargetWorld);
    TestTrue(TEXT("The actual solver target is driven by the exposed AimPitch/Yaw"), FromAngles.Equals(RightHigh.TargetWorld, .001f));

    Player->SetActorLocation(FVector(1000, -500, -400)); Update();
    TestTrue(TEXT("A target on the left and below produces negative yaw and pitch"), Anim->AimYaw < 0.f && Anim->AimPitch < 0.f);
    Player->SetActorLocation(FVector(1, 1000, 10000)); Update();
    TestTrue(TEXT("Extreme height is limited to the configured seventy-degree pitch"), FMath::IsNearlyEqual(Anim->AimPitch, 70.f, .01f));
    TestTrue(TEXT("Side targets are limited to the configured sixty-degree yaw"), FMath::IsNearlyEqual(Anim->AimYaw, 60.f, .01f));
    TestFalse(TEXT("Clamping changes the actual pose target rather than only a display variable"),
        Anim->GetEnemyAimContext().TargetWorld.Equals(Anim->GetEnemyAimContext().RawTargetWorld, .01f));
    Enemy->SetActorRotation(FRotator(0, 90, 0)); Player->SetActorLocation(FVector(0, 1000, 400)); Update();
    TestTrue(TEXT("World positive Y is straight ahead after a ninety-degree body turn"), FMath::Abs(Anim->AimYaw) < .01f && Anim->AimPitch > 0.f);
    Anim->bEnableUpperBodyAim = false; CheckOff(TEXT("Aim OFF resets the full snapshot and exposed angles immediately"));
    Anim->bEnableUpperBodyAim = true;
    for (const auto State : {EOBEnemyCombatState::PreparingAttack, EOBEnemyCombatState::Attacking,
        EOBEnemyCombatState::InCover, EOBEnemyCombatState::Reloading})
    {
        Enemy->SetCombatState(State); Update();
        TestTrue(TEXT("Preparing, attack, cover and reload can track a staged target"), Anim->GetEnemyAimContext().bActive);
    }
    Enemy->GetWeaponMesh()->SetHiddenInGame(true); CheckOff(TEXT("A hidden rifle clears aim alpha and target"));
    Enemy->GetWeaponMesh()->SetHiddenInGame(false);
    Enemy->GetWeaponMesh()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    CheckOff(TEXT("A detached weapon cannot retain an enemy aim pose"));
    Enemy->GetWeaponMesh()->AttachToComponent(Enemy->GetMesh(), FAttachmentTransformRules::KeepRelativeTransform);
    Enemy->ConfigureEnemyArchetype(EOBEnemyArchetype::RPGTrooper);
    Enemy->SetCombatState(EOBEnemyCombatState::PreparingAttack); Update();
    TestTrue(TEXT("A visible RPG launcher can aim even though its inherited rifle is hidden"), Anim->GetEnemyAimContext().bActive);
    Enemy->RPGLauncherMesh->SetHiddenInGame(true); CheckOff(TEXT("A hidden launcher clears RPG aim alpha"));
    Enemy->RPGLauncherMesh->SetHiddenInGame(false);
    Enemy->ConfigureEnemyArchetype(EOBEnemyArchetype::Rifleman);
    Enemy->SetCombatState(EOBEnemyCombatState::Idle); CheckOff(TEXT("An idle enemy never retains a combat aim pose"));
    Enemy->SetCombatState(EOBEnemyCombatState::MovingToCover); CheckOff(TEXT("Cover locomotion disables upper-body aim"));
    Enemy->SetCombatState(EOBEnemyCombatState::PreparingAttack);
    Enemy->GetCharacterMovement()->Velocity = FVector(30, 0, 0); CheckOff(TEXT("Actual movement disables upper-body aim"));
    Enemy->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Falling); CheckOff(TEXT("Airborne movement disables upper-body aim"));
    Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Enemy->SetEncounterCombatant(false); CheckOff(TEXT("A noncombat role cannot retain an upper-body aim snapshot"));
    Enemy->SetEncounterCombatant(true); Enemy->SetCombatState(EOBEnemyCombatState::PreparingAttack);

    auto* Stage = Player->GetStageFlowComponent(); Stage->Player = Player; Stage->bActive = true;
    for (const auto Phase : {EOBStagePhase::Opening, EOBStagePhase::Travel, EOBStagePhase::SupportingFire,
        EOBStagePhase::AwaitingDeparture, EOBStagePhase::QA, EOBStagePhase::Ending, EOBStagePhase::Complete})
    {
        Stage->Phase = Phase; CheckOff(TEXT("Every noncombat Stage phase disables aim and clears alpha"));
    }
    Stage->Phase = EOBStagePhase::Combat; Update();
    TestTrue(TEXT("Stage combat allows aim again after a prior disabled phase"), Anim->GetEnemyAimContext().bActive);
    Stage->bActive = false;
    Enemy->SetCombatState(EOBEnemyCombatState::Dead); CheckOff(TEXT("Death clears aim before ragdoll evaluation"));
    Enemy->SetCombatState(EOBEnemyCombatState::PreparingAttack);
    PC->UnPossess(); CheckOff(TEXT("Losing the target clears the snapshot instead of aiming at its old location"));
    PC->Possess(Player);
    auto* Hostage = World->SpawnActor<AOBHostageCharacter>(FVector(3000, 0, 0), FRotator::ZeroRotator);
    auto* HostageAnim = NewObject<UOBEnemyAnimInstance>(Hostage->GetMesh()); HostageAnim->NativeInitializeAnimation();
    HostageAnim->UpdateEnemyAimContext(.25f);
    TestTrue(TEXT("A dedicated unarmed hostage never receives the enemy aim correction"),
        !HostageAnim->GetEnemyAimContext().bActive && HostageAnim->AimAlpha == 0.f);
    return !HasAnyErrors();
}
#endif
