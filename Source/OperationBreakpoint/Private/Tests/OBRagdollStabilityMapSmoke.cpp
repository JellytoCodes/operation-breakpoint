#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Animation/OBEnemyAnimInstance.h"
#include "AI/OBAIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OBPlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Stage/OBStageFlowComponent.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"

// The boxes are test environment only. Characters use the saved Blueprint, PhysicsAsset,
// MM death montage, authored notifies and unmodified production death-physics settings.
// RagdollRun is an artifact label, not a production-settings switch. Baseline/nolock
// artifacts were captured before the stabilization patch was compiled.
class FOBRagdollStabilityMapSmoke
{
    struct FCase
    {
        FString Name,PhysicsAsset;TWeakObjectPtr<AOBEnemyCharacter> Enemy;
        FVector FloorCenter=FVector::ZeroVector,InitialPelvis=FVector::ZeroVector,LastPelvis=FVector::ZeroVector;
        TArray<TWeakObjectPtr<UBoxComponent>> Walls;TWeakObjectPtr<UBoxComponent> Floor;
        double SimStart=-1,LastSample=-1;int32 ExpectedBodies=0,MaxSimBodies=0,Samples=0,LateSamples=0,LateBodySamples=0,ContactSamples=0,LateAllSleeping=0;
        int32 LateSleepingSum=0;float MaxLinear=0,MaxAngular=0,LateMaxLinear=0,LateMaxAngular=0,MaxWallMTD=0,LateMaxWallMTD=0;
        float MaxFloorMTD=0,LatePelvisPath=0,LatePelvisStep=0,WeaponMaxLinear=0,WeaponMaxAngular=0;
        float FixtureGroundLift=0,PreDeathMinZ=0;
        double LateSpeedSquared=0;FBox LatePelvisBounds=FBox(ForceInit);bool bFinite=true,bWeaponObserved=false,bPrePhysicsWallOverlap=false;
    };
    static float FloatSetting(const UObject* Object,const TCHAR* Name)
    {
        const auto* Property=FindFProperty<FFloatProperty>(Object->GetClass(),Name);
        return Property ? Property->GetPropertyValue_InContainer(Object) : -1.f;
    }
    static UBoxComponent* AddEnvironmentBox(UWorld* W,const FVector& Location,const FVector& Extent,TArray<TWeakObjectPtr<AActor>>& Actors)
    {
        auto* Actor=W->SpawnActor<AActor>();Actors.Add(Actor);
        auto* Box=NewObject<UBoxComponent>(Actor);Actor->SetRootComponent(Box);Actor->AddInstanceComponent(Box);
        Box->SetBoxExtent(Extent);Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Box->SetCollisionResponseToAllChannels(ECR_Block);Box->SetGenerateOverlapEvents(false);Box->SetMobility(EComponentMobility::Static);
        Box->SetWorldLocation(Location);Box->RegisterComponent();
        auto* Visual=NewObject<UStaticMeshComponent>(Actor);Actor->AddInstanceComponent(Visual);Visual->SetupAttachment(Box);
        Visual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Visual->SetRelativeScale3D(Extent/50.f);Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);Visual->SetMobility(EComponentMobility::Static);Visual->RegisterComponent();
        return Box;
    }
    static float Penetration(FBodyInstance* Body,const UBoxComponent* Box,bool bContactShell=false)
    {
        if (!Body || !Box) return 0;
        FMTDResult MTD;const FVector Extent=Box->GetScaledBoxExtent()+(bContactShell ? FVector(1.f) : FVector::ZeroVector);
        return Body->OverlapTest(Box->GetComponentLocation(),Box->GetComponentQuat(),FCollisionShape::MakeBox(Extent),&MTD,false)
            ? FMath::Max(0.f,MTD.Distance) : 0.f;
    }
    static void ExportGeometry(const FCase& Case,const FString& Run,FString& CSV)
    {
        auto* Enemy=Case.Enemy.Get();auto* Mesh=Enemy->GetMesh();auto* PA=Mesh->GetPhysicsAsset();
        const FVector Scale=Mesh->GetComponentScale();
        for (const auto& Setup : PA->SkeletalBodySetups)
        {
            if (!Setup) continue;
            auto Write=[&](const TCHAR* Type,int32 Index,const FVector& Center,const FRotator& Rotation,float A,float B,float C) {
                CSV+=FString::Printf(TEXT("%s,%s,%s,%s,%d,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n"),
                    *Run,*Case.Name,*Setup->BoneName.ToString(),Type,Index,Center.X,Center.Y,Center.Z,Rotation.Pitch,Rotation.Yaw,Rotation.Roll,A,B,C,Scale.X,Scale.Y,Scale.Z);
            };
            for (int32 I=0;I<Setup->AggGeom.SphereElems.Num();++I) { const auto& E=Setup->AggGeom.SphereElems[I];Write(TEXT("sphere_radius"),I,E.Center,FRotator::ZeroRotator,E.Radius,0,0); }
            for (int32 I=0;I<Setup->AggGeom.SphylElems.Num();++I) { const auto& E=Setup->AggGeom.SphylElems[I];Write(TEXT("capsule_radius_cylinder_length"),I,E.Center,E.Rotation,E.Radius,E.Length,0); }
            for (int32 I=0;I<Setup->AggGeom.BoxElems.Num();++I) { const auto& E=Setup->AggGeom.BoxElems[I];Write(TEXT("box_full_xyz"),I,E.Center,E.Rotation,E.X,E.Y,E.Z); }
            if (!Setup->AggGeom.ConvexElems.IsEmpty()) Write(TEXT("convex_count"),0,FVector::ZeroVector,FRotator::ZeroRotator,float(Setup->AggGeom.ConvexElems.Num()),0,0);
            if (!Setup->AggGeom.TaperedCapsuleElems.IsEmpty()) Write(TEXT("tapered_capsule_count"),0,FVector::ZeroVector,FRotator::ZeroRotator,float(Setup->AggGeom.TaperedCapsuleElems.Num()),0,0);
        }
    }
    static void Sample(FCase& Case,const FString& Run,double Now,FString& CSV)
    {
        auto* Enemy=Case.Enemy.Get();auto* Mesh=Enemy ? Enemy->GetMesh() : nullptr;auto* PA=Mesh ? Mesh->GetPhysicsAsset() : nullptr;
        if (!PA || Case.SimStart<0 || Now-Case.LastSample<.001) return;
        const double Elapsed=Now-Case.SimStart;const bool bLate=Elapsed>=3. && Elapsed<=6.1;
        int32 SimBodies=0,Sleeping=0;bool bWallContact=false;
        for (const auto& Setup : PA->SkeletalBodySetups)
        {
            if (!Setup) continue;auto* Body=Mesh->GetBodyInstance(Setup->BoneName);
            if (!Body || !Body->IsValidBodyInstance() || !Body->IsInstanceSimulatingPhysics()) continue;
            ++SimBodies;const FVector Position=Body->GetUnrealWorldTransform().GetLocation();
            const FVector Linear=Body->GetUnrealWorldVelocity(),Angular=Body->GetUnrealWorldAngularVelocityInRadians();
            const bool bAwake=Body->IsInstanceAwake();if (!bAwake) ++Sleeping;
            const float Speed=Linear.Size(),AngularSpeed=Angular.Size();float WallMTD=0,FloorMTD=Penetration(Body,Case.Floor.Get());
            for (auto Wall : Case.Walls) if (Wall.IsValid())
            {
                WallMTD=FMath::Max(WallMTD,Penetration(Body,Wall.Get()));
                bWallContact |= WallMTD>0 || Penetration(Body,Wall.Get(),true)>0;
            }
            Case.bFinite &= !Position.ContainsNaN() && !Linear.ContainsNaN() && !Angular.ContainsNaN() && FMath::IsFinite(WallMTD) && FMath::IsFinite(FloorMTD);
            Case.MaxLinear=FMath::Max(Case.MaxLinear,Speed);Case.MaxAngular=FMath::Max(Case.MaxAngular,AngularSpeed);
            Case.MaxWallMTD=FMath::Max(Case.MaxWallMTD,WallMTD);Case.MaxFloorMTD=FMath::Max(Case.MaxFloorMTD,FloorMTD);
            if (bLate)
            {
                ++Case.LateBodySamples;Case.LateSpeedSquared+=double(Speed)*Speed;
                Case.LateMaxLinear=FMath::Max(Case.LateMaxLinear,Speed);Case.LateMaxAngular=FMath::Max(Case.LateMaxAngular,AngularSpeed);
                Case.LateMaxWallMTD=FMath::Max(Case.LateMaxWallMTD,WallMTD);
            }
            CSV+=FString::Printf(TEXT("%s,%s,%.6f,%s,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%d,%.6f,%.6f,%.5f,%.5f,%.5f,%d,%d\n"),
                *Run,*Case.Name,Elapsed,*Setup->BoneName.ToString(),Position.X,Position.Y,Position.Z,Linear.X,Linear.Y,Linear.Z,Angular.X,Angular.Y,Angular.Z,bAwake,WallMTD,FloorMTD,Body->GetBodyMass(),Body->LinearDamping,Body->AngularDamping,Body->bLockXTranslation,Body->bLockYTranslation);
        }
        auto* Pelvis=Mesh->GetBodyInstance(TEXT("pelvis"));
        if (Pelvis)
        {
            const FVector Position=Pelvis->GetUnrealWorldTransform().GetLocation();
            if (Case.Samples==0) Case.InitialPelvis=Position;
            if (bLate)
            {
                if (Case.LateSamples>0) { const float Step=FVector::Distance(Position,Case.LastPelvis);Case.LatePelvisPath+=Step;Case.LatePelvisStep=FMath::Max(Case.LatePelvisStep,Step); }
                Case.LatePelvisBounds+=Position;
            }
            Case.LastPelvis=Position;
        }
        if (auto* Weapon=Enemy->GetWeaponMesh(); Weapon && Weapon->IsAnySimulatingPhysics())
        {
            Case.bWeaponObserved=true;Case.WeaponMaxLinear=FMath::Max(Case.WeaponMaxLinear,Weapon->GetPhysicsLinearVelocity().Size());
            Case.WeaponMaxAngular=FMath::Max(Case.WeaponMaxAngular,Weapon->GetPhysicsAngularVelocityInRadians().Size());
        }
        Case.MaxSimBodies=FMath::Max(Case.MaxSimBodies,SimBodies);if (bWallContact) ++Case.ContactSamples;
        ++Case.Samples;if (bLate) { ++Case.LateSamples;Case.LateSleepingSum+=Sleeping;if (Sleeping==SimBodies && SimBodies>0) ++Case.LateAllSleeping; }
        Case.LastSample=Now;
    }
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);const double Began=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Began](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (P && W->GetTimeSeconds()>.7f) { Run(W,P);return false; }
            if (FPlatformTime::Seconds()-Began>100) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }return true;
        }),.1f);
    }
private:
    static void Run(UWorld* W,AOBPlayerCharacter* P)
    {
        struct FState
        {
            int32 Step=0,Photo=0;double Began=0,Mark=0;bool OK=true,bGrounded=false;FString Run=TEXT("baseline"),Log,CSV,Geometry;
            TArray<FCase> Cases;TArray<TWeakObjectPtr<AActor>> Actors;TWeakObjectPtr<ACameraActor> Camera;FTimerHandle Timer;
        };
        auto S=MakeShared<FState>();S->Began=S->Mark=W->GetTimeSeconds();FParse::Value(FCommandLine::Get(),TEXT("RagdollRun="),S->Run);
        S->bGrounded=FParse::Param(FCommandLine::Get(),TEXT("RagdollGrounded"));
        if (S->bGrounded) S->Run=TEXT("grounded");
        if (S->Run!=TEXT("baseline") && S->Run!=TEXT("nolock") && S->Run!=TEXT("stabilized") && S->Run!=TEXT("grounded")) S->Run=TEXT("baseline");
        FMath::RandInit(1908029);
        S->CSV=TEXT("run,case,time_since_sim_s,bone,x_cm,y_cm,z_cm,vx_cm_s,vy_cm_s,vz_cm_s,wx_rad_s,wy_rad_s,wz_rad_s,awake,wall_mtd_cm,floor_mtd_cm,mass_kg,linear_damping,angular_damping,lock_x,lock_y\n");
        S->Geometry=TEXT("run,case,bone,shape,index,center_x,center_y,center_z,pitch,yaw,roll,dim1,dim2,dim3,mesh_scale_x,mesh_scale_y,mesh_scale_z\n");
        const FString Folder=FPaths::ProjectDir()/TEXT("Build/Testing/Verification/RagdollStability");IFileManager::Get().MakeDirectory(*Folder,true);
        P->GetStageFlowComponent()->SuspendForQA();P->GetPlayerSplineComponent()->SetStageMovementBlocked(true);P->GetCharacterMovement()->DisableMovement();P->SetCanBeDamaged(false);
        auto* PC=Cast<AOBPlayerController>(P->GetController());if (auto* HUD=PC ? PC->GetOBHUD() : nullptr;HUD && HUD->GetPlayerOverlayWidget()) HUD->GetPlayerOverlayWidget()->SetVisibility(ESlateVisibility::Collapsed);
        TWeakObjectPtr<UWorld> Weak=W;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,Weak,Folder]() {
            auto* World=Weak.Get();if (!World) return;auto* Controller=Cast<AOBPlayerController>(World->GetFirstPlayerController());
            auto* Player=Controller ? Cast<AOBPlayerCharacter>(Controller->GetPawn()) : nullptr;if (!Player) return;
            const auto KeepAlive=S;const double Now=World->GetTimeSeconds();
            auto Check=[&](bool Good,const TCHAR* Label) { S->OK &= Good;S->Log+=FString::Printf(TEXT("%s %s\n"),Good ? TEXT("PASS") : TEXT("FAIL"),Label); };
            auto Finish=[&]() {
                auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("run"),S->Run);Root->SetNumberField(TEXT("seed"),1908029);Root->SetStringField(TEXT("stability_threshold_policy"),TEXT("record_only_no_tuned_pass_thresholds"));
                Root->SetBoolField(TEXT("fixture_grounded_crouch_only"),S->bGrounded);
                TArray<TSharedPtr<FJsonValue>> Results;
                for (const auto& C : S->Cases)
                {
                    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("case"),C.Name);J->SetStringField(TEXT("physics_asset"),C.PhysicsAsset);
                    J->SetNumberField(TEXT("expected_bodies"),C.ExpectedBodies);J->SetNumberField(TEXT("max_simulated_bodies"),C.MaxSimBodies);J->SetNumberField(TEXT("samples"),C.Samples);
                    J->SetNumberField(TEXT("fixture_ground_lift_cm"),C.FixtureGroundLift);J->SetNumberField(TEXT("predeath_pa_min_z_cm"),C.PreDeathMinZ);J->SetNumberField(TEXT("fixture_floor_top_z_cm"),C.FloorCenter.Z);
                    J->SetNumberField(TEXT("late_samples"),C.LateSamples);J->SetNumberField(TEXT("wall_contact_samples_1cm_envelope"),C.ContactSamples);
                    J->SetBoolField(TEXT("prephysics_actual_wall_overlap"),C.bPrePhysicsWallOverlap);
                    J->SetNumberField(TEXT("max_linear_cm_s"),C.MaxLinear);J->SetNumberField(TEXT("max_angular_rad_s"),C.MaxAngular);
                    J->SetNumberField(TEXT("late_max_linear_cm_s"),C.LateMaxLinear);J->SetNumberField(TEXT("late_max_angular_rad_s"),C.LateMaxAngular);
                    J->SetNumberField(TEXT("late_rms_speed_cm_s"),C.LateBodySamples>0 ? FMath::Sqrt(C.LateSpeedSquared/C.LateBodySamples) : 0);
                    J->SetNumberField(TEXT("max_wall_mtd_cm"),C.MaxWallMTD);J->SetNumberField(TEXT("late_max_wall_mtd_cm"),C.LateMaxWallMTD);J->SetNumberField(TEXT("max_floor_mtd_cm"),C.MaxFloorMTD);
                    J->SetNumberField(TEXT("pelvis_net_displacement_cm"),FVector::Distance(C.LastPelvis,C.InitialPelvis));J->SetNumberField(TEXT("late_pelvis_path_cm"),C.LatePelvisPath);J->SetNumberField(TEXT("late_pelvis_max_step_cm"),C.LatePelvisStep);
                    J->SetNumberField(TEXT("late_pelvis_range_cm"),C.LatePelvisBounds.IsValid ? C.LatePelvisBounds.GetSize().Size() : 0);
                    J->SetNumberField(TEXT("late_mean_sleeping_bodies"),C.LateSamples>0 ? double(C.LateSleepingSum)/C.LateSamples : 0);J->SetNumberField(TEXT("late_all_sleeping_samples"),C.LateAllSleeping);
                    J->SetBoolField(TEXT("weapon_simulation_observed"),C.bWeaponObserved);J->SetNumberField(TEXT("weapon_max_linear_cm_s"),C.WeaponMaxLinear);J->SetNumberField(TEXT("weapon_max_angular_rad_s"),C.WeaponMaxAngular);J->SetBoolField(TEXT("finite"),C.bFinite);
                    Results.Add(MakeShared<FJsonValueObject>(J));
                }
                Root->SetArrayField(TEXT("cases"),Results);Root->SetStringField(TEXT("outcome"),S->OK ? TEXT("PASS") : TEXT("FAIL"));
                FString JSON;const auto Writer=TJsonWriterFactory<>::Create(&JSON);FJsonSerializer::Serialize(Root,Writer);
                FFileHelper::SaveStringToFile(JSON,*(Folder/(TEXT("summary-")+S->Run+TEXT(".json"))));
                FFileHelper::SaveStringToFile(S->CSV,*(Folder/(TEXT("bodies-")+S->Run+TEXT(".csv"))));
                FFileHelper::SaveStringToFile(S->Geometry,*(Folder/(TEXT("geometry-")+S->Run+TEXT(".csv"))));
                S->Log+=FString::Printf(TEXT("outcome=%s run=%s\n"),S->OK ? TEXT("PASS") : TEXT("FAIL"),*S->Run);
                FFileHelper::SaveStringToFile(S->Log,*(Folder/(TEXT("runtime-")+S->Run+TEXT(".txt"))));UE_LOG(LogTemp,Display,TEXT("RAGDOLL_STABILITY_PIE\n%s"),*S->Log);
                Controller->SetViewTarget(Player);for (auto Actor : S->Actors) if (Actor.IsValid())
                { if (auto* Pawn=Cast<APawn>(Actor.Get());Pawn && Pawn->GetController()) Pawn->GetController()->Destroy();Actor->Destroy(); }
                const bool OK=S->OK;World->GetTimerManager().ClearTimer(S->Timer);GEditor->RequestEndPlayMap();
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) { if (GEditor && GEditor->PlayWorld) return true;FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false; }),.5f);
            };
            if (Now-S->Began>40) { Check(false,TEXT("Ragdoll scenario deadline"));Finish();return; }
            if (S->Step==0)
            {
                const TCHAR* Names[]={TEXT("StandingWallNear"),TEXT("CrouchShallowOverlap"),TEXT("RPGCorner")};
                for (int32 I=0;I<3;++I)
                {
                    FCase C;C.Name=Names[I];C.FloorCenter=FVector(100000,100000+3000*I,5000);
                    C.Floor=AddEnvironmentBox(World,C.FloorCenter-FVector(0,0,20),FVector(500,500,20),S->Actors);
                    const TCHAR* Path=I==2 ? TEXT("/Game/Blueprints/Enemy/BP_RPGTrooper.BP_RPGTrooper_C") : TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C");
                    auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,Path);Check(Class!=nullptr,TEXT("Saved scenario enemy Blueprint loads"));if (!Class) { Finish();return; }
                    const float HalfHeight=Class->GetDefaultObject<AOBEnemyCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                    auto* Enemy=World->SpawnActor<AOBEnemyCharacter>(Class,C.FloorCenter+FVector(0,0,HalfHeight),FRotator::ZeroRotator,Params);C.Enemy=Enemy;S->Actors.Add(Enemy);
                    Check(Enemy && Enemy->GetMesh()->GetPhysicsAsset(),TEXT("Scenario uses the real saved body PhysicsAsset"));if (!Enemy || !Enemy->GetMesh()->GetPhysicsAsset()) { Finish();return; }
                    // Optional causal control, only on these disposable instances of the old code.
                    if (S->Run==TEXT("nolock"))
                        if (auto* LegacyLock=FindFProperty<FBoolProperty>(Enemy->GetClass(),TEXT("bLockRagdollHorizontalMovement")))
                            LegacyLock->SetPropertyValue_InContainer(Enemy,false);
                    if (auto* AI=Cast<AOBAIController>(Enemy->GetController())) { AI->StopStateTree();AI->SetActorTickEnabled(false); }
                    Enemy->EncounterBehavior.bFacePlayer=false;Enemy->GetCharacterMovement()->StopMovementImmediately();
                    if (I==1) { Enemy->SetCombatState(EOBEnemyCombatState::InCover);Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Enemy->GetCharacterMovement()->Crouch(false);Check(Enemy->bIsCrouched,TEXT("Crouch case enters a real shortened capsule pose")); }
                    Enemy->GetCharacterMovement()->DisableMovement();
                    auto* Mesh=Enemy->GetMesh();Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;Mesh->bEnableUpdateRateOptimizations=false;Mesh->SetForcedLOD(1);
                    C.ExpectedBodies=Mesh->GetPhysicsAsset()->SkeletalBodySetups.Num();C.PhysicsAsset=Mesh->GetPhysicsAsset()->GetPathName();
                    S->Log+=FString::Printf(TEXT("INFO Case=%s PA=%s ExpectedBodies=%d MassScale=%.3f LinearDamping=%.3f AngularDamping=%.3f MaxDepenetration=%.3f BlendSeconds=%.3f\n"),
                        *C.Name,*C.PhysicsAsset,C.ExpectedBodies,FloatSetting(Enemy,TEXT("RagdollMassScale")),FloatSetting(Enemy,TEXT("RagdollLinearDamping")),FloatSetting(Enemy,TEXT("RagdollAngularDamping")),FloatSetting(Enemy,TEXT("RagdollMaxDepenetrationVelocity")),FloatSetting(Enemy,TEXT("DeathPhysicsBlendDuration")));
                    S->Cases.Add(MoveTemp(C));
                }
                auto* Camera=World->SpawnActor<ACameraActor>();if (Camera) { Camera->GetCameraComponent()->SetFieldOfView(70.f);S->Actors.Add(Camera);S->Camera=Camera; }
                S->Mark=Now;S->Step=1;return;
            }
            if (S->Step==1)
            {
                if (Now-S->Mark<1.) return;
                for (int32 I=0;I<S->Cases.Num();++I)
                {
                    auto& C=S->Cases[I];auto* Enemy=C.Enemy.Get();auto* Mesh=Enemy->GetMesh();Mesh->RefreshBoneTransforms();
                    auto ActualPABounds=[Mesh]() {
                        FBox Result(ForceInit);
                        // Include every PA body rather than the rendering bounds or only bounds-enabled bodies.
                        for (const auto& Setup : Mesh->GetPhysicsAsset()->SkeletalBodySetups)
                            if (Setup && Mesh->DoesSocketExist(Setup->BoneName)) Result+=Setup->AggGeom.CalcAABB(Mesh->GetSocketTransform(Setup->BoneName,RTS_World));
                        return Result;
                    };
                    FBox Bounds=ActualPABounds();
                    Check(Bounds.IsValid!=0,TEXT("Wall placement derives from the actual current PhysicsAsset pose"));
                    if (S->bGrounded && I==1 && Bounds.IsValid)
                    {
                        const double OriginalMinZ=Bounds.Min.Z;
                        C.FixtureGroundLift=float(FMath::Max(0.,C.FloorCenter.Z+1.-Bounds.Min.Z));
                        Enemy->SetActorLocation(Enemy->GetActorLocation()+FVector(0,0,C.FixtureGroundLift),false,nullptr,ETeleportType::TeleportPhysics);
                        Mesh->RefreshBoneTransforms();Bounds=ActualPABounds();
                        const bool bAboveFloor=Bounds.IsValid && Bounds.Min.Z>=C.FloorCenter.Z+.99;
                        Check(bAboveFloor,TEXT("Grounded crouch fixture starts every PA shape above the floor top"));
                        S->Log+=FString::Printf(TEXT("INFO Case=%s GroundedFixtureOnly=1 LiftCm=%.5f OriginalPAMinZ=%.5f GroundedPAMinZ=%.5f FloorTopZ=%.5f\n"),*C.Name,C.FixtureGroundLift,OriginalMinZ,Bounds.Min.Z,C.FloorCenter.Z);
                        if (!bAboveFloor) { Finish();return; }
                    }
                    C.PreDeathMinZ=float(Bounds.Min.Z);
                    const float BackFace=Bounds.Min.X+(I==1 ? 3.f : -2.f);
                    C.Walls.Add(AddEnvironmentBox(World,FVector(BackFace-20.f,C.FloorCenter.Y,C.FloorCenter.Z+150.f),FVector(20,500,150),S->Actors));
                    if (I==2) { const float SideFace=Bounds.Min.Y-2.f;C.Walls.Add(AddEnvironmentBox(World,FVector(C.FloorCenter.X,SideFace-20.f,C.FloorCenter.Z+150.f),FVector(500,20,150),S->Actors)); }
                    ExportGeometry(C,S->Run,S->Geometry);
                    S->Log+=FString::Printf(TEXT("INFO Case=%s InitialBoundsMin=%s Max=%s BackWallInnerFaceX=%.5f GapCm=%.3f\n"),*C.Name,*Bounds.Min.ToCompactString(),*Bounds.Max.ToCompactString(),BackFace,I==1 ? -3.f : 2.f);
                    FDamageEvent Damage;Enemy->TakeDamage(Enemy->GetMaxHealth()+1.f,Damage,Controller,Player);
                    Check(Enemy->GetCombatState()==EOBEnemyCombatState::Dead,TEXT("Normal damage starts the authored MM death path"));
                }
                S->Mark=Now;S->Step=2;return;
            }
            if (S->Step==2)
            {
                bool bAllDone=true;
                for (auto& C : S->Cases)
                {
                    auto* Enemy=C.Enemy.Get();if (!Enemy) { Check(false,TEXT("Scenario enemy remains present"));Finish();return; }
                    const bool bSimulating=Enemy->GetMesh()->IsAnySimulatingPhysics();
                    if (C.SimStart<0 && !bSimulating && !C.bPrePhysicsWallOverlap)
                    {
                        // Observe the authored dying pose before production clearance has a chance to
                        // remove its overlap. A prevented wall penetration need not become a later contact.
                        for (const auto& Setup : Enemy->GetMesh()->GetPhysicsAsset()->SkeletalBodySetups)
                        {
                            auto* Body=Setup ? Enemy->GetMesh()->GetBodyInstance(Setup->BoneName) : nullptr;
                            if (!Body || !Body->IsValidBodyInstance()) continue;
                            for (auto Wall : C.Walls) if (Wall.IsValid())
                            {
                                const float MTD=Penetration(Body,Wall.Get());
                                if (MTD>0.f)
                                {
                                    C.bPrePhysicsWallOverlap=true;
                                    S->Log+=FString::Printf(TEXT("INFO Case=%s PrePhysicsWallOverlap=1 Bone=%s MTD=%.5fcm DeathElapsed=%.5fs\n"),*C.Name,*Setup->BoneName.ToString(),MTD,Now-S->Mark);
                                    break;
                                }
                            }
                            if (C.bPrePhysicsWallOverlap) break;
                        }
                    }
                    if (C.SimStart<0 && bSimulating)
                    {
                        C.SimStart=Now;S->Log+=FString::Printf(TEXT("INFO Case=%s MMNotifyRagdollDelay=%.5fs\n"),*C.Name,Now-S->Mark);
                    }
                    if (C.SimStart<0) { bAllDone=false;if (Now-S->Mark>10.) { Check(false,TEXT("Authored death notify failed to start real Chaos simulation"));Finish();return; }continue; }
                    if (Now-C.SimStart<=6.1) Sample(C,S->Run,Now,S->CSV);
                    if (Now-C.SimStart<6.) bAllDone=false;
                }
                if (!bAllDone) return;
                for (const auto& C : S->Cases)
                {
                    Check(C.bFinite && C.Samples>0 && C.LateSamples>0,TEXT("Real per-body physics and late jitter samples are finite"));
                    Check(C.ExpectedBodies>0 && C.MaxSimBodies==C.ExpectedBodies,TEXT("All bodies in the saved PhysicsAsset entered simulation"));
                    Check(C.ContactSamples>0 || C.bPrePhysicsWallOverlap,TEXT("An actual prephysics wall overlap or later ragdoll wall contact was observed"));
                    Check(C.Enemy->IsRPGTrooper() ? !C.bWeaponObserved : C.bWeaponObserved,TEXT("Production rifle-drop and retained RPG-weapon policies are preserved"));
                }
                S->Photo=0;S->Step=3;S->Mark=Now;return;
            }
            if (S->Step==3)
            {
                if (S->Photo>=S->Cases.Num()) { Finish();return; }
                if (auto* Camera=S->Camera.Get())
                {
                    const auto& C=S->Cases[S->Photo];const FVector Target=C.LastPelvis+FVector(0,0,30);
                    const FVector Position=C.FloorCenter+FVector(270,300,180);Camera->SetActorLocationAndRotation(Position,(Target-Position).Rotation());Controller->SetViewTarget(Camera);
                }
                S->Mark=Now;S->Step=4;return;
            }
            if (S->Step==4)
            {
                if (Now-S->Mark<.3) return;
                FScreenshotRequest::RequestScreenshot(Folder/(S->Run+TEXT("-")+S->Cases[S->Photo].Name+TEXT(".png")),false,false);
                ++S->Photo;S->Mark=Now;S->Step=5;return;
            }
            if (S->Step==5 && Now-S->Mark>.2) S->Step=3;
        }),.02f,true);
    }
};
static FAutoConsoleCommand GOBRagdollStabilityPIE(TEXT("OB.Enemy.RagdollPIE"),
    TEXT("Disposable saved-enemy Chaos wall-contact measurements. -RagdollRun=baseline|nolock|stabilized|grounded; -RagdollGrounded lifts only the crouch fixture above the floor. Exits this editor."),
    FConsoleCommandDelegate::CreateStatic(&FOBRagdollStabilityMapSmoke::Start));
#endif
