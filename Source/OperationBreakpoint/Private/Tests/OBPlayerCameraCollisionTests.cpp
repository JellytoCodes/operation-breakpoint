#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBModularSoldierComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "World/OBBulletActor.h"
#include "Core/OBCombatCollision.h"
#include "World/OBRPGRocketActor.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBPlayerCameraCollisionTest,"OperationBreakpoint.Camera.ObstacleCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBPlayerCameraCollisionTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    // APawn::ShouldTakeDamage rejects even valid impacts without an authority GameMode.
    // Supply the same minimal game fixture as the combat tests without starting the rail.
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL);
    if (!TestNotNull(TEXT("Damage fixture has an authority GameMode"),World->GetAuthGameMode())) return false;
    if (!World->GetPhysicsScene()) World->CreatePhysicsScene();
    auto* Class=LoadClass<AOBPlayerCharacter>(nullptr,TEXT("/Game/Blueprints/Player/BP_PlayerCharacter.BP_PlayerCharacter_C"));
    if (!TestNotNull(TEXT("Original player Blueprint"),Class)) return false;
    auto* Player=World->SpawnActor<AOBPlayerCharacter>(Class);
    auto* Camera=Player->GetPlayerCamera();
    Camera->Activate();
    const FVector Anchor=Player->GetPawnViewLocation();
    Camera->SetWorldLocationAndRotation(Anchor+FVector(-300,0,0),FRotator::ZeroRotator);
    Camera->SetFieldOfView(90);
    const FTransform Authored=Camera->GetRelativeTransform();
    auto Step=[&](float Dt=1.f/60.f) { FMinimalViewInfo V; Player->CalcCamera(Dt,V); return V; };
    auto V=Step();
    TestTrue(TEXT("Open outdoor camera keeps authored distance"),V.Location.Equals(Camera->GetComponentLocation(),.01f));
    TestFalse(TEXT("Third-person travel shows the player's weapon"),Player->GetWeaponMesh()->bOwnerNoSee);

    // Fast projectiles must not shorten the boom or invalidate its safe position, including
    // saved Blueprint overrides and a custom camera trace channel that still blocks them.
    for (UClass* ProjectileClass : {AOBBulletActor::StaticClass(), AOBRPGRocketActor::StaticClass()})
    {
        auto* Projectile=World->SpawnActor<AOBBulletActor>(ProjectileClass,Anchor+FVector(-150,0,0),FRotator::ZeroRotator);
        if (!TestNotNull(TEXT("Projectile fixture spawns"),Projectile)) return false;
        auto* Sphere=Cast<USphereComponent>(Projectile->GetRootComponent());
        if (!TestNotNull(TEXT("Projectile retains its impact sphere"),Sphere)) return false;
        TestEqual(TEXT("Native projectile camera response is Ignore"),Sphere->GetCollisionResponseToChannel(ECC_Camera),ECR_Ignore);
        TestEqual(TEXT("Projectile ignores generic Pawn components"),Sphere->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
        TestEqual(TEXT("Projectile blocks only the player's dedicated receiver"),Sphere->GetCollisionResponseToChannel(OBCombatCollision::PlayerBulletTarget),ECR_Block);
        Sphere->SetCollisionResponseToChannel(ECC_Camera,ECR_Block); // Emulate an older serialized BP override.
        Projectile->DispatchBeginPlay();
        TestEqual(TEXT("BeginPlay repairs old projectile Camera blocking"),Sphere->GetCollisionResponseToChannel(ECC_Camera),ECR_Ignore);
        TestEqual(TEXT("BeginPlay preserves the player-only projectile filter"),Sphere->GetCollisionResponseToChannel(OBCombatCollision::PlayerBulletTarget),ECR_Block);
        TestTrue(TEXT("Projectile impact damage delegate is bound on the runtime instance"),Sphere->OnComponentHit.IsBound());

        Sphere->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);
        Sphere->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        for (ECollisionChannel Channel : {ECC_Camera,ECC_Visibility})
        {
            Player->CameraCollisionChannel=Channel;
            for (const FVector Position : {Anchor+FVector(-150,0,0),Camera->GetComponentLocation(),Anchor})
            {
                Projectile->SetActorLocation(Position);
                V=Step();
                TestTrue(TEXT("Projectile crossing boom, camera or anchor cannot move the view"),V.Location.Equals(Camera->GetComponentLocation(),.01f));
                TestFalse(TEXT("Projectile safety overlaps cannot invalidate a clear camera"),Player->bCameraCollisionUnresolved);
                TestFalse(TEXT("Projectiles do not toggle near-body visibility"),Player->IsCollisionCameraNearBody());
            }
        }
        Player->CameraCollisionChannel=ECC_Camera;

        // Sweep the actual impact sphere through the player. This exercises the production
        // OnComponentHit -> ApplyPointDamage path, not a direct call to TakeDamage.
        Player->SetCanBeDamaged(true);
        Player->bIsCrouched=false;
        Player->CurrentHealth=5.f;
        Projectile->SetActorLocation(Player->GetActorLocation()+FVector(-200,0,0));
        FHitResult Impact;
        {
            // This collision-only world intentionally skips actor initialization/BeginPlay.
            // AActor::ProcessEvent otherwise suppresses the dynamic OnComponentHit UFUNCTION,
            // even when MoveComponent has found a real blocking hit. Allow it for this sweep.
            FEditorScriptExecutionGuard ScriptExecution;
            Sphere->MoveComponent(FVector(400,0,0),FQuat::Identity,true,&Impact);
        }
        TestTrue(TEXT("Camera filtering preserves real projectile/player collision"),Impact.bBlockingHit && Impact.GetActor()==Player);
        TestTrue(TEXT("A real swept impact consumes the projectile through its hit callback"),Projectile->IsActorBeingDestroyed());
        TestEqual(TEXT("Projectile impact still applies its original damage"),Player->CurrentHealth,
            ProjectileClass->IsChildOf(AOBRPGRocketActor::StaticClass()) ? 3.f : 4.f);
        Projectile->Destroy();
    }

    auto* Obstacle=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Obstacle);
    Obstacle->SetRootComponent(Box); Obstacle->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(10,100,100));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);
    Obstacle->SetActorLocation(Anchor+FVector(-150,0,0));
    Box->RegisterComponent();

    V=Step();
    TestTrue(TEXT("An intervening outdoor object retracts the camera before its wall"),V.Location.X>-140 && V.Location.X<-100);
    Box->SetCollisionObjectType(ECC_WorldDynamic);
    V=Step();
    TestTrue(TEXT("Non-projectile dynamic walls and railings still retract the camera"),V.Location.X>-140 && V.Location.X<-100);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    TestTrue(TEXT("Camera component transform is not overwritten"),Camera->GetRelativeTransform().Equals(Authored));
    TestEqual(TEXT("Collision does not alter FOV"),V.FOV,90.f);
    TestTrue(TEXT("Collision does not alter view rotation"),V.Rotation.Equals(FRotator::ZeroRotator));

    Box->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    const float BlockedX=V.Location.X;
    V=Step();
    TestTrue(TEXT("Clearing Camera blocking recovers smoothly"),V.Location.X<BlockedX && V.Location.X>-299);
    for (int32 I=0; I<180; ++I) V=Step();
    TestTrue(TEXT("Recovery returns to the original distance"),V.Location.Equals(Camera->GetComponentLocation(),.02f));

    FVector RecoveryAt30=FVector::ZeroVector;
    for (int32 FPS : {30,120})
    {
        Player->bCameraCollisionInitialized=false;
        Box->SetCollisionResponseToChannel(ECC_Camera,ECR_Block); V=Step();
        Box->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
        for (int32 I=0; I<FPS; ++I) V=Step(1.f/FPS);
        if (FPS==30) RecoveryAt30=V.Location;
        else TestTrue(TEXT("Recovery speed is independent of 30/120 FPS"),V.Location.Equals(RecoveryAt30,.02f));
    }

    // A lateral-moving obstacle cuts across the boom while the camera itself was still outside it.
    Obstacle->SetActorLocation(Anchor+FVector(-75,0,0));
    Box->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);
    V=Step();
    TestTrue(TEXT("A nearer obstruction retracts immediately without smoothing through it"),V.Location.X>-65);
    TestTrue(TEXT("Close collision switches owner head visibility"),Player->IsCollisionCameraNearBody());
    TestTrue(TEXT("Collision close-up hides the owner's weapon on the same frame"),Player->GetWeaponMesh()->bOwnerNoSee);
    for (USkeletalMeshComponent* Part : Player->GetSoldierAppearanceComponent()->GetVisualParts())
        if (Part->GetSkeletalMeshAsset()->GetName().Contains(TEXT("Head")) || Part->GetSkeletalMeshAsset()->GetName().Contains(TEXT("Helmet")))
            TestTrue(TEXT("Head and helmet hide on the same evaluated frame"),Part->bOwnerNoSee);

    auto* PC=World->SpawnActor<APlayerController>(); World->AddController(PC); PC->Possess(Player);
    if (!PC->PlayerCameraManager) PC->SpawnPlayerCameraManager();
    if (TestNotNull(TEXT("Engine camera manager"),PC->PlayerCameraManager.Get()))
    {
        PC->PlayerCameraManager->bUseClientSideCameraUpdates=false;
        PC->SetViewTarget(Player); PC->PlayerCameraManager->UpdateCamera(1.f/60.f);
        TestTrue(TEXT("Aiming/render camera cache receives the collision position"),PC->PlayerCameraManager->GetCameraCacheView().Location.X>-65);
        TestEqual(TEXT("Camera cache retains original FOV"),PC->PlayerCameraManager->GetFOVAngle(),90.f);
    }

    // An anchor embedded in geometry must not drag a clear external camera inside.
    Obstacle->SetActorLocation(Anchor);
    V=Step();
    TestTrue(TEXT("An embedded anchor preserves an already clear camera"),V.Location.Equals(Camera->GetComponentLocation(),.01f));
    Camera->SetWorldLocation(Anchor);
    V=Step();
    FCollisionQueryParams Query; Query.AddIgnoredActor(Player);
    TestFalse(TEXT("Initial camera overlap is depenetrated or restored to a safe view"),World->OverlapBlockingTestByChannel(
        V.Location,FQuat::Identity,ECC_Camera,FCollisionShape::MakeSphere(16),Query));

    Player->bEnableCameraCollision=false;
    V=Step();
    TestTrue(TEXT("Collision can be disabled per Blueprint or instance"),V.Location.Equals(Camera->GetComponentLocation(),.01f));
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Player->bEnableCameraCollision=true;
    Camera->SetRelativeLocation(FVector(60,0,28));
    V=Step();
    TestTrue(TEXT("Unobstructed crouch camera retains its authored pose"),V.Location.Equals(Camera->GetComponentLocation(),.01f));
    TestTrue(TEXT("First-person crouch still hides the weapon"),Player->GetWeaponMesh()->bOwnerNoSee);
    return !HasAnyErrors();
}
#endif
