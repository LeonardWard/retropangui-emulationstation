#pragma once
#ifndef ES_APP_GUIS_GUI_MENU_H
#define ES_APP_GUIS_GUI_MENU_H

#include "components/MenuComponent.h"
#include "GuiComponent.h"
#include "components/OptionListComponent.h"
#include "FileData.h"
#include "RetropanguiFeatures.h"

class GuiSettings;

class GuiMenu : public GuiComponent
{
public:
	GuiMenu(Window* window);

	bool input(InputConfig* config, Input input) override;
	void onSizeChanged() override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	HelpStyle getHelpStyle() override;

private:
	void addEntry(const char* name, unsigned int color, bool add_arrow, const std::function<void()>& func);
	void addVersionInfo();
	void openCollectionSystemSettings();
	void openConfigInput();
	void openEmulatorSettings();
	void openKodiMediaCenter();
	void openNetworkSettings();
	void openOtherSettings();
	void openQuitMenu();
	void openRetroAchievements();
	void openScraperSettings();
	void openScreensaverOptions();
	void openSoundSettings();
	void openUISettings();
	void openUpdatesAndDownloads();

	void openFeatureMenu(const std::string& menuId);

	// restart 체크용 타입: {값이_변경됐는지_반환하는_람다, restart_레벨}
	using RestartCheck = std::pair<std::function<bool()>, std::string>;
	void addFeatureItem(GuiSettings* s, const FeatureItem& item,
	                    std::vector<RestartCheck>& checks);

	MenuComponent mMenu;
	TextComponent mVersion;
	std::vector<FeatureMenu> mFeatureMenus;

	typedef OptionListComponent<const FileData::SortType*> SortList;
	std::shared_ptr<SortList> mListSort;
};

#endif // ES_APP_GUIS_GUI_MENU_H
