#include "CoreMinimal.h"
#include "Windows/WindowsPlatformApplicationMisc.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Json.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

static const FName MCPBridgePanelTabName("MCPBridgePanel");

struct FMCPCommandHistoryEntry
{
    FString Timestamp;
    FString Command;
    FString Status;
    FString DurationMs;
    FString Error;
};

struct FMCPBridgePanelState
{
    bool bConnected = false;
    bool bListenerRunning = false;
    FString ListenerHost = TEXT("localhost");
    int32 ListenerPort = 8080;
    FString EngineVersion;
    FString ProjectName;
    FString CurrentLevel;
    FString LastPingAt;
    FString LastCommand;
    FString LastResult;
    FString LastErrorCode;
    FString LastErrorMessage;
    int32 CommandsHandled = 0;
    int32 Failures = 0;
    int32 Errors = 0;

    TMap<FString, bool> Safety;
    TArray<FMCPCommandHistoryEntry> History;
};

class SMCPBridgePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMCPBridgePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        ConfigureSlateStyles();
        LoadFiles();
        RebuildPanel();
        ProbeConnection(false);

        RegisterActiveTimer(2.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SMCPBridgePanel::HandleRefreshTimer));
    }

    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
    {
        SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

        const float Width = AllottedGeometry.GetLocalSize().X;
        if (Width <= 0.0f)
        {
            return;
        }

        const bool bShouldUseCompactLayout = Width < 1500.0f;
        if (bShouldUseCompactLayout != bCompactLayout)
        {
            bCompactLayout = bShouldUseCompactLayout;
            RebuildPanel();
            RebuildHistory();
        }
    }

private:
    void RebuildPanel()
    {
        const FString Message = CurrentMessage;

        ChildSlot
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(AppBackground)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                .Padding(ScaledMargin(24.0f, 20.0f, 24.0f, 28.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 18.0f))
                    [
                        MakeHeader()
                    ]

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 18.0f))
                    [
                        MakeReadyCheckCard()
                    ]

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 18.0f))
                    [
                        MakeDashboard()
                    ]

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SAssignNew(MessageText, STextBlock)
                        .AutoWrapText(true)
                        .Font(FontRegular(13))
                        .ColorAndOpacity(MutedColor())
                        .Text(FText::FromString(Message))
                    ]
                ]
            ]
        ];
    }

    FMCPBridgePanelState State;
    TSharedPtr<SVerticalBox> HistoryList;
    TSharedPtr<STextBlock> MessageText;
    bool bAutoRefresh = true;
    bool bCompactLayout = false;
    bool bConnectionProbeInFlight = false;
    bool bConnectionProbeHasResult = false;
    FString CurrentMessage = TEXT("Ready");
    TMap<FString, TSharedPtr<FSlateImageBrush>> IconBrushes;

    const FLinearColor AppBackground = FLinearColor(0.006f, 0.015f, 0.026f, 1.0f);
    const FLinearColor HeaderBackground = FLinearColor(0.012f, 0.028f, 0.044f, 1.0f);
    const FLinearColor CardBackground = FLinearColor(0.030f, 0.050f, 0.074f, 1.0f);
    const FLinearColor CardInsetBackground = FLinearColor(0.045f, 0.068f, 0.096f, 1.0f);
    const FLinearColor CardBorderColor = FLinearColor(0.120f, 0.175f, 0.235f, 1.0f);
    const FLinearColor ButtonNormal = FLinearColor(0.075f, 0.105f, 0.145f, 1.0f);
    const FLinearColor ButtonHover = FLinearColor(0.105f, 0.145f, 0.195f, 1.0f);
    const FLinearColor ButtonPressed = FLinearColor(0.050f, 0.075f, 0.110f, 1.0f);
    const FLinearColor PrimaryBlue = FLinearColor(0.150f, 0.455f, 0.920f, 1.0f);
    const FLinearColor PrimaryBlueHover = FLinearColor(0.205f, 0.555f, 1.000f, 1.0f);
    const FLinearColor PrimaryBluePressed = FLinearColor(0.090f, 0.320f, 0.720f, 1.0f);
    const FLinearColor TextPrimary = FLinearColor(0.930f, 0.950f, 0.980f, 1.0f);
    const FLinearColor TextSecondary = FLinearColor(0.740f, 0.790f, 0.860f, 1.0f);
    const FLinearColor TextMuted = FLinearColor(0.560f, 0.620f, 0.700f, 1.0f);
    const FLinearColor AccentBlue = FLinearColor(0.150f, 0.455f, 0.920f, 1.0f);
    const FLinearColor SuccessGreen = FLinearColor(0.235f, 0.740f, 0.445f, 1.0f);
    const FLinearColor WarningAmber = FLinearColor(1.000f, 0.690f, 0.270f, 1.0f);
    const FLinearColor ErrorRed = FLinearColor(0.930f, 0.320f, 0.320f, 1.0f);

    FSlateColorBrush ButtonNormalBrush;
    FSlateColorBrush ButtonHoverBrush;
    FSlateColorBrush ButtonPressedBrush;
    FSlateColorBrush PrimaryButtonNormalBrush;
    FSlateColorBrush PrimaryButtonHoverBrush;
    FSlateColorBrush PrimaryButtonPressedBrush;
    FSlateColorBrush TransparentBrush;
    FButtonStyle SecondaryButtonStyle;
    FButtonStyle PrimaryButtonStyle;
    FButtonStyle TransparentButtonStyle;

public:
    SMCPBridgePanel()
        : ButtonNormalBrush(ButtonNormal)
        , ButtonHoverBrush(ButtonHover)
        , ButtonPressedBrush(ButtonPressed)
        , PrimaryButtonNormalBrush(PrimaryBlue)
        , PrimaryButtonHoverBrush(PrimaryBlueHover)
        , PrimaryButtonPressedBrush(PrimaryBluePressed)
        , TransparentBrush(FLinearColor::Transparent)
    {
    }

private:
    void ConfigureSlateStyles()
    {
        SecondaryButtonStyle = FButtonStyle()
            .SetNormal(ButtonNormalBrush)
            .SetHovered(ButtonHoverBrush)
            .SetPressed(ButtonPressedBrush)
            .SetDisabled(ButtonPressedBrush)
            .SetNormalPadding(FMargin(0.0f))
            .SetPressedPadding(FMargin(0.0f));

        PrimaryButtonStyle = FButtonStyle()
            .SetNormal(PrimaryButtonNormalBrush)
            .SetHovered(PrimaryButtonHoverBrush)
            .SetPressed(PrimaryButtonPressedBrush)
            .SetDisabled(ButtonPressedBrush)
            .SetNormalPadding(FMargin(0.0f))
            .SetPressedPadding(FMargin(0.0f));

        TransparentButtonStyle = FButtonStyle()
            .SetNormal(TransparentBrush)
            .SetHovered(TransparentBrush)
            .SetPressed(TransparentBrush)
            .SetDisabled(TransparentBrush)
            .SetNormalPadding(FMargin(0.0f))
            .SetPressedPadding(FMargin(0.0f));
    }

    FString GetMCPDir() const
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MCP"));
    }

    FString GetStatusPath() const
    {
        return FPaths::Combine(GetMCPDir(), TEXT("bridge_status.json"));
    }

    FString GetConfigPath() const
    {
        return FPaths::Combine(GetMCPDir(), TEXT("bridge_config.json"));
    }

    FText TextFor(const FString& Value, const FString& Fallback = TEXT("-")) const
    {
        return FText::FromString(Value.IsEmpty() ? Fallback : Value);
    }

    FSlateColor ConnectionColor() const
    {
        return FSlateColor(State.bConnected ? SuccessGreen : ErrorRed);
    }

    FSlateColor MutedColor() const
    {
        return FSlateColor(TextMuted);
    }

    FSlateColor PrimaryColor() const
    {
        return FSlateColor(TextPrimary);
    }

    bool IsSafetyEnabled(const FString& Key) const
    {
        const bool* Value = State.Safety.Find(Key);
        return Value && *Value;
    }

    void SetMessage(const FString& Message)
    {
        CurrentMessage = Message;
        if (MessageText.IsValid())
        {
            MessageText->SetText(FText::FromString(Message));
        }
    }

    bool ReadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutJson) const
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *Path))
        {
            return false;
        }

        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        return FJsonSerializer::Deserialize(Reader, OutJson) && OutJson.IsValid();
    }

    void LoadFiles()
    {
        State.Safety.Empty();
        State.Safety.Add(TEXT("allow_actor_delete"), false);
        State.Safety.Add(TEXT("allow_asset_delete"), false);
        State.Safety.Add(TEXT("allow_console_command"), true);
        State.Safety.Add(TEXT("allow_python_proxy"), true);
        State.Safety.Add(TEXT("auto_compile_blueprints"), true);
        State.Safety.Add(TEXT("auto_save_after_operations"), false);
        State.Safety.Add(TEXT("pie_commands_enabled"), true);

        LoadStatusFile();
        LoadConfigFile();
    }

    void LoadStatusFile()
    {
        TSharedPtr<FJsonObject> Json;
        if (!ReadJsonFile(GetStatusPath(), Json))
        {
            return;
        }

        if (!bConnectionProbeHasResult)
        {
            Json->TryGetBoolField(TEXT("connected"), State.bConnected);
            Json->TryGetBoolField(TEXT("listener_running"), State.bListenerRunning);
        }
        Json->TryGetStringField(TEXT("listener_host"), State.ListenerHost);
        Json->TryGetNumberField(TEXT("listener_port"), State.ListenerPort);
        Json->TryGetStringField(TEXT("engine_version"), State.EngineVersion);
        Json->TryGetStringField(TEXT("project_name"), State.ProjectName);
        Json->TryGetStringField(TEXT("current_level"), State.CurrentLevel);
        Json->TryGetStringField(TEXT("last_ping_at"), State.LastPingAt);
        Json->TryGetStringField(TEXT("last_command"), State.LastCommand);
        Json->TryGetStringField(TEXT("last_command_status"), State.LastResult);
        Json->TryGetStringField(TEXT("last_error_code"), State.LastErrorCode);
        Json->TryGetStringField(TEXT("last_error_message"), State.LastErrorMessage);
        Json->TryGetNumberField(TEXT("commands_handled"), State.CommandsHandled);
        Json->TryGetNumberField(TEXT("failures"), State.Failures);
        State.Errors = 0;
        const bool bHasErrorsField = Json->TryGetNumberField(TEXT("errors"), State.Errors);

        State.History.Empty();
        const TArray<TSharedPtr<FJsonValue>>* HistoryArray = nullptr;
        if (Json->TryGetArrayField(TEXT("history"), HistoryArray) && HistoryArray)
        {
            for (const TSharedPtr<FJsonValue>& EntryValue : *HistoryArray)
            {
                TSharedPtr<FJsonObject> EntryObj = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
                if (!EntryObj.IsValid())
                {
                    continue;
                }

                FMCPCommandHistoryEntry Entry;
                EntryObj->TryGetStringField(TEXT("timestamp"), Entry.Timestamp);
                EntryObj->TryGetStringField(TEXT("command"), Entry.Command);
                EntryObj->TryGetStringField(TEXT("status"), Entry.Status);
                double DurationNumber = 0.0;
                if (EntryObj->TryGetNumberField(TEXT("duration_ms"), DurationNumber))
                {
                    Entry.DurationMs = FString::Printf(TEXT("%.1f"), DurationNumber);
                }
                EntryObj->TryGetStringField(TEXT("error"), Entry.Error);
                if (Entry.Error.IsEmpty())
                {
                    EntryObj->TryGetStringField(TEXT("error_code"), Entry.Error);
                }
                State.History.Add(Entry);
            }
        }

        if (!bHasErrorsField)
        {
            for (const FMCPCommandHistoryEntry& Entry : State.History)
            {
                const bool bErrorStatus = Entry.Status.Equals(TEXT("error"), ESearchCase::IgnoreCase);
                const bool bFailedStatus = Entry.Status.Equals(TEXT("failed"), ESearchCase::IgnoreCase);
                if (bErrorStatus || bFailedStatus || !Entry.Error.IsEmpty())
                {
                    State.Errors++;
                }
            }
        }
    }

    void LoadConfigFile()
    {
        TSharedPtr<FJsonObject> Json;
        if (!ReadJsonFile(GetConfigPath(), Json))
        {
            return;
        }

        const TSharedPtr<FJsonObject>* SafetyJsonPtr = nullptr;
        if (!Json->TryGetObjectField(TEXT("safety"), SafetyJsonPtr) || !SafetyJsonPtr)
        {
            return;
        }

        TSharedPtr<FJsonObject> SafetyJson = *SafetyJsonPtr;
        if (!SafetyJson.IsValid())
        {
            return;
        }

        for (TPair<FString, bool>& Pair : State.Safety)
        {
            bool Value = Pair.Value;
            if (SafetyJson->TryGetBoolField(Pair.Key, Value))
            {
                Pair.Value = Value;
            }
        }
    }

    bool SaveConfigFile()
    {
        IFileManager::Get().MakeDirectory(*GetMCPDir(), true);

        TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> SafetyJson = MakeShared<FJsonObject>();
        for (const TPair<FString, bool>& Pair : State.Safety)
        {
            SafetyJson->SetBoolField(Pair.Key, Pair.Value);
        }
        Root->SetObjectField(TEXT("safety"), SafetyJson);
        Root->SetStringField(TEXT("updated_at"), FDateTime::UtcNow().ToIso8601());

        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
        {
            return false;
        }

        return FFileHelper::SaveStringToFile(Output, *GetConfigPath());
    }

    FSlateFontInfo FontRegular(const int32 Size) const
    {
        return FCoreStyle::GetDefaultFontStyle("Regular", ScaledFont(Size));
    }

    FSlateFontInfo FontBold(const int32 Size) const
    {
        return FCoreStyle::GetDefaultFontStyle("Bold", ScaledFont(Size));
    }

    int32 ScaledFont(const int32 Size) const
    {
        const float Scale = bCompactLayout ? 0.60f : 1.0f;
        return FMath::Max(8, FMath::RoundToInt(static_cast<float>(Size) * Scale));
    }

    float ScaledSize(const float Value) const
    {
        const float Scale = bCompactLayout ? 0.72f : 1.0f;
        return Value * Scale;
    }

    FMargin ScaledMargin(const float Uniform) const
    {
        return FMargin(ScaledSize(Uniform));
    }

    FMargin ScaledMargin(const float Left, const float Top, const float Right, const float Bottom) const
    {
        return FMargin(ScaledSize(Left), ScaledSize(Top), ScaledSize(Right), ScaledSize(Bottom));
    }

    FSlateColor SecondaryColor() const
    {
        return FSlateColor(TextSecondary);
    }

    FLinearColor ResultColor(const FString& Status) const
    {
        if (Status.Equals(TEXT("success"), ESearchCase::IgnoreCase) || Status.Equals(TEXT("ok"), ESearchCase::IgnoreCase))
        {
            return SuccessGreen;
        }
        if (Status.Equals(TEXT("warning"), ESearchCase::IgnoreCase) || Status.Equals(TEXT("warn"), ESearchCase::IgnoreCase))
        {
            return WarningAmber;
        }
        if (Status.Equals(TEXT("error"), ESearchCase::IgnoreCase) || Status.Equals(TEXT("failed"), ESearchCase::IgnoreCase) || Status.Equals(TEXT("failure"), ESearchCase::IgnoreCase))
        {
            return ErrorRed;
        }
        return TextMuted;
    }

    FString ListenerEndpoint() const
    {
        const FString Host = State.ListenerHost.IsEmpty() ? TEXT("localhost") : State.ListenerHost;
        return FString::Printf(TEXT("%s:%d"), *Host, State.ListenerPort > 0 ? State.ListenerPort : 8080);
    }

    FString ListenerUrl() const
    {
        return FString::Printf(TEXT("http://%s"), *ListenerEndpoint());
    }

    FText ConnectionChannelText() const
    {
        return FText::FromString(FString::Printf(TEXT("MCP Bridge HTTP on %s"), *ListenerUrl()));
    }

    FString BuildConnectionPrompt() const
    {
        const FString Project = State.ProjectName.IsEmpty() ? FPaths::GetBaseFilename(FPaths::GetProjectFilePath()) : State.ProjectName;
        const FString Engine = State.EngineVersion.IsEmpty() ? TEXT("Unknown") : State.EngineVersion;
        const FString Level = State.CurrentLevel.IsEmpty() ? TEXT("Unknown") : State.CurrentLevel;
        const FString Status = State.bConnected ? TEXT("Connected") : TEXT("Disconnected in the panel");
        const FString LastPing = State.LastPingAt.IsEmpty() ? TEXT("No ping recorded") : State.LastPingAt;

        return FString::Printf(
            TEXT("Connect to my Unreal Engine MCP Bridge.\n\n")
            TEXT("Channel: MCP Bridge HTTP\n")
            TEXT("Listener URL: %s\n")
            TEXT("Listener endpoint: %s\n")
            TEXT("Project: %s\n")
            TEXT("Engine: %s\n")
            TEXT("Current level: %s\n")
            TEXT("Panel status: %s\n")
            TEXT("Last ping: %s\n\n")
            TEXT("Fast connection path:\n")
            TEXT("1. Do not search the repository for bridge schema unless this direct call fails.\n")
            TEXT("2. If an Unreal MCP tool named test_connection is available, call it first.\n")
            TEXT("3. If no Unreal MCP tool is preloaded, call the listener directly with POST / and this JSON body: {\"command\":\"test_connection\",\"params\":{}}\n")
            TEXT("4. PowerShell direct check: Invoke-RestMethod -Uri '%s' -Method Post -ContentType 'application/json' -Body '{\"command\":\"test_connection\",\"params\":{}}'\n\n")
            TEXT("After test_connection succeeds, continue with my Unreal task using this same listener URL."),
            *ListenerUrl(),
            *ListenerEndpoint(),
            *Project,
            *Engine,
            *Level,
            *Status,
            *LastPing,
            *ListenerUrl()
        );
    }

    bool AreDestructiveActionsDisabled() const
    {
        return !IsSafetyEnabled(TEXT("allow_actor_delete")) && !IsSafetyEnabled(TEXT("allow_asset_delete"));
    }

    TSharedRef<SWidget> MakeHeader()
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(HeaderBackground)
            .Padding(ScaledMargin(18.0f, 14.0f, 18.0f, 14.0f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(ScaledMargin(0.0f, 0.0f, 12.0f, 0.0f))
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(PrimaryBlue)
                    .Padding(ScaledMargin(10.0f, 6.0f, 10.0f, 6.0f))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("MCP")))
                        .Font(FontBold(13))
                        .ColorAndOpacity(FSlateColor(FLinearColor::White))
                    ]
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(ScaledMargin(0.0f, 0.0f, 14.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("MCP Bridge")))
                    .Font(FontBold(28))
                    .ColorAndOpacity(PrimaryColor())
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Editor Bridge")))
                    .Font(FontRegular(13))
                    .ColorAndOpacity(MutedColor())
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNullWidget::NullWidget
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(ScaledMargin(0.0f, 0.0f, 10.0f, 0.0f))
                [
                    MakeConnectionBadge()
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(ScaledMargin(0.0f, 0.0f, 8.0f, 0.0f))
                [
                    MakeToolbarButton(TEXT("Refresh"), [this]() { Refresh(); })
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(ScaledMargin(0.0f, 0.0f, 8.0f, 0.0f))
                [
                    MakeAutoRefreshToggle()
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    MakePrimaryButton(TEXT("Run Self-Test"), [this]() { QueueBridgeCommand(TEXT("bridge_self_test")); }, 132.0f)
                ]
            ];
    }

    TSharedRef<SWidget> MakeReadyCheckCard()
    {
        return MakeCard(TEXT("Ready Check"), TEXT("Current bridge readiness before editor-changing commands."),
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(ScaledMargin(0.0f, 0.0f, 10.0f, 0.0f))
            [
                MakeReadinessItem(TEXT("Bridge"), [this]() { return FText::FromString(State.bConnected ? TEXT("Connected") : TEXT("Disconnected")); }, [this]() { return State.bConnected ? SuccessGreen : ErrorRed; })
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(ScaledMargin(10.0f, 0.0f, 10.0f, 0.0f))
            [
                MakeReadinessItem(TEXT("Delete Safety"), [this]() { return FText::FromString(AreDestructiveActionsDisabled() ? TEXT("Protected") : TEXT("Deletion enabled")); }, [this]() { return AreDestructiveActionsDisabled() ? SuccessGreen : WarningAmber; })
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(ScaledMargin(10.0f, 0.0f, 0.0f, 0.0f))
            [
                MakeReadinessItem(TEXT("Python Proxy"), [this]() { return FText::FromString(IsSafetyEnabled(TEXT("allow_python_proxy")) ? TEXT("Python enabled") : TEXT("Python disabled")); }, [this]() { return IsSafetyEnabled(TEXT("allow_python_proxy")) ? PrimaryBlue : TextMuted; })
            ]);
    }

    TSharedRef<SWidget> MakeDashboard()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(ScaledMargin(0.0f, 0.0f, 10.0f, 0.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 14.0f))
                    [
                        MakeConnectionCard()
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        MakeSafetySection()
                    ]
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(ScaledMargin(10.0f, 0.0f, 0.0f, 0.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 14.0f))
                    [
                        MakeStatusSection()
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        MakeActionsSection()
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(ScaledMargin(0.0f, 18.0f, 0.0f, 0.0f))
            [
                MakeOptimizationSection()
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(ScaledMargin(0.0f, 18.0f, 0.0f, 0.0f))
            [
                MakeHistorySection()
            ];
    }

    TSharedRef<SWidget> MakeConnectionCard()
    {
        return MakeCard(TEXT("Connection"), TEXT("Current Unreal editor target."),
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeKeyValue(TEXT("Channel"), [this]() { return ConnectionChannelText(); })]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeKeyValue(TEXT("Listener"), [this]() { return FText::FromString(ListenerEndpoint()); })]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeKeyValue(TEXT("Engine"), [this]() { return TextFor(State.EngineVersion); })]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeKeyValue(TEXT("Project"), [this]() { return TextFor(State.ProjectName); })]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeKeyValue(TEXT("Level"), [this]() { return TextFor(State.CurrentLevel); })]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 12.0f))[MakeKeyValue(TEXT("Last ping"), [this]() { return TextFor(State.LastPingAt, TEXT("Waiting for ping")); })]
            + SVerticalBox::Slot().AutoHeight()[MakeActionButton(TEXT("Copy Chat Prompt"), TEXT("bridge_copy_connection_prompt"))]);
    }

    TSharedRef<SWidget> MakeSafetySection()
    {
        return MakeCard(TEXT("Safety"), TEXT("Destructive actions stay off until the user enables them."),
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("allow_actor_delete"), TEXT("Allow Actor Delete"), TEXT("Delete actors by name or pattern."))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("allow_asset_delete"), TEXT("Allow Asset Delete"), TEXT("Delete assets from the content browser."))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("allow_console_command"), TEXT("Allow Console Command"), TEXT("Run allowlisted console commands."))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("allow_python_proxy"), TEXT("Allow Python Proxy"), TEXT("Execute Unreal Python from MCP tools."))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("auto_compile_blueprints"), TEXT("Auto Compile Blueprints"), TEXT("Compile generated or changed Blueprint assets."))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 10.0f))[MakeSafetyToggle(TEXT("auto_save_after_operations"), TEXT("Auto Save After Operations"), TEXT("Save assets after successful batch actions."))]
            + SVerticalBox::Slot().AutoHeight()[MakeSafetyToggle(TEXT("pie_commands_enabled"), TEXT("PIE Commands Enabled"), TEXT("Allow start, stop, and status for Play In Editor."))]);
    }

    TSharedRef<SWidget> MakeStatusSection()
    {
        return MakeCard(TEXT("Status"), TEXT("Last command and bridge health."),
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 14.0f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(ScaledMargin(0.0f, 0.0f, 10.0f, 0.0f))
                [MakeStatusValue(TEXT("Last command"), [this]() { return TextFor(State.LastCommand, TEXT("No command yet")); }, TextPrimary)]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(ScaledMargin(10.0f, 0.0f, 10.0f, 0.0f))
                [MakeStatusValue(TEXT("Last result"), [this]() { return TextFor(State.LastResult, TEXT("Idle")); }, [this]() { return ResultColor(State.LastResult); })]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(ScaledMargin(10.0f, 0.0f, 0.0f, 0.0f))
                [MakeStatusValue(TEXT("Last error"), [this]() { return TextFor(State.LastErrorMessage.IsEmpty() ? State.LastErrorCode : State.LastErrorMessage, TEXT("No recent error")); }, TextSecondary)]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(ScaledMargin(0.0f, 0.0f, 8.0f, 0.0f))
                [MakeMetricTile(TEXT("CMD"), TEXT("Commands"), [this]() { return FText::AsNumber(State.CommandsHandled); }, PrimaryBlue)]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(ScaledMargin(8.0f, 0.0f, 8.0f, 0.0f))
                [MakeMetricTile(TEXT("WARN"), TEXT("Failures"), [this]() { return FText::AsNumber(State.Failures); }, WarningAmber)]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(ScaledMargin(8.0f, 0.0f, 0.0f, 0.0f))
                [MakeMetricTile(TEXT("ERR"), TEXT("Errors"), [this]() { return FText::AsNumber(State.Errors); }, ErrorRed)]
            ]);
    }

    TSharedRef<SWidget> MakeActionsSection()
    {
        if (bCompactLayout)
        {
            return MakeCard(TEXT("Actions"), TEXT("Common bridge actions."), MakeActionsList());
        }

        return MakeCard(TEXT("Actions"), TEXT("Common bridge actions."), MakeActionsGrid());
    }

    TSharedRef<SWidget> MakeActionsGrid()
    {
        return SNew(SUniformGridPanel)
            .SlotPadding(ScaledMargin(6.0f))
            + SUniformGridPanel::Slot(0, 0)[MakeActionButton(TEXT("Run Self-Test"), TEXT("bridge_self_test"))]
            + SUniformGridPanel::Slot(1, 0)[MakeActionButton(TEXT("Test Connection"), TEXT("test_connection"))]
            + SUniformGridPanel::Slot(2, 0)[MakeActionButton(TEXT("Copy Chat Prompt"), TEXT("bridge_copy_connection_prompt"))]
            + SUniformGridPanel::Slot(0, 1)[MakeActionButton(TEXT("Copy Project Info"), TEXT("bridge_copy_project_info"))]
            + SUniformGridPanel::Slot(1, 1)[MakeActionButton(TEXT("Clear Bridge Log"), TEXT("bridge_clear_log"))]
            + SUniformGridPanel::Slot(2, 1)[MakeActionButton(TEXT("Open Output Log"), TEXT("bridge_open_output_log"))]
            + SUniformGridPanel::Slot(0, 2)[MakeActionButton(TEXT("Save Current Level"), TEXT("level_save"))]
            + SUniformGridPanel::Slot(1, 2)[MakeActionButton(TEXT("Screenshot"), TEXT("viewport_screenshot"))];
    }

    TSharedRef<SWidget> MakeActionsList()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Run Self-Test"), TEXT("bridge_self_test"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Test Connection"), TEXT("test_connection"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Copy Chat Prompt"), TEXT("bridge_copy_connection_prompt"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Clear Bridge Log"), TEXT("bridge_clear_log"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Copy Project Info"), TEXT("bridge_copy_project_info"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Open Output Log"), TEXT("bridge_open_output_log"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Save Current Level"), TEXT("level_save"))]
            + SVerticalBox::Slot().AutoHeight()[MakeActionButton(TEXT("Screenshot"), TEXT("viewport_screenshot"))];
    }

    TSharedRef<SWidget> MakeOptimizationSection()
    {
        if (bCompactLayout)
        {
            return MakeCard(TEXT("Optimization"), TEXT("Run UE4.27 profiling captures, audits, and reports."),
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 14.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))
                    [MakeStatusValue(TEXT("Target"), []() { return FText::FromString(TEXT("Desktop 60 FPS")); }, PrimaryBlue)]
                    + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))
                    [MakeStatusValue(TEXT("Frame budget"), []() { return FText::FromString(TEXT("16.67 ms")); }, TextPrimary)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeStatusValue(TEXT("Next step"), []() { return FText::FromString(TEXT("Run Quick Triage")); }, TextSecondary)]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    MakeOptimizationCompactGrid()
                ]);
        }

        return MakeCard(TEXT("Optimization"), TEXT("Run UE4.27 profiling captures, audits, and reports."),
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 14.0f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(ScaledMargin(0.0f, 0.0f, 10.0f, 0.0f))
                [MakeStatusValue(TEXT("Target"), []() { return FText::FromString(TEXT("Desktop 60 FPS")); }, PrimaryBlue)]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(ScaledMargin(10.0f, 0.0f, 10.0f, 0.0f))
                [MakeStatusValue(TEXT("Frame budget"), []() { return FText::FromString(TEXT("16.67 ms")); }, TextPrimary)]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(ScaledMargin(10.0f, 0.0f, 0.0f, 0.0f))
                [MakeStatusValue(TEXT("Next step"), []() { return FText::FromString(TEXT("Run Quick Triage")); }, TextSecondary)]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SUniformGridPanel)
                .SlotPadding(ScaledMargin(6.0f))
                + SUniformGridPanel::Slot(0, 0)[MakeActionButton(TEXT("Quick Triage"), TEXT("optimization_quick_triage"))]
                + SUniformGridPanel::Slot(1, 0)[MakeActionButton(TEXT("GPU Capture"), TEXT("optimization_gpu_capture"))]
                + SUniformGridPanel::Slot(2, 0)[MakeActionButton(TEXT("CPU Capture"), TEXT("optimization_cpu_capture"))]
                + SUniformGridPanel::Slot(3, 0)[MakeActionButton(TEXT("Memory"), TEXT("optimization_memory_streaming_capture"))]
                + SUniformGridPanel::Slot(0, 1)[MakeActionButton(TEXT("Asset Audit"), TEXT("optimization_asset_audit"))]
                + SUniformGridPanel::Slot(1, 1)[MakeActionButton(TEXT("Scene Audit"), TEXT("optimization_scene_audit"))]
                + SUniformGridPanel::Slot(2, 1)[MakeActionButton(TEXT("VR Audit"), TEXT("optimization_vr_audit"))]
                + SUniformGridPanel::Slot(3, 1)[MakeActionButton(TEXT("Export Report"), TEXT("optimization_generate_report"))]
                + SUniformGridPanel::Slot(0, 2)[MakeActionButton(TEXT("Texture Rank"), TEXT("optimization_texture_harm_rank"))]
                + SUniformGridPanel::Slot(1, 2)[MakeActionButton(TEXT("Asset Rank"), TEXT("optimization_asset_cost_rank"))]
                + SUniformGridPanel::Slot(2, 2)[MakeActionButton(TEXT("Stat Capture"), TEXT("optimization_stat_capture"))]
                + SUniformGridPanel::Slot(3, 2)[MakeActionButton(TEXT("ProfileGPU+"), TEXT("optimization_profilegpu_capture"))]
                + SUniformGridPanel::Slot(0, 3)[MakeActionButton(TEXT("Insights"), TEXT("optimization_insights_trace_start"))]
                + SUniformGridPanel::Slot(1, 3)[MakeActionButton(TEXT("Camera Path"), TEXT("optimization_camera_path_profile"))]
                + SUniformGridPanel::Slot(2, 3)[MakeActionButton(TEXT("Fix Candidates"), TEXT("optimization_fix_candidates"))]
                + SUniformGridPanel::Slot(3, 3)[MakeActionButton(TEXT("Verify"), TEXT("optimization_before_after_verify"))]
            ]);
    }

    TSharedRef<SWidget> MakeOptimizationList()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Quick Triage"), TEXT("optimization_quick_triage"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("GPU Capture"), TEXT("optimization_gpu_capture"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("CPU Capture"), TEXT("optimization_cpu_capture"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Memory"), TEXT("optimization_memory_streaming_capture"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Asset Audit"), TEXT("optimization_asset_audit"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Scene Audit"), TEXT("optimization_scene_audit"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("VR Audit"), TEXT("optimization_vr_audit"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Export Report"), TEXT("optimization_generate_report"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Texture Rank"), TEXT("optimization_texture_harm_rank"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Asset Rank"), TEXT("optimization_asset_cost_rank"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Stat Capture"), TEXT("optimization_stat_capture"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("ProfileGPU+"), TEXT("optimization_profilegpu_capture"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Insights"), TEXT("optimization_insights_trace_start"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Camera Path"), TEXT("optimization_camera_path_profile"))]
            + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))[MakeActionButton(TEXT("Fix Candidates"), TEXT("optimization_fix_candidates"))]
            + SVerticalBox::Slot().AutoHeight()[MakeActionButton(TEXT("Verify"), TEXT("optimization_before_after_verify"))];
    }

    TSharedRef<SWidget> MakeOptimizationCompactGrid()
    {
        return SNew(SUniformGridPanel)
            .SlotPadding(ScaledMargin(6.0f))
            + SUniformGridPanel::Slot(0, 0)[MakeIconActionButton(TEXT("Quick Triage"), TEXT("optimization_quick_triage"), TEXT("Editor/Slate/Icons/Profiler/Profiler_FPS_Chart_40x.png"))]
            + SUniformGridPanel::Slot(1, 0)[MakeIconActionButton(TEXT("GPU Capture"), TEXT("optimization_gpu_capture"), TEXT("Editor/Slate/Icons/HardwareTargeting/GraphicsUnspecified.png"))]
            + SUniformGridPanel::Slot(0, 1)[MakeIconActionButton(TEXT("CPU Capture"), TEXT("optimization_cpu_capture"), TEXT("Editor/Slate/Icons/Profiler/profiler_GameThread_32x.png"))]
            + SUniformGridPanel::Slot(1, 1)[MakeIconActionButton(TEXT("Memory"), TEXT("optimization_memory_streaming_capture"), TEXT("Editor/Slate/Icons/Profiler/profiler_mem_40x.png"))]
            + SUniformGridPanel::Slot(0, 2)[MakeIconActionButton(TEXT("Asset Audit"), TEXT("optimization_asset_audit"), TEXT("Editor/Slate/Icons/AssetIcons/DataAsset_64x.png"))]
            + SUniformGridPanel::Slot(1, 2)[MakeIconActionButton(TEXT("Scene Audit"), TEXT("optimization_scene_audit"), TEXT("Editor/Slate/Icons/AssetIcons/SceneCapture2D_64x.png"))]
            + SUniformGridPanel::Slot(0, 3)[MakeIconActionButton(TEXT("VR Audit"), TEXT("optimization_vr_audit"), TEXT("Editor/Slate/Common/ScreenAttach_VR.png"))]
            + SUniformGridPanel::Slot(1, 3)[MakeIconActionButton(TEXT("Export Report"), TEXT("optimization_generate_report"), TEXT("Editor/Slate/Icons/Profiler/profiler_CopyToClipboard_32x.png"))]
            + SUniformGridPanel::Slot(0, 4)[MakeIconActionButton(TEXT("Texture Rank"), TEXT("optimization_texture_harm_rank"), TEXT("Editor/Slate/Icons/AssetIcons/Texture2D_64x.png"))]
            + SUniformGridPanel::Slot(1, 4)[MakeIconActionButton(TEXT("Asset Rank"), TEXT("optimization_asset_cost_rank"), TEXT("Editor/Slate/Icons/Profiler/profiler_SortBy_32x.png"))]
            + SUniformGridPanel::Slot(0, 5)[MakeIconActionButton(TEXT("Stat Capture"), TEXT("optimization_stat_capture"), TEXT("Editor/Slate/Icons/Profiler/profiler_stats_40x.png"))]
            + SUniformGridPanel::Slot(1, 5)[MakeIconActionButton(TEXT("ProfileGPU+"), TEXT("optimization_profilegpu_capture"), TEXT("Editor/Slate/Icons/Profiler/Profiler_Data_Capture_40x.png"))]
            + SUniformGridPanel::Slot(0, 6)[MakeIconActionButton(TEXT("Insights"), TEXT("optimization_insights_trace_start"), TEXT("Editor/Slate/Icons/Profiler/profiler_HotPath_32x.png"))]
            + SUniformGridPanel::Slot(1, 6)[MakeIconActionButton(TEXT("Camera Path"), TEXT("optimization_camera_path_profile"), TEXT("Editor/Slate/Icons/AssetIcons/CameraRig_Rail_64x.png"))]
            + SUniformGridPanel::Slot(0, 7)[MakeIconActionButton(TEXT("Fix Candidates"), TEXT("optimization_fix_candidates"), TEXT("Editor/Slate/Icons/GeneralTools/Settings_40x.png"))]
            + SUniformGridPanel::Slot(1, 7)[MakeIconActionButton(TEXT("Verify"), TEXT("optimization_before_after_verify"), TEXT("Editor/Slate/Common/Check.png"))];
    }

    TSharedRef<SWidget> MakeHistorySection()
    {
        return MakeCard(TEXT("Recent Commands"), TEXT("Bridge operations. Scroll to inspect long command history."),
            SNew(SBox)
            .HeightOverride(ScaledSize(765.0f))
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(CardInsetBackground)
                .Padding(ScaledMargin(14.0f, 12.0f, 14.0f, 12.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 8.0f))
                    [
                        MakeHistoryHeader()
                    ]
                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    [
                        SNew(SScrollBox)
                        .Orientation(Orient_Vertical)
                        + SScrollBox::Slot()
                        [
                            SAssignNew(HistoryList, SVerticalBox)
                        ]
                    ]
                ]
            ]);
    }

    TSharedRef<SWidget> MakeCard(const FString& Title, const FString& Subtitle, TSharedRef<SWidget> Content)
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(CardBorderColor)
            .Padding(ScaledSize(1.0f))
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(CardBackground)
                .Padding(ScaledMargin(18.0f, 16.0f, 18.0f, 16.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(ScaledMargin(0.0f, 0.0f, 0.0f, 14.0f))
                    [
                        MakeCardTitle(Title, Subtitle)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        Content
                    ]
                ]
            ];
    }

    TSharedRef<SWidget> MakeCardTitle(const FString& Title, const FString& Subtitle)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(ScaledMargin(0.0f, 0.0f, 10.0f, 0.0f))
            [
                SNew(SBox)
                .WidthOverride(ScaledSize(4.0f))
                .HeightOverride(ScaledSize(24.0f))
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(PrimaryBlue)
                ]
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Title))
                    .Font(FontBold(18))
                    .ColorAndOpacity(PrimaryColor())
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ScaledMargin(0.0f, 3.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Subtitle))
                    .Font(FontRegular(12))
                    .ColorAndOpacity(MutedColor())
                    .AutoWrapText(true)
                ]
            ];
    }

    TSharedRef<SWidget> MakePill(TFunction<FText()> LabelGetter, const FLinearColor& BackgroundColor, const FLinearColor& TextColor, const int32 FontSize)
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(BackgroundColor)
            .Padding(ScaledMargin(10.0f, 5.0f, 10.0f, 5.0f))
            [
                SNew(STextBlock)
                .Text_Lambda([LabelGetter]() { return LabelGetter(); })
                .Font(FontBold(FontSize))
                .ColorAndOpacity(FSlateColor(TextColor))
            ];
    }

    TSharedRef<SWidget> MakeConnectionBadge()
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor_Lambda([this]() { return State.bConnected ? SuccessGreen : ErrorRed; })
            .Padding(ScaledMargin(12.0f, 6.0f, 12.0f, 6.0f))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() { return FText::FromString(State.bConnected ? TEXT("Connected") : TEXT("Disconnected")); })
                .Font(FontBold(13))
                .ColorAndOpacity(FSlateColor(FLinearColor::White))
            ];
    }

    TSharedRef<SWidget> MakeToolbarButton(const FString& Label, TFunction<void()> Action)
    {
        return SNew(SBox)
            .WidthOverride(ScaledSize(96.0f))
            .HeightOverride(ScaledSize(34.0f))
            [
                SNew(SButton)
                .ButtonStyle(&SecondaryButtonStyle)
                .ContentPadding(ScaledMargin(12.0f, 0.0f, 12.0f, 0.0f))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .OnClicked_Lambda([Action]()
                {
                    Action();
                    return FReply::Handled();
                })
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Label))
                    .Font(FontBold(12))
                    .Justification(ETextJustify::Center)
                    .ColorAndOpacity(PrimaryColor())
                ]
            ];
    }

    TSharedRef<SWidget> MakePrimaryButton(const FString& Label, TFunction<void()> Action, const float Width)
    {
        return SNew(SBox)
            .WidthOverride(ScaledSize(Width))
            .HeightOverride(ScaledSize(34.0f))
            [
                SNew(SButton)
                .ButtonStyle(&PrimaryButtonStyle)
                .ContentPadding(ScaledMargin(12.0f, 0.0f, 12.0f, 0.0f))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .OnClicked_Lambda([Action]()
                {
                    Action();
                    return FReply::Handled();
                })
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Label))
                    .Font(FontBold(12))
                    .Justification(ETextJustify::Center)
                    .ColorAndOpacity(FSlateColor(FLinearColor::White))
                ]
            ];
    }

    TSharedRef<SWidget> MakeAutoRefreshToggle()
    {
        return SNew(SButton)
            .ButtonStyle(&TransparentButtonStyle)
            .OnClicked_Lambda([this]()
            {
                bAutoRefresh = !bAutoRefresh;
                SetMessage(bAutoRefresh ? TEXT("Auto-refresh enabled") : TEXT("Auto-refresh disabled"));
                return FReply::Handled();
            })
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this]() { return bAutoRefresh ? PrimaryBlue : ButtonNormal; })
                .Padding(ScaledMargin(10.0f, 7.0f, 10.0f, 7.0f))
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() { return FText::FromString(bAutoRefresh ? TEXT("Auto-refresh on") : TEXT("Auto-refresh off")); })
                    .Font(FontBold(12))
                    .ColorAndOpacity(FSlateColor(FLinearColor::White))
                ]
            ];
    }

    TSharedRef<SWidget> MakeReadinessItem(const FString& Label, TFunction<FText()> ValueGetter, TFunction<FLinearColor()> AccentGetter)
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(CardInsetBackground)
            .Padding(ScaledMargin(14.0f, 12.0f, 14.0f, 12.0f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(ScaledMargin(0.0f, 0.0f, 10.0f, 0.0f))
                [
                    SNew(SBox)
                    .WidthOverride(ScaledSize(8.0f))
                    .HeightOverride(ScaledSize(8.0f))
                    [
                        SNew(SBorder)
                        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                        .BorderBackgroundColor_Lambda([AccentGetter]() { return AccentGetter(); })
                    ]
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Label))
                        .Font(FontRegular(12))
                        .ColorAndOpacity(MutedColor())
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 3.0f, 0.0f, 0.0f))
                    [
                        SNew(STextBlock)
                        .Text_Lambda([ValueGetter]() { return ValueGetter(); })
                        .Font(FontBold(14))
                        .ColorAndOpacity(PrimaryColor())
                    ]
                ]
            ];
    }

    TSharedRef<SWidget> MakeKeyValue(const FString& Key, TFunction<FText()> ValueGetter)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(ScaledMargin(0.0f, 0.0f, 14.0f, 0.0f))
            [
                SNew(SBox)
                .WidthOverride(ScaledSize(112.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Key))
                    .Font(FontRegular(13))
                    .ColorAndOpacity(MutedColor())
                ]
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text_Lambda([ValueGetter]() { return ValueGetter(); })
                .Font(FontRegular(14))
                .AutoWrapText(true)
                .ColorAndOpacity(PrimaryColor())
            ];
    }

    TSharedRef<SWidget> MakeSafetyToggle(const FString Key, const FString Label, const FString HelpText)
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(CardInsetBackground)
            .Padding(ScaledMargin(12.0f, 10.0f, 12.0f, 10.0f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Label))
                        .Font(FontBold(14))
                        .ColorAndOpacity(PrimaryColor())
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 2.0f, 0.0f, 0.0f))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(HelpText))
                        .Font(FontRegular(12))
                        .ColorAndOpacity(MutedColor())
                        .AutoWrapText(true)
                    ]
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(ScaledMargin(12.0f, 0.0f, 0.0f, 0.0f))
                [
                    SNew(SButton)
                    .ButtonStyle(&TransparentButtonStyle)
                    .OnClicked_Lambda([this, Key]()
                    {
                        State.Safety.FindOrAdd(Key) = !IsSafetyEnabled(Key);
                        if (SaveConfigFile())
                        {
                            SetMessage(FString::Printf(TEXT("Updated safety: %s"), *Key));
                        }
                        else
                        {
                            SetMessage(FString::Printf(TEXT("Could not save safety setting: %s"), *Key));
                        }
                        return FReply::Handled();
                    })
                    [
                        SNew(SBorder)
                        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                        .BorderBackgroundColor_Lambda([this, Key]() { return IsSafetyEnabled(Key) ? PrimaryBlue : ButtonNormal; })
                        .Padding(ScaledMargin(12.0f, 6.0f, 12.0f, 6.0f))
                        [
                            SNew(SBox)
                            .WidthOverride(ScaledSize(34.0f))
                            [
                                SNew(STextBlock)
                                .Text_Lambda([this, Key]() { return FText::FromString(IsSafetyEnabled(Key) ? TEXT("ON") : TEXT("OFF")); })
                                .Font(FontBold(11))
                                .Justification(ETextJustify::Center)
                                .ColorAndOpacity_Lambda([this, Key]() { return FSlateColor(IsSafetyEnabled(Key) ? FLinearColor::White : TextMuted); })
                            ]
                        ]
                    ]
                ]
            ];
    }

    TSharedRef<SWidget> MakeStatusValue(const FString& Label, TFunction<FText()> ValueGetter, const FLinearColor& ValueColor)
    {
        return MakeStatusValue(Label, ValueGetter, [ValueColor]() { return ValueColor; });
    }

    TSharedRef<SWidget> MakeStatusValue(const FString& Label, TFunction<FText()> ValueGetter, TFunction<FLinearColor()> ValueColorGetter)
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(CardInsetBackground)
            .Padding(ScaledMargin(12.0f, 10.0f, 12.0f, 10.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Label))
                    .Font(FontRegular(12))
                    .ColorAndOpacity(MutedColor())
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 5.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text_Lambda([ValueGetter]() { return ValueGetter(); })
                    .Font(FontBold(14))
                    .ColorAndOpacity_Lambda([ValueColorGetter]() { return FSlateColor(ValueColorGetter()); })
                    .AutoWrapText(true)
                ]
            ];
    }

    TSharedRef<SWidget> MakeMetricTile(const FString& Code, const FString& Label, TFunction<FText()> ValueGetter, const FLinearColor& AccentColor)
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(CardInsetBackground)
            .Padding(ScaledMargin(14.0f, 14.0f, 14.0f, 14.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Code))
                    .Font(FontBold(12))
                    .ColorAndOpacity(FSlateColor(AccentColor))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(ScaledMargin(0.0f, 4.0f, 0.0f, 2.0f))
                [
                    SNew(STextBlock)
                    .Text_Lambda([ValueGetter]() { return ValueGetter(); })
                    .Font(FontBold(30))
                    .ColorAndOpacity(PrimaryColor())
                    .Justification(ETextJustify::Center)
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Label))
                    .Font(FontRegular(12))
                    .ColorAndOpacity(SecondaryColor())
                    .Justification(ETextJustify::Center)
                ]
            ];
    }

    TSharedRef<SWidget> MakeActionButton(const FString& Label, const FString& CommandName)
    {
        return SNew(SBox)
            .HeightOverride(ScaledSize(46.0f))
            .MinDesiredWidth(ScaledSize(150.0f))
            [
                SNew(SButton)
                .ButtonStyle(&SecondaryButtonStyle)
                .ContentPadding(ScaledMargin(12.0f, 0.0f, 12.0f, 0.0f))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .OnClicked_Lambda([this, CommandName]()
                {
                    return QueueBridgeCommand(CommandName);
                })
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Label))
                    .Font(FontBold(13))
                    .Justification(ETextJustify::Center)
                    .ColorAndOpacity(PrimaryColor())
                    .AutoWrapText(true)
                ]
            ];
    }

    const FSlateBrush* GetIconBrush(const FString& RelativeIconPath)
    {
        TSharedPtr<FSlateImageBrush>& Brush = IconBrushes.FindOrAdd(RelativeIconPath);
        if (!Brush.IsValid())
        {
            Brush = MakeShareable(new FSlateImageBrush(FPaths::Combine(FPaths::EngineContentDir(), RelativeIconPath), FVector2D(40.0f, 40.0f)));
        }

        return Brush.Get();
    }

    TSharedRef<SWidget> MakeIconActionButton(const FString& Label, const FString& CommandName, const FString& RelativeIconPath)
    {
        return SNew(SBox)
            .HeightOverride(ScaledSize(64.0f))
            .MinDesiredWidth(ScaledSize(230.0f))
            [
                SNew(SButton)
                .ButtonStyle(&SecondaryButtonStyle)
                .ContentPadding(ScaledMargin(14.0f, 0.0f, 14.0f, 0.0f))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .OnClicked_Lambda([this, CommandName]()
                {
                    return QueueBridgeCommand(CommandName);
                })
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(ScaledMargin(0.0f, 0.0f, 14.0f, 0.0f))
                    [
                        SNew(SBox)
                        .WidthOverride(ScaledSize(30.0f))
                        .HeightOverride(ScaledSize(30.0f))
                        [
                            SNew(SImage)
                            .Image(GetIconBrush(RelativeIconPath))
                            .ColorAndOpacity(FSlateColor(FLinearColor::White))
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Label))
                        .Font(FontBold(13))
                        .Justification(ETextJustify::Center)
                        .ColorAndOpacity(PrimaryColor())
                        .AutoWrapText(true)
                    ]
                ]
            ];
    }

    TSharedRef<SWidget> MakeHistoryHeader()
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.20f)[MakeColumnHeader(TEXT("Time UTC"))]
            + SHorizontalBox::Slot().FillWidth(1.65f)[MakeColumnHeader(TEXT("Command"))]
            + SHorizontalBox::Slot().FillWidth(0.90f)[MakeColumnHeader(TEXT("Result"))]
            + SHorizontalBox::Slot().FillWidth(0.65f)[MakeColumnHeader(TEXT("Duration"))]
            + SHorizontalBox::Slot().FillWidth(1.35f)[MakeColumnHeader(TEXT("Error"))];
    }

    TSharedRef<SWidget> MakeColumnHeader(const FString& Label)
    {
        return SNew(STextBlock)
            .Text(FText::FromString(Label))
            .Font(FontBold(12))
            .ColorAndOpacity(MutedColor());
    }

    void RebuildHistory()
    {
        if (!HistoryList.IsValid())
        {
            return;
        }

        HistoryList->ClearChildren();
        if (State.History.Num() == 0)
        {
            HistoryList->AddSlot()
            .FillHeight(1.0f)
            .VAlign(VAlign_Center)
            .HAlign(HAlign_Center)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("No recent commands")))
                    .Font(FontBold(15))
                    .ColorAndOpacity(PrimaryColor())
                ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(ScaledMargin(0.0f, 5.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Run Self-Test or Test Connection to verify the bridge.")))
                    .Font(FontRegular(13))
                    .ColorAndOpacity(MutedColor())
                ]
            ];
            return;
        }

        for (int32 Index = State.History.Num() - 1; Index >= 0; --Index)
        {
            HistoryList->AddSlot()
            .AutoHeight()
            [
                MakeHistoryRow(State.History[Index])
            ];
        }
    }

    TSharedRef<SWidget> MakeHistoryRow(const FMCPCommandHistoryEntry& Entry)
    {
        const FString Duration = Entry.DurationMs.IsEmpty() ? TEXT("-") : FString::Printf(TEXT("%s ms"), *Entry.DurationMs);

        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor::Transparent)
            .Padding(ScaledMargin(0.0f, 8.0f, 0.0f, 8.0f))
            .Clipping(EWidgetClipping::ClipToBounds)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.20f)[MakeHistoryCell(Entry.Timestamp, false)]
                + SHorizontalBox::Slot().FillWidth(1.65f)[MakeHistoryCell(Entry.Command, false)]
                + SHorizontalBox::Slot().FillWidth(0.90f).Padding(ScaledMargin(0.0f, 0.0f, 12.0f, 0.0f))[MakeHistoryBadge(Entry.Status)]
                + SHorizontalBox::Slot().FillWidth(0.65f)[MakeHistoryCell(Duration, false)]
                + SHorizontalBox::Slot().FillWidth(1.35f)[MakeHistoryCell(Entry.Error.IsEmpty() ? TEXT("-") : Entry.Error, true)]
            ];
    }

    TSharedRef<SWidget> MakeHistoryCell(const FString& Text, const bool bWrapText)
    {
        return SNew(STextBlock)
            .Text(FText::FromString(Text.IsEmpty() ? TEXT("-") : Text))
            .Font(FontRegular(13))
            .AutoWrapText(bWrapText)
            .Clipping(EWidgetClipping::ClipToBounds)
            .ColorAndOpacity(SecondaryColor());
    }

    TSharedRef<SWidget> MakeHistoryBadge(const FString& Status)
    {
        const FString Label = Status.IsEmpty() ? TEXT("-") : Status;
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(ResultColor(Label))
            .Padding(ScaledMargin(10.0f, 4.0f, 10.0f, 4.0f))
            [
                SNew(STextBlock)
                .Text(FText::FromString(Label))
                .Font(FontBold(12))
                .Justification(ETextJustify::Center)
                .ColorAndOpacity(FSlateColor(FLinearColor::White))
            ];
    }

    FReply QueueBridgeCommand(const FString& CommandName)
    {
        if (CommandName == TEXT("bridge_copy_connection_prompt"))
        {
            CopyConnectionPrompt();
            return FReply::Handled();
        }

        if (CommandName == TEXT("bridge_copy_project_info"))
        {
            CopyProjectInfo();
            return FReply::Handled();
        }

        if (CommandName == TEXT("bridge_open_output_log"))
        {
            OpenOutputLog();
            return FReply::Handled();
        }

        if (CommandName == TEXT("level_save"))
        {
            PostCommand(CommandName, TEXT("{\"save_all\": false}"));
            return FReply::Handled();
        }

        if (CommandName == TEXT("viewport_screenshot"))
        {
            PostCommand(CommandName, TEXT("{\"resolution\": {\"width\": 1920, \"height\": 1080}}"));
            return FReply::Handled();
        }

        if (CommandName == TEXT("optimization_quick_triage"))
        {
            PostCommand(CommandName, TEXT("{\"target\":\"desktop_60\",\"capture_screenshots\":true,\"include_asset_audit\":true,\"include_scene_audit\":true,\"generate_report\":true}"));
            return FReply::Handled();
        }

        if (CommandName == TEXT("optimization_cpu_capture") || CommandName == TEXT("optimization_memory_streaming_capture"))
        {
            if (CommandName == TEXT("optimization_memory_streaming_capture"))
            {
                PostCommand(CommandName, TEXT("{\"capture_screenshots\":true,\"include_asset_audit\":true,\"generate_report\":true}"));
            }
            else
            {
                PostCommand(CommandName, TEXT("{\"capture_screenshots\":true}"));
            }
            return FReply::Handled();
        }

        if (CommandName == TEXT("optimization_asset_audit"))
        {
            PostCommand(CommandName, TEXT("{\"path\":\"/Game/\",\"recursive\":true}"));
            return FReply::Handled();
        }

        if (CommandName == TEXT("optimization_scene_audit"))
        {
            PostCommand(CommandName, TEXT("{\"include_design_advice\":true}"));
            return FReply::Handled();
        }

        if (CommandName == TEXT("optimization_vr_audit"))
        {
            PostCommand(CommandName, TEXT("{\"target\":\"vr_90\",\"include_asset_audit\":true}"));
            return FReply::Handled();
        }

        if (CommandName == TEXT("optimization_generate_report"))
        {
            PostCommand(CommandName, TEXT("{\"target\":\"desktop_60\",\"capture_screenshots\":true,\"include_asset_audit\":true,\"include_scene_audit\":true}"));
            return FReply::Handled();
        }

        PostCommand(CommandName);
        return FReply::Handled();
    }

    void Refresh()
    {
        LoadFiles();
        RebuildHistory();
        ProbeConnection(true);
        SetMessage(TEXT("Refreshed"));
    }

    EActiveTimerReturnType HandleRefreshTimer(double InCurrentTime, float InDeltaTime)
    {
        if (bAutoRefresh)
        {
            LoadFiles();
            RebuildHistory();
            ProbeConnection(false);
        }
        return EActiveTimerReturnType::Continue;
    }

    void ProbeConnection(const bool bShowFailures)
    {
        if (bConnectionProbeInFlight)
        {
            return;
        }

        bConnectionProbeInFlight = true;

        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(FString::Printf(TEXT("%s/ping"), *ListenerUrl()));
        Request->SetVerb(TEXT("GET"));
        Request->SetTimeout(2.0f);
        Request->OnProcessRequestComplete().BindSP(this, &SMCPBridgePanel::OnConnectionProbeResponse, bShowFailures);
        Request->ProcessRequest();
    }

    void OnConnectionProbeResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, bool bShowFailures)
    {
        bConnectionProbeInFlight = false;

        const bool bHttpOk = bSucceeded && Response.IsValid() && Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300;
        bool bBridgeOk = bHttpOk;
        bConnectionProbeHasResult = true;

        if (bHttpOk)
        {
            TSharedPtr<FJsonObject> Root;
            const FString ResponseText = Response->GetContentAsString();
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseText);
            if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
            {
                bool bSuccess = false;
                if (Root->TryGetBoolField(TEXT("success"), bSuccess))
                {
                    bBridgeOk = bSuccess;
                }
            }
        }

        State.bConnected = bBridgeOk;
        State.bListenerRunning = bBridgeOk;
        if (bBridgeOk)
        {
            State.LastPingAt = FDateTime::UtcNow().ToIso8601();
        }
        else if (bShowFailures)
        {
            SetMessage(FString::Printf(TEXT("Connection probe failed for %s"), *ListenerUrl()));
        }
    }

    void PostCommand(const FString& Command, const FString& ParamsJson = TEXT("{}"))
    {
        const FString Url = ListenerUrl();
        const FString Payload = FString::Printf(TEXT("{\"command\":\"%s\",\"params\":%s}"), *Command, *ParamsJson);

        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Url);
        Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        Request->SetContentAsString(Payload);
        Request->OnProcessRequestComplete().BindSP(this, &SMCPBridgePanel::OnCommandResponse, Command);
        Request->ProcessRequest();
        SetMessage(FString::Printf(TEXT("Sent command: %s"), *Command));
    }

    void OnCommandResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, FString Command)
    {
        if (!bSucceeded || !Response.IsValid())
        {
            SetMessage(FString::Printf(TEXT("%s failed: no HTTP response"), *Command));
            return;
        }

        FString ResultMessage = FString::Printf(TEXT("%s returned HTTP %d"), *Command, Response->GetResponseCode());
        TSharedPtr<FJsonObject> Root;
        const FString ResponseText = Response->GetContentAsString();
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseText);
        if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
        {
            bool bSuccess = false;
            Root->TryGetBoolField(TEXT("success"), bSuccess);
            const TSharedPtr<FJsonObject>* DataPtr = nullptr;
            if (Root->TryGetObjectField(TEXT("data"), DataPtr) && DataPtr && DataPtr->IsValid())
            {
                TSharedPtr<FJsonObject> Data = *DataPtr;
                FString LikelyBottleneck;
                FString ReportPath;
                double FindingCount = 0.0;
                const bool bHasBottleneck = Data->TryGetStringField(TEXT("likely_bottleneck"), LikelyBottleneck);
                const bool bHasReport = Data->TryGetStringField(TEXT("report_path"), ReportPath);
                const bool bHasFindingCount = Data->TryGetNumberField(TEXT("finding_count"), FindingCount);

                const TArray<TSharedPtr<FJsonValue>>* Findings = nullptr;
                int32 FindingsFromArray = 0;
                if (Data->TryGetArrayField(TEXT("findings"), Findings) && Findings)
                {
                    FindingsFromArray = Findings->Num();
                }

                TArray<FString> Parts;
                Parts.Add(bSuccess ? TEXT("success") : TEXT("failed"));
                if (bHasBottleneck)
                {
                    Parts.Add(FString::Printf(TEXT("bottleneck: %s"), *LikelyBottleneck));
                }
                if (bHasFindingCount)
                {
                    Parts.Add(FString::Printf(TEXT("findings: %d"), FMath::RoundToInt(FindingCount)));
                }
                else if (FindingsFromArray > 0)
                {
                    Parts.Add(FString::Printf(TEXT("findings: %d"), FindingsFromArray));
                }
                if (bHasReport)
                {
                    Parts.Add(FString::Printf(TEXT("report: %s"), *FPaths::GetCleanFilename(ReportPath)));
                }
                ResultMessage = FString::Printf(TEXT("%s: %s"), *Command, *FString::Join(Parts, TEXT(", ")));
            }
            else
            {
                ResultMessage = FString::Printf(TEXT("%s: %s"), *Command, bSuccess ? TEXT("success") : TEXT("failed"));
            }
        }
        Refresh();
        SetMessage(ResultMessage);
    }

    void CopyConnectionPrompt()
    {
        const FString Prompt = BuildConnectionPrompt();
        FPlatformApplicationMisc::ClipboardCopy(*Prompt);
        SetMessage(FString::Printf(TEXT("Copied chat prompt for %s"), *ListenerUrl()));
    }

    void CopyProjectInfo()
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *GetStatusPath()))
        {
            SetMessage(TEXT("Could not read bridge_status.json"));
            return;
        }

        FPlatformApplicationMisc::ClipboardCopy(*Text);
        SetMessage(TEXT("Copied project info to clipboard"));
    }

    void OpenOutputLog()
    {
        FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("OutputLog")));
        SetMessage(TEXT("Opened Output Log"));
    }
};

class FMCPBridgePanelModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            MCPBridgePanelTabName,
            FOnSpawnTab::CreateRaw(this, &FMCPBridgePanelModule::SpawnPanelTab)
        )
        .SetDisplayName(FText::FromString(TEXT("MCP Bridge")))
        .SetTooltipText(FText::FromString(TEXT("Open the MCP Bridge status and safety panel.")))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMCPBridgePanelModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
        if (UToolMenus::IsToolMenuUIEnabled())
        {
            UToolMenus::UnregisterOwner(this);
        }
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MCPBridgePanelTabName);
    }

private:
    TSharedRef<SDockTab> SpawnPanelTab(const FSpawnTabArgs& Args)
    {
        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [
                SNew(SMCPBridgePanel)
            ];
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
        FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
        Section.AddMenuEntry(
            "MCPBridgePanel_OpenTab",
            FText::FromString(TEXT("MCP Bridge")),
            FText::FromString(TEXT("Open the dockable MCP Bridge panel.")),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateRaw(this, &FMCPBridgePanelModule::OpenPanel))
        );
    }

    void OpenPanel()
    {
        FGlobalTabmanager::Get()->TryInvokeTab(MCPBridgePanelTabName);
    }
};

IMPLEMENT_MODULE(FMCPBridgePanelModule, MCPBridgePanel)
