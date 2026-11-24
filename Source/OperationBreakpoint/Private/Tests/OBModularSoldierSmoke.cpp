#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Character/OBModularSoldierComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBWeaponGripComponent.h"
#include "Animation/OBEnemyAnimInstance.h"
#include "Animation/OBPlayerAnimInstance.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "AI/OBAIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "World/OBBulletActor.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

// Exercises the saved Blueprints and real montage notifies in PIE without saving the world.
static bool CheckSoldier(ACharacter* Character)
{
    auto* Appearance=Character->FindComponentByClass<UOBModularSoldierComponent>();
    auto* Mesh=Character->GetMesh();
    auto* Anim=Mesh->GetAnimInstance();
    auto* Generated=Anim ? Cast<UAnimBlueprintGeneratedClass>(Anim->GetClass()) : nullptr;
    bool bPass=Appearance && Mesh->GetSkeletalMeshAsset()==Appearance->BodyMesh
        && Generated && Generated->GetTargetSkeleton()==Appearance->BodyMesh->GetSkeleton()
        && Mesh->DoesSocketExist(TEXT("WeaponSocket")) && Mesh->GetPhysicsAsset()
        && !Mesh->IsVisible() && Mesh->bHiddenInGame;
    if (Appearance)
    {
        bPass &= Appearance->GetVisualParts().Num()==Appearance->OutfitMeshes.Num();
        for (USkeletalMeshComponent* Part : Appearance->GetVisualParts())
            bPass &= (Part->LeaderPoseComponent.Get()==Mesh || Part->GetAttachSocketName()==TEXT("head")) && Part->IsVisible() && !Part->bHiddenInGame
                && Part->GetCollisionEnabled()==ECollisionEnabled::NoCollision
                && (Part->GetAttachSocketName()==TEXT("head") || Part->GetSocketLocation(TEXT("head")).Equals(Mesh->GetSocketLocation(TEXT("head")),.1f));
    }
    UE_LOG(LogTemp,Display,TEXT("SOLDIER_PIE assembly actor=%s result=%s body=%s animation=%s parts=%d"),
        *Character->GetName(),bPass ? TEXT("PASS") : TEXT("FAIL"),*GetNameSafe(Mesh->GetSkeletalMeshAsset()),
        *GetNameSafe(Anim),Appearance ? Appearance->GetVisualParts().Num() : 0);
    return bPass;
}

static float GripError(AOBCharacter* Character)
{
    const auto* Grip=Character->GetWeaponGripComponent();
    const auto* Weapon=Character->GetWeaponMesh();
    if (!Grip || !Weapon || !Grip->ShouldApplyGrip()) return 0.f;
    return FVector::Distance(Character->GetMesh()->GetSocketLocation(TEXT("hand_l")),
        (Grip->LeftHandInWeapon*Weapon->GetComponentTransform()).GetLocation());
}

static void CreateGripPortraits(AOBCharacter* Character,const FString& Name,TMap<FString,TWeakObjectPtr<USceneCaptureComponent2D>>& Captures)
{
    const FVector Aim=(Character->GetMesh()->GetSocketLocation(TEXT("hand_l"))
        + Character->GetMesh()->GetSocketLocation(TEXT("hand_r")))*.5f;
    for (int32 Side=0; Side<2; ++Side)
    {
        auto* Owner=Character->GetWorld()->SpawnActor<AActor>();
        auto* Capture=NewObject<USceneCaptureComponent2D>(Owner);
        Owner->SetRootComponent(Capture); Owner->AddInstanceComponent(Capture);
        auto* Target=NewObject<UTextureRenderTarget2D>(Capture);
        Target->ClearColor=FLinearColor(.025f,.03f,.035f,1.f);
        Target->RenderTargetFormat=ETextureRenderTargetFormat::RTF_RGBA8;
        Target->InitCustomFormat(1024,1024,PF_B8G8R8A8,false);
        Capture->TextureTarget=Target; Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
        Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        Capture->ShowOnlyActorComponents(Character);
        Capture->FOVAngle=23.f;
        Capture->bAlwaysPersistRenderingState=true;
        Capture->ShowFlags.SetFog(false);
        Capture->ShowFlags.SetAtmosphere(false);
        Capture->ShowFlags.SetVolumetricFog(false);
        Capture->RegisterComponent();
        const FVector Location=Aim+Character->GetActorForwardVector()*130.f
            + Character->GetActorRightVector()*(Side==0 ? -115.f : 115.f)+FVector(0,0,35.f);
        Capture->SetWorldLocationAndRotation(Location,(Aim-Location).Rotation());
        Captures.Add(Name+(Side==0 ? TEXT("_Left.png") : TEXT("_Right.png")),Capture);
    }
    UE_LOG(LogTemp,Display,TEXT("SOLDIER_PIE grip actor=%s wrist_error_cm=%.4f"),*Character->GetName(),GripError(Character));
}

static void RunSoldierSmoke(UWorld* World)
{
    struct FState
    {
        int32 Stage=0, Bullets=0; bool bPass=true, bMovingObserved=false;
        float MaxMovingGripError=0.f;
        double Started=0, Mark=0; FTimerHandle Timer; FDelegateHandle SpawnHandler;
        TWeakObjectPtr<AOBEnemyCharacter> Enemy;
        TWeakObjectPtr<USceneCaptureComponent2D> Preview;
        TMap<FString,TWeakObjectPtr<USceneCaptureComponent2D>> GripCaptures;
    };
    auto S=MakeShared<FState>(); S->Started=World->GetTimeSeconds();
    const FString Folder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/ModularSoldier"));
    IFileManager::Get().MakeDirectory(*Folder,true);
    TWeakObjectPtr<UWorld> WeakWorld=World;
    S->SpawnHandler=World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([S](AActor* Actor) {
        if (Actor->IsA<AOBBulletActor>() && Actor->GetOwner()==S->Enemy.Get()) ++S->Bullets;
    }));
    World->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,WeakWorld,Folder]() {
        auto* W=WeakWorld.Get(); if (!W) return;
        auto End=[&](bool bPass) {
            W->GetTimerManager().ClearTimer(S->Timer); W->RemoveOnActorSpawnedHandler(S->SpawnHandler);
            UE_LOG(LogTemp,Display,TEXT("SOLDIER_PIE outcome=%s bullets=%d moving_observed=%d"),bPass ? TEXT("PASS") : TEXT("FAIL"),S->Bullets,S->bMovingObserved);
            UE_LOG(LogTemp,Display,TEXT("SOLDIER_PIE moving_grip_max_error_cm=%.4f"),S->MaxMovingGripError);
            GEditor->RequestEndPlayMap();
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bPass](float) {
                FPlatformMisc::RequestExitWithStatus(false,bPass ? 0 : 1); return false;
            }),1.f);
        };
        if (W->GetTimeSeconds()-S->Started>35) { End(false); return; }
        auto* PC=W->GetFirstPlayerController();
        auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
        if (!Player) return;
        auto* Rail=Player->GetPlayerSplineComponent();
        if (Rail->IsMoving() && Player->GetVelocity().Size()>1.f)
        {
            auto* Moving=FindFProperty<FBoolProperty>(Player->GetMesh()->GetAnimInstance()->GetClass(),TEXT("bIsRailMoving"));
            S->bMovingObserved |= Moving && Moving->GetPropertyValue_InContainer(Player->GetMesh()->GetAnimInstance());
            if (W->GetTimeSeconds()-S->Started>1.f) S->MaxMovingGripError=FMath::Max(S->MaxMovingGripError,GripError(Player));
        }
        const double Now=W->GetTimeSeconds();
        if (S->Stage==0 && Rail->HasFinishedRoutePreview())
        {
            S->bPass &= CheckSoldier(Player) && Player->GetMesh()->GetAnimInstance()->IsA<UOBPlayerAnimInstance>();
            auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
            if (!Class) { End(false); return; }
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            FVector Position=Player->GetActorLocation()+Player->GetActorForwardVector()*350.f;
            FHitResult Ground; FCollisionQueryParams GroundQuery; GroundQuery.AddIgnoredActor(Player);
            if (W->LineTraceSingleByChannel(Ground,Position+FVector(0,0,300),Position-FVector(0,0,1000),ECC_Visibility,GroundQuery))
                Position.Z=Ground.ImpactPoint.Z+Class->GetDefaultObject<AOBEnemyCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            auto* Enemy=W->SpawnActor<AOBEnemyCharacter>(Class,Position,Player->GetActorRotation()+FRotator(0,180,0),Params);
            if (!Enemy) { End(false); return; }
            S->Enemy=Enemy;
            if (auto* AI=Cast<AOBAIController>(Enemy->GetController())) AI->StopStateTree();
            Enemy->GetCharacterMovement()->DisableMovement();
            Enemy->SetCombatState(EOBEnemyCombatState::Idle);
            auto* Camera=W->SpawnActor<ACameraActor>();
            const FVector CameraPosition=Position-Player->GetActorForwardVector()*250.f+Player->GetActorRightVector()*80.f+FVector(0,0,15);
            Camera->SetActorLocation(CameraPosition);
            Camera->SetActorRotation((Position-CameraPosition).Rotation());
            Camera->GetCameraComponent()->SetFieldOfView(75.f);
            PC->SetViewTarget(Camera);
            auto* PreviewActor=W->SpawnActor<AActor>();
            auto* Capture=NewObject<USceneCaptureComponent2D>(PreviewActor);
            PreviewActor->SetRootComponent(Capture); PreviewActor->AddInstanceComponent(Capture);
            auto* Target=NewObject<UTextureRenderTarget2D>(Capture);
            Target->ClearColor=FLinearColor(.025f,.03f,.035f,1.f);
            Target->RenderTargetFormat=ETextureRenderTargetFormat::RTF_RGBA8;
            Target->InitCustomFormat(768,768,PF_B8G8R8A8,false);
            Capture->TextureTarget=Target; Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
            Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
            Capture->ShowOnlyActorComponents(Enemy); Capture->FOVAngle=40.f;
            Capture->RegisterComponent();
            const FVector PreviewPosition=Position-Player->GetActorForwardVector()*330.f+Player->GetActorRightVector()*80.f+FVector(0,0,5);
            PreviewActor->SetActorLocation(PreviewPosition);
            PreviewActor->SetActorRotation((Position-PreviewPosition).Rotation());
            S->Preview=Capture;
            S->Mark=Now; S->Stage=1;
        }
        else if (S->Stage==1 && Now-S->Mark>2)
        {
            auto* Enemy=S->Enemy.Get(); if (!Enemy) { End(false); return; }
            S->bPass &= CheckSoldier(Enemy) && Enemy->GetMesh()->GetAnimInstance()->IsA<UOBEnemyAnimInstance>();
            if (auto* Preview=S->Preview.Get())
            {
                UKismetRenderingLibrary::ExportRenderTarget(W,Preview->TextureTarget,Folder,TEXT("SoldierAppearance.png"));
                CreateGripPortraits(Enemy,TEXT("EnemyGrip"),S->GripCaptures);
                CreateGripPortraits(Player,TEXT("PlayerGrip"),S->GripCaptures);
                S->bPass &= GripError(Enemy)<.5f && GripError(Player)<.5f && S->MaxMovingGripError<.5f;
            }
            FScreenshotRequest::RequestScreenshot(Folder/TEXT("SoldierIdle.png"),false,false,false,FIntRect(),true);
            S->Mark=Now; S->Stage=2;
        }
        else if (S->Stage==2 && Now-S->Mark>2)
        {
            for (const auto& Pair : S->GripCaptures)
                if (auto* Capture=Pair.Value.Get())
                {
                    UKismetRenderingLibrary::ExportRenderTarget(W,Capture->TextureTarget,Folder,Pair.Key);
                    Capture->SetComponentTickEnabled(false); Capture->bCaptureEveryFrame=false;
                }
            S->Enemy->SetCombatState(EOBEnemyCombatState::Attacking);
            S->Mark=Now;
            S->Stage=3;
        }
        else if (S->Stage==3 && Now-S->Mark>.3)
        {
            FScreenshotRequest::RequestScreenshot(Folder/TEXT("SoldierAttack.png"),false,false,false,FIntRect(),true);
            S->Stage=4;
        }
        else if (S->Stage==4 && Now-S->Mark>2)
        {
            auto* Enemy=S->Enemy.Get(); if (!Enemy) { End(false); return; }
            S->bPass &= S->Bullets>0;
            TInlineComponentArray<UBoxComponent*> Boxes; Enemy->GetComponents(Boxes);
            S->bPass &= Boxes.Num()==10;
            UBoxComponent* Body=nullptr;
            for (auto* Box : Boxes) if (Box->ComponentHasTag(TEXT("HitZone.Body"))) Body=Box;
            if (!Body) { End(false); return; }
            FHitResult Hit(Enemy,Body,Body->GetComponentLocation(),FVector::UpVector);
            const float Damage=UGameplayStatics::ApplyPointDamage(Enemy,10.f,FVector::ForwardVector,Hit,PC,Player,nullptr);
            S->bPass &= FMath::IsNearlyEqual(Damage,10.f) && FMath::IsNearlyEqual(Enemy->GetCurrentHealth(),90.f);
            UGameplayStatics::ApplyPointDamage(Enemy,1000.f,FVector::ForwardVector,Hit,PC,Player,nullptr);
            S->bPass &= Enemy->GetCombatState()==EOBEnemyCombatState::Dead && Enemy->GetLastHitFeedback().bFatal;
            for (auto* Box : Boxes) S->bPass &= Box->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
            S->Mark=Now; S->Stage=5;
        }
        else if (S->Stage==5 && Now-S->Mark>5)
        {
            auto* Enemy=S->Enemy.Get(); if (!Enemy) { End(false); return; }
            const bool bRagdoll=Enemy->GetMesh()->IsSimulatingPhysics(TEXT("pelvis"));
            S->bPass &= bRagdoll && CheckSoldier(Enemy);
            UE_LOG(LogTemp,Display,TEXT("SOLDIER_PIE death ragdoll=%d health=%.1f"),bRagdoll,Enemy->GetCurrentHealth());
            FScreenshotRequest::RequestScreenshot(Folder/TEXT("SoldierDeath.png"),false,false,false,FIntRect(),true);
            S->Mark=Now; S->Stage=6;
        }
        else if (S->Stage==6 && Now-S->Mark>1)
            End(S->bPass && S->bMovingObserved && IFileManager::Get().FileSize(*(Folder/TEXT("SoldierIdle.png")))>0);
    }),.05f,true);
}

static FAutoConsoleCommand GOBSoldierPIE(TEXT("OB.Soldier.IntegrationPIE"),
    TEXT("Verify saved Soldier assembly, route animation, attack notifies, damage and ragdoll in PIE; exits its process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            if (GEditor && GEditor->PlayWorld) { RunSoldierSmoke(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Started>60) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
#endif
