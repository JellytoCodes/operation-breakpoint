#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBTrainingTarget.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingImpactTest,"OperationBreakpoint.Training.ImpactDecals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingImpactTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Target=World->SpawnActor<AOBTrainingTarget>();
    const FVector Point(5,12,20), Normal=FVector::ForwardVector;
    FHitResult Hit(Target,Target->BoardMesh,Point,Normal); Hit.bBlockingHit=true; Hit.ImpactPoint=Point; Hit.ImpactNormal=Normal;
    auto* Decal=Target->SpawnBulletHole(Hit);
    if (!TestNotNull(TEXT("Board hit spawns bullet hole"),Decal)) return false;
    TestTrue(TEXT("Material is an actual deferred decal"),Decal->GetDecalMaterial()->GetMaterial()->MaterialDomain==MD_DeferredDecal);
    TestTrue(TEXT("Hole is attached to the struck board"),Decal->GetAttachParent()==Target->BoardMesh);
    TestTrue(TEXT("Hole is located at impact surface"),Decal->GetComponentLocation().Equals(Point+Normal*.05f,.001));
    TestTrue(TEXT("Decal projects into board"),FVector::DotProduct(Decal->GetForwardVector(),Normal)<-.999);
    const FVector Local=Decal->GetRelativeLocation();
    Target->SetActorLocationAndRotation(FVector(500,25,10),FRotator(0,90,0));
    Target->SetTargetEnabled(false,true);
    TestTrue(TEXT("Folding and moving keep the same local shot location"),Decal->GetRelativeLocation().Equals(Local,.001));
    TestTrue(TEXT("World hole follows board pivot"),Decal->GetComponentLocation().Equals(Target->BoardMesh->GetComponentTransform().TransformPosition(Local),.001));
    Target->BoardMesh->SetRelativeScale3D(FVector(.1,2,3));
    TestTrue(TEXT("Scaled board does not distort hole diameter"),Decal->GetComponentScale().Equals(FVector::OneVector,.001));
    Target->bShowBulletHoles=false;
    TestNull(TEXT("Editor toggle disables impacts"),Target->SpawnBulletHole(Hit));
    Target->bShowBulletHoles=true;
    FHitResult Wrong(Target,Target->BaseMesh,Point,Normal); Wrong.bBlockingHit=true;
    TestNull(TEXT("Stand is not painted as scoring board"),Target->SpawnBulletHole(Wrong));
    Target->MaxBulletHoles=2;
    for (int32 I=0;I<5;++I) Target->SpawnBulletHole(Hit);
    TArray<UDecalComponent*> Holes; Target->GetComponents(Holes);
    TestEqual(TEXT("Per-target cap releases older holes"),Holes.Num(),2);
    Target->ClearBulletHoles(); Holes.Reset(); Target->GetComponents(Holes);
    TestEqual(TEXT("Fresh course clears old holes"),Holes.Num(),0);
    return !HasAnyErrors();
}

#if WITH_EDITOR
#include "Training/OBTrainingDirector.h"
#include "Character/OBPlayerCharacter.h"
#include "Core/OBPlayerController.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

class FOBTrainingImpactSmoke
{
public:
    static void Finish(bool bPass)
    {
        UE_LOG(LogTemp,Display,TEXT("IMPACT_MAP outcome=%s"),bPass ? TEXT("PASS") : TEXT("FAIL"));
        if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bPass](float) {
            FPlatformMisc::RequestExitWithStatus(false,bPass ? 0 : 1); return false;
        }),1.f);
    }
    static void Run(UWorld* World)
    {
        struct FState { int32 Stage=0; double Start=0; FTimerHandle Timer; FString Image,Before; };
        auto S=MakeShared<FState>(); S->Start=World->GetTimeSeconds();
        S->Image=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/TrainingImpacts/ZeroingBulletHoles.png"));
        S->Before=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/TrainingImpacts/ZeroingBefore.png"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(S->Image),true);
        TWeakObjectPtr<UWorld> WeakWorld=World;
        World->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,WeakWorld]() {
            auto* W=WeakWorld.Get(); if (!W) return;
            auto End=[&](bool bPass) { W->GetTimerManager().ClearTimer(S->Timer); Finish(bPass); };
            if (W->GetTimeSeconds()-S->Start>20) { End(false); return; }
            AOBTrainingDirector* D=nullptr;
            for (TActorIterator<AOBTrainingDirector> It(W);It;++It) { D=*It; break; }
            if (!D || !D->bInitialized) return;
            auto* P=Cast<AOBPlayerCharacter>(D->Controller->GetPawn());
            if (S->Stage==0)
            {
                D->StopPrototypeNarration(); W->GetTimerManager().ClearTimer(D->PrototypeIntroTimer); D->bPrototypeIntroPending=false;
                D->Controller->PlayerCameraManager->SetManualCameraFade(0,FLinearColor::Black,false); D->Controller->PlayerCameraManager->StopCameraFade();
                if (D->PoseInput) D->PoseInput->SetPoseInputEnabled(false);
                D->PrototypeRouteIndex=1; D->BeginPrototypeExercise(EOBTrainingStep::Accuracy);
                auto* T=D->AccuracyTarget.Get(); const FVector Aim=T->BoardMesh->Bounds.Origin;
                FVector N=T->GetScoringPlaneNormal();
                if (FVector::DotProduct(P->GetPlayerCamera()->GetComponentLocation()-Aim,N)<0) N=-N;
                P->bEnableEdgeLook=false;
                P->GetPlayerCamera()->SetWorldLocationAndRotation(Aim+N*120,(-N).Rotation());
                D->Overlay->SetVisibility(ESlateVisibility::Collapsed);
                S->Start=W->GetTimeSeconds(); S->Stage=1;
            }
            else if (S->Stage==1 && W->GetTimeSeconds()-S->Start>3)
            {
                FScreenshotRequest::RequestScreenshot(S->Before,false,false); S->Stage=2;
            }
            else if (S->Stage==2 && W->GetTimeSeconds()-S->Start>4)
            {
                auto* T=D->AccuracyTarget.Get(); const FVector Aim=T->BoardMesh->Bounds.Origin;
                FVector N=T->GetScoringPlaneNormal();
                if (FVector::DotProduct(P->GetPlayerCamera()->GetComponentLocation()-Aim,N)<0) N=-N;
                const FVector Side=FVector::CrossProduct(FVector::UpVector,N).GetSafeNormal();
                for (int32 I=0;I<3;++I)
                {
                    const FVector Point=Aim+FVector::UpVector*(I-1)*2+Side*(I==1 ? 2 : 0);
                    D->Controller->FireAlongRay(Point+N*100,Point-N*100);
                }
                D->StopPrototypeNarration(); T->SetTargetEnabled(true,true);
                TArray<UDecalComponent*> Holes; T->GetComponents(Holes);
                UE_LOG(LogTemp,Display,TEXT("IMPACT_MAP real_board=%s holes=%d"),*T->GetActorLabel(),Holes.Num());
                if (Holes.Num()!=3) { End(false); return; }
                S->Start=W->GetTimeSeconds(); S->Stage=3;
            }
            else if (S->Stage==3 && W->GetTimeSeconds()-S->Start>3)
            { FScreenshotRequest::RequestScreenshot(S->Image,false,false); S->Stage=4; }
            else if (S->Stage==4 && W->GetTimeSeconds()-S->Start>4)
                End(IFileManager::Get().FileSize(*S->Image)>0 && IFileManager::Get().FileSize(*S->Before)>0);
        }),.1f,true);
    }
};
static FAutoConsoleCommand GOBTrainingImpactPIE(TEXT("OB.Training.ImpactPIE"),TEXT("Render three real zeroing impacts on the saved TutorialMap board; exits its process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/TutorialMap");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Start=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Start](float) {
            if (GEditor && GEditor->PlayWorld) { FOBTrainingImpactSmoke::Run(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Start>30) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
#endif
#endif
