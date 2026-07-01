#include "guis/GuiMenu.h"
#include <unordered_map>

#include "components/OptionListComponent.h"
#include "components/SliderComponent.h"
#include "components/SwitchComponent.h"
#include "guis/GuiCollectionSystemsOptions.h"
#include "guis/GuiStorageSelect.h"
#include "guis/GuiDetectDevice.h"
#include "guis/GuiGeneralScreensaverOptions.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiScraperStart.h"
#include "guis/GuiSettings.h"
#include "views/UIModeController.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "EmulationStation.h"
#include "InputConfig.h"
#include "LocaleES.h"
#include "MusicManager.h"
#include "Scripting.h"
#include "SystemData.h"
#include "VolumeControl.h"
#include <SDL_events.h>
#include <algorithm>
#include "platform.h"
#include "FileSorts.h"
#include "views/gamelist/IGameListView.h"
#include "guis/GuiInfoPopup.h"
#include "guis/GuiArcadeVirtualKeyboard.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include "utils/FileSystemUtil.h"
#include "guis/GuiOtaUpdate.h"
#include "HttpReq.h"
#include <sys/wait.h>
#include <SDL_timer.h>

// forward declarations — 정의는 YAML 엔진 블록에 있음
static std::string rpConfPath();
static std::string cfgReadKey(const std::string& filePath, const std::string& fullKey,
                              const std::string& def);
static void cfgWriteKey(const std::string& filePath, const std::string& fullKey,
                        const std::string& value, bool quote);

GuiMenu::GuiMenu(Window* window) : GuiComponent(window), mMenu(window, _("MAIN MENU")), mVersion(window)
{
	bool isFullUI = UIModeController::getInstance()->isUIModeFull();

	// YAML 메뉴 로드
	mFeatureMenus = RetropanguiFeatures::load();

	if (isFullUI) {
		// RetroPangui: 메인 8개 골격 — 세부 항목은 각 카테고리 서브메뉴로 통합
		// (RETROACHIEVEMENTS/EMULATOR→GAME, CONFIGURE INPUT→CONTROLLER,
		//  COLLECTION→UI, UPDATES/OTHER→SYSTEM. YAML 메뉴는 parent로 흡수)
		addEntry(_("KODI MEDIA CENTER"),    0x777777FF, true, [this] { openKodiMediaCenter(); });
		addEntry(_("GAME SETTINGS"),        0x777777FF, true, [this] { openGameSettings(); });
		addEntry(_("CONTROLLER SETTINGS"),  0x777777FF, true, [this] { openControllerSettings(); });
		addEntry(_("UI SETTINGS"),          0x777777FF, true, [this] { openUISettings(); });
		addEntry(_("SOUND SETTINGS"),       0x777777FF, true, [this] { openSoundSettings(); });
		addEntry(_("SYSTEM SETTINGS"),      0x777777FF, true, [this] { openSystemSettings(); });
		// YAML parent=main 메뉴 삽입 (현재 기본 yml에는 없음 — 확장용)
		for (auto& fm : mFeatureMenus) {
			if (fm.parent == "main") {
				std::string id = fm.id;
				addEntry(_(fm.label.c_str()), 0x777777FF, true, [this, id] { openFeatureMenu(id); });
			}
		}
		addEntry(_("SCRAPER"),              0x777777FF, true, [this] { openScraperSettings(); });
	} else {
		addEntry(_("SOUND SETTINGS"), 0x777777FF, true, [this] { openSoundSettings(); });
	}

	addEntry(_("QUIT"), 0x777777FF, true, [this] {openQuitMenu(); });

	addChild(&mMenu);
	addVersionInfo();
	setSize(mMenu.getSize());
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2, Renderer::getScreenHeight() * 0.15f);
}

void GuiMenu::openScraperSettings()
{
	auto s = new GuiSettings(mWindow, _("SCRAPER"));

	// scrape from
	auto scraper_list = std::make_shared< OptionListComponent< std::string > >(mWindow, _("SCRAPE FROM"), false);
	std::vector<std::string> scrapers = getScraperList();

	// Select either the first entry of the one read from the settings, just in case the scraper from settings has vanished.
	for(auto it = scrapers.cbegin(); it != scrapers.cend(); it++)
		scraper_list->add(*it, *it, *it == Settings::getInstance()->getString("Scraper"));

	s->addWithLabel(_("SCRAPE FROM"), scraper_list);
	s->addSaveFunc([scraper_list] { Settings::getInstance()->setString("Scraper", scraper_list->getSelected()); });

	// scrape ratings
	auto scrape_ratings = std::make_shared<SwitchComponent>(mWindow);
	scrape_ratings->setState(Settings::getInstance()->getBool("ScrapeRatings"));
	s->addWithLabel(_("SCRAPE RATINGS"), scrape_ratings);
	s->addSaveFunc([scrape_ratings] { Settings::getInstance()->setBool("ScrapeRatings", scrape_ratings->getState()); });

	// scrape now
	ComponentListRow row;
	auto openScrapeNow = [this] { mWindow->pushGui(new GuiScraperStart(mWindow)); };
	std::function<void()> openAndSave = openScrapeNow;
	openAndSave = [s, openAndSave] { s->save(); openAndSave(); };
	row.makeAcceptInputHandler(openAndSave);

	auto scrape_now = std::make_shared<TextComponent>(mWindow, _("SCRAPE NOW"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF);
	auto bracket = makeArrow(mWindow);
	row.addElement(scrape_now, true);
	row.addElement(bracket, false);
	s->addRow(row);

	mWindow->pushGui(s);
}

void GuiMenu::openSoundSettings()
{
	auto s = new GuiSettings(mWindow, _("SOUND SETTINGS"));
	auto checks = std::make_shared<std::vector<RestartCheck>>();
	addFeatureItemsTo(s, "sound", *checks);

#ifdef _OMX_
	if (UIModeController::getInstance()->isUIModeFull())
	{
		// OMX player Audio Device
		auto omx_audio_dev = std::make_shared< OptionListComponent<std::string> >(mWindow, _("OMX PLAYER AUDIO DEVICE"), false);
		std::vector<std::string> omx_cards;
		// RPi Specific  Audio Cards
		omx_cards.push_back("local");
		omx_cards.push_back("hdmi");
		omx_cards.push_back("both");
		omx_cards.push_back("alsa");
		omx_cards.push_back("alsa:hw:0,0");
		omx_cards.push_back("alsa:hw:1,0");
		if (Settings::getInstance()->getString("OMXAudioDev") != "") {
			if (std::find(omx_cards.begin(), omx_cards.end(), Settings::getInstance()->getString("OMXAudioDev")) == omx_cards.end()) {
				omx_cards.push_back(Settings::getInstance()->getString("OMXAudioDev"));
			}
		}
		for (auto it = omx_cards.cbegin(); it != omx_cards.cend(); it++)
			omx_audio_dev->add(*it, *it, Settings::getInstance()->getString("OMXAudioDev") == *it);
		s->addWithLabel(_("OMX PLAYER AUDIO DEVICE"), omx_audio_dev);
		s->addSaveFunc([omx_audio_dev] {
			if (Settings::getInstance()->getString("OMXAudioDev") != omx_audio_dev->getSelected())
				Settings::getInstance()->setString("OMXAudioDev", omx_audio_dev->getSelected());
		});
	}
#endif

	setSaveWithRestartChecks(s, checks);
	mWindow->pushGui(s);

}

void GuiMenu::openUISettings()
{
	auto s = new GuiSettings(mWindow, _("UI SETTINGS"));

	//UI mode
	auto UImodeSelection = std::make_shared< OptionListComponent<std::string> >(mWindow, _("UI MODE"), false);
	std::vector<std::string> UImodes = UIModeController::getInstance()->getUIModes();
	for (auto it = UImodes.cbegin(); it != UImodes.cend(); it++)
		UImodeSelection->add(*it, *it, Settings::getInstance()->getString("UIMode") == *it);
	s->addWithLabel(_("UI MODE"), UImodeSelection);
	Window* window = mWindow;
	s->addSaveFunc([ UImodeSelection, window]
	{
		std::string selectedMode = UImodeSelection->getSelected();
		if (selectedMode != "Full")
		{
			std::string msg = _("You are changing the UI to a restricted mode:\n") + selectedMode + "\n";
			msg += _("This will hide most menu-options to prevent changes to the system.\n");
			msg += _("To unlock and return to the full UI, enter this code: \n");
			msg += "\"" + UIModeController::getInstance()->getFormattedPassKeyStr() + "\"\n\n";
			msg += _("Do you want to proceed?");
			window->pushGui(new GuiMsgBox(window, msg,
				_("YES"), [selectedMode] {
					LOG(LogDebug) << "Setting UI mode to " << selectedMode;
					Settings::getInstance()->setString("UIMode", selectedMode);
					Settings::getInstance()->saveFile();
			}, _("NO"),nullptr));
		}
	});

	// screensaver
	ComponentListRow screensaver_row;
	screensaver_row.elements.clear();
	screensaver_row.addElement(std::make_shared<TextComponent>(mWindow, _("SCREENSAVER SETTINGS"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
	screensaver_row.addElement(makeArrow(mWindow), false);
	screensaver_row.makeAcceptInputHandler(std::bind(&GuiMenu::openScreensaverOptions, this));
	s->addRow(screensaver_row);

	// 컬렉션 설정 (메인에서 이동)
	addSubmenuEntry(s, _("GAME COLLECTION SETTINGS"), [this] { openCollectionSystemSettings(); });

	// quick system select (left/right in game list view)
	auto quick_sys_select = std::make_shared<SwitchComponent>(mWindow);
	quick_sys_select->setState(Settings::getInstance()->getBool("QuickSystemSelect"));
	s->addWithLabel(_("QUICK SYSTEM SELECT"), quick_sys_select);
	s->addSaveFunc([quick_sys_select] { Settings::getInstance()->setBool("QuickSystemSelect", quick_sys_select->getState()); });

	// carousel transition option
	auto move_carousel = std::make_shared<SwitchComponent>(mWindow);
	move_carousel->setState(Settings::getInstance()->getBool("MoveCarousel"));
	s->addWithLabel(_("CAROUSEL TRANSITIONS"), move_carousel);
	s->addSaveFunc([move_carousel] {
		if (move_carousel->getState()
			&& !Settings::getInstance()->getBool("MoveCarousel")
			&& PowerSaver::getMode() == PowerSaver::INSTANT)
		{
			Settings::getInstance()->setString("PowerSaverMode", "default");
			PowerSaver::init();
		}
		Settings::getInstance()->setBool("MoveCarousel", move_carousel->getState());
	});

	// transition style
	auto transition_style = std::make_shared< OptionListComponent<std::string> >(mWindow, _("TRANSITION STYLE"), false);
	std::vector<std::string> transitions;
	transitions.push_back("fade");
	transitions.push_back("slide");
	transitions.push_back("instant");
	for(auto it = transitions.cbegin(); it != transitions.cend(); it++)
		transition_style->add(*it, *it, Settings::getInstance()->getString("TransitionStyle") == *it);
	s->addWithLabel(_("TRANSITION STYLE"), transition_style);
	s->addSaveFunc([transition_style] {
		if (Settings::getInstance()->getString("TransitionStyle") == "instant"
			&& transition_style->getSelected() != "instant"
			&& PowerSaver::getMode() == PowerSaver::INSTANT)
		{
			Settings::getInstance()->setString("PowerSaverMode", "default");
			PowerSaver::init();
		}
		Settings::getInstance()->setString("TransitionStyle", transition_style->getSelected());
	});

	// RetroPangui: Fallback font selection for CJK characters
	auto fallback_font = std::make_shared< OptionListComponent<std::string> >(mWindow, _("FALLBACK FONT"), false);
	std::vector<std::pair<std::string, std::string>> fontOptions;
	fontOptions.push_back(std::make_pair("Auto (System Default)", ""));
	fontOptions.push_back(std::make_pair("Nanum Gothic", "/usr/share/fonts/truetype/nanum/NanumGothic.ttf"));
	fontOptions.push_back(std::make_pair("Nanum Barun Gothic", "/usr/share/fonts/truetype/nanum/NanumBarunGothic.ttf"));
	fontOptions.push_back(std::make_pair("Droid Sans Fallback", "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"));

	std::string currentFont = Settings::getInstance()->getString("FallbackFont");
	for(auto it = fontOptions.cbegin(); it != fontOptions.cend(); it++)
	{
		fallback_font->add(it->first, it->second, it->second == currentFont);
	}
	s->addWithLabel(_("FALLBACK FONT"), fallback_font);
	s->addSaveFunc([fallback_font] {
		Settings::getInstance()->setString("FallbackFont", fallback_font->getSelected());
	});

	// theme set
	auto themeSets = ThemeData::getThemeSets();

	if(!themeSets.empty())
	{
		std::map<std::string, ThemeSet>::const_iterator selectedSet = themeSets.find(Settings::getInstance()->getString("ThemeSet"));
		if(selectedSet == themeSets.cend())
			selectedSet = themeSets.cbegin();

		auto theme_set = std::make_shared< OptionListComponent<std::string> >(mWindow, _("THEME SET"), false);
		for(auto it = themeSets.cbegin(); it != themeSets.cend(); it++)
			theme_set->add(it->first, it->first, it == selectedSet);
		s->addWithLabel(_("THEME SET"), theme_set);

		Window* window = mWindow;
		s->addSaveFunc([window, theme_set]
		{
			bool needReload = false;
			std::string oldTheme = Settings::getInstance()->getString("ThemeSet");
			if(oldTheme != theme_set->getSelected())
				needReload = true;

			Settings::getInstance()->setString("ThemeSet", theme_set->getSelected());

			if(needReload)
			{
				Scripting::fireEvent("theme-changed", theme_set->getSelected(), oldTheme);
				CollectionSystemManager::get()->updateSystemsList();
				ViewController::get()->reloadAll(true); // TODO - replace this with some sort of signal-based implementation
			}
		});
	}

	// GameList view style
	auto gamelist_style = std::make_shared< OptionListComponent<std::string> >(mWindow, _("GAMELIST VIEW STYLE"), false);
	std::vector<std::string> styles;
	styles.push_back("automatic");
	styles.push_back("basic");
	styles.push_back("detailed");
	styles.push_back("video");
	styles.push_back("grid");

	for (auto it = styles.cbegin(); it != styles.cend(); it++)
		gamelist_style->add(*it, *it, Settings::getInstance()->getString("GamelistViewStyle") == *it);
	s->addWithLabel(_("GAMELIST VIEW STYLE"), gamelist_style);
	s->addSaveFunc([gamelist_style] {
		bool needReload = false;
		if (Settings::getInstance()->getString("GamelistViewStyle") != gamelist_style->getSelected())
			needReload = true;
		Settings::getInstance()->setString("GamelistViewStyle", gamelist_style->getSelected());
		if (needReload)
			ViewController::get()->reloadAll();
	});

	// RetroPangui: Show folders (gamelist.xml-based)
	auto show_folders = std::make_shared< OptionListComponent<std::string> >(mWindow, _("GAMELIST MODE"), false);
	show_folders->add(_("ALL FILES"), "ALL", Settings::getInstance()->getString("ShowFolders") == "ALL");
	show_folders->add(_("SCRAPED ONLY"), "SCRAPED", Settings::getInstance()->getString("ShowFolders") == "SCRAPED");
	show_folders->add(_("SCRAPED FIRST"), "AUTO", Settings::getInstance()->getString("ShowFolders") == "AUTO");
	s->addWithLabel(_("GAMELIST MODE"), show_folders);
	s->addSaveFunc([show_folders] {
		bool needReload = false;
		if (Settings::getInstance()->getString("ShowFolders") != show_folders->getSelected())
			needReload = true;
		Settings::getInstance()->setString("ShowFolders", show_folders->getSelected());
		if (needReload)
			ViewController::get()->reloadAll();
	});

	// 게임목록 관련 항목 (OTHER SETTINGS에서 이동)
	auto gamelistsSaveMode = std::make_shared< OptionListComponent<std::string> >(mWindow, _("SAVE METADATA"), false);
	std::vector<std::string> saveModes;
	saveModes.push_back("on exit");
	saveModes.push_back("always");
	saveModes.push_back("never");
	for(auto it = saveModes.cbegin(); it != saveModes.cend(); it++)
		gamelistsSaveMode->add(*it, *it, Settings::getInstance()->getString("SaveGamelistsMode") == *it);
	s->addWithLabel(_("SAVE METADATA"), gamelistsSaveMode);
	s->addSaveFunc([gamelistsSaveMode] {
		Settings::getInstance()->setString("SaveGamelistsMode", gamelistsSaveMode->getSelected());
	});

	auto parse_gamelists = std::make_shared<SwitchComponent>(mWindow);
	parse_gamelists->setState(Settings::getInstance()->getBool("ParseGamelistOnly"));
	s->addWithLabel(_("PARSE GAMESLISTS ONLY"), parse_gamelists);
	s->addSaveFunc([parse_gamelists] { Settings::getInstance()->setBool("ParseGamelistOnly", parse_gamelists->getState()); });

	auto local_art = std::make_shared<SwitchComponent>(mWindow);
	local_art->setState(Settings::getInstance()->getBool("LocalArt"));
	s->addWithLabel(_("SEARCH FOR LOCAL ART"), local_art);
	s->addSaveFunc([local_art] { Settings::getInstance()->setBool("LocalArt", local_art->getState()); });

	auto hidden_files = std::make_shared<SwitchComponent>(mWindow);
	hidden_files->setState(Settings::getInstance()->getBool("ShowHiddenFiles"));
	s->addWithLabel(_("SHOW HIDDEN FILES"), hidden_files);
	s->addSaveFunc([hidden_files] { Settings::getInstance()->setBool("ShowHiddenFiles", hidden_files->getState()); });

	// Optionally ignore leading articles when sorting game titles
	auto ignore_articles = std::make_shared<SwitchComponent>(mWindow);
	ignore_articles->setState(Settings::getInstance()->getBool("IgnoreLeadingArticles"));
	s->addWithLabel(_("IGNORE ARTICLES (NAME SORT ONLY)"), ignore_articles);
	s->addSaveFunc([ignore_articles, window] {
		bool articles_are_ignored = Settings::getInstance()->getBool("IgnoreLeadingArticles");
		Settings::getInstance()->setBool("IgnoreLeadingArticles", ignore_articles->getState());
		if (ignore_articles->getState() != articles_are_ignored)
		{
			//For each system...
			for (auto it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); it++)
			{
				//Apply sort recursively
				FileData* root = (*it)->getRootFolder();
				root->sort(getSortTypeFromString(root->getSortName()));

				//Notify that the root folder was sorted
				ViewController::get()->getGameListView((*it))->onFileChanged(root, FILE_SORTED);
			}

			//Display popup to inform user
			GuiInfoPopup* popup = new GuiInfoPopup(window, _("Files sorted"), 4000);
			window->setInfoPopup(popup);
		}
	});

	// lb/rb uses full screen size paging instead of -10/+10 steps
	auto use_fullscreen_paging = std::make_shared<SwitchComponent>(mWindow);
	use_fullscreen_paging->setState(Settings::getInstance()->getBool("UseFullscreenPaging"));
	s->addWithLabel(_("USE FULL SCREEN PAGING FOR LB/RB"), use_fullscreen_paging);
	s->addSaveFunc([use_fullscreen_paging] {
		Settings::getInstance()->setBool("UseFullscreenPaging", use_fullscreen_paging->getState());
	});

	// Optionally start in selected system
	auto systemfocus_list = std::make_shared< OptionListComponent<std::string> >(mWindow, _("START ON SYSTEM"), false);
	systemfocus_list->add(_("NONE"), "", Settings::getInstance()->getString("StartupSystem") == "");
	for (auto it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); it++)
	{
		if ("retropie" != (*it)->getName())
		{
			systemfocus_list->add((*it)->getName(), (*it)->getName(), Settings::getInstance()->getString("StartupSystem") == (*it)->getName());
		}
	}
	s->addWithLabel(_("START ON SYSTEM"), systemfocus_list);
	s->addSaveFunc([systemfocus_list] {
		Settings::getInstance()->setString("StartupSystem", systemfocus_list->getSelected());
	});

	// show help
	auto show_help = std::make_shared<SwitchComponent>(mWindow);
	show_help->setState(Settings::getInstance()->getBool("ShowHelpPrompts"));
	s->addWithLabel(_("ON-SCREEN HELP"), show_help);
	s->addSaveFunc([show_help] { Settings::getInstance()->setBool("ShowHelpPrompts", show_help->getState()); });

	// enable filters (ForceDisableFilters)
	auto enable_filter = std::make_shared<SwitchComponent>(mWindow);
	enable_filter->setState(!Settings::getInstance()->getBool("ForceDisableFilters"));
	s->addWithLabel(_("ENABLE FILTERS"), enable_filter);
	s->addSaveFunc([enable_filter] {
		bool filter_is_enabled = !Settings::getInstance()->getBool("ForceDisableFilters");
		Settings::getInstance()->setBool("ForceDisableFilters", !enable_filter->getState());
		if (enable_filter->getState() != filter_is_enabled) ViewController::get()->ReloadAndGoToStart();
	});

	// hide start menu in Kid Mode
	auto disable_start = std::make_shared<SwitchComponent>(mWindow);
	disable_start->setState(Settings::getInstance()->getBool("DisableKidStartMenu"));
	s->addWithLabel(_("DISABLE START MENU IN KID MODE"), disable_start);
	s->addSaveFunc([disable_start] { Settings::getInstance()->setBool("DisableKidStartMenu", disable_start->getState()); });

	// maximum vram (ES UI 텍스처 한도)
	auto max_vram = std::make_shared<SliderComponent>(mWindow, 0.f, 1000.f, 10.f, "Mb");
	max_vram->setValue((float)(Settings::getInstance()->getInt("MaxVRAM")));
	s->addWithLabel(_("ES VRAM 제한"), max_vram);
	s->addSaveFunc([max_vram] { Settings::getInstance()->setInt("MaxVRAM", (int)Math::round(max_vram->getValue())); });

	// framerate
	auto framerate = std::make_shared<SwitchComponent>(mWindow);
	framerate->setState(Settings::getInstance()->getBool("DrawFramerate"));
	s->addWithLabel(_("ES 프레임 표시"), framerate);
	s->addSaveFunc([framerate] { Settings::getInstance()->setBool("DrawFramerate", framerate->getState()); });

	// YAML: UI 관련 항목 (LANGUAGE 등)
	auto checks = std::make_shared<std::vector<RestartCheck>>();
	addFeatureItemsTo(s, "ui", *checks);
	setSaveWithRestartChecks(s, checks);

	mWindow->pushGui(s);

}

// SYSTEM SETTINGS > ADVANCED — 공장 초기화
void GuiMenu::openAdvancedSettings()
{
	auto s = new GuiSettings(mWindow, _("ADVANCED SETTINGS"));

#ifdef _OMX_
	// Video Player - VideoOmxPlayer
	auto omx_player = std::make_shared<SwitchComponent>(mWindow);
	omx_player->setState(Settings::getInstance()->getBool("VideoOmxPlayer"));
	s->addWithLabel(_("USE OMX PLAYER (HW ACCELERATED)"), omx_player);
	s->addSaveFunc([omx_player]
	{
		bool needReload = false;
		if(Settings::getInstance()->getBool("VideoOmxPlayer") != omx_player->getState())
			needReload = true;
		Settings::getInstance()->setBool("VideoOmxPlayer", omx_player->getState());
		if(needReload)
			ViewController::get()->reloadAll();
	});
#endif

	// 공장 초기화
	{
		ComponentListRow row;
		Window* window = mWindow;
		row.makeAcceptInputHandler([window] {
			window->pushGui(new GuiMsgBox(window,
				_("시스템 설정을 초기화합니다.\nROMs와 세이브 파일은 유지됩니다.\n\n정말 초기화하시겠습니까?"),
				_("YES"), [] {
					system("mount -o remount,rw /boot 2>/dev/null; touch /boot/.factory-reset; sync");
					Scripting::fireEvent("quit", "reboot");
					Scripting::fireEvent("reboot");
					quitES(QuitMode::REBOOT);
				},
				_("NO"), nullptr));
		});
		row.addElement(std::make_shared<TextComponent>(window, _("공장 초기화"), Font::get(FONT_SIZE_MEDIUM), 0xFF5555FF), true);
		s->addRow(row);
	}

	mWindow->pushGui(s);

}

void GuiMenu::openConfigInput()
{
	Window* window = mWindow;
	window->pushGui(new GuiMsgBox(window, _("ARE YOU SURE YOU WANT TO CONFIGURE INPUT?"), _("YES"),
		[window] {
		window->pushGui(new GuiDetectDevice(window, false, nullptr));
	}, _("NO"), nullptr)
	);

}

void GuiMenu::openQuitMenu()
{
	auto s = new GuiSettings(mWindow, _("QUIT"));

	Window* window = mWindow;

	// command line switch
	bool confirm_quit = Settings::getInstance()->getBool("ConfirmQuit");

	ComponentListRow row;
	if (UIModeController::getInstance()->isUIModeFull())
	{
		auto static restart_es_fx = []() {
			Scripting::fireEvent("quit");
			if (quitES(QuitMode::RESTART)) {
				LOG(LogWarning) << "Restart terminated with non-zero result!";
			}
		};

		if (confirm_quit) {
			row.makeAcceptInputHandler([window] {
				window->pushGui(new GuiMsgBox(window, _("REALLY RESTART?"), _("YES"), restart_es_fx, _("NO"), nullptr));
			});
		} else {
			row.makeAcceptInputHandler(restart_es_fx);
		}
		row.addElement(std::make_shared<TextComponent>(window, _("RESTART EMULATIONSTATION"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
		s->addRow(row);

		if(Settings::getInstance()->getBool("ShowExit"))
		{
			auto static quit_es_fx = [] {
				Scripting::fireEvent("quit");
				quitES();
			};

			row.elements.clear();
			if (confirm_quit) {
				row.makeAcceptInputHandler([window] {
					window->pushGui(new GuiMsgBox(window, _("REALLY QUIT?"), _("YES"), quit_es_fx, _("NO"), nullptr));
				});
			} else {
				row.makeAcceptInputHandler(quit_es_fx);
			}
			row.addElement(std::make_shared<TextComponent>(window, _("QUIT EMULATIONSTATION"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
			s->addRow(row);
		}

	}

	auto static reboot_sys_fx = [] {
		Scripting::fireEvent("quit", "reboot");
		Scripting::fireEvent("reboot");
		if (quitES(QuitMode::REBOOT)) {
			LOG(LogWarning) << "Restart terminated with non-zero result!";
		}
	};

	row.elements.clear();
	if (confirm_quit) {
		row.makeAcceptInputHandler([window] {
			window->pushGui(new GuiMsgBox(window, _("REALLY RESTART?"), _("YES"), {reboot_sys_fx}, _("NO"), nullptr));
		});
	} else {
		row.makeAcceptInputHandler(reboot_sys_fx);
	}
	row.addElement(std::make_shared<TextComponent>(window, _("RESTART SYSTEM"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
	s->addRow(row);

	auto static shutdown_sys_fx = [] {
		Scripting::fireEvent("quit", "shutdown");
		Scripting::fireEvent("shutdown");
		if (quitES(QuitMode::SHUTDOWN)) {
			LOG(LogWarning) << "Shutdown terminated with non-zero result!";
		}
	};

	row.elements.clear();
	if (confirm_quit) {
		row.makeAcceptInputHandler([window] {
			window->pushGui(new GuiMsgBox(window, _("REALLY SHUTDOWN?"), _("YES"), shutdown_sys_fx, _("NO"), nullptr));
		});
	} else {
		row.makeAcceptInputHandler(shutdown_sys_fx);
	}
	row.addElement(std::make_shared<TextComponent>(window, _("SHUTDOWN SYSTEM"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
	s->addRow(row);
	mWindow->pushGui(s);
}

void GuiMenu::addVersionInfo()
{
	std::string  buildDate = (Settings::getInstance()->getBool("Debug") ? std::string( "   (" + Utils::String::toUpper(PROGRAM_BUILT_STRING) + ")") : (""));

	// RetroPangui: 하단 버전 표시 임시 숨김 - 추후 다른 정보로 대체 예정
	// mVersion.setFont(Font::get(FONT_SIZE_SMALL));
	// mVersion.setColor(0x5E5E5EFF);
	// mVersion.setText("EMULATIONSTATION V" + Utils::String::toUpper(PROGRAM_VERSION_STRING) + buildDate);
	// mVersion.setHorizontalAlignment(ALIGN_CENTER);
	// addChild(&mVersion);
}

void GuiMenu::openScreensaverOptions() {
	mWindow->pushGui(new GuiGeneralScreensaverOptions(mWindow, _("SCREENSAVER SETTINGS")));
}

void GuiMenu::openCollectionSystemSettings() {
	mWindow->pushGui(new GuiCollectionSystemsOptions(mWindow));
}

void GuiMenu::openKodiMediaCenter()
{
	// RetroPangui: ES 종료 후 Kodi 실행, Kodi 종료 시 ES 재시작
	Scripting::fireEvent("quit");
	quitES(QuitMode::KODI);
}

// ---------------------------------------------------------------------------
// RetroAchievements - retropangui.conf / retroarch.cfg 읽기·쓰기 헬퍼
// ---------------------------------------------------------------------------

// RETROPANGUI_SHARE 환경 변수 → /share → ~/share 순서로 탐색
static std::string getSharePath()
{
	const char* env = getenv("RETROPANGUI_SHARE");
	if (env && env[0] != '\0')
		return env;
	struct stat st;
	if (stat("/share", &st) == 0 && S_ISDIR(st.st_mode))
		return "/share";
	const char* home = getenv("HOME");
	return home ? std::string(home) + "/share" : "/share";
}

static std::string getShareSystemPath() { return getSharePath() + "/system"; }
static std::string rpConfPath()         { return getShareSystemPath() + "/retropangui.conf"; }
static std::string raCfgPath()          { return getShareSystemPath() + "/retroarch/retroarch.cfg"; }

// key=value 행 파서 (공백·따옴표 제거)
static std::string cfgReadKey(const std::string& filePath, const std::string& fullKey,
                               const std::string& def = "")
{
	std::ifstream f(filePath);
	if (!f.is_open()) return def;
	std::string line;
	while (std::getline(f, line))
	{
		if (line.empty() || line[0] == '#') continue;
		auto eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string k = line.substr(0, eq);
		while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
		if (k != fullKey) continue;
		std::string v = line.substr(eq + 1);
		while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '"')) v.erase(v.begin());
		while (!v.empty() && (v.back()  == ' ' || v.back()  == '\t' || v.back()  == '"' || v.back() == '\r')) v.pop_back();
		return v;
	}
	return def;
}

static void cfgWriteKey(const std::string& filePath, const std::string& fullKey,
                         const std::string& value, bool quotedValue)
{
	Utils::FileSystem::createDirectory(filePath.substr(0, filePath.rfind('/')));
	std::ifstream fin(filePath);
	std::vector<std::string> lines;
	bool found = false;
	if (fin.is_open())
	{
		std::string line;
		while (std::getline(fin, line))
		{
			if (!line.empty() && line[0] != '#')
			{
				auto eq = line.find('=');
				if (eq != std::string::npos)
				{
					std::string k = line.substr(0, eq);
					while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
					if (k == fullKey)
					{
						line = quotedValue ? (fullKey + " = \"" + value + "\"")
						                   : (fullKey + " = " + value);
						found = true;
					}
				}
			}
			lines.push_back(line);
		}
		fin.close();
	}
	if (!found)
		lines.push_back(quotedValue ? (fullKey + " = \"" + value + "\"")
		                            : (fullKey + " = " + value));
	std::ofstream fout(filePath);
	for (auto& l : lines) fout << l << "\n";
	fout.close();
	// exFAT은 lazy write-back이라 재부팅 시 dirty page가 유실될 수 있음.
	// fsync로 eMMC까지 강제 flush하여 conf 값 유실 방지.
	int fd = ::open(filePath.c_str(), O_WRONLY);
	if (fd >= 0) { ::fsync(fd); ::close(fd); }
}

// ── 공개 헬퍼 ────────────────────────────────────────────────────────────────

// retropangui.conf 에서 읽기 (global.KEY 형식, 따옴표 없음)
// PATH에서 바이너리 존재 여부 확인 (결과 캐시 — 프로세스 당 1회 체크)
static bool isBinAvailable(const std::string& bin)
{
	static std::unordered_map<std::string, bool> sCache;
	auto it = sCache.find(bin);
	if (it != sCache.end()) return it->second;
	FILE* f = popen(("which " + bin + " 2>/dev/null").c_str(), "r");
	char buf[4] = {};
	if (f) { fgets(buf, sizeof(buf), f); pclose(f); }
	return sCache[bin] = (buf[0] != '\0');
}

static std::string raCfgGet(const std::string& key, const std::string& def = "")
{
	return cfgReadKey(rpConfPath(), "global." + key, def);
}

// retropangui.conf 에 쓰고, retroarch.cfg 에도 즉시 반영 (부팅 대기 없이 효과)
static void raCfgSet(const std::string& key, const std::string& value)
{
	cfgWriteKey(rpConfPath(), "global." + key, value, false); // retropangui.conf: 따옴표 없음
	cfgWriteKey(raCfgPath(),  key,             value, true);  // retroarch.cfg:    따옴표 있음
}

// ---------------------------------------------------------------------------
// YAML 메뉴 엔진 — addFeatureItem / openFeatureMenu
// ---------------------------------------------------------------------------

static std::string strongerRestart(const std::string& a, const std::string& b)
{
	if (a == "system" || b == "system") return "system";
	if (a == "es"     || b == "es")     return "es";
	return "none";
}

void GuiMenu::addFeatureItem(GuiSettings* s, const FeatureItem& item,
                              std::vector<RestartCheck>& checks)
{
	if (item.type == "toggle")
	{
		// conf.default는 0/1 컨벤션, ES는 true/false로 기록 — 양쪽 모두 인식
		std::string fallback = item.default_val.empty() ? "false" : item.default_val;
		std::string orig = cfgReadKey(rpConfPath(), item.conf_key, fallback);
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_(item.label.c_str()), sw);

		// BackgroundMusic: toggle 변경 즉시 conf + 메모리 반영 (메뉴를 닫기 전에도 적용)
		// addSaveFunc 는 BACK으로 닫을 때만 실행되므로, 비정상 종료나 다른 경로 재시작 대비
		// setChangedCallback 에서도 conf에 즉시 기록한다.
		if (item.conf_key == "emulationstation.BackgroundMusic") {
			sw->setChangedCallback([item](bool val) {
				Settings::getInstance()->setBool("BackgroundMusic", val);
				cfgWriteKey(rpConfPath(), item.conf_key, val ? "true" : "false", false);
				if (val) MusicManager::getInstance()->start();
				else     MusicManager::getInstance()->stop();
			});
		}

		s->addSaveFunc([item, sw] {
			bool newVal = sw->getState();
			cfgWriteKey(rpConfPath(), item.conf_key, newVal ? "true" : "false", false);
			if (item.conf_key == "emulationstation.EnableSounds") {
				if (newVal && !Settings::getInstance()->getBool("EnableSounds")
				    && PowerSaver::getMode() == PowerSaver::INSTANT) {
					Settings::getInstance()->setString("PowerSaverMode", "default");
					PowerSaver::init();
				}
				Settings::getInstance()->setBool("EnableSounds", newVal);
			} else if (item.conf_key == "emulationstation.VideoAudio") {
				Settings::getInstance()->setBool("VideoAudio", newVal);
			} else if (item.conf_key == "emulationstation.BackgroundMusic") {
				Settings::getInstance()->setBool("BackgroundMusic", newVal);
				if (newVal) MusicManager::getInstance()->start();
				else MusicManager::getInstance()->stop();
			}
		});
		if (item.restart != "none")
			checks.push_back({ [sw, state]{ return sw->getState() != state; },
			                   item.restart });
	}
	else if (item.type == "list")
	{
		std::string confVal = cfgReadKey(rpConfPath(), item.conf_key);
		auto list = std::make_shared<OptionListComponent<std::string>>(
			mWindow, _(item.label.c_str()), false);
		bool anySelected = false;
		bool isFirst = true;
		for (auto& opt : item.options) {
			// conf 값이 없으면 첫 번째 옵션을 기본 선택
			bool sel = (opt.value == confVal) || (isFirst && confVal.empty());
			isFirst = false;
			if (sel) anySelected = true;
			list->add(opt.label, opt.value, sel);
		}
		if (!anySelected && !item.options.empty())
			list->add(item.options[0].label, item.options[0].value, true);
		// 화면에 실제 표시된 초기값 기준으로 비교 (conf 없을 때도 정확히 동작)
		std::string effectiveOrig = list->getSelected();
		s->addWithLabel(_(item.label.c_str()), list);
		s->addSaveFunc([item, list] {
			cfgWriteKey(rpConfPath(), item.conf_key, list->getSelected(), false);
			if (item.conf_key == "global.audio_device") {
				Settings::getInstance()->setString("AudioCard", list->getSelected());
				VolumeControl::getInstance()->deinit();
				VolumeControl::getInstance()->init();
			} else if (item.conf_key == "emulationstation.AudioDevice") {
				Settings::getInstance()->setString("AudioDevice", list->getSelected());
				VolumeControl::getInstance()->deinit();
				VolumeControl::getInstance()->init();
			}
		});
		if (item.restart != "none")
			checks.push_back({ [list, effectiveOrig]{ return list->getSelected() != effectiveOrig; },
			                   item.restart });
	}
	else if (item.type == "input")
	{
		std::string orig = cfgReadKey(rpConfPath(), item.conf_key);
		auto curVal = std::make_shared<std::string>(orig);

		auto lbl = std::make_shared<TextComponent>(mWindow, _(item.label.c_str()),
			Font::get(FONT_SIZE_MEDIUM), 0x777777FF);
		auto valText = std::make_shared<TextComponent>(mWindow, orig,
			Font::get(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_RIGHT);

		ComponentListRow row;
		row.addElement(lbl, true);
		row.addElement(valText, false);

		Window* window = mWindow;
		std::string label = item.label;
		row.makeAcceptInputHandler([window, label, valText, curVal] {
			window->pushGui(new GuiArcadeVirtualKeyboard(window, _(label.c_str()),
				*curVal,
				[valText, curVal](const std::string& v) {
					*curVal = v;
					valText->setValue(v);
				}));
		});
		s->addRow(row);

		s->addSaveFunc([item, curVal] {
			cfgWriteKey(rpConfPath(), item.conf_key, *curVal, false);
		});
		if (item.restart != "none")
			checks.push_back({ [curVal, orig]{ return *curVal != orig; },
			                   item.restart });
	}
	else if (item.type == "slider")
	{
		std::string raw = cfgReadKey(rpConfPath(), item.conf_key);
		float orig = item.min;
		if (!raw.empty()) { try { orig = std::stof(raw); } catch (...) {} }
		auto sl = std::make_shared<SliderComponent>(mWindow, item.min, item.max, item.step, item.unit);
		sl->setValue(orig);
		if (item.conf_key == "system.volume") {
			sl->setChangedCallback([](float val) {
				VolumeControl::getInstance()->setVolume((int)Math::round(val));
			});
		}
		s->addWithLabel(_(item.label.c_str()), sl);
		s->addSaveFunc([item, sl] {
			cfgWriteKey(rpConfPath(), item.conf_key, std::to_string((int)sl->getValue()), false);
		});
		if (item.restart != "none")
			checks.push_back({ [sl, orig]{ return (int)sl->getValue() != (int)orig; },
			                   item.restart });
	}
	else if (item.type == "command")
	{
		if (item.exec.empty()) return;
		// exec의 첫 번째 단어(바이너리)가 PATH에 존재하는지 확인
		std::string bin = item.exec.substr(0, item.exec.find(' '));
		if (!isBinAvailable(bin)) return;
		std::string exec = item.exec;
		ComponentListRow row;
		row.makeAcceptInputHandler([this, exec] {
			::system((exec + " &").c_str());
		});
		auto lbl = std::make_shared<TextComponent>(mWindow, _(item.label.c_str()),
		                                           Font::get(FONT_SIZE_MEDIUM), 0x777777FF);
		row.addElement(lbl, true);
		s->addRow(row);
	}
}

void GuiMenu::addFeatureItemsTo(GuiSettings* s, const std::string& parent,
                                std::vector<RestartCheck>& checks)
{
	for (auto& fm : mFeatureMenus)
		if (fm.parent == parent)
			for (auto& item : fm.items)
				addFeatureItem(s, item, checks);
}

void GuiMenu::addSubmenuEntry(GuiSettings* s, const std::string& label,
                              const std::function<void()>& openFunc)
{
	ComponentListRow row;
	row.addElement(std::make_shared<TextComponent>(mWindow, label,
		Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
	row.addElement(makeArrow(mWindow), false);
	row.makeAcceptInputHandler(openFunc);
	s->addRow(row);
}

void GuiMenu::setSaveWithRestartChecks(GuiSettings* s,
                                       std::shared_ptr<std::vector<RestartCheck>> checks)
{
	s->setOnSave([this, checks, s] {
		// 실제로 값이 변경된 항목 중 가장 강한 restart 레벨 계산
		std::string actualRestart = "none";
		for (auto& [changed, level] : *checks)
			if (changed())
				actualRestart = strongerRestart(actualRestart, level);

		// 이 함수는 GuiSettings 소멸자에서 호출되므로 저장을 팝업 콜백으로
		// 미루면 s가 이미 파괴된 뒤에 실행됨 (use-after-free) → 항상 즉시 저장
		s->executeSaveFuncs();

		// retropangui.conf → OS/ES/RA 즉시 반영 (timezone, hostname 등 runtime 적용)
		system("/usr/share/retropangui/apply_retropangui_conf.sh &");

		if (actualRestart == "none")
			return;

		std::string msg = (actualRestart == "system")
			? _("재부팅이 필요합니다.\n지금 재부팅하시겠습니까?")
			: _("ES 재시작이 필요합니다.\n지금 재시작하시겠습니까?");
		mWindow->pushGui(new GuiMsgBox(mWindow, msg,
			_("OK"), [actualRestart] {
				if (actualRestart == "system")
					quitES(QuitMode::REBOOT);
				else
					quitES(QuitMode::RESTART);
			},
			_("CANCEL"), nullptr
		));
	});
}

void GuiMenu::openFeatureMenu(const std::string& menuId)
{
	auto it = std::find_if(mFeatureMenus.begin(), mFeatureMenus.end(),
		[&menuId](const FeatureMenu& m){ return m.id == menuId; });
	if (it == mFeatureMenus.end()) return;

	auto s = new GuiSettings(mWindow, _(it->label.c_str()));
	auto checks = std::make_shared<std::vector<RestartCheck>>();

	for (auto& item : it->items)
		addFeatureItem(s, item, *checks);

	setSaveWithRestartChecks(s, checks);

	mWindow->pushGui(s);
}

// GAME SETTINGS — 에뮬레이터(코어) 선택 + YAML(비디오/게임 옵션) + RetroAchievements
void GuiMenu::openGameSettings()
{
	auto s = new GuiSettings(mWindow, _("GAME SETTINGS"));
	auto checks = std::make_shared<std::vector<RestartCheck>>();

	addSubmenuEntry(s, _("EMULATOR SETTINGS"), [this] { openEmulatorSettings(); });

	// YAML: 스무딩/정수 스케일(구 VIDEO SETTINGS) + 되감기/자동저장(구 GAME SETTINGS)
	addFeatureItemsTo(s, "game", *checks);

	// 백그라운드 인덱싱
	auto background_indexing = std::make_shared<SwitchComponent>(mWindow);
	background_indexing->setState(Settings::getInstance()->getBool("BackgroundIndexing"));
	s->addWithLabel(_("INDEX FILES DURING SCREENSAVER"), background_indexing);
	s->addSaveFunc([background_indexing] { Settings::getInstance()->setBool("BackgroundIndexing", background_indexing->getState()); });

	addSubmenuEntry(s, _("RETROACHIEVEMENTS"), [this] { openRetroAchievements(); });

	setSaveWithRestartChecks(s, checks);
	mWindow->pushGui(s);
}

// CONTROLLER SETTINGS — 입력 설정 + 버튼 방식 + YAML(드라이버/통합 컨트롤)
void GuiMenu::openControllerSettings()
{
	auto s = new GuiSettings(mWindow, _("CONTROLLER SETTINGS"));
	auto checks = std::make_shared<std::vector<RestartCheck>>();

	addSubmenuEntry(s, _("CONFIGURE INPUT"), [this] { openConfigInput(); });

	// 버튼 방식 (UI SETTINGS에서 이동)
	auto button_layout = std::make_shared< OptionListComponent<std::string> >(mWindow, _("BUTTON LAYOUT"), false);
	std::vector<std::string> layouts;
	layouts.push_back("nintendo");
	layouts.push_back("sony");
	layouts.push_back("xbox");
	std::string currentLayout = Settings::getInstance()->getString("ButtonLayout");
	for(auto it = layouts.cbegin(); it != layouts.cend(); it++)
		button_layout->add(*it, *it, *it == currentLayout);
	s->addWithLabel(_("BUTTON LAYOUT"), button_layout);
	s->addSaveFunc([button_layout] {
		Settings::getInstance()->setString("ButtonLayout", button_layout->getSelected());
		InputConfig::initActionMapping();
	});

	// YAML: 조이패드 드라이버 / 통합 컨트롤 설정 (구 ADVANCED SETTINGS)
	addFeatureItemsTo(s, "controller", *checks);

	setSaveWithRestartChecks(s, checks);
	mWindow->pushGui(s);
}

// SYSTEM SETTINGS — 언어 + YAML(시간대/SSH) + 업데이트(준비 중) + 고급
void GuiMenu::openStorageSettings()
{
	mWindow->pushGui(new GuiStorageSelect(mWindow));
}

void GuiMenu::openSystemSettings()
{
	auto s = new GuiSettings(mWindow, _("SYSTEM SETTINGS"));
	auto checks = std::make_shared<std::vector<RestartCheck>>();

	// YAML: 시간대(구 SYSTEM SETTINGS) + SSH(구 NETWORK SETTINGS)
	addFeatureItemsTo(s, "system", *checks);

	// power saver
	auto power_saver = std::make_shared< OptionListComponent<std::string> >(mWindow, _("POWER SAVER MODES"), false);
	std::vector<std::string> modes;
	modes.push_back("disabled");
	modes.push_back("default");
	modes.push_back("enhanced");
	modes.push_back("instant");
	for (auto it = modes.cbegin(); it != modes.cend(); it++)
		power_saver->add(*it, *it, Settings::getInstance()->getString("PowerSaverMode") == *it);
	s->addWithLabel(_("POWER SAVER MODES"), power_saver);
	s->addSaveFunc([this, power_saver] {
		if (Settings::getInstance()->getString("PowerSaverMode") != "instant" && power_saver->getSelected() == "instant") {
			Settings::getInstance()->setString("TransitionStyle", "instant");
			Settings::getInstance()->setBool("MoveCarousel", false);
			Settings::getInstance()->setBool("EnableSounds", false);
		}
		Settings::getInstance()->setString("PowerSaverMode", power_saver->getSelected());
		PowerSaver::init();
	});

	addSubmenuEntry(s, _("UPDATES & DOWNLOADS"), [this] { openUpdatesAndDownloads(); });
	if (isBinAvailable("storage-mgr"))
		addSubmenuEntry(s, _("STORAGE DEVICE"), [this] { openStorageSettings(); });
	addSubmenuEntry(s, _("ADVANCED SETTINGS"),   [this] { openAdvancedSettings(); });

	setSaveWithRestartChecks(s, checks);
	mWindow->pushGui(s);
}

void GuiMenu::openRetroAchievements()
{
	auto s = new GuiSettings(mWindow, _("RETROACHIEVEMENTS"));

	// --- 활성화 ---
	auto cheevos_enable = std::make_shared<SwitchComponent>(mWindow);
	cheevos_enable->setState(raCfgGet("cheevos_enable", "false") == "true");
	s->addWithLabel(_("ENABLE"), cheevos_enable);

	// --- 사용자 이름 ---
	// 고정 너비 필요: TextComponent는 초기 크기로 레이아웃이 결정되므로
	// setValue() 이후 크기 변화가 레이아웃에 반영되지 않음 → 충분한 너비 고정
	float valW = (float)Renderer::getScreenWidth() * 0.22f;
	auto username_text = std::make_shared<TextComponent>(mWindow,
		raCfgGet("cheevos_username"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_RIGHT);
	username_text->setSize(valW, Font::get(FONT_SIZE_MEDIUM)->getHeight());
	ComponentListRow username_row;
	username_row.addElement(std::make_shared<TextComponent>(mWindow,
		_("USERNAME"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
	username_row.addElement(username_text, false);
	username_row.makeAcceptInputHandler([this, username_text] {
		mWindow->pushGui(new GuiArcadeVirtualKeyboard(mWindow, _("USERNAME"),
			username_text->getValue(),
			[username_text](const std::string& val) { username_text->setValue(val); }));
	});
	s->addRow(username_row);

	// --- 비밀번호 ---
	auto password_text = std::make_shared<TextComponent>(mWindow,
		raCfgGet("cheevos_password").empty() ? "" : "••••••••",
		Font::get(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_RIGHT);
	password_text->setSize(valW, Font::get(FONT_SIZE_MEDIUM)->getHeight());
	auto password_val = std::make_shared<std::string>(raCfgGet("cheevos_password"));
	ComponentListRow password_row;
	password_row.addElement(std::make_shared<TextComponent>(mWindow,
		_("PASSWORD"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
	password_row.addElement(password_text, false);
	password_row.makeAcceptInputHandler([this, password_text, password_val] {
		mWindow->pushGui(new GuiArcadeVirtualKeyboard(mWindow, _("PASSWORD"),
			*password_val,
			[password_text, password_val](const std::string& val) {
				*password_val = val;
				password_text->setValue(val.empty() ? "" : "••••••••");
			}));
	});
	s->addRow(password_row);

	// --- 하드코어 모드 ---
	auto hardcore = std::make_shared<SwitchComponent>(mWindow);
	hardcore->setState(raCfgGet("cheevos_hardcore_mode_enable", "false") == "true");
	s->addWithLabel(_("HARDCORE MODE"), hardcore);

	// 하드코어 모드 안내 텍스트
	ComponentListRow hardcore_note_row;
	auto hardcore_note = std::make_shared<TextComponent>(mWindow,
		_("* DISABLES SAVE STATES, REWIND AND CHEATS"),
		Font::get(FONT_SIZE_SMALL), 0x999999FF);
	hardcore_note_row.addElement(hardcore_note, true);
	s->addRow(hardcore_note_row);

	// --- 리더보드 ---
	auto leaderboards = std::make_shared<OptionListComponent<std::string>>(mWindow, _("LEADERBOARDS"), false);
	std::string lb_val = raCfgGet("cheevos_leaderboards_enable", "false");
	leaderboards->add(_("DISABLED"), "false",  lb_val == "false");
	leaderboards->add(_("ENABLED"),   "true",   lb_val == "true");
	leaderboards->add(_("TRACKERS ONLY"), "trackers only", lb_val == "trackers only");
	s->addWithLabel(_("LEADERBOARDS"), leaderboards);

	// --- 상세 알림 ---
	auto verbose = std::make_shared<SwitchComponent>(mWindow);
	verbose->setState(raCfgGet("cheevos_verbose_enable", "false") == "true");
	s->addWithLabel(_("VERBOSE MODE"), verbose);

	// --- 자동 스크린샷 ---
	auto screenshot = std::make_shared<SwitchComponent>(mWindow);
	screenshot->setState(raCfgGet("cheevos_auto_screenshot", "false") == "true");
	s->addWithLabel(_("AUTO SCREENSHOT"), screenshot);

	// --- 리치 프레즌스 ---
	auto richpresence = std::make_shared<SwitchComponent>(mWindow);
	richpresence->setState(raCfgGet("cheevos_rich_presence_enable", "true") == "true");
	s->addWithLabel(_("RICH PRESENCE"), richpresence);

	// --- 배지 표시 ---
	auto badges = std::make_shared<SwitchComponent>(mWindow);
	badges->setState(raCfgGet("cheevos_badges_enable", "false") == "true");
	s->addWithLabel(_("SHOW BADGES"), badges);

	// --- 앙코르 모드 ---
	auto encore = std::make_shared<SwitchComponent>(mWindow);
	encore->setState(raCfgGet("cheevos_encore_mode_enable", "false") == "true");
	s->addWithLabel(_("ENCORE MODE"), encore);

	// --- 저장 ---
	s->addSaveFunc([cheevos_enable, username_text, password_val,
	                hardcore, leaderboards, verbose, screenshot, richpresence, badges, encore] {
		raCfgSet("cheevos_enable",                cheevos_enable->getState() ? "true" : "false");
		raCfgSet("cheevos_username",              username_text->getValue());
		raCfgSet("cheevos_password",              *password_val);
		raCfgSet("cheevos_hardcore_mode_enable",  hardcore->getState() ? "true" : "false");
		raCfgSet("cheevos_leaderboards_enable",   leaderboards->getSelected());
		raCfgSet("cheevos_verbose_enable",        verbose->getState() ? "true" : "false");
		raCfgSet("cheevos_auto_screenshot",       screenshot->getState() ? "true" : "false");
		raCfgSet("cheevos_rich_presence_enable",  richpresence->getState() ? "true" : "false");
		raCfgSet("cheevos_badges_enable",         badges->getState() ? "true" : "false");
		raCfgSet("cheevos_encore_mode_enable",    encore->getState() ? "true" : "false");
	});

	mWindow->pushGui(s);
}

void GuiMenu::openUpdatesAndDownloads()
{
	// conf 읽기 (없어도 USB 스캔은 진행)
	std::string serverUrl;
	std::string device = "odroidc5";
	{
		auto trim = [](std::string& s) {
			s.erase(s.find_last_not_of(" \t\r\n") + 1);
			s.erase(0, s.find_first_not_of(" \t\r\n"));
		};
		std::ifstream f("/etc/retropangui-ota.conf");
		if (f.good()) {
			std::getline(f, serverUrl); trim(serverUrl);
			std::string d; std::getline(f, d); trim(d);
			if (!d.empty()) device = d;
		}
	}

	// 현재 버전
	std::string curVer;
	{
		std::ifstream f("/etc/retropangui-version");
		if (f.good()) {
			std::getline(f, curVer);
			curVer.erase(curVer.find_last_not_of(" \t\r\n") + 1);
		}
	}

	// USB 스캔: /media/ 하위 디렉토리 루트에서 retropangui-<device>.squashfs 검색
	std::string usbPath;
	std::string usbTarget = "retropangui-" + device + ".squashfs";
	{
		DIR* media = opendir("/media");
		if (media) {
			struct dirent* entry;
			while ((entry = readdir(media)) != nullptr && usbPath.empty()) {
				if (entry->d_type != DT_DIR) continue;
				if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") continue;
				std::string candidate = std::string("/media/") + entry->d_name + "/" + usbTarget;
				struct stat st;
				if (stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode))
					usbPath = candidate;
			}
			closedir(media);
		}
	}

	// USB 파일 발견 — USB 설치 흐름
	if (!usbPath.empty()) {
		std::string msg = "USB 업데이트 파일을 발견했습니다.\n" + usbPath + "\n\n지금 설치하시겠습니까?\n(재부팅 후 적용됩니다)";
		mWindow->pushGui(new GuiMsgBox(mWindow, msg,
			"설치", [this, usbPath]() {
				auto install_fn = [usbPath]() -> int {
					std::string cmd = "/usr/share/retropangui/usb-ota-install.sh \"" + usbPath + "\"";
					int ret = ::system(cmd.c_str());
					return WEXITSTATUS(ret);
				};
				auto done_fn = [this](bool success) {
					if (success) {
						mWindow->pushGui(new GuiMsgBox(mWindow,
							"USB 업데이트 설치 완료!\n재부팅하면 적용됩니다.\n\n지금 재부팅하시겠습니까?",
							"재부팅", []() { ::system("reboot"); },
							"나중에", nullptr));
					} else {
						mWindow->pushGui(new GuiMsgBox(mWindow,
							"USB 업데이트 실패.\n파일을 확인하세요.",
							"확인", nullptr));
					}
				};
				mWindow->pushGui(new GuiOtaDownload(mWindow, install_fn, done_fn));
			},
			"취소", nullptr));
		return;
	}

	// USB 없음 — 네트워크 체크
	if (serverUrl.empty()) {
		mWindow->pushGui(new GuiMsgBox(mWindow,
			"업데이트 서버가 설정되지 않았습니다.\n/etc/retropangui-ota.conf 를 확인하세요.",
			"확인", nullptr));
		return;
	}

	auto check_fn = [serverUrl]() -> std::string {
		auto req = std::make_shared<HttpReq>(serverUrl + "/version");
		for (int i = 0; i < 50 && req->status() == HttpReq::REQ_IN_PROGRESS; i++)
			SDL_Delay(100);
		if (req->status() != HttpReq::REQ_SUCCESS) return "";
		std::string v = req->getContent();
		v.erase(v.find_last_not_of(" \t\r\n") + 1);
		return v;
	};

	auto check_done = [this, curVer, serverUrl, device](std::string serverVer) {
		if (serverVer.empty()) {
			mWindow->pushGui(new GuiMsgBox(mWindow,
				"서버 연결 실패.\n네트워크 연결을 확인하세요.",
				"확인", nullptr));
			return;
		}
		// 버전 형식: "0.14-10-g785974d" (git describe: <tag>-<commits>-g<hash>)
		// 단순 문자열 비교 시 "10" < "9" 오류 → commit 수를 숫자로 비교
		auto getTag = [](const std::string& v) -> std::string {
			auto d2 = v.rfind('-');
			if (d2 == std::string::npos) return v;
			auto d1 = v.rfind('-', d2 - 1);
			return (d1 == std::string::npos) ? v : v.substr(0, d1);
		};
		auto getCommits = [](const std::string& v) -> int {
			auto d2 = v.rfind('-');
			if (d2 == std::string::npos) return 0;
			auto d1 = v.rfind('-', d2 - 1);
			if (d1 == std::string::npos) return 0;
			try { return std::stoi(v.substr(d1 + 1, d2 - d1 - 1)); } catch (...) { return 0; }
		};
		bool serverNewer = (getTag(serverVer) != getTag(curVer))
		                   ? (getTag(serverVer) > getTag(curVer))
		                   : (getCommits(serverVer) > getCommits(curVer));
		if (!serverNewer) {
			mWindow->pushGui(new GuiMsgBox(mWindow,
				"현재 최신 버전입니다.\n현재 버전: " + curVer,
				"확인", nullptr));
			return;
		}
		std::string msg = "새 버전이 있습니다.\n현재: " + curVer +
		                  "  →  새 버전: " + serverVer +
		                  "\n\n업데이트를 다운로드하시겠습니까?\n(재부팅 후 적용됩니다)";
		mWindow->pushGui(new GuiMsgBox(mWindow, msg,
			"업데이트", [this, serverUrl, device, serverVer]() {
				auto download_fn = [serverUrl, device]() -> int {
					std::string cmd = "/usr/share/retropangui/ota-update.sh " + serverUrl + " " + device;
					int ret = ::system(cmd.c_str());
					return WEXITSTATUS(ret);
				};
				auto done_fn = [this, serverVer](bool success) {
					if (success) {
						mWindow->pushGui(new GuiMsgBox(mWindow,
							"업데이트 준비 완료!\n새 버전: " + serverVer +
							"\n\n지금 재부팅하시겠습니까?",
							"재부팅", []() { ::system("reboot"); },
							"나중에", nullptr));
					} else {
						mWindow->pushGui(new GuiMsgBox(mWindow,
							"업데이트 실패.\n다운로드 또는 검증 오류입니다.",
							"확인", nullptr));
					}
				};
				mWindow->pushGui(new GuiOtaDownload(mWindow, download_fn, done_fn));
			},
			"취소", nullptr));
	};

	mWindow->pushGui(new GuiOtaCheck(mWindow, check_fn, check_done));
}

void GuiMenu::onSizeChanged()
{
	mVersion.setSize(mSize.x(), 0);
	mVersion.setPosition(0, mSize.y() - mVersion.getSize().y());
}

void GuiMenu::addEntry(const char* name, unsigned int color, bool add_arrow, const std::function<void()>& func)
{
	std::shared_ptr<Font> font = Font::get(FONT_SIZE_MEDIUM);

	// populate the list
	ComponentListRow row;
	row.addElement(std::make_shared<TextComponent>(mWindow, name, font, color), true);

	if(add_arrow)
	{
		std::shared_ptr<ImageComponent> bracket = makeArrow(mWindow);
		row.addElement(bracket, false);
	}

	row.makeAcceptInputHandler(func);

	mMenu.addRow(row);
}

bool GuiMenu::input(InputConfig* config, Input input)
{
	if(GuiComponent::input(config, input))
		return true;

	if((config->isMappedToAction("back", input) || config->isMappedTo("start", input)) && input.value != 0)
	{
		delete this;
		return true;
	}

	return false;
}

void GuiMenu::openEmulatorSettings()
{
	auto s = new GuiSettings(mWindow, _("EMULATOR SETTINGS"));

	// Iterate through all game systems (not collections)
	for (auto system : SystemData::sSystemVector)
	{
		if (system->isCollection() || !system->isGameSystem())
			continue;

		std::vector<CoreInfo> cores = system->getCores();
		if (cores.empty())
			continue;

		// Create emulator selection list for this system
		auto emulatorList = std::make_shared<OptionListComponent<std::string>>(mWindow,
			system->getFullName() + " DEFAULT EMULATOR", false);

		// Add all available emulators sorted by priority
		std::string currentDefault = "";
		for (const auto& core : cores)
		{
			if (core.priority == 1)
				currentDefault = core.name;

			std::string label = core.fullname;
			if (core.priority == 1)
				label += " (Current Default)";

			emulatorList->add(label, core.name, core.priority == 1);
		}

		s->addWithLabel(Utils::String::toUpper(system->getName()), emulatorList);
		s->addSaveFunc([system, emulatorList, currentDefault] {
			std::string selectedCore = emulatorList->getSelected();
			std::string systemName = system->getName();

			// Only update if selection changed
			if (selectedCore == currentDefault)
			{
				LOG(LogDebug) << "No change in default emulator for " << systemName << ", skipping update";
				return;
			}

			// Step 1: Update memory (immediate effect)
			// First, increment all cores' priority by 1
			for (auto& core : system->getSystemEnvData()->mCores)
			{
				core.priority++;
			}

			// Then, set selected core to priority 1
			for (auto& core : system->getSystemEnvData()->mCores)
			{
				if (core.name == selectedCore)
				{
					core.priority = 1;
					break;
				}
			}

			// Re-sort cores by priority to ensure correct order
			std::sort(system->getSystemEnvData()->mCores.begin(),
			          system->getSystemEnvData()->mCores.end(),
			          [](const CoreInfo& a, const CoreInfo& b) {
			              return a.priority < b.priority;
			          });

			LOG(LogInfo) << "Updated in-memory default emulator for " << systemName << " to " << selectedCore;

			// Step 2: Update XML file (persist for next launch)
			std::string cmd = "bash -c 'source /home/pangui/scripts/retropangui/scriptmodules/es_systems_updater.sh && "
				"set_default_core \"" + systemName + "\" \"" + selectedCore + "\"'";

			int result = ::system(cmd.c_str());
			if (result != 0)
			{
				LOG(LogError) << "Failed to update XML for " << systemName;
			}
		});
	}

	mWindow->pushGui(s);
}

HelpStyle GuiMenu::getHelpStyle()
{
	HelpStyle style = HelpStyle();
	style.applyTheme(ViewController::get()->getState().getSystem()->getTheme(), "system");
	return style;
}

std::vector<HelpPrompt> GuiMenu::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt("up/down", "choose"));

	// RetroPangui: Use logical button names based on ButtonLayout
	std::string acceptButton = Settings::getInstance()->getString("ButtonLayout") == "nintendo" ? "b" : "a";
	prompts.push_back(HelpPrompt(acceptButton, "select"));
	prompts.push_back(HelpPrompt("start", "close"));
	return prompts;
}
