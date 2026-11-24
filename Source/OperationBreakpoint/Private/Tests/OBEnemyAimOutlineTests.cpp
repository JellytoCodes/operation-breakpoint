#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Core/OBPlayerController.h"
#include "Stage/OBStageFlowComponent.h"
#include "Widget/OBHUD.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ImageUtils.h"
#include "UnrealClient.h"

namespace OBAimOutlineTest
{
AOBEnemyCharacter* SpawnEnemy(UWorld* W,const FVector& Location,const TCHAR* Path=TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"))
{
    auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,Path);if (!Class) return nullptr;
    FTransform Transform(FRotator(0,180,0),Location);
    auto* Enemy=W->SpawnActorDeferred<AOBEnemyCharacter>(Class,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Enemy) return nullptr;
    Enemy->AutoPossessAI=EAutoPossessAI::Disabled;Enemy->FinishSpawning(Transform);
    if (!Enemy->HasActorBegunPlay()) Enemy->DispatchBeginPlay();
    Enemy->SetCanBeDamaged(true);Enemy->GetCharacterMovement()->DisableMovement();
    Enemy->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Enemy->GetMesh()->RefreshBoneTransforms();return Enemy;
}
FVector BodyTarget(AOBEnemyCharacter* Enemy)
{
    TInlineComponentArray<UBoxComponent*> Boxes(Enemy);
    for (auto* Box:Boxes) if (Box->GetFName()==TEXT("BodyHitBox")) return Box->GetComponentLocation();
    return Enemy->GetActorLocation();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBEnemyAimOutlineTest,"OperationBreakpoint.Combat.AimOutline",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBEnemyAimOutlineTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT { W->DestroyWorld(false);GEngine->DestroyWorldContext(W); };
    W->SetGameInstance(NewObject<UGameInstance>(GEngine));FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));W->SetGameMode(URL);
    if (!W->GetPhysicsScene()) W->CreatePhysicsScene();FEditorScriptExecutionGuard Scripts;
    auto* PC=W->SpawnActor<AOBPlayerController>();
    auto* A=OBAimOutlineTest::SpawnEnemy(W,FVector(2000,0,200));
    auto* B=OBAimOutlineTest::SpawnEnemy(W,FVector(2000,1000,200),TEXT("/Game/Blueprints/Enemy/BP_RPGTrooper.BP_RPGTrooper_C"));
    auto* Hostage=Cast<AOBHostageCharacter>(OBAimOutlineTest::SpawnEnemy(W,FVector(2000,2000,200),TEXT("/Game/Blueprints/Hostage/BP_HostageCharacter.BP_HostageCharacter_C")));
    if (!TestNotNull(TEXT("Authored rifle enemy"),A) || !TestNotNull(TEXT("Authored RPG enemy"),B)) return false;
    auto Aim=[&](AOBEnemyCharacter* E) { const auto Target=OBAimOutlineTest::BodyTarget(E);PC->UpdateAimOutlineAlongRay(Target-FVector(1000,0,0),Target+FVector(100,0,0)); };
    const int32 Ammo=PC->GetCurrentAmmo();
    // The authored soldier hides the animation leader and renders modular outfit parts.
    UMeshComponent* VisibleMesh=nullptr;TInlineComponentArray<UMeshComponent*> Meshes(A);
    for (auto* Part:Meshes) if (Part->IsVisible() && !Part->bHiddenInGame) { VisibleMesh=Part;break; }
    if (!TestNotNull(TEXT("Actual visible soldier mesh"),VisibleMesh)) return false;
    VisibleMesh->SetCustomDepthStencilValue(7);VisibleMesh->SetRenderCustomDepth(true);
    Aim(A);TestTrue(TEXT("Production shot trace selects the live enemy without firing"),A->IsAimOutlined() && VisibleMesh->bRenderCustomDepth && VisibleMesh->CustomDepthStencilValue==253);
    Aim(A);Aim(B);TestTrue(TEXT("Switching targets restores the previous native depth/stencil settings"),!A->IsAimOutlined() && VisibleMesh->bRenderCustomDepth && VisibleMesh->CustomDepthStencilValue==7 && B->IsAimOutlined());
    PC->UpdateAimOutlineAlongRay(FVector(0,9000,0),FVector(100,9000,0));TestFalse(TEXT("Leaving the hit box immediately clears the outline"),B->IsAimOutlined());
    TestEqual(TEXT("Hovering consumes no ammunition"),PC->GetCurrentAmmo(),Ammo);
    if (!TestNotNull(TEXT("Authored hostage loads"),Hostage)) return false;
    Aim(Hostage);TestTrue(TEXT("Real hit trace highlights noncombatant hostage with green stencil"),Hostage->IsAimOutlined() && !Hostage->IsEncounterCombatant() && Hostage->GetMesh()->CustomDepthStencilValue==AOBHostageCharacter::HostageAimOutlineStencil && Hostage->GetMesh()->bRenderCustomDepth);
    auto* Worker=Cast<AOBHostageCharacter>(OBAimOutlineTest::SpawnEnemy(W,FVector(2000,3000,200),TEXT("/Game/Blueprints/Hostage/BP_HostageWorker.BP_HostageWorker_C")));
    if (!TestNotNull(TEXT("Worker hostage variant loads"),Worker)) return false;
    Aim(Worker);TestTrue(TEXT("Worker hostage also uses green and clears previous hostage"),Worker->IsAimOutlined() && Worker->GetMesh()->CustomDepthStencilValue==252 && !Hostage->IsAimOutlined());
    Aim(A);TestTrue(TEXT("Switching hostage to enemy clears green and restores orange"),!Hostage->IsAimOutlined() && !Worker->IsAimOutlined() && !Hostage->GetMesh()->bRenderCustomDepth && A->IsAimOutlined() && VisibleMesh->CustomDepthStencilValue==AOBCharacter::AimOutlineStencil);
    B->SetEncounterCombatant(false);PC->SetOutlinedEnemy(B);TestFalse(TEXT("Noncombatants are not outlined"),B->IsAimOutlined());B->SetEncounterCombatant(true);B->SetCanBeDamaged(true);
    Aim(B);FDamageEvent Damage;B->TakeDamage(10000,Damage,PC,nullptr);TestFalse(TEXT("Death immediately clears the outline before corpse/drop rendering"),B->IsAimOutlined());
    Aim(A);PC->OnUnPossess();TestFalse(TEXT("Unpossessing clears the current target"),A->IsAimOutlined());
    auto* Material=LoadObject<UMaterial>(nullptr,TEXT("/Game/Assets/Materials/M_PP_AimOutline.M_PP_AimOutline"));
    TestTrue(TEXT("Runtime outline material is a saved post-process material"),Material && Material->MaterialDomain==MD_PostProcess);
    return !HasAnyErrors();
}

class FOBEnemyAimOutlineMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();GEditor->RequestPlaySession(Params);
            struct FState { int32 Step=0,ObservedStep=INDEX_NONE;uint64 FrameAt=0;bool OK=true;double Began=FPlatformTime::Seconds(),At=0;FString Log;TWeakObjectPtr<AOBEnemyCharacter> Enemy;FVector2D Aim;int32 Before=0,On=0,After=0;TWeakObjectPtr<UMaterialInstanceDynamic> Diagnostic; };
        const bool HostageCheck=FParse::Param(FCommandLine::Get(),TEXT("AimOutlineHostage"));
        auto S=MakeShared<FState>();const FString Out=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/(HostageCheck ? TEXT("Tests/HostageAimOutline/PIE") : TEXT("Tests/EnemyAimOutline/PIE")));IFileManager::Get().MakeDirectory(*Out,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S,Out,HostageCheck](float) {
            auto Check=[&](bool Good,const TCHAR* What) { S->OK &= Good;S->Log+=FString::Printf(TEXT("%s %s\n"),Good ? TEXT("PASS") : TEXT("FAIL"),What);FFileHelper::SaveStringToFile(S->Log,*(Out/TEXT("progress.txt"))); };
            auto Finish=[&]() { S->Log+=S->OK ? TEXT("outcome=PASS\n") : TEXT("outcome=FAIL\n");FFileHelper::SaveStringToFile(S->Log,*(Out/TEXT("verification.txt")));GEditor->RequestEndPlayMap();const bool OK=S->OK;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) { if (GEditor && GEditor->PlayWorld) return true;FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false; }),.25f); };
            const double Now=FPlatformTime::Seconds();if (Now-S->Began>100) { Check(false,TEXT("Rendered cursor scenario deadline"));Finish();return false; }
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;if (!Player || W->GetTimeSeconds()<1) return true;
            auto Capture=[&](const TCHAR* Name) {
                auto* View=W->GetGameViewport()->Viewport;TArray<FColor> Pixels;TArray64<uint8> PNG;
                if (!View || !View->ReadPixels(Pixels)) { Check(false,TEXT("Actual PIE frame is readable"));return 0; }
                const auto Size=View->GetSizeXY();FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Out/Name));
                int32 Gold=0;for (const auto& C:Pixels) if (HostageCheck ? (C.G>220 && C.R<110 && C.B<150) : (C.R>220 && C.G>100 && C.G<220 && C.B<90)) ++Gold;
                return Gold;
            };
            if (S->Step==0)
            {
                Player->GetStageFlowComponent()->AbortForNavigation();Player->GetActionComponent()->SetExternalActionState(true);
                if (auto* HUD=PC->GetOBHUD()) HUD->HideAllWidgets();
                Player->GetPlayerSplineComponent()->SetComponentTickEnabled(false);Player->GetCharacterMovement()->DisableMovement();
                const FVector Base(100000,100000,5000);auto* Camera=W->SpawnActor<ACameraActor>(Base,FRotator::ZeroRotator);
                Camera->GetCameraComponent()->SetFieldOfView(60);PC->SetViewTarget(Camera);PC->PlayerCameraManager->StopCameraFade();
                S->Enemy=OBAimOutlineTest::SpawnEnemy(W,Base+FVector(600,0,-45),HostageCheck ? TEXT("/Game/Blueprints/Hostage/BP_HostageCharacter.BP_HostageCharacter_C") : TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
                Check(S->Enemy.IsValid() && PC->AimOutlineMaterial.Get(),TEXT("Authored character and runtime outline material load in actual Stage_01 PIE"));
                if (!S->Enemy.IsValid()) { Finish();return false; }
                PC->SetMouseLocation(30,30);S->At=Now;S->Step=1;return true;
            }
            // Loading stalls advance wall time without presenting the changed render state.
            if (S->ObservedStep!=S->Step) { S->ObservedStep=S->Step;S->FrameAt=GFrameCounter; }
            if (Now-S->At<1 || GFrameCounter-S->FrameAt<6) return true;
            if (S->Step==1)
            {
                S->Before=Capture(TEXT("Before.png"));Check(!S->Enemy->IsAimOutlined(),TEXT("Cursor away from enemy starts without outline"));
                const bool Projected=PC->ProjectWorldLocationToScreen(OBAimOutlineTest::BodyTarget(S->Enemy.Get()),S->Aim);
                Check(Projected,TEXT("Enemy body projects into actual gameplay viewport"));PC->SetMouseLocation(FMath::RoundToInt(S->Aim.X),FMath::RoundToInt(S->Aim.Y));S->At=Now;S->Step=2;return true;
            }
            if (S->Step==2)
            {
                if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs()>0) return true;
                S->On=Capture(TEXT("Aimed.png"));Check(S->Enemy->IsAimOutlined(),TEXT("Actual cursor movement enables the live target through normal PlayerTick"));
                Check(S->Enemy->GetAimOutlineStencilValue()==(HostageCheck ? 252 : 253),TEXT("Target uses the correct hostage or enemy outline stencil"));
                Check(PC->AimOutlinePostProcess && PC->AimOutlinePostProcess->GetOwner()==Player && PC->AimOutlinePostProcess->GetProperties().bIsEnabled,TEXT("Outline post-process is owned by the visible player instead of the hidden controller"));
                S->Log+=FString::Printf(TEXT("customdepth=%d pp_registered=%d enabled=%d weight=%f blendables=%d\n"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"))->GetInt(),PC->AimOutlinePostProcess->IsRegistered(),PC->AimOutlinePostProcess->bEnabled,PC->AimOutlinePostProcess->BlendWeight,PC->AimOutlinePostProcess->Settings.WeightedBlendables.Array.Num());
                TInlineComponentArray<UMeshComponent*> Parts(S->Enemy.Get());for (auto* Part:Parts) S->Log+=FString::Printf(TEXT("mesh=%s visible=%d hidden=%d custom=%d stencil=%d\n"),*Part->GetName(),Part->IsVisible(),Part->bHiddenInGame,Part->bRenderCustomDepth,Part->CustomDepthStencilValue);
                S->Log+=FString::Printf(TEXT("%s_pixels before=%d on=%d\n"),HostageCheck ? TEXT("green") : TEXT("orange"),S->Before,S->On);
                Check(S->On>S->Before+100,HostageCheck ? TEXT("Actual rendered image gains green hostage silhouette while aimed") : TEXT("Actual rendered image gains orange enemy silhouette while aimed"));
                auto* MID=UMaterialInstanceDynamic::Create(PC->AimOutlineMaterial.LoadSynchronous(),PC);S->Diagnostic=MID;PC->AimOutlinePostProcess->Settings.WeightedBlendables.Array.Reset();PC->AimOutlinePostProcess->Settings.AddBlendable(MID,1);MID->SetScalarParameterValue(TEXT("DebugMode"),1);
                S->At=Now;S->Step=4;return true;
            }
            if (S->Step==4) { Capture(TEXT("Diagnostic-Stencil.png"));S->Diagnostic->SetScalarParameterValue(TEXT("DebugMode"),2);S->At=Now;S->Step=5;return true; }
            if (S->Step==5) { Capture(TEXT("Diagnostic-Blendable.png"));S->Diagnostic->SetScalarParameterValue(TEXT("DebugMode"),0);PC->SetMouseLocation(30,30);S->At=Now;S->Step=3;return true; }
            if (S->Step==3)
            {
                S->After=Capture(TEXT("Left-Target.png"));Check(!S->Enemy->IsAimOutlined() && !S->Enemy->GetMesh()->bRenderCustomDepth,TEXT("Moving the actual cursor away clears the native mesh flag"));
                Check(S->After<S->On-100,TEXT("Actual colored silhouette disappears after leaving target"));
                PC->SetOutlinedEnemy(S->Enemy.Get());PC->GetOBHUD()->ShowTitleWidget();PC->PlayerTick(.016f);
                Check(!S->Enemy->IsAimOutlined(),TEXT("Opening a menu clears the previous enemy outline"));Finish();return false;
            }
            return true;
        }),.05f);
    }
};
static FAutoConsoleCommand GOBEnemyAimOutlinePIE(TEXT("OB.Enemy.AimOutlinePIE"),TEXT("Verify real cursor enter/exit and rendered enemy outline in PIE."),FConsoleCommandDelegate::CreateStatic(&FOBEnemyAimOutlineMapSmoke::Start));
#endif
