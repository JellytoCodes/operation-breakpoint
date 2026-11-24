#if WITH_EDITOR
#include "Training/OBTrainingUIData.h"
#include "Training/OBTrainingResultWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Engine/Texture2D.h"
#include "AssetCompilingManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Materials/MaterialInterface.h"
#include "MediaSource.h"

namespace
{
void ConfigureTrainingPages()
{
    auto Fail=[](const TCHAR* Reason) { UE_LOG(LogTemp,Error,TEXT("TRAINING_UI_SETUP FAIL %s"),Reason); FPlatformMisc::RequestExitWithStatus(false,1); };
    FString Json;
    TArray<TSharedPtr<FJsonValue>> Values;
    if (!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectSavedDir()/TEXT("Tests/TrainingUI/pages.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Values) || Values.Num()!=10) { Fail(TEXT("UI input data")); return; }
    TMap<int32,FOBTrainingUIRow> Rows;
    for (const auto& Value : Values)
    {
        const auto Object=Value->AsObject(); const int32 Id=Object->GetIntegerField(TEXT("id"));
        FOBTrainingUIRow Row; Row.Title=FText::FromString(Object->GetStringField(TEXT("title"))); Row.Body=FText::FromString(Object->GetStringField(TEXT("body")));
        Row.Image=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Object->GetStringField(TEXT("image"))));
        if (Id<3001 || Id>3010 || Rows.Contains(Id) || !Row.Image.LoadSynchronous()) { Fail(TEXT("UI row or texture")); return; }
        Rows.Add(Id,Row);
    }
    auto* Guide=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Guide.WBP_Guide"));
    auto* Result=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingResult.WBP_TrainingResult"));
    auto* Click=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Training_UI/Sub_UI/Button_Click_UI.Button_Click_UI"));
    if (!Guide || !Result || !Click || !Guide->WidgetTree || !Result->WidgetTree) { Fail(TEXT("Widget assets")); return; }
    FAssetCompilingManager::Get().FinishAllCompilation();
    const int32 ClickWidth=Click->Source.GetSizeX(),ClickHeight=Click->Source.GetSizeY();
    if (ClickWidth<=0 || ClickHeight<=0) { Fail(TEXT("Trigger source texture dimensions")); return; }
    auto* VideoMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Blueprints/Widget/Media/M_UIVideo.M_UIVideo"));
    auto* TriggerSource=LoadObject<UMediaSource>(nullptr,TEXT("/Game/Assets/UI/TriggerPress.TriggerPress"));
    if (!VideoMaterial || !TriggerSource) { Fail(TEXT("Guide video assets")); return; }
    const FString TablePackage=TEXT("/Game/Blueprints/Training/Data/DT_TrainingUI");
    auto* Table=LoadObject<UDataTable>(nullptr,*(TablePackage+TEXT(".DT_TrainingUI")),nullptr,LOAD_NoWarn);
    if (Table && Table->GetRowStruct()!=FOBTrainingUIRow::StaticStruct()) { Fail(TEXT("Existing UI table has a different row struct")); return; }
    const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups")/(TEXT("TrainingUI_")+FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S")));
    IFileManager::Get().MakeDirectory(*Backup,true);
    for (UObject* Asset : {static_cast<UObject*>(Guide),static_cast<UObject*>(Result),static_cast<UObject*>(Table)})
    {
        if (!Asset) continue;
        const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        if (IFileManager::Get().FileExists(*File) && IFileManager::Get().Copy(*(Backup/FPaths::GetCleanFilename(File)),*File)!=COPY_OK) { Fail(TEXT("Asset backup")); return; }
    }
    if (!Table) { Table=NewObject<UDataTable>(CreatePackage(*TablePackage),TEXT("DT_TrainingUI"),RF_Public|RF_Standalone); Table->RowStruct=FOBTrainingUIRow::StaticStruct(); FAssetRegistryModule::AssetCreated(Table); }
    for (const auto& Pair : Rows) Table->AddRow(FName(*FString::FromInt(Pair.Key)),Pair.Value);
    Table->MarkPackageDirty();

    auto* Trigger=Cast<UImage>(Guide->WidgetTree->FindWidget(TEXT("TriggerImage")));
    if (!Trigger)
    {
        auto* Prompt=Guide->WidgetTree->FindWidget(TEXT("PromptText"));
        auto* OldSlot=Prompt ? Cast<UCanvasPanelSlot>(Prompt->Slot) : nullptr;
        auto* Parent=OldSlot ? Cast<UCanvasPanel>(Prompt->GetParent()) : nullptr;
        if (!Parent || !OldSlot) { Fail(TEXT("Guide prompt Canvas slot")); return; }
        const FAnchorData Layout=OldSlot->GetLayout();
        Trigger=Guide->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("TriggerImage"));
        auto* Slot=Parent->AddChildToCanvas(Trigger); Slot->SetLayout(Layout); Slot->SetAutoSize(false);
        const float Width=400.f; Slot->SetSize(FVector2D(Width,Width*ClickHeight/ClickWidth));
        Guide->WidgetTree->RemoveWidget(Prompt); Prompt->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
        Trigger->SetBrushFromMaterial(VideoMaterial);
    }
    Trigger->SetVisibility(ESlateVisibility::Hidden);
    auto* Border=Cast<UBorder>(Result->WidgetTree->FindWidget(TEXT("ResultBorder")));
    if (!Border)
    {
        Border=Cast<UBorder>(Result->WidgetTree->FindWidget(TEXT("Border_0")));
        if (!Border) { Fail(TEXT("Result Border")); return; }
        Border->Rename(TEXT("ResultBorder"),Result->WidgetTree,REN_DontCreateRedirectors);
    }
    Border->SetBrushFromTexture(Rows[3010].Image.LoadSynchronous());
    auto Brush=Border->Background; Brush.DrawAs=ESlateBrushDrawType::Image; Border->SetBrush(Brush); Border->SetBrushColor(FLinearColor::White);
    for (auto* BP : {Guide,Result})
    {
        TSet<FName> Names;
        BP->WidgetTree->ForEachWidget([&](UWidget* Widget) {
            Names.Add(Widget->GetFName());
            if (Widget->GetName()==TEXT("TitleText") || Widget->GetName()==TEXT("BodyText") || Widget->GetName()==TEXT("BodyImage")
                || Widget->GetName()==TEXT("TriggerImage") || Widget->GetName()==TEXT("ResultBorder")) Widget->bIsVariable=true;
        });
        for (auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It) if (!Names.Contains(It.Key())) It.RemoveCurrent();
        for (FName Name : Names) if (!BP->WidgetVariableNameToGuidMap.Contains(Name)) BP->OnVariableAdded(Name);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status==BS_Error) { Fail(TEXT("Widget compilation; original files preserved in backup")); return; }
    }
    // Reinstancing a newly added native BindWidget can copy the default 32px image slot.
    // Persist the authored source tree and its generated template together after compilation.
    auto* GuideClass=Cast<UWidgetBlueprintGeneratedClass>(Guide->GeneratedClass);
    if (GuideClass) CastChecked<UOBGuideWidget>(GuideClass->GetDefaultObject())->TriggerSource=TriggerSource;
    for (UWidgetTree* Tree : {Guide->WidgetTree.Get(),GuideClass ? GuideClass->GetWidgetTreeArchetype() : nullptr})
    {
        auto* Icon=Tree ? Cast<UImage>(Tree->FindWidget(TEXT("TriggerImage"))) : nullptr;
        if (!Icon) continue;
        Icon->SetBrushFromMaterial(VideoMaterial); Icon->SetVisibility(ESlateVisibility::Hidden);
    }
    for (UObject* Asset : {static_cast<UObject*>(Table),static_cast<UObject*>(Guide),static_cast<UObject*>(Result)})
    {
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
        const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        if (!UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args)) { Fail(TEXT("Asset save")); return; }
    }
    UE_LOG(LogTemp,Display,TEXT("TRAINING_UI_SETUP PASS rows=10 trigger_video=TriggerPress backup=%s"),*Backup);
    FPlatformMisc::RequestExitWithStatus(false,0);
}
static FAutoConsoleCommand GConfigureTrainingPages(TEXT("OB.UI.ConfigureTrainingPages"),TEXT("Back up and configure training UI table, Guide trigger image and Result border."),FConsoleCommandDelegate::CreateStatic(&ConfigureTrainingPages));

// Scoped asset update for the video change. No title/other widgets or layout values are saved.
static FAutoConsoleCommand GConfigureGuideVideo(TEXT("OB.UI.ConfigureGuideVideo"),TEXT("Back up Guide and change only its trigger brush/source to video, retaining the authored layout."),FConsoleCommandDelegate::CreateLambda([]() {
    auto Fail=[](const TCHAR* Why) { UE_LOG(LogTemp,Error,TEXT("GUIDE_VIDEO_ASSET FAIL %s"),Why); FPlatformMisc::RequestExitWithStatus(false,1); };
    auto* Guide=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Guide.WBP_Guide"));
    auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Blueprints/Widget/Media/M_UIVideo.M_UIVideo"));
    auto* Source=LoadObject<UMediaSource>(nullptr,TEXT("/Game/Assets/UI/TriggerPress.TriggerPress"));
    if (!Guide || !Guide->WidgetTree || !Material || !Source) { Fail(TEXT("Required assets")); return; }
    auto* Image=Cast<UImage>(Guide->WidgetTree->FindWidget(TEXT("TriggerImage")));
    if (!Image) { Fail(TEXT("TriggerImage")); return; }
    const FString File=FPackageName::LongPackageNameToFilename(Guide->GetOutermost()->GetName(),TEXT(".uasset"));
    const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups")/(TEXT("GuideVideo_")+FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S")));
    IFileManager::Get().MakeDirectory(*Backup,true);
    if (IFileManager::Get().Copy(*(Backup/TEXT("WBP_Guide.uasset")),*File)!=COPY_OK) { Fail(TEXT("Backup")); return; }
    if (auto* Slot=Cast<UCanvasPanelSlot>(Image->Slot))
        UE_LOG(LogTemp,Display,TEXT("GUIDE_VIDEO_ASSET retained_position=%s retained_size=%s"),*Slot->GetPosition().ToString(),*Slot->GetSize().ToString());
    Image->SetBrushFromMaterial(Material);
    if (auto* Class=Cast<UWidgetBlueprintGeneratedClass>(Guide->GeneratedClass))
    {
        CastChecked<UOBGuideWidget>(Class->GetDefaultObject())->TriggerSource=Source;
        if (auto* Tree=Class->GetWidgetTreeArchetype())
            if (auto* TemplateImage=Cast<UImage>(Tree->FindWidget(TEXT("TriggerImage")))) TemplateImage->SetBrushFromMaterial(Material);
    }
    Guide->MarkPackageDirty();
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    if (!UPackage::SavePackage(Guide->GetOutermost(),Guide,*File,Args)) { Fail(TEXT("Save")); return; }
    UE_LOG(LogTemp,Display,TEXT("GUIDE_VIDEO_ASSET PASS material=M_UIVideo source=TriggerPress layout_unchanged=1 backup=%s"),*Backup);
    FPlatformMisc::RequestExitWithStatus(false,0);
}));

void PreviewTrainingPages()
{
    bool bSuccess=true;
    for (int32 Id=3001;Id<=3010;++Id)
    {
        const bool bGuide=Id<=3007;
        auto* Class=LoadClass<UUserWidget>(nullptr,bGuide
            ? TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Guide.WBP_Guide_C")
            : TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingResult.WBP_TrainingResult_C"));
        if (!Class) { bSuccess=false; break; }
        TStrongObjectPtr<UUserWidget> Widget(NewObject<UUserWidget>(GetTransientPackage(),Class));
        Widget->SetDesignerFlags(EWidgetDesignFlags::Designing|EWidgetDesignFlags::ExecutePreConstruct);
        if (!Widget->Initialize()) { bSuccess=false; break; }
        const auto Slate=Widget->TakeWidget();
        if (bGuide) CastChecked<UOBGuideWidget>(Widget.Get())->SetGuidePage(Id,Id==3001);
        else
        {
            FOBTrainingResultData Data; Data.bPrototypeFlow=true; Data.bScenarioCompleted=Id==3009; Data.RecordedFireLimitSeconds=120; Data.RecordedFireSeconds=75;
            if (Id==3008) CastChecked<UOBTrainingResultWidget>(Widget.Get())->SetWarning(Data);
            else CastChecked<UOBTrainingResultWidget>(Widget.Get())->SetResult(Data);
        }
        FAssetCompilingManager::Get().FinishAllCompilation(); FlushRenderingCommands();
        const FIntPoint Size=bGuide ? FIntPoint(1080,600) : FIntPoint(1920,1080);
        FWidgetRenderer Renderer(true); Renderer.DrawWidget(Slate,FVector2D(Size)); FlushRenderingCommands();
        if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
        auto* Target=Renderer.DrawWidget(Slate,FVector2D(Size)); TArray<FColor> Pixels;
        if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)) { bSuccess=false; break; }
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("Tests/TrainingUI/Preview"); IFileManager::Get().MakeDirectory(*Folder,true);
        bSuccess &= FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("UI_%d.png"),Id)));
    }
    UE_LOG(LogTemp,Display,TEXT("TRAINING_UI_PREVIEW %s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
    FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
}
static FAutoConsoleCommand GPreviewTrainingPages(TEXT("OB.UI.PreviewTrainingPages"),TEXT("Render all training UI pages without saving assets, then exit this editor process."),FConsoleCommandDelegate::CreateStatic(&PreviewTrainingPages));
}
#endif
