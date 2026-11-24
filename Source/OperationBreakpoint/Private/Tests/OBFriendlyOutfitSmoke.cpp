#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Character/OBAllyNavigationComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBModularSoldierComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"

namespace OBFriendlyOutfitSmoke
{
bool Check(AOBCharacter* Character,bool bStage)
{
    auto* Appearance=Character->GetSoldierAppearanceComponent();
    auto* Leader=Character->GetMesh();
    auto Has=[&](const TCHAR* Name) { return Appearance->OutfitMeshes.ContainsByPredicate([Name](const auto& Mesh) { return Mesh && Mesh->GetName()==Name; }); };
    bool Pass=Leader->GetSkeletalMeshAsset()==Appearance->BodyMesh && Leader->GetAnimClass()==Appearance->AnimationClass
        && Leader->DoesSocketExist(TEXT("WeaponSocket")) && Leader->GetPhysicsAsset()
        && Appearance->GetVisualParts().Num()==(bStage ? 11 : 7)
        && Has(bStage ? TEXT("SKM_Helmet2_v2") : TEXT("SKM_Helmet1"))
        && Has(TEXT("SKM_Binoculars2"))==bStage && Has(TEXT("SKM_Headset"))==bStage
        && Has(TEXT("SKM_Balaclava1_Head"))==bStage && Has(TEXT("SKM_Head"))==!bStage;
    for (USkeletalMeshComponent* Part:Appearance->GetVisualParts())
    {
        const bool Rigid=Part->GetSkeletalMeshAsset()->GetName()==TEXT("SKM_Binoculars2");
        Pass &= Part->IsRegistered() && Part->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
        Pass &= Rigid ? Part->GetAttachParent()==Leader && Part->GetAttachSocketName()==TEXT("head")
            : Part->LeaderPoseComponent.Get()==Leader && Part->GetSocketLocation(TEXT("head")).Equals(Leader->GetSocketLocation(TEXT("head")),.1f);
        if (Part->GetSkeletalMeshAsset()->GetName()==TEXT("SKM_Torso_Long"))
            Pass &= Part->GetMaterial(0)->GetName()==(bStage ? TEXT("MI_Stage_M_Torso_Pattern8_Special_Inst") : TEXT("MI_Training_M_Torso_Pattern8_Special_Flat_Inst"));
    }
    UE_LOG(LogTemp,Display,TEXT("FRIENDLY_OUTFIT actor=%s kit=%s parts=%d result=%s"),*Character->GetName(),
        bStage ? TEXT("special_forces") : TEXT("training"),Appearance->GetVisualParts().Num(),Pass ? TEXT("PASS") : TEXT("FAIL"));
    return Pass;
}

USceneCaptureComponent2D* Portrait(AOBCharacter* Character,bool Close)
{
    auto* World=Character->GetWorld();
    auto* Owner=World->SpawnActor<AActor>();
    auto* Capture=NewObject<USceneCaptureComponent2D>(Owner);
    Owner->SetRootComponent(Capture);Owner->AddInstanceComponent(Capture);
    auto* Target=NewObject<UTextureRenderTarget2D>(Capture);
    Target->ClearColor=FLinearColor(.035f,.04f,.045f,1.f);
    Target->RenderTargetFormat=ETextureRenderTargetFormat::RTF_RGBA8;
    Target->InitCustomFormat(1024,1024,PF_B8G8R8A8,false);
    Capture->TextureTarget=Target;Capture->CaptureSource=ESceneCaptureSource::SCS_BaseColor;
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowOnlyActorComponents(Character);
    if (Close) Capture->RemoveShowOnlyComponent(Character->GetWeaponMesh());
    Capture->FOVAngle=Close ? 27.f : 38.f;
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
    Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetVolumetricFog(false);
    Capture->RegisterComponent();
    const FVector Aim=Close ? Character->GetMesh()->GetSocketLocation(TEXT("head"))-FVector(0,0,8) : Character->GetActorLocation()-FVector(0,0,8);
    const FVector Location=Aim+Character->GetActorForwardVector()*(Close ? 115.f : 310.f)
        +Character->GetActorRightVector()*(Close ? 65.f : 95.f)+FVector(0,0,Close ? 12.f : 15.f);
    Capture->SetWorldLocationAndRotation(Location,(Aim-Location).Rotation());
    return Capture;
}

void Run(UWorld* World,bool bStage)
{
    struct FState { int32 Step=0;double Started=0,Mark=0;bool Pass=true;FTimerHandle Timer;TMap<FString,TWeakObjectPtr<USceneCaptureComponent2D>> Captures; };
    auto State=MakeShared<FState>();State->Started=World->GetTimeSeconds();
    const FString Folder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/FriendlyOutfits")/(bStage ? TEXT("Stage_01") : TEXT("TutorialMap")));
    IFileManager::Get().MakeDirectory(*Folder,true);
    TWeakObjectPtr<UWorld> Weak=World;
    World->GetTimerManager().SetTimer(State->Timer,FTimerDelegate::CreateLambda([State,Weak,Folder,bStage]() {
        auto* W=Weak.Get();if (!W) return;
        auto End=[&](bool Pass) {
            W->GetTimerManager().ClearTimer(State->Timer);
            UE_LOG(LogTemp,Display,TEXT("FRIENDLY_OUTFIT outcome=%s map=%s"),Pass ? TEXT("PASS") : TEXT("FAIL"),*W->GetMapName());
            FFileHelper::SaveStringToFile(Pass ? TEXT("PASS") : TEXT("FAIL"),*(Folder/TEXT("result.txt")));
            GEditor->RequestEndPlayMap();
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Pass](float) { FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);return false; }),1.f);
        };
        if (W->GetTimeSeconds()-State->Started>150) { End(false);return; }
        auto* PC=W->GetFirstPlayerController();auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
        if (!Player || W->GetTimeSeconds()-State->Started<1.f) return;
        if (State->Step==0)
        {
            Player->GetStageFlowComponent()->SuspendForQA();
            Player->GetPlayerSplineComponent()->StopTrainingMovement();
            Player->GetPlayerSplineComponent()->SetComponentTickEnabled(false);
            Player->GetCharacterMovement()->DisableMovement();
            TArray<AOBCharacter*> Characters{Player};
            for (TActorIterator<AOBAllyCharacter> It(W);It;++It) Characters.Add(*It);
            if (!bStage && Characters.Num()==1)
            {
                auto* Class=LoadClass<AOBAllyCharacter>(nullptr,TEXT("/Game/Blueprints/Ally/BP_AllyCharacter.BP_AllyCharacter_C"));
                FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                if (Class) Characters.Add(W->SpawnActor<AOBAllyCharacter>(Class,Player->GetActorLocation()+Player->GetActorRightVector()*250,Player->GetActorRotation(),Params));
            }
            State->Pass &= Characters.Num()==(bStage ? 3 : 2);
            for (auto* Character:Characters)
            {
                if (!Character) { State->Pass=false;continue; }
                if (auto* Ally=Cast<AOBAllyCharacter>(Character)) Ally->GetRouteComponent()->StopRoute();
                Character->GetCharacterMovement()->DisableMovement();
                State->Pass &= Check(Character,bStage);
                Character->RerunConstructionScripts();
                State->Pass &= Check(Character,bStage);
                Character->SetActorHiddenInGame(false);
                if (auto* Ally=Cast<AOBAllyCharacter>(Character))
                {
                    Ally->GetRouteComponent()->SetComponentTickEnabled(false);
                    Ally->GetNavigationComponent()->StopNavigation();
                    Ally->GetNavigationComponent()->SetComponentTickEnabled(false);
                }
                Character->GetCharacterMovement()->DisableMovement();
                Character->GetCharacterMovement()->SetComponentTickEnabled(false);
                if (Character==Player)
                {
                    auto* Camera=Player->GetPlayerCamera();auto* Appearance=Player->GetSoldierAppearanceComponent();
                    Camera->SetRelativeLocation(FVector(60,0,64));Appearance->UpdateFirstPersonVisibility();
                    for (USkeletalMeshComponent* Part:Appearance->GetVisualParts())
                    {
                        const FString Name=Part->GetSkeletalMeshAsset()->GetName();
                        if (Name.Contains(TEXT("Head")) || Name.Contains(TEXT("Helmet")) || Name.Contains(TEXT("Balaclava")) || Name.Contains(TEXT("Binocular")))
                            State->Pass &= Part->bOwnerNoSee;
                    }
                    Camera->SetRelativeLocation(FVector(-300,0,180));Appearance->UpdateFirstPersonVisibility();
                    for (USkeletalMeshComponent* Part:Appearance->GetVisualParts()) State->Pass &= !Part->bOwnerNoSee;
                }
                State->Captures.Add(Character->GetName()+TEXT("_Full.png"),Portrait(Character,false));
                State->Captures.Add(Character->GetName()+TEXT("_Head.png"),Portrait(Character,true));
            }
            State->Step=1;State->Mark=W->GetTimeSeconds();
        }
        else if (State->Step==1 && W->GetTimeSeconds()-State->Mark>8 && (!GShaderCompilingManager || !GShaderCompilingManager->IsCompiling()))
        {
            for (const auto& Pair:State->Captures) if (auto* Capture=Pair.Value.Get())
            {
                Capture->CaptureScene();FlushRenderingCommands();TArray<FColor> Pixels;
                State->Pass &= Capture->TextureTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
                for (auto& Pixel:Pixels) Pixel.A=255;
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1024,1024,Pixels,PNG);
                State->Pass &= FFileHelper::SaveArrayToFile(PNG,*(Folder/Pair.Key));
            }
            End(State->Pass);
        }
    }),.1f,true);
}
}

static FAutoConsoleCommand GOBFriendlyOutfitPIE(TEXT("OB.Soldier.OutfitPIE"),
    TEXT("Verify saved friendly outfits, first-person visibility and portrait renders on Stage_01 or -OutfitTraining. Exits this editor."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        const bool bStage=!FParse::Param(FCommandLine::Get(),TEXT("OutfitTraining"));
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=bStage ? TEXT("/Game/Maps/Stage_01") : TEXT("/Game/Maps/TutorialMap");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false;GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started,bStage](float) {
            if (GEditor && GEditor->PlayWorld) { OBFriendlyOutfitSmoke::Run(GEditor->PlayWorld,bStage);return false; }
            if (FPlatformTime::Seconds()-Started>90) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.1f);
    }));
#endif
