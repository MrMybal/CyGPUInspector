// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
//
// The level editor gets a Capture button and a CyGPUInspector menu beside it in its toolbar, and
// the same actions in Tools. A capture ends with a notification, the way a RenderDoc capture does.

#include "CyGPUInspector.h"
#include "CyGPUInspectorSettings.h"

#include "Brushes/SlateImageBrush.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ISettingsModule.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CyGPUInspectorEditor"

class FCyGPUInspectorEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegisterStyle();
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FCyGPUInspectorEditorModule::RegisterMenus));
		if (FCyGPUInspectorModule* Runtime = FCyGPUInspectorModule::Get())
		{
			CaptureEventHandle = Runtime->OnCaptureEvent.AddRaw(this, &FCyGPUInspectorEditorModule::OnCaptureEvent);
			// A capture is about the 3D render of the main viewport, not the editor around it:
			// Play In Editor while it runs, else the level viewport last worked in.
			Runtime->SetMainViewportResolver([]() -> FViewport*
			{
				if (GEditor != nullptr && GEditor->PlayWorld != nullptr)
				{
					if (FViewport* PlayViewport = GEditor->GetPIEViewport())
					{
						return PlayViewport;
					}
				}
				if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
				{
					if (const TSharedPtr<SLevelViewport> Viewport = LevelEditor->GetFirstActiveLevelViewport())
					{
						return Viewport->GetActiveViewport();
					}
				}
				return nullptr;
			});
		}
	}

	virtual void ShutdownModule() override
	{
		if (FCyGPUInspectorModule* Runtime = FCyGPUInspectorModule::Get())
		{
			Runtime->OnCaptureEvent.Remove(CaptureEventHandle);
			Runtime->SetMainViewportResolver(nullptr);
		}
		if (UObjectInitialized())
		{
			UToolMenus::UnRegisterStartupCallback(this);
			UToolMenus::UnregisterOwner(this);
		}
		if (Style.IsValid())
		{
			FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
			Style.Reset();
		}
	}

private:
	static FName StyleName() { return TEXT("CyGPUInspectorStyle"); }

	void RegisterStyle()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CyGPUInspector"));
		if (!Plugin.IsValid())
		{
			return;
		}
		Style = MakeShared<FSlateStyleSet>(StyleName());
		Style->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
		const FString Icon = Style->RootToContentDir(TEXT("Icon128"), TEXT(".png"));
		Style->Set("CyGPUInspector.Capture", new FSlateImageBrush(Icon, FVector2D(20.0, 20.0)));
		Style->Set("CyGPUInspector.Capture.Small", new FSlateImageBrush(Icon, FVector2D(16.0, 16.0)));
		FSlateStyleRegistry::RegisterSlateStyle(*Style);
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped Owner(this);

		UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
		FToolMenuSection& Section = Toolbar->FindOrAddSection("CyGPUInspector");
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			"CyGPUInspectorCapture",
			FUIAction(FExecuteAction::CreateRaw(this, &FCyGPUInspectorEditorModule::CaptureWithSettings),
			          FCanExecuteAction::CreateRaw(this, &FCyGPUInspectorEditorModule::CanCapture)),
			LOCTEXT("CaptureLabel", "Capture"),
			TAttribute<FText>::CreateRaw(this, &FCyGPUInspectorEditorModule::CaptureTooltip),
			FSlateIcon(StyleName(), "CyGPUInspector.Capture")));
		Section.AddEntry(FToolMenuEntry::InitComboButton(
			"CyGPUInspectorMenu",
			FUIAction(),
			FOnGetContent::CreateRaw(this, &FCyGPUInspectorEditorModule::MakeMenu),
			LOCTEXT("MenuLabel", "CyGPUInspector"),
			LOCTEXT("MenuTooltip", "Capture options, the standalone and the settings of CyGPUInspector."),
			FSlateIcon(StyleName(), "CyGPUInspector.Capture"),
			true));

		UToolMenu* Tools = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		// The one-argument form: the one with a label only exists from 5.5 on.
		FToolMenuSection& ToolsSection = Tools->FindOrAddSection("CyGPUInspector");
		ToolsSection.Label = LOCTEXT("ToolsSection", "CyGPUInspector");
		ToolsSection.AddMenuEntry(
			"CyGPUInspectorCaptureMenu",
			LOCTEXT("CaptureMenuLabel", "Capture a Frame with CyGPUInspector"),
			TAttribute<FText>::CreateRaw(this, &FCyGPUInspectorEditorModule::CaptureTooltip),
			FSlateIcon(StyleName(), "CyGPUInspector.Capture.Small"),
			FUIAction(FExecuteAction::CreateRaw(this, &FCyGPUInspectorEditorModule::CaptureWithSettings),
			          FCanExecuteAction::CreateRaw(this, &FCyGPUInspectorEditorModule::CanCapture)));
		ToolsSection.AddMenuEntry(
			"CyGPUInspectorOpenStandalone",
			LOCTEXT("OpenLabel", "Open CyGPUInspector"),
			LOCTEXT("OpenTooltip", "Starts the CyGPUInspector standalone, connected to this editor."),
			FSlateIcon(StyleName(), "CyGPUInspector.Capture.Small"),
			FUIAction(FExecuteAction::CreateRaw(this, &FCyGPUInspectorEditorModule::OpenStandalone)));
	}

	TSharedRef<SWidget> MakeMenu()
	{
		FMenuBuilder Menu(true, nullptr);

		Menu.BeginSection("Capture", LOCTEXT("CaptureSection", "Capture"));
		for (const int32 Frames : { 1, 2, 4 })
		{
			Menu.AddMenuEntry(
				FText::Format(LOCTEXT("CaptureFrames", "Capture {0} {0}|plural(one=frame,other=frames)"), FText::AsNumber(Frames)),
				LOCTEXT("CaptureFramesTooltip", "Deep capture of the next frames, with the options below."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, Frames]() { Capture(Frames); }),
				          FCanExecuteAction::CreateRaw(this, &FCyGPUInspectorEditorModule::CanCapture)));
		}
		Menu.EndSection();

		Menu.BeginSection("Options", LOCTEXT("OptionsSection", "Record"));
		AddToggle(Menu, LOCTEXT("SaveBuffers", "Save the buffers"),
			LOCTEXT("SaveBuffersTooltip", "Every texture the last captured frame wrote to, saved as PNG and DDS by the standalone."),
			&UCyGPUInspectorSettings::bSaveBuffers);
		AddToggle(Menu, LOCTEXT("Descriptors", "Descriptors"),
			LOCTEXT("DescriptorsTooltip", "Every descriptor bound to every command."),
			&UCyGPUInspectorSettings::bRecordDescriptors);
		AddToggle(Menu, LOCTEXT("Barriers", "Barriers"),
			LOCTEXT("BarriersTooltip", "Resource transitions. Saving the buffers records them anyway: on Direct3D 12 they say which state a texture is in."),
			&UCyGPUInspectorSettings::bRecordBarriers);
		AddToggle(Menu, LOCTEXT("Timestamps", "Timestamp per command"),
			LOCTEXT("TimestampsTooltip", "A GPU time for each command rather than each pass."),
			&UCyGPUInspectorSettings::bTimestampPerCommand);
		Menu.EndSection();

		Menu.BeginSection("Standalone", LOCTEXT("StandaloneSection", "CyGPUInspector"));
		Menu.AddMenuEntry(
			LOCTEXT("OpenLabel", "Open CyGPUInspector"),
			LOCTEXT("OpenTooltip", "Starts the CyGPUInspector standalone, connected to this editor."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FCyGPUInspectorEditorModule::OpenStandalone)));
		Menu.AddMenuEntry(
			LOCTEXT("SettingsLabel", "Settings..."),
			LOCTEXT("SettingsTooltip", "Project Settings > Plugins > CyGPUInspector."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "CyGPUInspectorSettings");
			})));
		Menu.EndSection();

		Menu.BeginSection("Status", LOCTEXT("StatusSection", "Status"));
		Menu.AddWidget(
			SNew(STextBlock)
				.Text_Raw(this, &FCyGPUInspectorEditorModule::CaptureTooltip)
				.WrapTextAt(380.0f),
			FText::GetEmpty(), true);
		Menu.EndSection();

		return Menu.MakeWidget();
	}

	void AddToggle(FMenuBuilder& Menu, const FText& Label, const FText& Tooltip, bool UCyGPUInspectorSettings::* Member)
	{
		Menu.AddMenuEntry(
			Label, Tooltip, FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Member]()
				{
					UCyGPUInspectorSettings* Settings = GetMutableDefault<UCyGPUInspectorSettings>();
					Settings->*Member = !(Settings->*Member);
					Settings->SaveConfig();
				}),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([Member]()
				{
					return GetDefault<UCyGPUInspectorSettings>()->*Member ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})),
			NAME_None, EUserInterfaceActionType::ToggleButton);
	}

	bool CanCapture() const
	{
		const FCyGPUInspectorModule* Runtime = FCyGPUInspectorModule::Get();
		return Runtime != nullptr && Runtime->IsAvailable() && !Runtime->IsBusy();
	}

	FText CaptureTooltip() const
	{
		const FCyGPUInspectorModule* Runtime = FCyGPUInspectorModule::Get();
		return Runtime != nullptr ? Runtime->GetStatusText() : LOCTEXT("NoRuntime", "The CyGPUInspector module is not loaded.");
	}

	void CaptureWithSettings()
	{
		Capture(FCyGPUInspectorCaptureOptions::FromSettings().FrameCount);
	}

	void Capture(int32 Frames)
	{
		FCyGPUInspectorModule* Runtime = FCyGPUInspectorModule::Get();
		if (Runtime == nullptr)
		{
			return;
		}
		FCyGPUInspectorCaptureOptions Options = FCyGPUInspectorCaptureOptions::FromSettings();
		Options.FrameCount = Frames;
		FText Message;
		if (Runtime->RequestCapture(Options, Message) && !Runtime->IsStandaloneConnected())
		{
			// Waiting for the standalone: say so now, the rest comes through OnCaptureEvent.
			Notify(true, Message, 4.0f);
		}
	}

	void OpenStandalone()
	{
		if (FCyGPUInspectorModule* Runtime = FCyGPUInspectorModule::Get())
		{
			FText Message;
			const bool bLaunched = Runtime->LaunchStandalone(Message);
			Notify(bLaunched, Message, bLaunched ? 4.0f : 8.0f);
		}
	}

	void OnCaptureEvent(bool bSucceeded, const FText& Message)
	{
		Notify(bSucceeded, Message, bSucceeded ? 5.0f : 8.0f);
	}

	static void Notify(bool bSucceeded, const FText& Message, float Seconds)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = Seconds;
		Info.bUseSuccessFailIcons = true;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSucceeded ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	TSharedPtr<FSlateStyleSet> Style;
	FDelegateHandle CaptureEventHandle;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCyGPUInspectorEditorModule, CyGPUInspectorEditor)
