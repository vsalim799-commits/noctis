// NoctisEditor module: Message Log listing "Noctis", Tools menu section "Noctis" and console commands.
//   Tools > Noctis > Valider les données scientifiques   (noctis.Editor.ValidateData)
//   Tools > Noctis > Recharger les données (sessions PIE)   (noctis.Editor.ReloadData)
// The same validation runs headless with the commandlet: UnrealEditor-Cmd Noctis.uproject -run=NoctisValidateData
#include "NoctisDataCheck.h"
#include "NoctisEditorLog.h"

#include "Framework/Notifications/NotificationManager.h"
#include "HAL/IConsoleManager.h"
#include "MessageLogInitializationOptions.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY(LogNoctisEditor);

#define LOCTEXT_NAMESPACE "NoctisEditor"

namespace NoctisEditorCommands
{
static void Notify(const FText& Text, bool bSuccess)
{
	FNotificationInfo Info(Text);
	Info.ExpireDuration = 6.0f;
	Info.bUseLargeFont = false;
	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

static void ValidateData()
{
	const FNoctisDataReport Report = NoctisEditorData::RunFullCheck(/*bOpenMessageLog*/ true);
	Notify(FText::Format(LOCTEXT("ValidateDone", "Données Noctis : {0} erreur(s), {1} avertissement(s) — détails dans Message Log > Noctis."), Report.Errors,
						 Report.Warnings),
		   Report.bLoaded && Report.Errors == 0);
}

static void ReloadData()
{
	// Validate first: a broken file would make the rebuilt valley fail to boot.
	const FNoctisDataReport Report = NoctisEditorData::RunFullCheck(/*bOpenMessageLog*/ false);
	if (!Report.bLoaded)
	{
		Notify(LOCTEXT("ReloadRefused", "Rechargement annulé : Data/ ne se charge pas (voir Message Log > Noctis)."), false);
		return;
	}
	const int32 Worlds = NoctisEditorData::ReloadRunningSimulations();
	Notify(Worlds > 0 ? FText::Format(LOCTEXT("Reloaded", "Vallée reconstruite depuis Data/ dans {0} session(s) en cours."), Worlds)
					  : LOCTEXT("ReloadNoPie", "Aucune session PIE en cours : la prochaine lira Data/ à son démarrage."),
		   true);
}
} // namespace NoctisEditorCommands

static FAutoConsoleCommand GNoctisEditorValidateCommand(TEXT("noctis.Editor.ValidateData"),
														TEXT("Valide Data/ (chargement par le coeur, cohérence scientifique) et les fiches visuelles d'espèces."),
														FConsoleCommandDelegate::CreateStatic(&NoctisEditorCommands::ValidateData));

static FAutoConsoleCommand GNoctisEditorReloadCommand(TEXT("noctis.Editor.ReloadData"),
													  TEXT("Relit Data/ et reconstruit la vallée dans les sessions PIE en cours."),
													  FConsoleCommandDelegate::CreateStatic(&NoctisEditorCommands::ReloadData));

class FNoctisEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FMessageLogModule& MessageLog = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
		FMessageLogInitializationOptions Options;
		Options.bShowPages = true;
		Options.bAllowClear = true;
		Options.bShowFilters = true;
		MessageLog.RegisterLogListing(NoctisEditorData::MessageLogName, LOCTEXT("MessageLogLabel", "Noctis"), Options);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FNoctisEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		if (FModuleManager::Get().IsModuleLoaded(TEXT("MessageLog")))
		{
			FMessageLogModule& MessageLog = FModuleManager::GetModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
			MessageLog.UnregisterLogListing(NoctisEditorData::MessageLogName);
		}
		NoctisEditorData::Invalidate();
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		// VERIFY: "LevelEditor.MainMenu.Tools" is the Tools menu name in 5.4/5.5 (Window/Tools menus were reorganised in 5.1).
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("Noctis"), LOCTEXT("MenuSection", "Noctis"));
		Section.AddMenuEntry(TEXT("NoctisValidateData"), LOCTEXT("ValidateLabel", "Valider les données scientifiques"),
							 LOCTEXT("ValidateTooltip", "Charge Data/ comme le jeu (coeur NoctisCore), lance les contrôles de cohérence scientifique "
														"et vérifie les fiches visuelles d'espèces. Résultats : Message Log > Noctis."),
							 FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Check")),
							 FUIAction(FExecuteAction::CreateStatic(&NoctisEditorCommands::ValidateData)));
		Section.AddMenuEntry(TEXT("NoctisReloadData"), LOCTEXT("ReloadLabel", "Recharger les données (sessions PIE)"),
							 LOCTEXT("ReloadTooltip", "Relit Data/ depuis le disque et reconstruit la vallée dans les sessions PIE en cours "
													  "(même graine, mêmes réglages). Les fiches visuelles déjà chargées sont conservées."),
							 FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")),
							 FUIAction(FExecuteAction::CreateStatic(&NoctisEditorCommands::ReloadData)));
	}
};

IMPLEMENT_MODULE(FNoctisEditorModule, NoctisEditor);

#undef LOCTEXT_NAMESPACE
