#include "guis/GuiMenu.h"
#include <unordered_map>

#include "components/OptionListComponent.h"
#include "components/SliderComponent.h"
#include "components/SwitchComponent.h"
#include "guis/GuiCollectionSystemsOptions.h"
#include "guis/GuiStorageSelect.h"
#include "guis/GuiWifiSelect.h"
#include "guis/GuiBtDevices.h"
#include "guis/GuiBtPairing.h"
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
#include <SDL_joystick.h>
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
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "HttpReq.h"
#include <sys/wait.h>
#include <SDL_timer.h>

// fork+execlp로 직접 실행 — 쉘을 거치지 않아 안전 (GuiWifiSelect/GuiBtDevices와 동일 패턴)
static void removeAllBtPairings()
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("rpui-bt", "rpui-bt", "remove-all", (char*)nullptr);
		_exit(127);
	} else if (pid > 0) {
		waitpid(pid, nullptr, 0);
	}
}

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

	// 2026-07-11: AUDIO CARD - "default/sysdefault/dmix/hw/plughw/null" 같은
	// ALSA 기술 용어 고정 목록 대신, 지금 실제로 연결된 사운드카드를
	// /proc/asound/cards에서 그때그때 스캔해서 보여줌(실시간 데이터라
	// YAML 정적 목록으로 표현 불가 - BLUETOOTH DEVICES와 동일한 이유).
	// 온보드(AML-AUGESOUND)는 "HDMI"로, 그 외(USB 오디오 등)는 실제 장치
	// 이름 그대로 표시. 카드 번호가 아니라 이름으로 값을 구성해서(hw:CARD=
	// 이름,DEV=0) USB 장치가 먼저 꽂혀 번호가 밀려도 안 깨지게 함.
	{
		std::vector<std::pair<std::string, std::string>> cards; // (label, alsaId)
		std::ifstream f("/proc/asound/cards");
		std::string line;
		while (std::getline(f, line))
		{
			auto lb = line.find('[');
			auto rb = line.find(']');
			if (lb == std::string::npos || rb == std::string::npos || rb < lb)
				continue;
			std::string id = line.substr(lb + 1, rb - lb - 1);
			while (!id.empty() && id.back() == ' ') id.pop_back();
			if (id.empty()) continue;
			std::string label = (id == "AMLAUGESOUND") ? "HDMI" : id;
			cards.push_back({ label, id });
		}
		std::string origCard = Settings::getInstance()->getString("AudioCard");
		auto audio_card = std::make_shared< OptionListComponent<std::string> >(mWindow, _("AUDIO CARD"), false);
		bool anySel = false;
		for (auto& c : cards)
		{
			std::string val = "hw:CARD=" + c.second + ",DEV=0";
			bool sel = (val == origCard);
			if (sel) anySel = true;
			audio_card->add(c.first, val, sel);
		}
		if (!anySel)
			audio_card->add("default", "default", true);
		s->addWithLabel(_("AUDIO CARD"), audio_card);
		s->addSaveFunc([audio_card, origCard] {
			std::string newVal = audio_card->getSelected();
			if (newVal == origCard) return;
			Settings::getInstance()->setString("AudioCard", newVal);
			cfgWriteKey(rpConfPath(), "global.audio_device", newVal, false);
			VolumeControl::getInstance()->deinit();
			VolumeControl::getInstance()->init();
		});
	}

	addFeatureItemsTo(s, "sound", *checks);

	// 실시간 스캔 목록 표시가 필요해 YAML로 표현 불가 (BLUETOOTH DEVICES와 동일 이유)
	addSubmenuEntry(s, _("PAIR A BLUETOOTH AUDIO DEVICE"), [this] {
		mWindow->pushGui(new GuiBtPairing(mWindow, "audio-", "scan-start-audio"));
	});

	// 페어링된 블루투스 오디오 기기 목록 — 실시간 데이터라 YAML로 표현 불가
	addSubmenuEntry(s, _("BLUETOOTH DEVICES"), [this] { mWindow->pushGui(new GuiBtDevices(mWindow)); });

	// 목록에서 골라 지우는 게 아니라 전체 초기화 — 확인 팝업이 필요해 YAML로 표현 불가
	addSubmenuEntry(s, _("REMOVE ALL BLUETOOTH PAIRINGS"), [this] {
		mWindow->pushGui(new GuiMsgBox(mWindow, _("REMOVE ALL BLUETOOTH PAIRINGS?"),
			_("YES"), [] { removeAllBtPairings(); },
			"아니오", nullptr));
	});

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
	auto checks = std::make_shared<std::vector<RestartCheck>>();

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
	s->addWithLabel(_("ES VRAM LIMIT"), max_vram);
	s->addSaveFunc([max_vram] { Settings::getInstance()->setInt("MaxVRAM", (int)Math::round(max_vram->getValue())); });

	// framerate
	auto framerate = std::make_shared<SwitchComponent>(mWindow);
	framerate->setState(Settings::getInstance()->getBool("DrawFramerate"));
	s->addWithLabel(_("ES SHOW FRAMERATE"), framerate);
	s->addSaveFunc([framerate] { Settings::getInstance()->setBool("DrawFramerate", framerate->getState()); });

	// RetroPangui(2026-07-11): UI SCALE - 물리 HDMI 출력(1920x1080)은 고정이고,
	// ES가 그리는 논리 캔버스 크기(ScreenWidth/Height)만 줄여서 그 안의 UI를
	// 확대해서 그리게 함(Renderer.cpp에서 뷰포트/투영을 분리해 구현). 폭/높이를
	// 한 쌍으로 같이 정해야 해서(비율 안 맞으면 레이아웃 깨짐) YAML의
	// type: list(값 하나만 다루는) 로는 표현이 안 되어 네이티브로 추가함.
	// 뷰포트/투영은 Renderer::init()에서 한 번만 계산되므로 재시작 필요.
	struct UiScaleOption { const char* label; int w; int h; };
	static const UiScaleOption uiScaleOptions[] = {
		{ "100% (1920x1080)", 1920, 1080 },
		{ "120% (1600x900)",  1600, 900 },
		{ "140% (1366x768)",  1366, 768 },
		{ "150% (1280x720)",  1280, 720 },
		{ "188% (1024x576)",  1024, 576 },
		{ "200% (960x540)",    960, 540 },
		{ "225% (854x480)",    854, 480 },
		{ "300% (640x360)",    640, 360 },
	};
	int curScreenW = Settings::getInstance()->getInt("ScreenWidth");
	int curScreenH = Settings::getInstance()->getInt("ScreenHeight");
	if (curScreenW <= 0 || curScreenH <= 0) { curScreenW = 1920; curScreenH = 1080; }
	std::string curScaleKey = std::to_string(curScreenW) + "x" + std::to_string(curScreenH);

	auto ui_scale = std::make_shared< OptionListComponent<std::string> >(mWindow, _("UI SCALE"), false);
	for (auto& opt : uiScaleOptions)
	{
		std::string key = std::to_string(opt.w) + "x" + std::to_string(opt.h);
		ui_scale->add(opt.label, key, key == curScaleKey);
	}
	s->addWithLabel(_("UI SCALE"), ui_scale);
	s->addSaveFunc([ui_scale] {
		std::string sel = ui_scale->getSelected();
		auto xpos = sel.find('x');
		if (xpos != std::string::npos)
		{
			int w = atoi(sel.substr(0, xpos).c_str());
			int h = atoi(sel.substr(xpos + 1).c_str());
			Settings::getInstance()->setInt("ScreenWidth", w);
			Settings::getInstance()->setInt("ScreenHeight", h);
		}
	});
	checks->push_back({ [ui_scale, curScaleKey]{ return ui_scale->getSelected() != curScaleKey; }, "es" });

	// YAML: UI 관련 항목 (LANGUAGE 등)
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
				_("THIS WILL RESET SYSTEM SETTINGS.\nROMS AND SAVE FILES WILL BE KEPT.\n\nARE YOU SURE YOU WANT TO RESET?"),
				_("YES"), [] {
					system("mount -o remount,rw /boot 2>/dev/null; touch /boot/.factory-reset; sync");
					Scripting::fireEvent("quit", "reboot");
					Scripting::fireEvent("reboot");
					quitES(QuitMode::REBOOT);
				},
				_("NO"), nullptr));
		});
		row.addElement(std::make_shared<TextComponent>(window, _("FACTORY RESET"), Font::get(FONT_SIZE_MEDIUM), 0xFF5555FF), true);
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
				window->pushGui(new GuiMsgBox(window, _("REALLY RESTART?"), _("YES"), restart_es_fx, "아니오", nullptr));
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
					window->pushGui(new GuiMsgBox(window, _("REALLY QUIT?"), _("YES"), quit_es_fx, "아니오", nullptr));
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
			window->pushGui(new GuiMsgBox(window, _("REALLY RESTART?"), _("YES"), {reboot_sys_fx}, "아니오", nullptr));
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
			window->pushGui(new GuiMsgBox(window, _("REALLY SHUTDOWN?"), _("YES"), shutdown_sys_fx, "아니오", nullptr));
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

// NETWORK 서브메뉴의 CURRENT IP ADDRESS 표시용 - 유선/무선 구분 없이 lo를 뺀
// 첫 IPv4 주소 하나(멀티 인터페이스 동시 연결은 흔치 않은 임베디드 환경이라
// "활성 인터페이스 대표 하나"로 충분하다고 판단, 2026-07-10).
// 2026-07-11: IP만 보여주면 유선/무선 어느 쪽으로 붙어있는지 알 수 없어서
// 인터페이스 이름(net.ifnames=0 커널 옵션으로 eth*/wlan* 고정됨을 전제)도
// 같이 반환하도록 확장.
static std::string getCurrentIpAddress(std::string* outIface = nullptr)
{
	struct ifaddrs* ifaddr = nullptr;
	if (getifaddrs(&ifaddr) == -1)
		return "-";

	std::string result = "-";
	for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
	{
		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if (std::string(ifa->ifa_name) == "lo")
			continue;
		char buf[INET_ADDRSTRLEN] = {};
		auto* sin = (struct sockaddr_in*)ifa->ifa_addr;
		inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
		result = buf;
		if (outIface) *outIface = ifa->ifa_name;
		break;
	}
	freeifaddrs(ifaddr);
	return result;
}

static std::string getCurrentIpAddressWithLink()
{
	std::string iface;
	std::string ip = getCurrentIpAddress(&iface);
	if (ip == "-") return ip;
	std::string link = "?";
	if (iface.rfind("eth", 0) == 0 || iface.rfind("en", 0) == 0)
		link = "유선";
	else if (iface.rfind("wlan", 0) == 0 || iface.rfind("wl", 0) == 0)
		link = "무선";
	return ip + " (" + link + ")";
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
		s->addSaveFunc([item, list, effectiveOrig] {
			std::string newVal = list->getSelected();
			cfgWriteKey(rpConfPath(), item.conf_key, newVal, false);
			// 값이 실제로 안 바뀌었으면 ALSA 믹서 재초기화(deinit+init)를 스킵 —
			// 이 메뉴를 나가기만 해도 매번 무조건 재초기화(무거운 작업, 카드
			// 재검색 등)가 실행돼서 메인 스레드가 블로킹되고, 그 사이 쌓인
			// 입력 이벤트가 한꺼번에 재생되어 "커서가 미끄러지듯 계속 이동"하는
			// 것처럼 보이는 버그의 원인이었음(2026-07-05 실기기 로그로 확인 —
			// SOUND SETTINGS를 나갈 때마다 VolumeControl::init()이 AUDIO CARD/
			// AUDIO DEVICE 두 항목 분량 연달아 2번 호출됨).
			if (newVal == effectiveOrig) return;
			if (item.conf_key == "global.audio_device") {
				Settings::getInstance()->setString("AudioCard", newVal);
				VolumeControl::getInstance()->deinit();
				VolumeControl::getInstance()->init();
			} else if (item.conf_key == "emulationstation.AudioDevice") {
				Settings::getInstance()->setString("AudioDevice", newVal);
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
		// 2026-07-11: 다른 값 표시 텍스트(OptionListComponent 선택값,
		// 슬라이더 등)는 전부 FONT_PATH_LIGHT(콘덴스드 라이트체)를 쓰는데
		// 여기만 기본 폰트라 눈에 띄게 달라 보이던 것을 통일.
		auto valText = std::make_shared<TextComponent>(mWindow, orig,
			Font::get(FONT_SIZE_MEDIUM, FONT_PATH_LIGHT), 0x777777FF, ALIGN_RIGHT);

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
			// 슬라이더는 좌우 입력을 누르고 있으면 40ms(MOVE_REPEAT_RATE)마다
			// setValue()를 호출해 이 콜백을 매번 실행함 — VolumeControl::setVolume()은
			// ALSA 믹서 호출(snd_mixer_selem_*)이라 매번 정확히 얼마나 걸릴지 예측 불가.
			// 백그라운드 음악 재생 중 등 조건에 따라 지연되면, 메인 스레드가 그동안
			// 막혀서 큐에 쌓인 입력 이벤트가 한꺼번에 재생되어 "커서가 미끄러지듯
			// 계속 이동"하는 것처럼 보임(2026-07-05, 다른 슬라이더(VRAM 제한)는
			// 이런 콜백 자체가 없어서 재현 안 됨 — 볼륨 슬라이더 전용 문제로 확인).
			// 실제 ALSA 반영 빈도를 제한(스로틀)해서 연타로 인한 블로킹 누적을 방지.
			auto lastCallTick = std::make_shared<Uint32>(0);
			sl->setChangedCallback([lastCallTick](float val) {
				Uint32 now = SDL_GetTicks();
				if (now - *lastCallTick < 80) return;
				*lastCallTick = now;
				VolumeControl::getInstance()->setVolume((int)Math::round(val));
			});
		}
		s->addWithLabel(_(item.label.c_str()), sl);
		s->addSaveFunc([item, sl] {
			cfgWriteKey(rpConfPath(), item.conf_key, std::to_string((int)sl->getValue()), false);
			// 스로틀 때문에 조작 중 마지막 값이 ALSA에 반영 안 됐을 수 있어 나갈 때 최종 동기화
			if (item.conf_key == "system.volume")
				VolumeControl::getInstance()->setVolume((int)Math::round(sl->getValue()));
		});
		if (item.restart != "none")
			checks.push_back({ [sl, orig]{ return (int)sl->getValue() != (int)orig; },
			                   item.restart });
	}
	else if (item.type == "submenu")
	{
		// item.id가 다른 FeatureMenu 블록의 id를 그대로 가리킴 - 그 블록은
		// parent를 6개 고정 카테고리가 아닌 값(관례상 자기 id)으로 둬서
		// addFeatureItemsTo()의 자동 flatten 대상이 안 되게 하고, 여기서만
		// 명시적으로 열림(2026-07-10, network_settings에서 처음 쓴 패턴을
		// 재사용 가능한 일반 기능으로 승격 - ticker_settings에도 적용).
		std::string targetId = item.id;
		addSubmenuEntry(s, _(item.label.c_str()), [this, targetId] { openFeatureMenu(targetId); });
	}
	else if (item.type == "command")
	{
		if (item.exec.empty()) return;
		// exec의 첫 번째 단어(바이너리)가 PATH에 존재하는지 확인
		std::string bin = item.exec.substr(0, item.exec.find(' '));
		if (!isBinAvailable(bin)) return;
		std::string exec = item.exec;
		std::string feedbackMsg = item.feedback_msg;
		ComponentListRow row;
		row.makeAcceptInputHandler([this, exec, feedbackMsg] {
			::system((exec + " &").c_str());
			if (!feedbackMsg.empty())
				mWindow->pushGui(new GuiMsgBox(mWindow, _(feedbackMsg.c_str())));
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

		// 2026-07-11: 확인 팝업 없이 바로 재시작/재부팅 - 이미 설정 메뉴에서
		// 명시적으로 값을 바꾼 뒤라 재차 확인받는 게 불필요한 절차로 느껴짐.
		// (예외: A/B 버튼 전환은 잘못 건드리기 쉬운 설정이라 openControllerSettings()에서
		// 별도로 확인+되돌리기 다이얼로그를 둠 - 여긴 그 케이스가 아님)
		if (actualRestart == "system")
			quitES(QuitMode::REBOOT);
		else
			quitES(QuitMode::RESTART);
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

	// 2026-07-11: 순서 재배치 - RETROACHIEVEMENTS 맨 위, SCRAPER/EMULATOR
	// SETTINGS는 맨 아래(자주 안 쓰는 설정이라는 판단, 사용자 요청).
	addSubmenuEntry(s, _("RETROACHIEVEMENTS"), [this] { openRetroAchievements(); });

	// YAML: 스무딩/정수 스케일(구 VIDEO SETTINGS) + 되감기/자동저장(구 GAME SETTINGS)
	addFeatureItemsTo(s, "game", *checks);

	// 백그라운드 인덱싱
	auto background_indexing = std::make_shared<SwitchComponent>(mWindow);
	background_indexing->setState(Settings::getInstance()->getBool("BackgroundIndexing"));
	s->addWithLabel(_("INDEX FILES DURING SCREENSAVER"), background_indexing);
	s->addSaveFunc([background_indexing] { Settings::getInstance()->setBool("BackgroundIndexing", background_indexing->getState()); });

	addSubmenuEntry(s, _("SCRAPER"), [this] { openScraperSettings(); });
	addSubmenuEntry(s, _("EMULATOR SETTINGS"), [this] { openEmulatorSettings(); });

	setSaveWithRestartChecks(s, checks);
	mWindow->pushGui(s);
}

// CONTROLLER SETTINGS — 입력 설정 + 버튼 방식 + YAML(드라이버/통합 컨트롤)
void GuiMenu::openControllerSettings()
{
	auto s = new GuiSettings(mWindow, _("CONTROLLER SETTINGS"));
	auto checks = std::make_shared<std::vector<RestartCheck>>();

	addSubmenuEntry(s, _("CONFIGURE INPUT"), [this] { openConfigInput(); });

	// 버튼 방식 — nintendo(A/B 전환)와 sony/xbox(전환 안 함, 코드상 완전히 동일)
	// 두 상태밖에 없어서 3개짜리 OptionList 대신 간단한 토글로 단순화
	auto button_ab_swap = std::make_shared<SwitchComponent>(mWindow);
	bool origButtonSwap = Settings::getInstance()->getString("ButtonLayout") == "nintendo";
	button_ab_swap->setState(origButtonSwap);
	s->addWithLabel(_("SWAP BUTTONS A/B"), button_ab_swap);
	// 2026-07-11: 잘못 건드리기 쉬운 설정이라 다른 토글과 달리 별도로
	// 확인+되돌리기 처리 - "아니오"를 누르면 값 자체를 안 바꾸고 그대로 둠
	// (재시작 안 함은 물론, Settings::ButtonLayout도 원래 값 유지).
	s->addSaveFunc([this, button_ab_swap, origButtonSwap] {
		bool newState = button_ab_swap->getState();
		if (newState == origButtonSwap)
			return;
		mWindow->pushGui(new GuiMsgBox(mWindow,
			_("ES 재시작이 필요합니다.\n지금 재시작하시겠습니까?"),
			_("OK"), [newState] {
				Settings::getInstance()->setString("ButtonLayout", newState ? "nintendo" : "xbox");
				// 2026-07-11: saveFile() 누락 버그 - 메모리에서만 값이 바뀌고
				// es_settings.cfg에 저장이 안 돼서, ES가 재시작되면 새 프로세스가
				// 예전 값을 다시 읽어와 설정이 "안 먹히는" 것처럼 보였음(실기기 확인).
				Settings::getInstance()->saveFile();
				InputConfig::initActionMapping();
				quitES(QuitMode::RESTART);
			},
			_("CANCEL"), nullptr
		));
	});

	// YAML: 조이패드 드라이버 / 통합 컨트롤 설정 (구 ADVANCED SETTINGS)
	addFeatureItemsTo(s, "controller", *checks);

	// 실시간 스캔 목록 표시가 필요해 YAML로 표현 불가 (BLUETOOTH DEVICES와 동일 이유)
	addSubmenuEntry(s, _("PAIR A BLUETOOTH CONTROLLER"), [this] {
		mWindow->pushGui(new GuiBtPairing(mWindow, "input-gaming", "scan-start-pad"));
	});

	// 2026-07-11: "BLUETOOTH DEVICES"(페어링된 기기 목록) 제거 - 사용자
	// 판단으로 불필요. 페어링/전체 해제는 아래 두 항목으로 충분.

	// 목록에서 골라 지우는 게 아니라 전체 초기화 — 확인 팝업이 필요해 YAML로 표현 불가
	addSubmenuEntry(s, _("REMOVE ALL BLUETOOTH PAIRINGS"), [this] {
		mWindow->pushGui(new GuiMsgBox(mWindow, _("REMOVE ALL BLUETOOTH PAIRINGS?"),
			_("YES"), [] { removeAllBtPairings(); },
			"아니오", nullptr));
	});

	// 2026-07-11: PLAYER 1~4 CONTROLLER - 지금 연결된 패드 중 어느 걸 어느
	// 플레이어 슬롯으로 쓸지 지정. RetroArch의 input_playerN_joypad_index를
	// 직접 써서 실제 패드/RA 양쪽에 그대로 적용됨(실시간 연결 목록이라
	// YAML로 표현 불가). SDL 장치 인덱스는 재부팅/재연결 시 바뀔 수 있는
	// RetroArch 자체의 한계라 이 프로젝트에서 더 견고하게 만들 방법은 없음.
	for (int p = 1; p <= 4; p++)
	{
		std::string confKey = "input_player" + std::to_string(p) + "_joypad_index";
		std::string origIdxStr = cfgReadKey(raCfgPath(), confKey, "");
		int origIdx = -1;
		if (!origIdxStr.empty()) { try { origIdx = std::stoi(origIdxStr); } catch (...) {} }

		std::string rowLabel = "PLAYER " + std::to_string(p) + " CONTROLLER";
		auto padList = std::make_shared< OptionListComponent<std::string> >(mWindow, rowLabel, false);

		padList->add("None", "-1", origIdx < 0);
		int numJoy = SDL_NumJoysticks();
		bool anySel = (origIdx < 0);
		for (int j = 0; j < numJoy; j++)
		{
			const char* name = SDL_JoystickNameForIndex(j);
			std::string label = (name ? std::string(name) : ("Joystick " + std::to_string(j)))
				+ " (#" + std::to_string(j) + ")";
			bool sel = (j == origIdx);
			if (sel) anySel = true;
			padList->add(label, std::to_string(j), sel);
		}
		if (!anySel)
			padList->add("None", "-1", true);

		s->addWithLabel(rowLabel, padList);
		s->addSaveFunc([padList, confKey, origIdxStr] {
			std::string newVal = padList->getSelected();
			if (newVal == origIdxStr || (origIdxStr.empty() && newVal == "-1"))
				return;
			cfgWriteKey(raCfgPath(), confKey, newVal, false);
		});
	}

	setSaveWithRestartChecks(s, checks);
	mWindow->pushGui(s);
}

// SYSTEM SETTINGS — 언어 + YAML(시간대/SSH) + 업데이트(준비 중) + 고급
void GuiMenu::openStorageSettings()
{
	mWindow->pushGui(new GuiStorageSelect(mWindow));
}

// NETWORK — SYSTEM SETTINGS 안의 서브메뉴. IP 표시(네이티브) + SSH/SAMBA/WIFI
// 토글(YAML network_settings, parent를 "network_settings"로 둬서 SYSTEM
// SETTINGS 최상위엔 안 풀리고 여기서만 명시적으로 끌어옴) + WiFi 스캔·연결
// 화면(실시간 데이터라 YAML로 표현 불가, 기존 GuiWifiSelect 재사용).
void GuiMenu::openNetworkSettings()
{
	auto s = new GuiSettings(mWindow, _("NETWORK"));
	auto checks = std::make_shared<std::vector<RestartCheck>>();

	auto ipText = std::make_shared<TextComponent>(mWindow, getCurrentIpAddressWithLink(),
		Font::get(FONT_SIZE_MEDIUM, FONT_PATH_LIGHT), 0x777777FF, ALIGN_RIGHT);
	s->addWithLabel(_("CURRENT IP ADDRESS"), ipText);

	addFeatureItemsTo(s, "network_settings", *checks);

	// 2026-07-11: WIFI 토글 바로 아래에 SSID/비밀번호를 미리 입력해둘 수
	// 있게 함 - SSID는 현재 연결된 걸 기본값으로 보여주되(없으면 "None")
	// 직접 타이핑도 가능. 비밀번호는 여기 미리 넣어두면 아래 "WIFI 네트워크
	// 설정"에서 목록을 골랐을 때 또 물어보지 않고 바로 이 값을 씀
	// (GuiWifiSelect::addSaveFunc 참고).
	{
		bool wConnected = false;
		std::string wSsid, wIp;
		GuiWifiSelect::readStatus(wConnected, wSsid, wIp);
		std::string ssidDefault = wConnected && !wSsid.empty() ? wSsid : "None";

		auto ssidCur = std::make_shared<std::string>(ssidDefault);
		auto ssidText = std::make_shared<TextComponent>(mWindow, ssidDefault,
			Font::get(FONT_SIZE_MEDIUM, FONT_PATH_LIGHT), 0x777777FF, ALIGN_RIGHT);
		ComponentListRow ssidRow;
		ssidRow.addElement(std::make_shared<TextComponent>(mWindow, _("SSID"),
			Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
		ssidRow.addElement(ssidText, false);
		Window* window = mWindow;
		ssidRow.makeAcceptInputHandler([window, ssidText, ssidCur] {
			window->pushGui(new GuiArcadeVirtualKeyboard(window, _("SSID"), *ssidCur,
				[ssidText, ssidCur](const std::string& v) {
					*ssidCur = v;
					ssidText->setValue(v);
				}));
		});
		s->addRow(ssidRow);
		s->addSaveFunc([ssidCur] {
			cfgWriteKey(rpConfPath(), "system.wifi_ssid", *ssidCur, false);
		});

		auto pwCur = std::make_shared<std::string>();
		auto pwText = std::make_shared<TextComponent>(mWindow, "",
			Font::get(FONT_SIZE_MEDIUM, FONT_PATH_LIGHT), 0x777777FF, ALIGN_RIGHT);
		ComponentListRow pwRow;
		pwRow.addElement(std::make_shared<TextComponent>(mWindow, _("SSID PASSWORD"),
			Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
		pwRow.addElement(pwText, false);
		pwRow.makeAcceptInputHandler([window, pwText, pwCur] {
			window->pushGui(new GuiArcadeVirtualKeyboard(window, _("SSID PASSWORD"), *pwCur,
				[pwText, pwCur](const std::string& v) {
					*pwCur = v;
					pwText->setValue(std::string(v.size(), '*'));
				}));
		});
		s->addRow(pwRow);
		s->addSaveFunc([pwCur] {
			// 빈 채로 그냥 나가면(수정 안 함) 기존 저장된 비밀번호를 지우지
			// 않음 - 실수로 이 화면만 열었다 나가도 이미 입력해둔 비밀번호가
			// 날아가지 않게.
			if (pwCur->empty()) return;
			cfgWriteKey(rpConfPath(), "system.wifi_password", *pwCur, false);
		});
	}

	addSubmenuEntry(s, _("SELECT WIFI NETWORK"), [this] { mWindow->pushGui(new GuiWifiSelect(mWindow)); });

	setSaveWithRestartChecks(s, checks);
	mWindow->pushGui(s);
}

void GuiMenu::openSystemSettings()
{
	auto s = new GuiSettings(mWindow, _("SYSTEM SETTINGS"));
	auto checks = std::make_shared<std::vector<RestartCheck>>();

	// 최상단: 현재 버전 표시(클릭 동작 없음, 정보 표시 전용)
	{
		std::string curVer;
		std::ifstream f("/etc/retropangui-version");
		if (f.good()) {
			std::getline(f, curVer);
			curVer.erase(curVer.find_last_not_of(" \t\r\n") + 1);
		}
		if (curVer.empty())
			curVer = "-";
		// 2026-07-11: FONT_SIZE_SMALL + 기본 폰트였어서 아래 다른 값
		// 텍스트(HOSTNAME 입력값, TIMEZONE 선택값 등 - 전부 FONT_SIZE_MEDIUM
		// + FONT_PATH_LIGHT)보다 작고 다른 서체로 눈에 띄게 달라 보이던 것을 통일.
		auto verText = std::make_shared<TextComponent>(mWindow, curVer, Font::get(FONT_SIZE_MEDIUM, FONT_PATH_LIGHT), 0x777777FF);
		s->addWithLabel(_("VERSION"), verText);
	}

	// 2026-07-11: HDMI 출력 해상도 - 잘못 고르면 화면이 아예 안 보이게 될
	// 수 있는 위험한 설정이라, 다른 목록형 설정과 달리 A/B 버튼전환과
	// 동일하게 확인+되돌리기 처리. "CANCEL"을 누르면 conf도 안 바꾸고
	// 재시작도 안 함(다음에 메뉴 들어가면 원래 값 그대로 보임).
	// restart는 항상 es로 충분 - S99emulationstation의 while 루프 안에
	// HDMI 모드 설정 코드가 있어서 ES 재시작만으로 그 루프가 다시 돌며
	// hdmi-set-resolution.py도 재실행됨(전체 리부팅 불필요).
	{
		std::string origRes = cfgReadKey(rpConfPath(), "system.hdmi_resolution", "auto");
		// 2026-07-11: "AUTO (모니터 네이티브)" 같은 고정 설명 대신, 지금
		// 실제로 켜져 있는 해상도(Renderer::getWindowWidth/Height - 물리
		// HDMI 출력 그 자체)를 그대로 찍어서 "AUTO(2560x1600)"처럼 보여줌.
		std::string autoLabel = "AUTO(" + std::to_string(Renderer::getWindowWidth())
			+ "x" + std::to_string(Renderer::getWindowHeight()) + ")";
		std::vector<std::pair<std::string, std::string>> resOptions = {
			{ autoLabel,            "auto" },
			{ "1920x1080 (16:9)",   "1920x1080p60hz" },
			{ "1280x720 (16:9)",    "1280x720p60hz" },
		};
		auto hdmi_res = std::make_shared< OptionListComponent<std::string> >(mWindow, _("OUTPUT RESOLUTION"), false);
		bool anySel = false;
		for (auto& opt : resOptions)
		{
			bool sel = (opt.second == origRes);
			if (sel) anySel = true;
			hdmi_res->add(opt.first, opt.second, sel);
		}
		if (!anySel)
			hdmi_res->add(resOptions[0].first, resOptions[0].second, true);
		s->addWithLabel(_("OUTPUT RESOLUTION"), hdmi_res);
		s->addSaveFunc([this, hdmi_res, origRes] {
			std::string newVal = hdmi_res->getSelected();
			if (newVal == origRes)
				return;
			mWindow->pushGui(new GuiMsgBox(mWindow,
				_("ES 재시작이 필요합니다.\n지금 재시작하시겠습니까?"),
				_("OK"), [newVal] {
					cfgWriteKey(rpConfPath(), "system.hdmi_resolution", newVal, false);
					quitES(QuitMode::RESTART);
				},
				_("CANCEL"), nullptr
			));
		});
	}

	// YAML: 시간대 등 system 최상위 항목
	addFeatureItemsTo(s, "system", *checks);

	// NETWORK 서브메뉴 — SSH/SAMBA/WIFI 토글 + IP + WiFi 선택을 한 화면으로 묶음.
	// 2026-07-10: 예전엔 이 항목들이 SYSTEM SETTINGS에 flat하게 다 풀려있었음
	// ("구 NETWORK SETTINGS" 주석은 "예전엔 진짜 서브메뉴였다가 flat으로
	// 합쳐졌다"는 뜻이었는데, 문서에서 반대로("이미 서브메뉴로 존재") 오독됐던
	// 걸 재확인 과정에서 발견 - todo-20260704-wifi-menu-polish.html 참고.
	addSubmenuEntry(s, _("NETWORK"), [this] { openNetworkSettings(); });

	// power saver
	// 2026-07-11: 예전엔 "disabled/default/enhanced/instant" 영어 단어를
	// 번역도 없이 그대로 보여줘서 뭘 하는 옵션인지 전혀 알 수 없었음.
	// PowerSaver.cpp 기준 실제 동작: 유휴 상태에서 화면을 얼마나 뜸하게
	// 다시 그릴지(ms) 정하는 값이 커질수록 절전 효과가 큼(disabled=-1은
	// 그 기능 자체를 끔 = 절전 없음, instant=200ms는 대기 간격은 가장
	// 짧지만 대신 전환 애니메이션/캐러셀 이동/효과음을 아예 꺼서(아래
	// addSaveFunc) 렌더링 자체를 줄이는 방식이라 종합적으로는 가장
	// 공격적인 절전 모드).
	struct PowerSaverOption { const char* value; const char* label; };
	static const PowerSaverOption psOptions[] = {
		{ "disabled", "사용 안 함 (절전 없음, 항상 즉시 반응)" },
		{ "default",  "기본 (평소 정도 절전, 유휴 10초마다 갱신)" },
		{ "enhanced", "절전 강화 (유휴 3초마다 갱신, 배터리 더 아낌)" },
		{ "instant",  "최대 절전 (전환 애니메이션/효과음 꺼짐, 반응은 즉시)" },
	};
	auto power_saver = std::make_shared< OptionListComponent<std::string> >(mWindow, _("POWER SAVER MODES"), false);
	for (auto& opt : psOptions)
		power_saver->add(opt.label, opt.value, Settings::getInstance()->getString("PowerSaverMode") == opt.value);
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
						// 재부팅 여부를 묻지 않고 바로 재부팅
						::system("reboot");
					} else {
						mWindow->pushGui(new GuiMsgBox(mWindow,
							"USB 업데이트 실패.\n파일을 확인하세요.",
							"확인", nullptr));
					}
				};
				std::string installMsg = "USB 업데이트 설치 중...\n\n설치가 완료되면 바로 재부팅됩니다.\n기기의 전원을 끄지 마세요.";
				mWindow->pushGui(new GuiOtaDownload(mWindow, install_fn, done_fn, installMsg));
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
						// 재부팅 여부를 묻지 않고 바로 재부팅 — 다운로드 중 메시지에
						// 이미 "완료되면 바로 재부팅됩니다"라고 안내했으므로 확인 절차 불필요
						::system("reboot");
					} else {
						mWindow->pushGui(new GuiMsgBox(mWindow,
							"업데이트 실패.\n다운로드 또는 검증 오류입니다.",
							"확인", nullptr));
					}
				};
				std::string downloadMsg = "새 버전 다운로드 중: " + serverVer +
					"\n\n다운로드가 완료되면 바로 재부팅됩니다.\n기기의 전원을 끄지 마세요.";
				mWindow->pushGui(new GuiOtaDownload(mWindow, download_fn, done_fn, downloadMsg));
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
			"DEFAULT EMULATOR", false);

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
