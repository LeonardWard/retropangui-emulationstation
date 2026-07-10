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
	void openAdvancedSettings();
	void openCollectionSystemSettings();
	void openConfigInput();
	void openControllerSettings();
	void openEmulatorSettings();
	void openGameSettings();
	void openKodiMediaCenter();
	void openNetworkSettings();
	void openQuitMenu();
	void openRetroAchievements();
	void openScraperSettings();
	void openScreensaverOptions();
	void openSoundSettings();
	void openStorageSettings();
	void openSystemSettings();
	void openUISettings();
	void openUpdatesAndDownloads();

	void openFeatureMenu(const std::string& menuId);

	// restart 체크용 타입: {값이_변경됐는지_반환하는_람다, restart_레벨}
	using RestartCheck = std::pair<std::function<bool()>, std::string>;
	void addFeatureItem(GuiSettings* s, const FeatureItem& item,
	                    std::vector<RestartCheck>& checks);
	// parent가 일치하는 YAML 메뉴들의 항목을 s에 직접 삽입
	void addFeatureItemsTo(GuiSettings* s, const std::string& parent,
	                       std::vector<RestartCheck>& checks);
	// 서브메뉴로 들어가는 행 (라벨 + 화살표)
	void addSubmenuEntry(GuiSettings* s, const std::string& label,
	                     const std::function<void()>& openFunc);
	// 닫을 때 저장 + 변경 항목의 restart 레벨에 따라 재시작/재부팅 질문
	void setSaveWithRestartChecks(GuiSettings* s,
	                              std::shared_ptr<std::vector<RestartCheck>> checks);

	MenuComponent mMenu;
	TextComponent mVersion;
	std::vector<FeatureMenu> mFeatureMenus;

	typedef OptionListComponent<const FileData::SortType*> SortList;
	std::shared_ptr<SortList> mListSort;
};

#endif // ES_APP_GUIS_GUI_MENU_H
