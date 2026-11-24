#if WITH_EDITOR
#include "Training/OBTrainingStatusWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "AssetToolsModule.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Widget/OBHudPanelWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBSubtitleEditorHelpers.h"
#include "Widget/OBSystemMessageWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Engine/UserInterfaceSettings.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Misc/DateTime.h"
#include "AssetImportTask.h"
#include "Engine/Texture2D.h"
#include "Animation/WidgetAnimation.h"
#include "TextureCompiler.h"
#include "RenderingThread.h"
#include "Components/TextWidgetTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/ObjectRedirector.h"
#include "Factories/MaterialFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpression.h"
#include "ShaderCompiler.h"
#include "Training/OBRailTrainingTarget.h"
#include "FileHelpers.h"
#include "EngineUtils.h"

namespace OBTrainingWidgetAssets
{
    void InspectOverlay()
    {
        auto* BP = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
        if (BP && BP->WidgetTree)
        {
            BP->WidgetTree->ForEachWidget([](UWidget* Widget) {
                const auto* Label = Cast<UTextBlock>(Widget);
                UE_LOG(LogTemp, Display, TEXT("OVERLAY_UI name=%s class=%s parent=%s visibility=%d designer_hidden=%d text=%s"), *Widget->GetName(), *Widget->GetClass()->GetName(), *GetNameSafe(Widget->GetParent()), static_cast<int32>(Widget->GetVisibility()), Widget->bHiddenInDesigner, Label ? *Label->GetText().ToString() : TEXT(""));
            });
            for (const auto& Binding : BP->Bindings)
                UE_LOG(LogTemp, Display, TEXT("OVERLAY_BINDING object=%s property=%s function=%s"), *Binding.ObjectName, *Binding.PropertyName.ToString(), *Binding.FunctionName.ToString());
            TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
            for (auto* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
                UE_LOG(LogTemp, Display, TEXT("OVERLAY_GRAPH graph=%s node=%s title=%s"), *Graph->GetName(), *Node->GetName(), *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Replace(TEXT("\n"), TEXT(" ")));
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("TrainingWidgetInspect"))) FPlatformMisc::RequestExitWithStatus(false, BP ? 0 : 1);
    }
    constexpr const TCHAR* Folder = TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components");
    bool Save(UObject* Asset);
    void PrepareWidgetGuids(UWidgetBlueprint* Blueprint)
    {
        TSet<FName> Names;
        Blueprint->WidgetTree->ForEachWidget([&](UWidget* Widget) { Names.Add(Widget->GetFName()); });
        for (const auto& Animation : Blueprint->Animations) if (Animation) Names.Add(Animation->GetFName());
        for (auto It=Blueprint->WidgetVariableNameToGuidMap.CreateIterator();It;++It)
            if (!Names.Contains(It.Key())) It.RemoveCurrent();
        for (FName Name : Names)
            if (!Blueprint->WidgetVariableNameToGuidMap.Contains(Name)) Blueprint->OnVariableAdded(Name);
    }
    UWidgetBlueprint* Create(const TCHAR* Name, UUserWidget* Seed, bool bReplace=false)
    {
        const FString Path = FString(Folder) / Name;
        auto* Existing = LoadObject<UWidgetBlueprint>(nullptr, *(Path + TEXT(".") + Name));
        if (Existing && !bReplace)
        {
            if (Existing->ParentClass != Seed->GetClass())
            {
                Existing->ParentClass = Seed->GetClass();
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Existing);
            }
            PrepareWidgetGuids(Existing);
            FKismetEditorUtilities::CompileBlueprint(Existing);
            return Existing->Status != BS_Error && Save(Existing) ? Existing : nullptr;
        }
        auto* Factory = NewObject<UWidgetBlueprintFactory>();
        Factory->ParentClass = Seed->GetClass();
        auto& Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        auto* Blueprint = Existing ? Existing : Cast<UWidgetBlueprint>(Assets.CreateAsset(Name, Folder, UWidgetBlueprint::StaticClass(), Factory));
        if (!Blueprint) return nullptr;
        if (Blueprint->WidgetTree)
            Blueprint->WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
        Blueprint->WidgetTree = DuplicateObject<UWidgetTree>(Seed->WidgetTree, Blueprint, TEXT("WidgetTree"));
        Blueprint->WidgetTree->ClearFlags(RF_Transient);
        Blueprint->WidgetTree->SetFlags(RF_Transactional);
        Blueprint->WidgetTree->ForEachWidget([](UWidget* Widget) {
            Widget->ClearFlags(RF_Transient);
            Widget->SetFlags(RF_Transactional);
            Widget->bIsVariable = true;
        });
        PrepareWidgetGuids(Blueprint);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass) return nullptr;
        if (const auto* PanelSeed = Cast<UOBHudPanelWidget>(Seed))
            GetMutableDefault<UOBHudPanelWidget>(Blueprint->GeneratedClass)->Role = PanelSeed->Role;
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        const FString Filename = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Filename));
        if (!UPackage::SavePackage(Blueprint->GetPackage(), Blueprint, *Filename, Args)) return nullptr;
        UE_LOG(LogTemp, Display, TEXT("TRAINING_WBP created=%s"), *Path);
        return Blueprint;
    }
    bool Preview(UWidgetBlueprint* Blueprint, const TCHAR* Filename, EOBOverlayPreviewMode Mode=EOBOverlayPreviewMode::Layout,
        FIntPoint DrawSize=FIntPoint(1280,720), bool bViewportDPI=false)
    {
        auto* Widget = NewObject<UUserWidget>(GetTransientPackage(), Blueprint->GeneratedClass);
        Widget->SetDesignerFlags(EWidgetDesignFlags::Previewing);
        if (auto* Overlay = Cast<UOBPlayerOverlayWidget>(Widget))
        {
            Overlay->DesignerPreview = Mode;
            Overlay->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
        }
        if (!Widget->Initialize()) return false;
        TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
        Widget->SynchronizeProperties();
        if (auto* Result = Cast<UOBTrainingResultWidget>(Widget))
        {
            FOBTrainingResultData Sample; Sample.bPrototypeFlow = true; Sample.bScenarioCompleted = true;
            Sample.RecordedFireSeconds = FString(Filename).Contains(TEXT("Fail")) ? 121 : 75; Sample.RecordedFireLimitSeconds = 120;
            Result->SetResult(Sample);
        }
        if (auto* Overlay = Cast<UOBPlayerOverlayWidget>(Widget))
        {
            if (auto* Result = Overlay->GetTrainingResultWidget())
            {
                TArray<UTexture*> Textures;
                if (auto* Texture=Result->PassStamp.LoadSynchronous()) Textures.Add(Texture);
                if (auto* Texture=Result->FailStamp.LoadSynchronous()) Textures.Add(Texture);
                FTextureCompilingManager::Get().FinishCompilation(Textures);
                FlushRenderingCommands();
                Overlay->SynchronizeProperties();
            }
        }
        if (bViewportDPI)
            SlateWidget=SNew(SDPIScaler).DPIScale(GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(DrawSize))[SlateWidget];
        if (FString(Filename).StartsWith(TEXT("MissionObjectives")))
        {
            auto* Overlay=Cast<UOBPlayerOverlayWidget>(Widget);
            auto* Mission=Overlay ? Cast<UOBMissionWidget>(Overlay->WidgetTree->FindWidget(TEXT("Mission"))) : nullptr;
            if (!Mission) return false;
            const bool bBasic=!FString(Filename).Contains(TEXT("Mock"));
            FOBTrainingResultData Sample; Sample.TotalTargets=6; Sample.UniqueTargetsHit=6;
            if (!FString(Filename).Contains(TEXT("Reset")))
                for (EOBTrainingStep Step : {EOBTrainingStep::WeaponCheck,EOBTrainingStep::Cover})
                { FOBTrainingStepResult Item; Item.Step=Step; Item.Outcome=EOBTrainingOutcome::Passed; Sample.Steps.Add(Item); }
            Mission->UpdateTrainingObjectives(Sample,bBasic);
        }
        FWidgetRenderer Renderer(true);
        // Render again after the text layout settles.
        Renderer.DrawWidget(SlateWidget, FVector2D(DrawSize));
        // Slate UI materials can request shader permutations on their first paint.
        FlushRenderingCommands();
        if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
        if (auto* Overlay=Cast<UOBPlayerOverlayWidget>(Widget))
            if (auto* Markers=Overlay->GetTrainingMarkers(); Markers && Markers->IsVisible())
                Markers->UpdateDesignerMarkers(Markers->GetCachedGeometry().GetLocalSize());
        auto* Target = Renderer.DrawWidget(SlateWidget, FVector2D(DrawSize));
        if (!Target) return false;
        TArray<FColor> Pixels;
        if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)) return false;
        TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(DrawSize.X, DrawSize.Y, Pixels, PNG);
        const FString Output = FPaths::ProjectSavedDir() / TEXT("Tests/TrainingWidgets") / Filename;
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Output));
        return FFileHelper::SaveArrayToFile(PNG, *Output);
    }
    bool Save(UObject* Asset)
    {
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
        return UPackage::SavePackage(Asset->GetPackage(), Asset, *Filename, Args);
    }
    void PreviewMissionObjectives()
    {
        auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
        bool bSuccess=Overlay!=nullptr;
        if (bSuccess)
            for (const TCHAR* Name : {TEXT("MissionObjectivesBasic.png"),TEXT("MissionObjectivesMock.png"),TEXT("MissionObjectivesReset.png")})
                bSuccess &= Preview(Overlay,Name,EOBOverlayPreviewMode::TrainingPractice,FIntPoint(1920,1080),true);
        UE_LOG(LogTemp,Display,TEXT("MISSION_OBJECTIVES preview=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (FParse::Param(FCommandLine::Get(),TEXT("MissionObjectivesPreview"))) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
    }
    void OrganizePlayerOverlay()
    {
        auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        Registry.SearchAllAssets(true);
        const FString OldFolder=TEXT("/Game/Blueprints/Widget/Training");
        const FString NewRoot=TEXT("/Game/Blueprints/Widget/PlayerOverlay");
        const FString OldOverlay=TEXT("/Game/Blueprints/Widget/WBP_PlayerOverlay");
        TArray<FAssetData> Widgets, OldAssets;
        Registry.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(),Widgets,true);
        Registry.GetAssetsByPath(FName(*OldFolder),OldAssets,true);
        if (const auto Overlay=Registry.GetAssetByObjectPath(FSoftObjectPath(OldOverlay+TEXT(".WBP_PlayerOverlay"))); Overlay.IsValid()) OldAssets.Add(Overlay);

        bool bSuccess=true;
        TSet<FName> BackupPackages;
        for (const auto& Data : Widgets) if (Data.PackageName.ToString().StartsWith(TEXT("/Game/"))) BackupPackages.Add(Data.PackageName);
        for (const auto& Data : OldAssets)
        {
            BackupPackages.Add(Data.PackageName);
            TArray<FName> Referencers; Registry.GetReferencers(Data.PackageName,Referencers);
            for (FName Ref : Referencers) if (Ref.ToString().StartsWith(TEXT("/Game/"))) BackupPackages.Add(Ref);
        }
        auto& PlatformFile=FPlatformFileManager::Get().GetPlatformFile();
        for (FName Package : BackupPackages)
        {
            FString Source=FPackageName::LongPackageNameToFilename(Package.ToString(),FPackageName::GetAssetPackageExtension());
            if (!PlatformFile.FileExists(*Source)) Source=FPackageName::LongPackageNameToFilename(Package.ToString(),FPackageName::GetMapPackageExtension());
            if (!PlatformFile.FileExists(*Source)) continue;
            const FString Dest=FPaths::ProjectSavedDir()/TEXT("Backups/PlayerOverlayFolders")/Package.ToString().Mid(6)+FPaths::GetExtension(Source,true);
            PlatformFile.CreateDirectoryTree(*FPaths::GetPath(Dest));
            if (!PlatformFile.FileExists(*Dest)) bSuccess &= PlatformFile.CopyFile(*Dest,*Source);
        }
        TArray<FAssetRenameData> Moves;
        TArray<FString> OldPaths;
        for (const auto& Data : OldAssets)
        {
            if (Data.AssetClassPath==UObjectRedirector::StaticClass()->GetClassPathName()) continue;
            auto* Asset=Data.GetAsset();
            if (!Asset) { bSuccess=false; continue; }
            const FString OldPath=Data.PackagePath.ToString();
            const FString Dest=Data.PackageName.ToString()==OldOverlay ? NewRoot :
                OldPath.StartsWith(OldFolder/TEXT("Art")) ? NewRoot+OldPath.Mid(OldFolder.Len()) :
                NewRoot/TEXT("Components")+OldPath.Mid(OldFolder.Len());
            Moves.Emplace(Asset,Dest,Data.AssetName.ToString()); OldPaths.Add(Data.GetSoftObjectPath().ToString());
        }
        auto& Assets=FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        if (bSuccess && Moves.Num()) bSuccess=Assets.RenameAssets(Moves);
        if (bSuccess)
        {
            TArray<UObjectRedirector*> Redirectors;
            for (const FString& Path : OldPaths)
                if (auto* Redirector=FindObject<UObjectRedirector>(nullptr,*Path)) Redirectors.Add(Redirector);
            if (Redirectors.Num()) Assets.FixupReferencers(Redirectors,false,ERedirectFixupMode::DeleteFixedUpRedirectors);
            Widgets.Reset(); Registry.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(),Widgets,true);
            int32 WidgetCount=0, TextCount=0, ChangedCount=0;
            for (const auto& Data : Widgets)
            {
                if (!Data.PackageName.ToString().StartsWith(TEXT("/Game/"))) continue;
                auto* BP=Cast<UWidgetBlueprint>(Data.GetAsset());
                if (!BP || !BP->WidgetTree) { bSuccess=false; continue; }
                ++WidgetCount; bool bChanged=false;
                BP->WidgetTree->ForEachWidget([&](UWidget* Widget) {
                    if (auto* Text=Cast<UTextLayoutWidget>(Widget))
                    {
                        ++TextCount;
                        if (Text->GetAutoWrapText()) { Text->Modify(); Text->SetAutoWrapText(false); bChanged=true; ++ChangedCount; }
                    }
                });
                if (bChanged)
                {
                    BP->Modify(); FBlueprintEditorUtils::MarkBlueprintAsModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
                    bSuccess &= BP->Status!=BS_Error && Save(BP);
                }
                UE_LOG(LogTemp,Display,TEXT("WIDGET_NO_WRAP asset=%s changed=%d"),*BP->GetPathName(),bChanged);
            }
            auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,*(NewRoot/TEXT("WBP_PlayerOverlay.WBP_PlayerOverlay")));
            bSuccess &= Overlay!=nullptr;
            if (Overlay)
            {
                FKismetEditorUtilities::CompileBlueprint(Overlay); bSuccess &= Overlay->Status!=BS_Error && Save(Overlay);
                if (bSuccess && FParse::Param(FCommandLine::Get(),TEXT("TrainingWidgetPreview")))
                    bSuccess=Preview(Overlay,TEXT("OverlayNoWrap.png"),EOBOverlayPreviewMode::TrainingPractice);
            }
            UE_LOG(LogTemp,Display,TEXT("PLAYER_OVERLAY_FOLDERS moves=%d widgets=%d texts=%d wrap_disabled=%d"),Moves.Num(),WidgetCount,TextCount,ChangedCount);
        }
        UE_LOG(LogTemp,Display,TEXT("PLAYER_OVERLAY_FOLDERS setup=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (FParse::Param(FCommandLine::Get(),TEXT("PlayerOverlayFolderSetup"))) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
    }
    void FixOverlayDesigner()
    {
        auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
        bool bSuccess=BP && BP->WidgetTree && BP->WidgetTree->RootWidget;
        if (bSuccess)
        {
            BP->Modify();
            BP->WidgetTree->RootWidget->bHiddenInDesigner=false;
            BP->WidgetTree->RootWidget->SetVisibility(ESlateVisibility::Visible);
            for (const TCHAR* Name : {TEXT("Guide"),TEXT("Subtitle"),TEXT("Mission"),TEXT("Timer"),TEXT("Life"),TEXT("Pause"),TEXT("TrainingMarkers"),TEXT("TrainingResult")})
            {
                auto* Child=BP->WidgetTree->FindWidget(Name);
                if (!Child) { bSuccess=false; continue; }
                Child->Modify(); Child->bHiddenInDesigner=false;
                const bool bModal=Child->GetFName()==TEXT("Pause") || Child->GetFName()==TEXT("TrainingResult") || Child->GetFName()==TEXT("TrainingMarkers");
                Child->SetVisibility(bModal ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
            }
            PrepareWidgetGuids(BP);
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
            bSuccess &= BP->Status != BS_Error && Save(BP);
            if (bSuccess && FParse::Param(FCommandLine::Get(),TEXT("TrainingWidgetPreview")))
                bSuccess = Preview(BP,TEXT("OverlayLayout.png")) && Preview(BP,TEXT("OverlayStage.png"),EOBOverlayPreviewMode::Stage) &&
                    Preview(BP,TEXT("OverlayPractice.png"),EOBOverlayPreviewMode::TrainingPractice) && Preview(BP,TEXT("OverlayPause.png"),EOBOverlayPreviewMode::Pause) &&
                    Preview(BP,TEXT("OverlayResult.png"),EOBOverlayPreviewMode::ResultPass);
        }
        UE_LOG(LogTemp,Display,TEXT("OVERLAY_DESIGNER setup=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (FParse::Param(FCommandLine::Get(),TEXT("TrainingWidgetDesignerFix"))) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
    }
    void FixSubtitleRow()
    {
        auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
        bool bSuccess=OBSubtitleEditor::ValidateEmbeddedSubtitle(BP);
        if (bSuccess) { PrepareWidgetGuids(BP); bSuccess=Save(BP); }
        if (bSuccess && FParse::Param(FCommandLine::Get(),TEXT("TrainingWidgetPreview")))
            bSuccess=Preview(BP,TEXT("OverlaySubtitleRow.png"),EOBOverlayPreviewMode::TrainingPractice);
        UE_LOG(LogTemp,Display,TEXT("SUBTITLE_ROW update=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (FParse::Param(FCommandLine::Get(),TEXT("SubtitleRowSetup"))) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
    }

    void UpdateTrainingPanels()
    {
        auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
        bool bSuccess=OBSubtitleEditor::ValidateEmbeddedSubtitle(BP);
        if (bSuccess)
        {
            BP->Modify();
            auto* Mission=BP->WidgetTree->FindWidget(TEXT("Mission"));
            auto* Subtitle=BP->WidgetTree->FindWidget(TEXT("Subtitle"));
            auto* MissionSlot=Mission ? Cast<UCanvasPanelSlot>(Mission->Slot) : nullptr;
            auto* SubtitleSlot=Subtitle ? Cast<UCanvasPanelSlot>(Subtitle->Slot) : nullptr;
            bSuccess=MissionSlot && SubtitleSlot;
            if (MissionSlot) { MissionSlot->SetAnchors(FAnchors(0,0)); MissionSlot->SetAlignment(FVector2D::ZeroVector); MissionSlot->SetAutoSize(false); MissionSlot->SetOffsets(FMargin(32,32,360,250)); }
            if (SubtitleSlot) { SubtitleSlot->SetAnchors(FAnchors(.1f,1.f,.9f,1.f)); SubtitleSlot->SetAlignment(FVector2D(0,1)); SubtitleSlot->SetAutoSize(false); SubtitleSlot->SetOffsets(FMargin(0,-32,0,170)); }
            PrepareWidgetGuids(BP); FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
            bSuccess &= BP->Status!=BS_Error && Save(BP);
        }
        UE_LOG(LogTemp,Display,TEXT("TRAINING_PANELS update=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (FParse::Param(FCommandLine::Get(),TEXT("TrainingPanelsSetup"))) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
    }

    UTexture2D* ImportStamp(const TCHAR* Name, const TCHAR* Source)
    {
        const FString Dest = TEXT("/Game/Blueprints/Widget/PlayerOverlay/Art");
        auto* Texture = LoadObject<UTexture2D>(nullptr, *(Dest / Name + TEXT(".") + Name));
        if (!Texture)
        {
            TStrongObjectPtr<UAssetImportTask> Task(NewObject<UAssetImportTask>());
            Task->Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Support/Submission/Assets/Art/TrainingStamps") / Source);
            Task->DestinationPath = Dest; Task->DestinationName = Name; Task->bAutomated = true;
            FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks({Task.Get()});
            Texture = LoadObject<UTexture2D>(nullptr, *(Dest / Name + TEXT(".") + Name));
        }
        if (Texture)
        {
            Texture->LODGroup = TEXTUREGROUP_UI; Texture->CompressionSettings = TC_EditorIcon;
            Texture->MipGenSettings = TMGS_NoMipmaps; Texture->SRGB = true;
            Texture->UpdateResource(); if (!Save(Texture)) return nullptr;
        }
        return Texture;
    }
    struct FPlacement
    {
        FAnchors Anchors; FMargin Offsets; FVector2D Alignment; int32 Z=0; bool bAuto=false;
        FPlacement(FAnchors A, FMargin O=FMargin(0), FVector2D Align=FVector2D::ZeroVector, int32 Layer=0):Anchors(A),Offsets(O),Alignment(Align),Z(Layer) {}
        void Copy(UWidget* Widget)
        {
            if (auto* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
            { Anchors=Slot->GetAnchors(); Offsets=Slot->GetOffsets(); Alignment=Slot->GetAlignment(); Z=Slot->GetZOrder(); bAuto=Slot->GetAutoSize(); }
        }
        void Apply(UCanvasPanelSlot* Slot) const
        { Slot->SetAnchors(Anchors); Slot->SetOffsets(Offsets); Slot->SetAlignment(Alignment); Slot->SetZOrder(Z); Slot->SetAutoSize(bAuto); }
    };
    void WrapComponentSizes()
    {
        auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
        const TCHAR* Names[]={TEXT("Guide"),TEXT("Pause"),TEXT("Subtitle"),TEXT("Mission"),TEXT("Timer"),TEXT("Life"),TEXT("TrainingMarkers"),TEXT("TrainingResult")};
        TArray<UWidgetBlueprint*> Panels;
        bool bSuccess=Overlay && Overlay->WidgetTree;
        for (const TCHAR* Name : Names)
        {
            if (FString(Name)==TEXT("Subtitle")) { Panels.Add(nullptr); continue; }
            const FString Asset=FString::Printf(TEXT("WBP_%s"),Name);
            auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(FString(Folder)/Asset+TEXT(".")+Asset));
            bSuccess &= BP && BP->WidgetTree && BP->WidgetTree->RootWidget;
            Panels.Add(BP);
        }
        const bool bInspect=FParse::Param(FCommandLine::Get(),TEXT("ComponentSizeBoxInspect"));
        // Use the current 1080p canvas allocation as the baseline, in logical UMG units.
        const FVector2D ReferenceSize(1920,1080);
        auto AllocatedSize=[&](UCanvasPanelSlot* Slot, FVector2D Desired) {
            const auto A=Slot->GetAnchors(); const auto O=Slot->GetOffsets();
            const bool X=A.Minimum.X!=A.Maximum.X, Y=A.Minimum.Y!=A.Maximum.Y;
            return FVector2D(X ? (A.Maximum.X-A.Minimum.X)*ReferenceSize.X-O.Left-O.Right : Slot->GetAutoSize() ? Desired.X : O.Right,
                Y ? (A.Maximum.Y-A.Minimum.Y)*ReferenceSize.Y-O.Top-O.Bottom : Slot->GetAutoSize() ? Desired.Y : O.Bottom);
        };
        if (bSuccess)
        {
            if (FParse::Param(FCommandLine::Get(),TEXT("TrainingWidgetPreview")))
                for (const auto Mode : {EOBOverlayPreviewMode::Stage,EOBOverlayPreviewMode::TrainingGuide,EOBOverlayPreviewMode::TrainingPractice,EOBOverlayPreviewMode::Pause,EOBOverlayPreviewMode::ResultPass,EOBOverlayPreviewMode::MarkersBreach})
                    bSuccess &= Preview(Overlay,*FString::Printf(TEXT("SizeBoxBefore_%d.png"),static_cast<int32>(Mode)),Mode,FIntPoint(1920,1080),true);
            const FString BackupDir=FPaths::ProjectSavedDir()/TEXT("Backups/ComponentSizeBoxes")/FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
            auto& Files=FPlatformFileManager::Get().GetPlatformFile();
            if (!bInspect)
            {
                TArray<UWidgetBlueprint*> Backups=Panels; Backups.Remove(nullptr); Backups.Add(Overlay);
                for (auto* BP : Backups)
                {
                    const FString Source=FPackageName::LongPackageNameToFilename(BP->GetPackage()->GetName(),FPackageName::GetAssetPackageExtension());
                    const FString Dest=BackupDir/BP->GetName()+TEXT(".uasset");
                    Files.CreateDirectoryTree(*FPaths::GetPath(Dest)); bSuccess &= Files.CopyFile(*Dest,*Source);
                }
            }
            auto NewSizeBox=[](UWidgetTree* Tree,FName Name,FVector2D Size,bool bFlexibleWidth=false) {
                auto* Box=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),Name);
                if (!bFlexibleWidth) Box->SetWidthOverride(Size.X);
                Box->SetHeightOverride(Size.Y); return Box;
            };
            for (int32 I=0;bSuccess && I<8;++I)
            {
                if (I==2) continue; // Subtitle is authored directly in the overlay.
                auto* BP=Panels[I]; auto* Tree=BP->WidgetTree.Get();
                auto* Instance=Overlay->WidgetTree->FindWidget(Names[I]);
                auto* Slot=Instance ? Cast<UCanvasPanelSlot>(Instance->Slot) : nullptr;
                if (!Slot) { bSuccess=false; break; }
                TStrongObjectPtr<UUserWidget> Sample(NewObject<UUserWidget>(GetTransientPackage(),BP->GeneratedClass));
                Sample->Initialize(); Sample->TakeWidget()->SlatePrepass();
                const FVector2D Size=AllocatedSize(Slot,Sample->GetDesiredSize());
                UE_LOG(LogTemp,Display,TEXT("COMPONENT_SIZE name=%s size=%s root=%s auto=%d anchors=%s/%s"),Names[I],*Size.ToString(),*GetNameSafe(Tree->RootWidget),Slot->GetAutoSize(),*Slot->GetAnchors().Minimum.ToString(),*Slot->GetAnchors().Maximum.ToString());
                if (bInspect) continue;
                if (I<6)
                {
                    if (Tree->FindWidget(TEXT("BaselineSizeBox"))) continue; // Preserve later Designer edits on reruns.
                    if (Size.X<=0 || Size.Y<=0) { bSuccess=false; break; }
                    auto* OldRoot=Tree->RootWidget.Get();
                    auto* Box=NewSizeBox(Tree,TEXT("BaselineSizeBox"),Size,I==2);
                    Box->SetContent(OldRoot); Tree->RootWidget=Box;
                    const auto A=Slot->GetAnchors();
                    if (I==2)
                    {
                        // The subtitle remains a bottom-centered Name / Spacer / Script row.
                        // Only its height is fixed; available width still follows the overlay anchors.
                        Slot->SetAutoSize(true);
                    }
                    else if (A.Minimum!=A.Maximum)
                    {
                        auto* Fit=Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("FitScaleBox"));
                        Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::DownOnly);
                        auto* FitSlot=Cast<UScaleBoxSlot>(Fit->AddChild(Box));
                        FitSlot->SetHorizontalAlignment(HAlign_Center); FitSlot->SetVerticalAlignment(VAlign_Center);
                        Tree->RootWidget=Fit;
                    }
                    else Slot->SetAutoSize(true);
                }
                else
                {
                    // Marker geometry and result backdrop must still occupy the entire viewport.
                    const TCHAR* Children[]={TEXT("BreachText"),TEXT("TitleText"),TEXT("StampImage"),TEXT("InteractionText")};
                    for (int32 J=I==6 ? 0 : 1;J<(I==6 ? 1 : 4);++J)
                    {
                        auto* Child=Tree->FindWidget(Children[J]);
                        const FName BoxName(*FString::Printf(TEXT("%sSizeBox"),Children[J]));
                        if (Tree->FindWidget(BoxName)) continue;
                        auto* ChildSlot=Child ? Cast<UCanvasPanelSlot>(Child->Slot) : nullptr;
                        auto* Canvas=Child ? Cast<UCanvasPanel>(Child->GetParent()) : nullptr;
                        if (!Canvas || !ChildSlot) { bSuccess=false; break; }
                        FPlacement Placement(FAnchors(0,0)); Placement.Copy(Child);
                        const FVector2D ChildSize=AllocatedSize(ChildSlot,Child->GetDesiredSize());
                        const bool bFlexibleWidth=Placement.Anchors.Minimum.X!=Placement.Anchors.Maximum.X;
                        auto* Box=NewSizeBox(Tree,BoxName,ChildSize,bFlexibleWidth);
                        const int32 ChildIndex=Canvas->GetChildIndex(Child);
                        Canvas->RemoveChild(Child); Box->SetContent(Child);
                        Placement.bAuto=true; Placement.Apply(Canvas->AddChildToCanvas(Box));
                        Canvas->ShiftChild(ChildIndex,Box);
                        UE_LOG(LogTemp,Display,TEXT("COMPONENT_SIZE inner=%s size=%s"),*BoxName.ToString(),*ChildSize.ToString());
                    }
                }
                if (bSuccess)
                {
                    PrepareWidgetGuids(BP); FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
                    FKismetEditorUtilities::CompileBlueprint(BP); bSuccess &= BP->Status!=BS_Error && Save(BP);
                }
            }
            if (bSuccess && !bInspect)
            {
                PrepareWidgetGuids(Overlay); FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Overlay);
                FKismetEditorUtilities::CompileBlueprint(Overlay); bSuccess &= Overlay->Status!=BS_Error && Save(Overlay);
                if (bSuccess && FParse::Param(FCommandLine::Get(),TEXT("TrainingWidgetPreview")))
                    for (const auto Resolution : {FIntPoint(1280,720),FIntPoint(1920,1080),FIntPoint(2560,1080)})
                        for (const auto Mode : {EOBOverlayPreviewMode::Stage,EOBOverlayPreviewMode::TrainingGuide,EOBOverlayPreviewMode::TrainingPractice,EOBOverlayPreviewMode::Pause,EOBOverlayPreviewMode::ResultPass,EOBOverlayPreviewMode::MarkersBreach})
                            bSuccess &= Preview(Overlay,*FString::Printf(TEXT("SizeBoxAfter_%dx%d_%d.png"),Resolution.X,Resolution.Y,static_cast<int32>(Mode)),Mode,Resolution,true);
            }
        }
        UE_LOG(LogTemp,Display,TEXT("COMPONENT_SIZE setup=%s inspect=%d"),bSuccess ? TEXT("PASS") : TEXT("FAIL"),bInspect);
        if (FParse::Param(FCommandLine::Get(),TEXT("ComponentSizeBoxSetup")) || bInspect) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
    }
    TStrongObjectPtr<UOBHudPanelWidget> PanelSeed(EOBHudPanelRole Role, UWidget* Source=nullptr)
    {
        const TSubclassOf<UOBHudPanelWidget> Classes[]={UOBGuideWidget::StaticClass(),UOBPauseWidget::StaticClass(),UOBSubtitleWidget::StaticClass(),
            UOBMissionWidget::StaticClass(),UOBTimerWidget::StaticClass(),UOBLifeWidget::StaticClass()};
        TStrongObjectPtr<UOBHudPanelWidget> Seed(NewObject<UOBHudPanelWidget>(GetTransientPackage(),Classes[static_cast<int32>(Role)]));
        Seed->BuildDefaultLayout(Role);
        if (Source && Role != EOBHudPanelRole::Guide)
        {
            Seed->WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
            auto* SourceTree = Source->GetTypedOuter<UWidgetTree>();
            Seed->WidgetTree = DuplicateObject<UWidgetTree>(SourceTree, Seed.Get(),TEXT("WidgetTree"));
            auto* CopiedRoot = Seed->WidgetTree->FindWidget(Source->GetFName());
            Seed->WidgetTree->RootWidget = CopiedRoot;
            Seed->WidgetTree->RootWidget->Slot = nullptr;
            auto Rename = [&](const TCHAR* Old, const TCHAR* New) {
                if (auto* Widget=Seed->WidgetTree->FindWidget(Old)) Widget->Rename(New, nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
            };
            if (Role==EOBHudPanelRole::Subtitle) Rename(TEXT("Status"),TEXT("BodyText"));
            if (Role==EOBHudPanelRole::Mission) Rename(TEXT("MissionText"),TEXT("BodyText"));
            if (Role==EOBHudPanelRole::Pause) Rename(TEXT("PauseText"),TEXT("BodyText"));
            if (Role==EOBHudPanelRole::Timer) { Rename(TEXT("Text_Zone"),TEXT("TitleText")); Rename(TEXT("Text_Timer"),TEXT("ValueText")); }
            if (Role==EOBHudPanelRole::Life) Rename(TEXT("Text_Life"),TEXT("TitleText"));
        }
        else if (auto* OldBorder = Cast<UBorder>(Source))
        {
            auto* NewBorder = Cast<UBorder>(Seed->WidgetTree->RootWidget);
            NewBorder->SetBrush(OldBorder->Background); NewBorder->SetBrushColor(OldBorder->GetBrushColor()); NewBorder->SetPadding(OldBorder->GetPadding());
            if (auto* OldText=Cast<UTextBlock>(OldBorder->GetContent()))
                for (const TCHAR* Name : {TEXT("BodyText"),TEXT("PromptText")})
                    if (auto* Text=Cast<UTextBlock>(Seed->WidgetTree->FindWidget(Name)))
                    { Text->SetFont(OldText->GetFont()); Text->SetColorAndOpacity(OldText->GetColorAndOpacity()); }
        }
        Seed->WidgetTree->RootWidget->SetVisibility(ESlateVisibility::Visible);
        Seed->WidgetTree->RootWidget->bHiddenInDesigner = false;
        return Seed;
    }
    void CreateAll()
    {
        bool bSuccess = ImportStamp(TEXT("T_TrainingPassStamp"), TEXT("pass_stamp.png")) && ImportStamp(TEXT("T_TrainingFailStamp"), TEXT("fail_stamp.png"));
        auto* OverlayBP = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
        auto* OldStatus = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingStatus.WBP_TrainingStatus"));
        if (!OverlayBP || !OverlayBP->WidgetTree) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
        auto* Canvas = Cast<UCanvasPanel>(OverlayBP->WidgetTree->RootWidget);
        if (!Canvas) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
        auto OldPanel = [&](const TCHAR* Name)->UWidget* { return OldStatus && OldStatus->WidgetTree ? OldStatus->WidgetTree->FindWidget(Name) : nullptr; };
        TArray<FPlacement> Places={FPlacement(FAnchors(.2f,.2f,.8f,.65f),FMargin(0),FVector2D::ZeroVector,50),
            FPlacement(FAnchors(.25f,.3f,.75f,.65f),FMargin(0),FVector2D::ZeroVector,100),
            FPlacement(FAnchors(.05f,.78f,.95f,.78f),FMargin(0,0,0,150),FVector2D::ZeroVector,40),
            FPlacement(FAnchors(.72f,.06f,.96f,.32f),FMargin(0),FVector2D::ZeroVector,40),
            FPlacement(FAnchors(0,0)),FPlacement(FAnchors(0,0))};
        Places[0].Copy(OldPanel(TEXT("Guide")));
        Places[2].Copy(OldPanel(TEXT("Status"))); Places[3].Copy(OldPanel(TEXT("Mission")));
        Places[4].Copy(OverlayBP->WidgetTree->FindWidget(TEXT("Border_Timer"))); Places[5].Copy(OverlayBP->WidgetTree->FindWidget(TEXT("Border_Life")));
        TArray<TStrongObjectPtr<UOBHudPanelWidget>> Seeds;
        Seeds.Add(PanelSeed(EOBHudPanelRole::Guide, OldPanel(TEXT("Guide"))));
        Seeds.Add(TStrongObjectPtr<UOBHudPanelWidget>()); // Reserved legacy pause slot; Warning lives in Result.
        Seeds.Add(PanelSeed(EOBHudPanelRole::Subtitle, OldPanel(TEXT("Status"))));
        Seeds.Add(PanelSeed(EOBHudPanelRole::Mission, OldPanel(TEXT("Mission"))));
        Seeds.Add(PanelSeed(EOBHudPanelRole::Timer, OverlayBP->WidgetTree->FindWidget(TEXT("Border_Timer"))));
        Seeds.Add(PanelSeed(EOBHudPanelRole::Life, OverlayBP->WidgetTree->FindWidget(TEXT("Border_Life"))));
        const TCHAR* Names[]={TEXT("WBP_Guide"),nullptr,TEXT("WBP_Subtitle"),TEXT("WBP_Mission"),TEXT("WBP_Timer"),TEXT("WBP_Life")};
        TArray<UWidgetBlueprint*> Panels;
        for (int32 I=0;I<6;++I)
        {
            if (I==1) { Panels.Add(nullptr);continue; }
            auto* BP=I==2 ? LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Subtitle.WBP_Subtitle")) : Create(Names[I],Seeds[I].Get());
            bSuccess &= BP!=nullptr;Panels.Add(BP);
        }
        TStrongObjectPtr<UOBTrainingStatusWidget> MarkerSeed(NewObject<UOBTrainingStatusWidget>()); MarkerSeed->BuildDefaultLayout();
        TStrongObjectPtr<UOBTrainingResultWidget> ResultSeed(NewObject<UOBTrainingResultWidget>()); ResultSeed->BuildDefaultLayout();
        auto* OldResult=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingResult.WBP_TrainingResult"));
        const bool bReplaceResult=OldResult && !OldResult->WidgetTree->FindWidget(TEXT("StampImage"));
        auto* ResultBP=Create(TEXT("WBP_TrainingResult"), ResultSeed.Get(), bReplaceResult);
        auto* MarkerBP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingMarkers.WBP_TrainingMarkers"));
        if (!MarkerBP && OldStatus)
        {
            auto& Tools=FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
            if (Tools.RenameAssets({FAssetRenameData(OldStatus,Folder,TEXT("WBP_TrainingMarkers"))}))
                MarkerBP=Create(TEXT("WBP_TrainingMarkers"), MarkerSeed.Get(), true);
        }
        if (!MarkerBP) MarkerBP=Create(TEXT("WBP_TrainingMarkers"),MarkerSeed.Get());
        bSuccess &= ResultBP && MarkerBP;
        if (bSuccess && !OverlayBP->WidgetTree->FindWidget(TEXT("TrainingResult")))
        {
            for (const TCHAR* Name : {TEXT("Border_Timer"),TEXT("Border_Life")})
                if (auto* Widget=OverlayBP->WidgetTree->FindWidget(Name)) Canvas->RemoveChild(Widget);
            const TCHAR* Instances[]={TEXT("Guide"),TEXT("Pause"),TEXT("Subtitle"),TEXT("Mission"),TEXT("Timer"),TEXT("Life")};
            for (int32 I=0;I<6;++I)
            {
                if (I==1) continue;
                auto* Child=OverlayBP->WidgetTree->ConstructWidget<UUserWidget>(Panels[I]->GeneratedClass.Get(),Instances[I]);
                Places[I].Apply(Canvas->AddChildToCanvas(Child)); Child->SetVisibility(I==1 ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
                Child->bHiddenInDesigner=false; Child->bIsVariable=true; Child->ClearFlags(RF_Transient); Child->SetFlags(RF_Transactional);
                if (I==2) bSuccess &= OBSubtitleEditor::ValidateEmbeddedSubtitle(OverlayBP);
            }
            for (const auto& Entry : {TPair<const TCHAR*,UWidgetBlueprint*>(TEXT("TrainingMarkers"),MarkerBP), TPair<const TCHAR*,UWidgetBlueprint*>(TEXT("TrainingResult"),ResultBP)})
            {
                auto* Child=OverlayBP->WidgetTree->ConstructWidget<UUserWidget>(Entry.Value->GeneratedClass.Get(),Entry.Key);
                FPlacement(FAnchors(0,0,1,1),FMargin(0),FVector2D::ZeroVector,Entry.Value==ResultBP ? 200 : 20).Apply(Canvas->AddChildToCanvas(Child));
                Child->SetVisibility(ESlateVisibility::Collapsed); Child->bHiddenInDesigner=false; Child->bIsVariable=true; Child->ClearFlags(RF_Transient); Child->SetFlags(RF_Transactional);
            }
            PrepareWidgetGuids(OverlayBP);
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(OverlayBP); FKismetEditorUtilities::CompileBlueprint(OverlayBP);
            bSuccess &= OverlayBP->Status != BS_Error && Save(OverlayBP);
        }
        PrepareWidgetGuids(OverlayBP); FKismetEditorUtilities::CompileBlueprint(OverlayBP);
        bSuccess &= OverlayBP->Status != BS_Error && Save(OverlayBP);
        if (bSuccess && FParse::Param(FCommandLine::Get(),TEXT("TrainingWidgetPreview")))
            bSuccess = Preview(ResultBP,TEXT("Result.png")) && Preview(ResultBP,TEXT("ResultFail.png")) && Preview(Panels[0],TEXT("Guide.png"));
        UE_LOG(LogTemp,Display,TEXT("TRAINING_WBP modular_setup=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (FParse::Param(FCommandLine::Get(),TEXT("TrainingWidgetSetup"))) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
    }
    UMaterial* MarkerMaterial(const TCHAR* Name,bool bRing)
    {
        const FString FolderPath=TEXT("/Game/Blueprints/Widget/PlayerOverlay/Art/Markers");
        const FString Path=FolderPath/Name+TEXT(".")+Name;
        if (auto* Existing=LoadObject<UMaterial>(nullptr,*Path)) return Existing;
        auto& Assets=FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        auto* Material=Cast<UMaterial>(Assets.CreateAsset(Name,FolderPath,UMaterial::StaticClass(),NewObject<UMaterialFactoryNew>()));
        if (!Material) return nullptr;
        Material->MaterialDomain=MD_UI; Material->BlendMode=BLEND_Translucent;
        auto Add=[&](UMaterialExpression* Expression) { Material->GetExpressionCollection().AddExpression(Expression); };
        auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(Material); Add(UV);
        auto* Thickness=NewObject<UMaterialExpressionScalarParameter>(Material); Thickness->ParameterName=TEXT("Thickness"); Thickness->DefaultValue=bRing ? .025f : .015f; Add(Thickness);
        auto* Progress=NewObject<UMaterialExpressionScalarParameter>(Material); Progress->ParameterName=TEXT("Progress"); Progress->DefaultValue=1; Add(Progress);
        auto* Shape=NewObject<UMaterialExpressionCustom>(Material); Shape->OutputType=CMOT_Float1;
        Shape->Description=bRing ? TEXT("UMG ring and progress arc") : TEXT("UMG frame");
        Shape->Code=bRing ?
            TEXT("float2 p=UV-0.5; float d=length(p); float aa=max(fwidth(d),0.0005); float a=1-smoothstep(Thickness,Thickness+aa,abs(d-0.45)); float angle=frac(atan2(p.x,-p.y)/6.28318530718+1); return a*step(angle,saturate(Progress));") :
            TEXT("float edge=min(min(UV.x,1-UV.x),min(UV.y,1-UV.y)); return 1-smoothstep(Thickness,Thickness+max(fwidth(edge),0.0005),edge);");
        FCustomInput Input; Input.InputName=TEXT("UV"); Input.Input.Expression=UV; Shape->Inputs.Add(Input);
        Input.InputName=TEXT("Thickness"); Input.Input.Expression=Thickness; Shape->Inputs.Add(Input);
        Input.InputName=TEXT("Progress"); Input.Input.Expression=Progress; Shape->Inputs.Add(Input); Add(Shape);
        auto* White=NewObject<UMaterialExpressionConstant3Vector>(Material); White->Constant=FLinearColor::White; Add(White);
        auto* Data=Material->GetEditorOnlyData(); Data->EmissiveColor.Expression=White; Data->Opacity.Expression=Shape;
        Material->PostEditChange(); return Save(Material) ? Material : nullptr;
    }
    bool MarkerInstance(const TCHAR* Name,UMaterial* Parent,float Thickness)
    {
        const FString Path=FString(TEXT("/Game/Blueprints/Widget/PlayerOverlay/Art/Markers"))/Name;
        if (LoadObject<UMaterialInstanceConstant>(nullptr,*(Path+TEXT(".")+Name))) return true;
        auto* Package=CreatePackage(*Path);
        auto* Instance=NewObject<UMaterialInstanceConstant>(Package,Name,RF_Public|RF_Standalone);
        Instance->SetParentEditorOnly(Parent);
        Instance->SetScalarParameterValueEditorOnly(TEXT("Thickness"),Thickness);
        Instance->PostEditChange(); FAssetRegistryModule::AssetCreated(Instance);
        return Save(Instance);
    }
    void BuildUMGMarkers()
    {
        auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingMarkers.WBP_TrainingMarkers"));
        auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
        bool bSuccess=BP && Overlay && BP->WidgetTree && Cast<UCanvasPanel>(BP->WidgetTree->RootWidget);
        if (bSuccess)
        {
            auto& Files=FPlatformFileManager::Get().GetPlatformFile();
            const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups/UMGTrainingMarkers")/FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
            for (auto* Asset : {BP,Overlay})
            {
                const FString Source=FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(),FPackageName::GetAssetPackageExtension());
                const FString Dest=Backup/Asset->GetName()+TEXT(".uasset");
                Files.CreateDirectoryTree(*FPaths::GetPath(Dest)); bSuccess &= Files.CopyFile(*Dest,*Source);
            }
        }
        if (bSuccess)
        {
            auto* Ring=MarkerMaterial(TEXT("M_TrainingMarkerRing"),true);
            auto* Frame=MarkerMaterial(TEXT("M_TrainingMarkerFrame"),false);
            bSuccess=Ring && Frame;
            if (bSuccess) bSuccess=MarkerInstance(TEXT("MI_TrainingMarkerRingOutline"),Ring,.055f) &&
                MarkerInstance(TEXT("MI_TrainingMarkerFrameOutline"),Frame,.035f) &&
                MarkerInstance(TEXT("MI_TrainingMarkerCameraFrame"),Frame,.001f);
        }
        if (bSuccess)
        {
            // Work on a copy of the current tree, adding missing groups without rebuilding existing content.
            TStrongObjectPtr<UOBTrainingStatusWidget> Seed(NewObject<UOBTrainingStatusWidget>());
            Seed->WidgetTree=DuplicateObject<UWidgetTree>(BP->WidgetTree,Seed.Get(),TEXT("WidgetTree"));
            const auto* Defaults=GetDefault<UOBTrainingStatusWidget>(BP->GeneratedClass);
            Seed->AimColor=Defaults->AimColor; Seed->ProgressColor=Defaults->ProgressColor;
            Seed->LeftHandColor=Defaults->LeftHandColor; Seed->RightHandColor=Defaults->RightHandColor; Seed->SuccessColor=Defaults->SuccessColor;
            Seed->WristMarkerRadius=Defaults->WristMarkerRadius; Seed->GuideLabelFontSize=Defaults->GuideLabelFontSize;
            Seed->BuildDefaultLayout();
            BP->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
            BP->WidgetTree=DuplicateObject<UWidgetTree>(Seed->WidgetTree,BP,TEXT("WidgetTree"));
            BP->WidgetTree->ClearFlags(RF_Transient); BP->WidgetTree->SetFlags(RF_Transactional);
            BP->WidgetTree->ForEachWidget([](UWidget* Widget) { Widget->ClearFlags(RF_Transient); Widget->SetFlags(RF_Transactional); });
            PrepareWidgetGuids(BP); FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
            bSuccess=BP->Status!=BS_Error && Save(BP);
            if (bSuccess) { PrepareWidgetGuids(Overlay); FKismetEditorUtilities::CompileBlueprint(Overlay); bSuccess=Overlay->Status!=BS_Error && Save(Overlay); }
            if (bSuccess && FParse::Param(FCommandLine::Get(),TEXT("TrainingWidgetPreview")))
            {
                if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
                FlushRenderingCommands();
                for (const auto Size : {FIntPoint(1280,720),FIntPoint(1920,1080),FIntPoint(2560,1080)})
                    for (const auto Mode : {EOBOverlayPreviewMode::MarkersAim,EOBOverlayPreviewMode::MarkersBreach})
                        bSuccess &= Preview(Overlay,*FString::Printf(TEXT("UMGMarkers_%dx%d_%d.png"),Size.X,Size.Y,static_cast<int32>(Mode)),Mode,Size,true);
            }
        }
        UE_LOG(LogTemp,Display,TEXT("UMG_TRAINING_MARKERS setup=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (FParse::Param(FCommandLine::Get(),TEXT("UMGMarkerSetup"))) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
    }
    void ConnectSystemMessage()
    {
        auto* Message=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_SystemMessage.WBP_SystemMessage"));
        auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
        bool bSuccess=Message && Overlay && Overlay->WidgetTree && Cast<UOBSystemMessageWidget>(Overlay->WidgetTree->FindWidget(TEXT("SystemMessage")));
        if (bSuccess)
        {
            auto& Files=FPlatformFileManager::Get().GetPlatformFile();
            const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups/SystemMessage")/FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
            Files.CreateDirectoryTree(*Backup);
            for (auto* BP : {Message,Overlay})
            {
                const FString Filename=FPackageName::LongPackageNameToFilename(BP->GetPackage()->GetName(),FPackageName::GetAssetPackageExtension());
                bSuccess &= Files.CopyFile(*(Backup/BP->GetName()+TEXT(".uasset")),*Filename);
            }
        }
        if (bSuccess)
        {
            Message->WidgetTree->ForEachWidget([](UWidget* Widget) {
                if (auto* Text=Cast<UTextBlock>(Widget))
                {
                    Text->SetAutoWrapText(false);
                    UE_LOG(LogTemp,Display,TEXT("SYSTEM_MESSAGE text_widget=%s"),*Text->GetName());
                }
            });
            for (auto* BP : {Message,Overlay})
            {
                PrepareWidgetGuids(BP);
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
                FKismetEditorUtilities::CompileBlueprint(BP);
                bSuccess &= BP->Status!=BS_Error && Save(BP);
            }
            if (bSuccess && FParse::Param(FCommandLine::Get(),TEXT("TrainingWidgetPreview")))
                bSuccess=Preview(Overlay,TEXT("OverlaySystemMessage.png"),EOBOverlayPreviewMode::SystemMessage,FIntPoint(1920,1080),true);
        }
        UE_LOG(LogTemp,Display,TEXT("SYSTEM_MESSAGE setup=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (FParse::Param(FCommandLine::Get(),TEXT("SystemMessageSetup"))) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
    }
    void ApplyLoopUpdates()
    {
        auto* Guide=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Guide.WBP_Guide"));
        auto* Message=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_SystemMessage.WBP_SystemMessage"));
        auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
        auto* RailBP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/Blueprints/Training/BP_OBRailTrainingTarget.BP_OBRailTrainingTarget"));
        auto* Body=Guide && Guide->WidgetTree ? Cast<UTextBlock>(Guide->WidgetTree->FindWidget(TEXT("BodyText"))) : nullptr;
        bool bSuccess=Body && Message && Overlay && RailBP;
        auto& Files=FPlatformFileManager::Get().GetPlatformFile();
        const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups/TrainingLoopUpdates")/FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
        Files.CreateDirectoryTree(*Backup);
        if (bSuccess)
        {
            for (UObject* Asset : {static_cast<UObject*>(Guide),static_cast<UObject*>(Message),static_cast<UObject*>(Overlay),static_cast<UObject*>(RailBP)})
            {
                const FString Filename=FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(),FPackageName::GetAssetPackageExtension());
                bSuccess &= Files.CopyFile(*(Backup/Asset->GetName()+TEXT(".uasset")),*Filename);
            }
            const FString MapFile=FPackageName::LongPackageNameToFilename(TEXT("/Game/Maps/TutorialMap"),TEXT(".umap"));
            bSuccess &= Files.CopyFile(*(Backup/TEXT("TutorialMap.umap")),*MapFile);
        }
        if (bSuccess)
        {
            Body->SetText(FText::FromString(TEXT("훈련 안내 내용 (임시)")));
            for (auto* BP : {Guide,Message,Overlay})
            {
                TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
                for (auto* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes) for (auto* Pin : Node->Pins)
                    if (Pin->PinName==TEXT("InTutorialMessageWidget")) Pin->PinName=TEXT("InSystemMessageWidget");
                PrepareWidgetGuids(BP);
                FBlueprintEditorUtils::RefreshAllNodes(BP);
                FKismetEditorUtilities::CompileBlueprint(BP);
                bSuccess &= BP->Status!=BS_Error && Save(BP);
            }
            FKismetEditorUtilities::CompileBlueprint(RailBP);
            auto* Defaults=Cast<AOBRailTrainingTarget>(RailBP->GeneratedClass->GetDefaultObject());
            if (Defaults)
            {
                Defaults->FoldRotation=FRotator(0,0,90); Defaults->LowerSeconds=.25f; Defaults->RaiseSeconds=0;
                RailBP->Modify(); bSuccess &= Save(RailBP);
            }
            else bSuccess=false;
        }
        if (bSuccess)
        {
            bSuccess=FEditorFileUtils::LoadMap(TEXT("/Game/Maps/TutorialMap"),false,true);
            if (bSuccess)
            {
                UWorld* World=GEditor->GetEditorWorldContext().World();
                int32 Count=0;
                for (TActorIterator<AOBRailTrainingTarget> It(World); It; ++It)
                {
                    It->Modify(); It->FoldRotation=FRotator(0,0,90); It->LowerSeconds=.25f; It->RaiseSeconds=0;
                    ++Count;
                }
                bSuccess=Count==3 && FEditorFileUtils::SaveMap(World,FPackageName::LongPackageNameToFilename(TEXT("/Game/Maps/TutorialMap"),TEXT(".umap")));
                UE_LOG(LogTemp,Display,TEXT("TRAINING_LOOP rail_instances=%d"),Count);
            }
        }
        UE_LOG(LogTemp,Display,TEXT("TRAINING_LOOP setup=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (FParse::Param(FCommandLine::Get(),TEXT("TrainingLoopSetup"))) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
    }
    void OpenAll()
    {
        TArray<UObject*> Widgets;
        for (const TCHAR* Name : {TEXT("WBP_Guide"), TEXT("WBP_Mission"), TEXT("WBP_Timer"), TEXT("WBP_Life"), TEXT("WBP_TrainingMarkers"), TEXT("WBP_TrainingResult")})
            if (auto* Asset = LoadObject<UWidgetBlueprint>(nullptr, *(FString(Folder) / Name + TEXT(".") + Name))) Widgets.Add(Asset);
        if (auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"))) Widgets.Add(Overlay);
        const bool bOpened = GEditor && Widgets.Num() == 7 && GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(Widgets);
        UE_LOG(LogTemp, Display, TEXT("TRAINING_WBP opened=%d count=%d"), bOpened, Widgets.Num());
    }
}
static FAutoConsoleCommand GOBCreateTrainingWidgets(TEXT("OB.Training.CreateWidgets"), TEXT("Create initial training Widget Blueprints; existing assets are preserved."), FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::CreateAll));
static FAutoConsoleCommand GOBOpenTrainingWidgets(TEXT("OB.Training.OpenWidgets"), TEXT("Open PlayerOverlay and its eight child Widget Blueprints."), FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::OpenAll));
static FAutoConsoleCommand GOBInspectOverlay(TEXT("OB.UI.InspectOverlay"), TEXT("Inspect PlayerOverlay hierarchy and bindings."), FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::InspectOverlay));
static FAutoConsoleCommand GOBFixOverlayDesigner(TEXT("OB.UI.FixOverlayDesigner"), TEXT("Enable PlayerOverlay Designer previews while preserving panel layout."), FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::FixOverlayDesigner));
static FAutoConsoleCommand GOBUpdateTrainingPanels(TEXT("OB.UI.UpdateTrainingPanels"), TEXT("Update trigger-only pause, two-field subtitles and HUD placement."), FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::UpdateTrainingPanels));
static FAutoConsoleCommand GOBFixSubtitleRow(TEXT("OB.UI.FixSubtitleRow"), TEXT("Arrange existing subtitle labels as Name, Spacer, Script without changing PlayerOverlay placement."), FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::FixSubtitleRow));
static FAutoConsoleCommand GOBOrganizePlayerOverlay(TEXT("OB.UI.OrganizePlayerOverlay"), TEXT("Move PlayerOverlay and its components, fix references and disable Auto Wrap Text on project widgets."), FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::OrganizePlayerOverlay));
static FAutoConsoleCommand GOBWrapComponentSizes(TEXT("OB.UI.WrapComponentSizes"), TEXT("Wrap existing HUD components in editable baseline SizeBoxes, preserving content and fullscreen marker geometry."), FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::WrapComponentSizes));
static FAutoConsoleCommand GOBPreviewMissionObjectives(TEXT("OB.UI.PreviewMissionObjectives"), TEXT("Render basic, mock and retraining mission objective samples without saving assets."), FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::PreviewMissionObjectives));
static FAutoConsoleCommand GOBBuildUMGMarkers(TEXT("OB.UI.BuildUMGMarkers"), TEXT("Add editable UMG marker images and UI materials to the current TrainingMarkers tree."), FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::BuildUMGMarkers));
static FAutoConsoleCommand GOBConnectSystemMessage(TEXT("OB.UI.ConnectSystemMessage"),TEXT("Compile the current SystemMessage and PlayerOverlay assets while preserving authored layout."),FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::ConnectSystemMessage));
static FAutoConsoleCommand GOBApplyLoopUpdates(TEXT("OB.Training.ApplyLoopUpdates"),TEXT("Migrate SystemMessage names, seed Guide placeholder, and configure rail hit folding in TutorialMap."),FConsoleCommandDelegate::CreateStatic(&OBTrainingWidgetAssets::ApplyLoopUpdates));
#endif
