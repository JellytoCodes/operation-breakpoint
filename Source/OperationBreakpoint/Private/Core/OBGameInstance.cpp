#include "Core/OBGameInstance.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Scalability.h"
#include "GenericPlatform/GenericApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UOBGameInstance::Init()
{
    Super::Init();
    FString ProfilePath;
    if (ReadLearnerProfileArgument(FCommandLine::Get(), ProfilePath))
    {
        if (!LoadLearnerProfile(ProfilePath))
        {
            UE_LOG(LogTemp, Error, TEXT("LAUNCHER_IDENTITY rejected: missing or invalid learner profile"));
            if (!GIsEditor && !IsRunningCommandlet()) FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("LAUNCHER_IDENTITY loaded for this launch"));
    }
    ApplyFixedStartupSettings(TEXT("Init"));
}

bool UOBGameInstance::ReadLearnerProfileArgument(const TCHAR* CommandLine, FString& Path)
{
    const FString Prefix(TEXT("-OBLearnerProfile="));
    FString Argument;
    while (FParse::Token(CommandLine, Argument, false))
    {
        if (!Argument.StartsWith(Prefix, ESearchCase::IgnoreCase)) continue;
        Path = Argument.Mid(Prefix.Len());
        if (Path.StartsWith(TEXT("\"")) && Path.EndsWith(TEXT("\""))) Path = Path.Mid(1, Path.Len() - 2);
        return true;
    }
    return false;
}

bool UOBGameInstance::LoadLearnerProfile(const FString& Path)
{
    // Never fall back to another learner if an explicit launch profile is invalid.
    LearnerName.Reset(); LearnerId.Reset();
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size <= 0 || Size > 65536) return false;
    FString Json;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Json, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) return false;
    double Schema = 0;
    FString Name, Id;
    if (!Root->TryGetNumberField(TEXT("schema_version"), Schema) || Schema != 1 ||
        !Root->HasTypedField<EJson::String>(TEXT("learner_name")) ||
        !Root->HasTypedField<EJson::String>(TEXT("learner_id")) ||
        !Root->TryGetStringField(TEXT("learner_name"), Name) ||
        !Root->TryGetStringField(TEXT("learner_id"), Id)) return false;
    for (TCHAR Ch : Name) if (FChar::IsControl(Ch)) return false;
    Name.TrimStartAndEndInline();
    if (Name.IsEmpty() || Name.Len() > 50 || Id.Len() != 11 || Id[2] != TEXT('-')) return false;
    for (int32 I = 0; I < Id.Len(); ++I)
        if (I != 2 && (Id[I] < TEXT('0') || Id[I] > TEXT('9'))) return false;
    LearnerName = MoveTemp(Name); LearnerId = MoveTemp(Id);
    return true;
}

void UOBGameInstance::OnStart()
{
    Super::OnStart();
    // The game window exists now. Reapply without command-line window overrides.
    ApplyFixedStartupSettings(TEXT("Start"));
}

void UOBGameInstance::ApplyFixedStartupSettings(const TCHAR* Phase)
{
    if (GIsEditor || IsRunningDedicatedServer() || IsRunningCommandlet() || !GEngine) return;
    auto* Settings=GEngine->GetGameUserSettings();
    if (!Settings) return;
    const int32 PreviousMode=static_cast<int32>(Settings->GetFullscreenMode());
    const int32 PreviousQuality=Settings->GetOverallScalabilityLevel();
    FDisplayMetrics Displays;
    FDisplayMetrics::RebuildDisplayMetrics(Displays);
    int32 Selected=INDEX_NONE;
    int32 RequestedX=0,RequestedY=0;
    const bool bRequested=FParse::Value(FCommandLine::Get(),TEXT("OBMonitorX="),RequestedX) &&
        FParse::Value(FCommandLine::Get(),TEXT("OBMonitorY="),RequestedY);
    for (int32 I=0;I<Displays.MonitorInfo.Num();++I)
    {
        const auto& Monitor=Displays.MonitorInfo[I];
        const auto& Rect=Monitor.DisplayRect;
        if (bRequested && RequestedX>=Rect.Left && RequestedX<Rect.Right && RequestedY>=Rect.Top && RequestedY<Rect.Bottom)
        { Selected=I; break; }
        if (!Monitor.bIsPrimary && (Selected==INDEX_NONE || Rect.Left<Displays.MonitorInfo[Selected].DisplayRect.Left ||
            (Rect.Left==Displays.MonitorInfo[Selected].DisplayRect.Left && Rect.Top<Displays.MonitorInfo[Selected].DisplayRect.Top))) Selected=I;
    }
    if (Selected==INDEX_NONE)
        for (int32 I=0;I<Displays.MonitorInfo.Num();++I)
            if (Displays.MonitorInfo[I].bIsPrimary) { Selected=I; break; }
    if (Selected==INDEX_NONE && !Displays.MonitorInfo.IsEmpty()) Selected=0;
    FIntPoint Desktop=Settings->GetDesktopResolution();
    if (Displays.MonitorInfo.IsValidIndex(Selected))
    {
        const auto& Monitor=Displays.MonitorInfo[Selected];
        const auto& Rect=Monitor.DisplayRect;
        Desktop=FIntPoint(Rect.Right-Rect.Left,Rect.Bottom-Rect.Top);
        Settings->SetDisplayProperties(Monitor.ID,Selected);
        UE_LOG(LogTemp,Display,TEXT("DISPLAY_POLICY phase=%s index=%d primary=%d origin=%d,%d resolution=%dx%d id=%s"),
            Phase,Selected,Monitor.bIsPrimary,Rect.Left,Rect.Top,Desktop.X,Desktop.Y,*Monitor.ID);
    }
    if (Desktop.X>0 && Desktop.Y>0) Settings->SetScreenResolution(Desktop);
    Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
    Settings->ApplyResolutionSettings(false);
    // Resolution updates can adjust render scale, so apply the complete preset afterwards.
    Settings->SetOverallScalabilityLevel(1);
    // ApplyNonResolutionSettings skips scalability before GEngine finishes initialization.
    // Apply explicitly here so Init also uses Medium before the first map is rendered.
    Scalability::SetQualityLevels(Settings->ScalabilityQuality);
    Settings->ApplyNonResolutionSettings();
    Settings->ConfirmVideoMode();
    Settings->SaveSettings();
    const FIntPoint Resolution=Settings->GetScreenResolution();
    UE_LOG(LogTemp,Display,TEXT("FULLSCREEN_POLICY phase=%s previous=%d mode=%d resolution=%dx%d"),
        Phase,PreviousMode,static_cast<int32>(Settings->GetFullscreenMode()),Resolution.X,Resolution.Y);
    UE_LOG(LogTemp,Display,TEXT("GRAPHICS_POLICY phase=%s previous=%d quality=%d resolution_quality=%.3f"),
        Phase,PreviousQuality,Settings->GetOverallScalabilityLevel(),Scalability::GetQualityLevels().ResolutionQuality);
}
