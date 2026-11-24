#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/DamageEvents.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "TimerManager.h"
#include "AI/OBAIController.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Core/OBPlayerController.h"
#include "Stage/OBStageFlowComponent.h"
#include "World/OBRPGRocketActor.h"
#include "World/OBRPGDamageType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBRPGCombatTest, "OperationBreakpoint.Combat.RPGTrooper",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBRPGCombatTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase")); World->SetGameMode(URL);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto Advance = [&](float Seconds) {
        const int32 Steps = FMath::RoundToInt(Seconds / .05f);
        for (int32 I = 0; I < Steps; ++I) { ++GFrameCounter; World->GetTimerManager().Tick(.05f); }
    };

    auto* Rifle = World->SpawnActor<AOBEnemyCharacter>();
    TestTrue(TEXT("Existing enemies default to rifleman"), Rifle->GetEnemyArchetype() == EOBEnemyArchetype::Rifleman);
    TestEqual(TEXT("Rifle HP remains 100"), Rifle->GetMaxHealth(), 100.f);
    TestEqual(TEXT("Rifle minimum integer direct-aim percent"),Rifle->DirectAimMinPercent,2);
    TestEqual(TEXT("Rifle maximum integer direct-aim percent"),Rifle->DirectAimMaxPercent,2);
    TestEqual(TEXT("Rifle body damage multiplier remains unchanged"), Rifle->BodyDamageMultiplier, 1.f);
    TestFalse(TEXT("Rifle never intercepts the shared StateTree"), Rifle->StartArchetypeAttack());

    auto* Enemy = World->SpawnActor<AOBEnemyCharacter>(FVector(2000, 0, 0), FRotator::ZeroRotator);
    Enemy->SetCanBeDamaged(true);
    Enemy->ConfigureEnemyArchetype(EOBEnemyArchetype::RPGTrooper);
    TestEqual(TEXT("RPG has its own 150 HP"), Enemy->GetMaxHealth(), 150.f);
    TestEqual(TEXT("RPG starts at full health"), Enemy->GetCurrentHealth(), 150.f);
    TestEqual(TEXT("RPG minimum integer direct-aim percent"),Enemy->DirectAimMinPercent,25);
    TestEqual(TEXT("RPG maximum integer direct-aim percent"),Enemy->DirectAimMaxPercent,25);
    TestTrue(TEXT("RPG projectile class is configured"), Enemy->RPGRocketClass == AOBRPGRocketActor::StaticClass());
    TestTrue(TEXT("RPG has a visible launcher rather than the rifle"), Enemy->RPGLauncherMesh->IsVisible() && !Enemy->GetWeaponMesh()->IsVisible());
    Enemy->CurrentHealth = 110.f;
    Enemy->ConfigureEnemyArchetype(EOBEnemyArchetype::RPGTrooper);
    TestEqual(TEXT("Repeated preparation cannot heal an RPG"), Enemy->GetCurrentHealth(), 110.f);
    Enemy->StartEncounterBehavior();
    TestFalse(TEXT("RPG cannot start before an AI has completed staging"), Enemy->IsRPGAttackActive());

    auto* AI = World->SpawnActor<AOBAIController>(); AI->Possess(Enemy);
    Enemy->EncounterBehavior.EntryMode = EOBEnemyEntryMode::WaitAtSpawn;
    AI->PrepareEncounterEntry(Enemy->GetActorTransform(), FTransform::Identity);
    Enemy->StartEncounterBehavior();
    TestTrue(TEXT("A staged RPG starts the dedicated attack clock"), Enemy->IsRPGAttackActive());
    Advance(1.9f); TestEqual(TEXT("No attack occurs during the two-second warning"), Enemy->GetRPGShotsFired(), 0);
    Advance(.2f); TestEqual(TEXT("First rocket fires after the warning"), Enemy->GetRPGShotsFired(), 1);
    Enemy->FireBullet(); TestEqual(TEXT("Rifle animation notify cannot duplicate a rocket"), Enemy->GetRPGShotsFired(), 1);
    Advance(3.8f); TestEqual(TEXT("RPG does not fire again before four seconds"), Enemy->GetRPGShotsFired(), 1);
    Advance(.2f); TestEqual(TEXT("RPG repeats after the four-second interval"), Enemy->GetRPGShotsFired(), 2);
    AI->StopStateTree();
    TestFalse(TEXT("Stopping encounter AI immediately stops RPG timers"), Enemy->IsRPGAttackActive());
    Advance(5.f); TestEqual(TEXT("No pending rockets survive an AI stop"), Enemy->GetRPGShotsFired(), 2);
    Enemy->StartEncounterBehavior(); Enemy->SetEncounterCombatant(false);
    TestFalse(TEXT("Noncombat configuration cancels RPG attacks"), Enemy->IsRPGAttackActive());
    Enemy->SetEncounterCombatant(true);

    auto* PC = World->SpawnActor<AOBPlayerController>(); World->AddController(PC);
    auto* Player = World->SpawnActor<AOBPlayerCharacter>(FVector(1000, 0, 0), FRotator::ZeroRotator); PC->Possess(Player);
    Player->SetCanBeDamaged(true);
    Player->GetActionComponent()->SetExternalActionState(true);
    auto* Stage = Player->GetStageFlowComponent();
    Stage->Player = Player; Stage->bActive = true; Stage->CurrentZone = 2;
    Stage->Phase = EOBStagePhase::Combat; Stage->RemainingLives = 5;
    Player->bIsCrouched = true;
    Player->GetCapsuleComponent()->SetCapsuleHalfHeight(44.f);
    const FVector CrouchedAim = Enemy->SelectShotAimLocation(*Player, 1, 0);
    TestTrue(TEXT("A crouched RPG target lies safely inside the actual shortened capsule"),
        FMath::Abs(CrouchedAim.Z - Player->GetActorLocation().Z) <= Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * .5f);
    TestTrue(TEXT("RPG integer roll 25 selects the covered target"),
        Enemy->SelectShotAimLocation(*Player, 25, 0).Equals(CrouchedAim));
    TestFalse(TEXT("RPG integer roll 26 selects a miss coordinate"),
        Enemy->SelectShotAimLocation(*Player, 26, 0).Equals(CrouchedAim));
    int32 DirectAimSamples = 0, SurroundingAimSamples = 0;
    for (int32 Index = 0; Index < 100; ++Index)
    {
        const FVector Aim = Enemy->SelectShotAimLocation(*Player, Index+1, Index % 6);
        if (Aim.Equals(CrouchedAim)) ++DirectAimSamples;
        else if (Aim.Equals(CrouchedAim + Enemy->SurroundingAimOffsets[Index % 6])) ++SurroundingAimSamples;
    }
    TestEqual(TEXT("RPG has 25 direct target coordinates over 100 evenly spaced rolls"), DirectAimSamples, 25);
    TestEqual(TEXT("RPG has 75 real surrounding target coordinates over the same rolls"), SurroundingAimSamples, 75);
    FDamageEvent RifleDamage;
    TestEqual(TEXT("Cover still rejects rifle damage"), Player->TakeDamage(1.f, RifleDamage, nullptr, nullptr), 0.f);
    TestEqual(TEXT("Rifle cover rejection costs no lives"), Stage->GetRemainingLives(), 5);
    FDamageEvent RPGDamage(UOBRPGDamageType::StaticClass());
    TestEqual(TEXT("RPG damage is accepted through cover"), Player->TakeDamage(2.f, RPGDamage, nullptr, nullptr), 2.f);
    TestEqual(TEXT("A covered RPG impact consumes two hearts"), Stage->GetRemainingLives(), 3);
    TestEqual(TEXT("Zero damage never consumes hearts"), Player->TakeDamage(0.f, RPGDamage, nullptr, nullptr), 0.f);

    auto* Rocket = World->SpawnActor<AOBRPGRocketActor>(FVector(4000, 0, 0), FRotator::ZeroRotator);
    TestEqual(TEXT("Rocket carries a two-heart damage amount"), Rocket->Damage, 2.f);
    TestTrue(TEXT("Rocket preserves the RPG damage type"), Rocket->DamageTypeClass == UOBRPGDamageType::StaticClass());
    TestTrue(TEXT("Rocket has a visible projectile mesh"), Rocket->BulletMesh->GetStaticMesh() != nullptr);
    TestEqual(TEXT("Rocket uses a deliberately slower readable speed"), Rocket->ProjectileMovement->InitialSpeed, 900.f);
    Stage->RemainingLives = 5;
    const FHitResult Hit(Player, Player->GetCapsuleComponent(), Player->GetActorLocation(), FVector::UpVector);
    Rocket->SetPlayerHitAllowed(false);
    TestFalse(TEXT("A missed RPG roll never enables homing"),bool(Rocket->ProjectileMovement->bIsHomingProjectile));
    Rocket->HandleBulletHit(nullptr, Player, Player->GetCapsuleComponent(), FVector::ZeroVector, Hit);
    TestEqual(TEXT("Missed-roll rockets cannot accidentally damage a player crossing their trajectory"),Stage->GetRemainingLives(),5);
    Rocket->SetPlayerHitAllowed(true);Rocket->LockOnPlayer(Player);
    TestTrue(TEXT("A successful RPG roll follows the actual crouching collision capsule"),Rocket->ProjectileMovement->bIsHomingProjectile && Rocket->ProjectileMovement->HomingTargetComponent.Get()==Player->GetCapsuleComponent());
    Rocket->HandleBulletHit(nullptr, Player, Player->GetCapsuleComponent(), FVector::ZeroVector, Hit);
    TestEqual(TEXT("Actual projectile collision reaches Stage with cover-piercing two-heart damage"), Stage->GetRemainingLives(), 3);
    Rocket->HandleBulletHit(nullptr, Player, Player->GetCapsuleComponent(), FVector::ZeroVector, Hit);
    TestEqual(TEXT("Duplicate collision callbacks cannot apply another impact"), Stage->GetRemainingLives(), 3);

    Stage->Phase = EOBStagePhase::SupportingFire;
    Enemy->StartEncounterBehavior(); TestFalse(TEXT("Support fire blocks RPG restart"), Enemy->IsRPGAttackActive());
    Stage->Phase = EOBStagePhase::Travel;
    const int32 BeforeArrivalOrderTest = Enemy->GetRPGShotsFired();
    Enemy->StartEncounterBehavior();
    TestTrue(TEXT("Action arrival may arm the warning before the Stage arrival callback"), Enemy->IsRPGAttackActive());
    TestEqual(TEXT("Arming during Travel never immediately launches a rocket"), Enemy->GetRPGShotsFired(), BeforeArrivalOrderTest);
    Stage->Phase = EOBStagePhase::Combat;
    Advance(2.1f);
    TestEqual(TEXT("Stage arrival completing later on the same frame cannot lose the RPG start"), Enemy->GetRPGShotsFired(), BeforeArrivalOrderTest + 1);
    Enemy->StopArchetypeAttack();
    Enemy->StartEncounterBehavior(); TestTrue(TEXT("Combat can explicitly restart a stopped RPG"), Enemy->IsRPGAttackActive());
    Stage->Phase = EOBStagePhase::QA;
    Advance(2.1f); TestFalse(TEXT("QA cancels the RPG before a pending launch"), Enemy->IsRPGAttackActive());
    Stage->bActive = false;
    Enemy->StartEncounterBehavior();
    Enemy->SetCombatState(EOBEnemyCombatState::Dead);
    TestFalse(TEXT("Death cancels the attack clock"), Enemy->IsRPGAttackActive());
    TestFalse(TEXT("Death clears the actual timer handle"), World->GetTimerManager().IsTimerActive(Enemy->RPGAttackTimer));
    return !HasAnyErrors();
}
#endif
