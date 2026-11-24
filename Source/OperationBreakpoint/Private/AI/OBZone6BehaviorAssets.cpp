#if WITH_EDITOR
#include "AI/StateTree/OBStateTreeZone6GuardTask.h"
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
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace OBZone6BehaviorAssets
{
static bool Save(UObject* Asset)
{
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Asset->GetOutermost(),Asset,*FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args);
}
static void Build()
{
    const FString Path=TEXT("/Game/Blueprints/Stage/AI/ST_Zone6_HostageGuard");
    auto* Tree=LoadObject<UStateTree>(nullptr,*(Path+TEXT(".ST_Zone6_HostageGuard")),nullptr,LOAD_NoWarn);
    if (!Tree)
    {
        Tree=NewObject<UStateTree>(CreatePackage(*Path),TEXT("ST_Zone6_HostageGuard"),RF_Public|RF_Standalone|RF_Transactional);
        FAssetRegistryModule::AssetCreated(Tree);
    }
    if (Tree->EditorData) Tree->EditorData->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    auto* Data=NewObject<UStateTreeEditorData>(Tree,NAME_None,RF_Transactional);Tree->EditorData=Data;
    Data->Schema=NewObject<UStateTreeAIComponentSchema>(Data);
    auto& Root=Data->AddRootState();Root.Name=TEXT("Zone6 Hostage Scene");
    auto& State=Root.AddChildState(TEXT("Guard hostage; far guard reacts on entry"));
    auto& Node=State.AddTask<FOBStateTreeZone6GuardTask>();Node.SetNodeName(TEXT("Watch hostage / entry target"));
    for (const auto& Context:Data->Schema->GetContextDataDescs()) if (Context.Name==TEXT("Actor"))
        Data->AddPropertyBinding(FPropertyBindingPath(Context.ID),FPropertyBindingPath(Node.ID,TEXT("Actor")));
    State.AddTransition(EStateTreeTransitionTrigger::OnStateFailed,EStateTreeTransitionType::Succeeded);
    Data->UpdateBindings();FStateTreeCompilerLog Log;FStateTreeCompiler Compiler(Log);
    bool OK=Compiler.Compile(*Tree);Log.DumpToLog(Tree,LogTemp);OK=OK && Tree->IsReadyToRun() && Save(Tree);
    auto* Markers=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingMarkers.WBP_TrainingMarkers"));
    if (Markers && Markers->WidgetTree)
    {
        if (auto* Bar=Markers->WidgetTree->FindWidget(TEXT("BreachProgress")))
        {
            Markers->WidgetTree->RemoveWidget(Bar);
            Markers->WidgetVariableNameToGuidMap.Remove(TEXT("BreachProgress"));
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Markers);
            FKismetEditorUtilities::CompileBlueprint(Markers);
            OK &= Markers->Status!=BS_Error && Save(Markers);
        }
        OK &= Markers->WidgetTree->FindWidget(TEXT("BreachProgress"))==nullptr;
    }
    else OK=false;
    const FString Out=FPaths::ProjectSavedDir()/TEXT("Tests/Zone6Revision");IFileManager::Get().MakeDirectory(*Out,true);
    FFileHelper::SaveStringToFile(OK ? TEXT("outcome=PASS\n") : TEXT("outcome=FAIL\n"),*(Out/TEXT("assets-built.txt")));
    if (!OK) FPlatformMisc::RequestExitWithStatus(false,1);
}
static FAutoConsoleCommand GOBZone6BehaviorAssetCommand(TEXT("OB.Stage.BuildZone6Tree"),TEXT("Build the Zone6 hostage-guard StateTree and remove the authored breach progress bar."),FConsoleCommandDelegate::CreateStatic(&Build));
}
#endif
