#if WITH_EDITOR
#include "AI/OBEnemyCombatPattern.h"
#include "AI/StateTree/OBStateTreeEnemyPatternTask.h"
#include "Components/StateTreeAIComponentSchema.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace OBEnemyCombatPatternAssets
{
static bool Save(UObject* Asset)
{
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args);
}
template<typename T> T* Asset(const FString& Path)
{
    if (auto* Existing=LoadObject<T>(nullptr,*(Path+TEXT(".")+FPackageName::GetShortName(Path)),nullptr,LOAD_NoWarn)) return Existing;
    auto* Result=NewObject<T>(CreatePackage(*Path),*FPackageName::GetShortName(Path),RF_Public|RF_Standalone|RF_Transactional);
    FAssetRegistryModule::AssetCreated(Result);return Result;
}
static void Build()
{
    FString Report;
    bool OK=true;
    const FString Folder=TEXT("/Game/Blueprints/Enemy/Patterns/");
    auto* Set=Asset<UOBEnemyCombatPatternSet>(Folder+TEXT("DA_EnemyCombatPatterns"));
    const TPair<EOBEnemyCombatPattern,const TCHAR*> Choices[]={
        {EOBEnemyCombatPattern::Crouch,TEXT("ST_Enemy_Crouch")},
        {EOBEnemyCombatPattern::Prone,TEXT("ST_Enemy_Prone")},
        {EOBEnemyCombatPattern::Stand,TEXT("ST_Enemy_Stand")},
        {EOBEnemyCombatPattern::WindowCover,TEXT("ST_Enemy_WindowCover")},
        {EOBEnemyCombatPattern::RPG,TEXT("ST_Enemy_RPG")}};
    for (const auto& Choice : Choices)
    {
        auto* Tree=Asset<UStateTree>(Folder+Choice.Value);
        if (Tree->EditorData) Tree->EditorData->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
        auto* Data=NewObject<UStateTreeEditorData>(Tree,NAME_None,RF_Transactional);Tree->EditorData=Data;
        Data->Schema=NewObject<UStateTreeAIComponentSchema>(Data);
        auto& Root=Data->AddRootState();
        Root.Name=FName(Choice.Value);
        auto Step=[&](const TCHAR* Name,EOBEnemyPatternAction Action,EOBEnemyPosture Posture,bool Cover=false,int32 Shots=5) -> UStateTreeState& {
            auto& State=Root.AddChildState(FName(Name));
            auto& Node=State.AddTask<FOBStateTreeEnemyPatternTask>();Node.SetNodeName(FName(Name));
            auto& Instance=Node.GetInstanceData();Instance.Action=Action;Instance.Posture=Posture;
            Instance.bWindowCoverHold=Cover;Instance.ShotCount=Shots;
            for (const auto& Context : Data->Schema->GetContextDataDescs())
                if (Context.Name==TEXT("Actor")) Data->AddPropertyBinding(FPropertyBindingPath(Context.ID),FPropertyBindingPath(Node.ID,TEXT("Actor")));
            State.AddTransition(EStateTreeTransitionTrigger::OnStateFailed,EStateTreeTransitionType::Succeeded);
            return State;
        };
        TArray<UStateTreeState*> States;
        const bool RPG=Choice.Key==EOBEnemyCombatPattern::RPG;
        const bool Window=Choice.Key==EOBEnemyCombatPattern::WindowCover;
        const auto Posture=Choice.Key==EOBEnemyCombatPattern::Prone ? EOBEnemyPosture::Prone
            : RPG || Window || Choice.Key==EOBEnemyCombatPattern::Crouch ? EOBEnemyPosture::Crouch : EOBEnemyPosture::Stand;
        if (Posture!=EOBEnemyPosture::Stand)
            States.Add(&Step(Posture==EOBEnemyPosture::Prone ? TEXT("Enter Prone") : TEXT("Enter Crouch"),EOBEnemyPatternAction::Posture,Posture,Window));
        if (Window) States.Add(&Step(TEXT("Stand Up"),EOBEnemyPatternAction::Posture,EOBEnemyPosture::Stand));
        if (RPG || Choice.Key==EOBEnemyCombatPattern::Stand)
            States.Add(&Step(TEXT("Initial Reload"),EOBEnemyPatternAction::Reload,Posture));
        auto* Aim=&Step(TEXT("Aim At Player"),EOBEnemyPatternAction::Aim,Posture);States.Add(Aim);
        States.Add(&Step(RPG ? TEXT("Fire 1 Rocket") : TEXT("Fire 5 Rounds"),EOBEnemyPatternAction::Fire,Posture,false,RPG ? 1 : 5));
        States.Add(&Step(TEXT("Reload"),EOBEnemyPatternAction::Reload,Posture));
        States.Add(&Step(TEXT("Health Above Zero"),EOBEnemyPatternAction::CheckAlive,Posture));
        for (int32 Index=0;Index<States.Num()-1;++Index)
            States[Index]->AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded,EStateTreeTransitionType::GotoState,States[Index+1]);
        States.Last()->AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded,EStateTreeTransitionType::GotoState,Window ? States[0] : Aim);
        Data->UpdateBindings();
        FStateTreeCompilerLog Log;FStateTreeCompiler Compiler(Log);
        const bool Compiled=Compiler.Compile(*Tree);Log.DumpToLog(Tree,LogTemp);
        const bool Saved=Compiled && Tree->IsReadyToRun() && Save(Tree);OK &= Saved;
        Report+=FString::Printf(TEXT("%s states=%d compiled=%d saved=%d\n"),Choice.Value,States.Num(),Compiled,Saved);
        Set->Trees.Add(Choice.Key,Tree);
    }
    OK &= Save(Set);
    const FString Output=FPaths::ProjectDir()/TEXT("Build/Testing/Verification/EnemyCombatPatterns");
    IFileManager::Get().MakeDirectory(*Output,true);
    Report+=OK ? TEXT("outcome=PASS\n") : TEXT("outcome=FAIL\n");
    FFileHelper::SaveStringToFile(Report,*(Output/TEXT("state-tree-assets.txt")));
    UE_LOG(LogTemp,Display,TEXT("ENEMY_PATTERNS\n%s"),*Report);
    if (!OK) FPlatformMisc::RequestExitWithStatus(false,1);
}
static FAutoConsoleCommand Command(TEXT("OB.AI.BuildCombatPatterns"),TEXT("Build the five arrival-triggered enemy StateTrees and shared asset set."),FConsoleCommandDelegate::CreateStatic(&Build));
}
#endif
