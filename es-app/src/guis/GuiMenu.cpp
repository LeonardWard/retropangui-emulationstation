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
#include "guis/GuiBiosCheck.h"
#include "guis/GuiGamelistRefresh.h"
#include "guis/GuiGeneralScreensaverOptions.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiScraperStart.h"
#include "guis/GuiSettings.h"
#include "views/UIModeController.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "EmulationStation.h"
#include "InputConfig.h"
#include "InputManager.h"
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

// forward declarations — 정의는 파일 하단 공개 헬퍼 블록에 있음
static std::string rpConfPath();
static std::string cfgReadKey(const std::string& filePath, const std::string& fullKey,
                              const std::string& def);
static void cfgWriteKey(const std::string& filePath, const std::string& fullKey,
                        const std::string& value, bool quote);
GuiMenu::GuiMenu(Window* window) : GuiComponent(window), mMenu(window, _("MAIN MENU")), mVersion(window)
{
	bool isFullUI = UIModeController::getInstance()->isUIModeFull();

	if (isFullUI) {
		// RetroPangui: 메인 8개 골격 — 세부 항목은 각 카테고리 서브메뉴로 통합
		// (RETROACHIEVEMENTS/EMULATOR→GAME, CONFIGURE INPUT→CONTROLLER,
		//  COLLECTION→UI, UPDATES/OTHER→SYSTEM. YAML 메뉴 엔진은 제거되고 각
		//  항목은 네이티브로 흡수됨)
		addEntry(_("KODI MEDIA CENTER"),    0x777777FF, true, [this] { openKodiMediaCenter(); });
		addEntry(_("GAME SETTINGS"),        0x777777FF, true, [this] { openGameSettings(); });
		addEntry(_("CONTROLLER SETTINGS"),  0x777777FF, true, [this] { openControllerSettings(); });
		addEntry(_("UI SETTINGS"),          0x777777FF, true, [this] { openUISettings(); });
		addEntry(_("SOUND SETTINGS"),       0x777777FF, true, [this] { openSoundSettings(); });
		addEntry(_("SYSTEM SETTINGS"),      0x777777FF, true, [this] { openSystemSettings(); });
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

	// 2026-07-18 PulseAudio 전환에 맞춰 재구현(todo-20260716-pulseaudio-migration.html
	// 마지막 후속 항목) - 예전엔 /proc/asound/cards의 raw ALSA 하드웨어를 스캔해서
	// hw:CARD=... 문자열로 직접 여는 방식이었는데, 지금은 라우팅/믹싱을 PA가
	// 전담하므로(ctl.!default가 type pulse) 그 경로는 PA를 우회해 죽은 선택지였음
	// (블루투스 bluealsa: 문자열도 마찬가지 - bluealsa 데몬이 이제 no-op).
	// PA가 실제로 아는 sink 목록(pactl list sinks)을 그대로 보여주고, 선택 시
	// pactl set-default-sink 한 줄로 전환 - 재생 중인 스트림까지 즉시 옮겨간다.
	// 블루투스 스피커는 연결되면 PA가 자동으로 sink를 만들어주므로 별도 스캔
	// 코드(discovery.json 조회) 불필요 - 목록에 자연히 나타남.
	// VolumeControl은 그대로 둬도 됨: AudioCard 설정을 안 건드리므로 항상
	// "default"→ctl pulse Master로 남고, 그게 PA의 현재 default sink 볼륨에
	// 자동으로 매핑됨(재초기화 불필요). 선택 상태 자체는 PA의
	// module-default-device-restore가 재부팅 간 영속화하므로 ES 쪽에 별도
	// 저장이 필요 없음 - 매번 pactl get-default-sink로 그때그때 조회.
	{
		std::vector<std::pair<std::string, std::string>> sinks; // (label, sinkName)
		FILE* p = popen("pactl list sinks 2>/dev/null", "r");
		if (p)
		{
			char buf[512];
			std::string curName, curDesc;
			auto flush = [&]() {
				if (!curName.empty())
					sinks.push_back({ curDesc.empty() ? curName : curDesc, curName });
				curName.clear(); curDesc.clear();
			};
			while (fgets(buf, sizeof(buf), p))
			{
				std::string line(buf);
				if (line.rfind("Sink #", 0) == 0) { flush(); continue; }
				auto trimEol = [](std::string& s) {
					while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
				};
				size_t nm = line.find("\tName: ");
				if (nm != std::string::npos) { curName = line.substr(nm + 7); trimEol(curName); }
				size_t ds = line.find("\tDescription: ");
				if (ds != std::string::npos) { curDesc = line.substr(ds + 14); trimEol(curDesc); }
			}
			flush();
			pclose(p);
		}

		std::string curDefault;
		{
			FILE* pd = popen("pactl get-default-sink 2>/dev/null", "r");
			if (pd)
			{
				char buf[256];
				if (fgets(buf, sizeof(buf), pd)) { curDefault = buf; while (!curDefault.empty() && (curDefault.back() == '\n' || curDefault.back() == '\r')) curDefault.pop_back(); }
				pclose(pd);
			}
		}

		auto audio_card = std::make_shared< OptionListComponent<std::string> >(mWindow, _("AUDIO CARD"), false);
		for (auto& sk : sinks)
			audio_card->add(sk.first, sk.second, sk.second == curDefault);
		s->addWithLabel(_("AUDIO CARD"), audio_card);
		s->addSaveFunc([audio_card, curDefault] {
			std::string newVal = audio_card->getSelected();
			if (newVal.empty() || newVal == curDefault) return;
			std::string cmd = "pactl set-default-sink '" + newVal + "'";
			system(cmd.c_str());
			// 재생 중이던 스트림(BGM 등)도 새 default로 옮겨줌 - set-default-sink는
			// 이후 새로 열리는 스트림에만 적용되고 기존 스트림은 안 옮기므로 명시 필요
			std::string moveCmd = "for i in $(pactl list sink-inputs short | awk '{print $1}'); do "
				"pactl move-sink-input \"$i\" '" + newVal + "' 2>/dev/null; done";
			system(moveCmd.c_str());
		});
	}

	// 2026-07-11: MIDI HARDWARE OUTPUT - 배경음악 MIDI를 소프트 신디사이저
	// 대신 실제 연결된 MIDI 하드웨어(MT-32 에뮬레이터 등)로 직접 보냄.
	// aplaymidi -l로 지금 붙어있는 시퀀서 포트를 실시간 스캔(YAML 정적
	// 목록으로 표현 불가). "None"이면 기존 libvlc/fluidsynth 경로 그대로.
	{
		std::vector<std::pair<std::string, std::string>> ports; // (label, "client:port")
		FILE* p = popen("aplaymidi -l 2>/dev/null", "r");
		if (p)
		{
			char buf[256];
			while (fgets(buf, sizeof(buf), p))
			{
				std::string line(buf);
				size_t colon = line.find(':');
				if (colon == std::string::npos || colon == 0) continue;
				// aplaymidi -l은 클라이언트 번호를 3칸 오른쪽 정렬로 찍어서
				// 두 자리 이하 번호(예: " 16:0")는 앞에 공백이 붙는다 -
				// 콜론 앞부분을 곧바로 숫자 판정하면 이런 줄이 전부 헤더로
				// 오인돼 걸러짐(2026-07-14 실기기에서 확인 - 클라이언트
				// 번호가 3자리인 장치만 목록에 남고 나머지는 사라짐).
				// 앞쪽 공백은 건너뛰고 실제 숫자 구간만 검사.
				size_t numStart = line.find_first_not_of(" \t");
				bool numeric = (numStart != std::string::npos && numStart < colon);
				for (size_t i = numStart; numeric && i < colon; i++)
					if (!isdigit((unsigned char)line[i])) { numeric = false; break; }
				if (!numeric) continue; // 헤더 줄("Port  Client name  ...") 건너뜀

				size_t sp = line.find_first_of(" \t", colon);
				if (sp == std::string::npos) continue;
				std::string port = line.substr(0, sp);
				std::string label = line.substr(sp);
				// 앞뒤 공백 정리
				size_t b = label.find_first_not_of(" \t");
				size_t e = label.find_last_not_of(" \t\r\n");
				label = (b == std::string::npos) ? port : label.substr(b, e - b + 1) + " (" + port + ")";
				ports.push_back({ label, port });
			}
			pclose(p);
		}

		std::string origMidi = Settings::getInstance()->getString("MidiHardwareDevice");
		auto midi_hw = std::make_shared< OptionListComponent<std::string> >(mWindow, _("MIDI HARDWARE OUTPUT"), false);
		bool anyMidiSel = (origMidi.empty());
		midi_hw->add("None", "", origMidi.empty());
		for (auto& port : ports)
		{
			bool sel = (port.second == origMidi);
			if (sel) anyMidiSel = true;
			midi_hw->add(port.first, port.second, sel);
		}
		if (!anyMidiSel)
			midi_hw->add("None", "", true);
		s->addWithLabel(_("MIDI HARDWARE OUTPUT"), midi_hw);
		s->addSaveFunc([midi_hw, origMidi] {
			std::string newVal = midi_hw->getSelected();
			if (newVal == origMidi) return;
			Settings::getInstance()->setString("MidiHardwareDevice", newVal);
			// 2026-07-11: saveFile() 누락 버그(A/B 전환 때 겪은 것과 동일) 재발 방지 -
			// 이 값은 retropangui.conf를 거치지 않는 순수 ES Settings라 여기서 직접 저장.
			Settings::getInstance()->saveFile();
		});
	}

	// YAML→네이티브 이관(audio_settings): rp.bgm — BACKGROUND MUSIC
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.BackgroundMusic", "true");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto bgm_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("BACKGROUND MUSIC"), bgm_sw);

		// BackgroundMusic: toggle 변경 즉시 conf + 메모리 반영 (메뉴를 닫기 전에도 적용).
		// addSaveFunc 는 BACK으로 닫을 때만 실행되므로, 비정상 종료나 다른 경로 재시작 대비
		// setChangedCallback 에서도 conf에 즉시 기록한다.
		bgm_sw->setChangedCallback([](bool val) {
			Settings::getInstance()->setBool("BackgroundMusic", val);
			cfgWriteKey(rpConfPath(), "emulationstation.BackgroundMusic", val ? "true" : "false", false);
			if (val) MusicManager::getInstance()->start();
			else     MusicManager::getInstance()->stop();
		});

		s->addSaveFunc([bgm_sw] {
			bool newVal = bgm_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.BackgroundMusic", newVal ? "true" : "false", false);
			Settings::getInstance()->setBool("BackgroundMusic", newVal);
			if (newVal) MusicManager::getInstance()->start();
			else MusicManager::getInstance()->stop();
		});
		// restart: none
	}

	// YAML→네이티브 이관(audio_settings): rp.volume — SYSTEM VOLUME
	{
		std::string raw = cfgReadKey(rpConfPath(), "system.volume");
		float orig = 0.f;
		if (!raw.empty()) { try { orig = std::stof(raw); } catch (...) {} }
		auto vol_sl = std::make_shared<SliderComponent>(mWindow, 0.f, 100.f, 1.f, "%");
		vol_sl->setValue(orig);

		// 슬라이더는 좌우 입력을 누르고 있으면 40ms(MOVE_REPEAT_RATE)마다 setValue()를
		// 호출해 이 콜백을 매번 실행함 — VolumeControl::setVolume()은 ALSA 믹서 호출
		// (snd_mixer_selem_*)이라 매번 정확히 얼마나 걸릴지 예측 불가. 백그라운드 음악
		// 재생 중 등 조건에 따라 지연되면, 메인 스레드가 그동안 막혀서 큐에 쌓인 입력
		// 이벤트가 한꺼번에 재생되어 "커서가 미끄러지듯 계속 이동"하는 것처럼 보임
		// (2026-07-05, 다른 슬라이더(VRAM 제한)는 이런 콜백 자체가 없어서 재현 안 됨 —
		// 볼륨 슬라이더 전용 문제로 확인). 실제 ALSA 반영 빈도를 제한(스로틀)해서
		// 연타로 인한 블로킹 누적을 방지.
		auto lastCallTick = std::make_shared<Uint32>(0);
		vol_sl->setChangedCallback([lastCallTick](float val) {
			Uint32 now = SDL_GetTicks();
			if (now - *lastCallTick < 80) return;
			*lastCallTick = now;
			VolumeControl::getInstance()->setVolume((int)Math::round(val));
		});

		s->addWithLabel(_("SYSTEM VOLUME"), vol_sl);
		s->addSaveFunc([vol_sl] {
			cfgWriteKey(rpConfPath(), "system.volume", std::to_string((int)vol_sl->getValue()), false);
			// 스로틀 때문에 조작 중 마지막 값이 ALSA에 반영 안 됐을 수 있어 나갈 때 최종 동기화
			VolumeControl::getInstance()->setVolume((int)Math::round(vol_sl->getValue()));
		});
		// restart: none
	}

	// YAML→네이티브 이관(audio_settings): rp.enable_sounds — ENABLE NAVIGATION SOUNDS
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.EnableSounds", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto snd_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("ENABLE NAVIGATION SOUNDS"), snd_sw);

		s->addSaveFunc([snd_sw] {
			bool newVal = snd_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.EnableSounds", newVal ? "true" : "false", false);

			// PowerSaver 전환 판단은 이번에 갱신하기 전의 "이전" 메모리값이 필요하므로,
			// 아래 Settings 동기화보다 먼저 확인한다.
			if (newVal && !Settings::getInstance()->getBool("EnableSounds")
			    && PowerSaver::getMode() == PowerSaver::INSTANT) {
				Settings::getInstance()->setString("PowerSaverMode", "default");
				PowerSaver::init();
			}

			// emulationstation.* 토글은 항상 Settings 메모리도 같이 갱신 - 누락되면
			// 같은 세션 안에서 반영이 안 되고 재부팅해야만 적용되는 회귀가 생김
			// (SaveStatePreview 사례로 실기기에서 확인됨).
			Settings::getInstance()->setBool("EnableSounds", newVal);
		});
		// restart: none
	}

	// YAML→네이티브 이관(audio_settings): rp.video_audio — ENABLE VIDEO AUDIO
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.VideoAudio", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto va_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("ENABLE VIDEO AUDIO"), va_sw);
		s->addSaveFunc([va_sw] {
			bool newVal = va_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.VideoAudio", newVal ? "true" : "false", false);
			Settings::getInstance()->setBool("VideoAudio", newVal);
		});
		// restart: none
	}

	// YAML→네이티브 이관(audio_settings): rp.audio_latency — RA AUDIO LATENCY
	{
		std::string raw = cfgReadKey(rpConfPath(), "global.audio_latency");
		float orig = 16.f;
		if (!raw.empty()) { try { orig = std::stof(raw); } catch (...) {} }
		auto lat_sl = std::make_shared<SliderComponent>(mWindow, 16.f, 256.f, 8.f, "ms");
		lat_sl->setValue(orig);
		s->addWithLabel(_("RA AUDIO LATENCY"), lat_sl);
		s->addSaveFunc([lat_sl] {
			cfgWriteKey(rpConfPath(), "global.audio_latency", std::to_string((int)lat_sl->getValue()), false);
		});
		// restart: none
	}

	// 실시간 스캔 목록 표시가 필요해 YAML로 표현 불가 (BLUETOOTH DEVICES와 동일 이유)
	addSubmenuEntry(s, _("PAIR A BLUETOOTH AUDIO DEVICE"), [this] {
		mWindow->pushGui(new GuiBtPairing(mWindow, "audio-", "scan-start-audio"));
	});

	// 2026-07-11: "BLUETOOTH DEVICES"(페어링된 기기 목록) 제거 - CONTROLLER
	// SETTINGS와 동일하게 사용자 판단으로 불필요.

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

	// YAML→네이티브 이관(ui_settings): rp.language — LANGUAGE
	// 2026-07-21: system.language → emulationstation.Language로 개명. 예전엔
	// apply_retropangui_conf.sh(첫 부팅/키 병합 시에만, 또는 ES 설정 메뉴 저장
	// 이벤트로만 트리거)가 es_settings.cfg에 비동기로 동기화해야만 ES가 이 값을
	// 읽었음 - 그 사이 시점에 ES가 먼저 뜨면(예: 공장 초기화 직후 몇 차례 재부팅)
	// es_settings.cfg가 stale해서 영어로 뜨는 레이스가 있었음(사용자가 2번 겪음).
	// 다른 emulationstation.* 항목들과 동일한 이름으로 바꿔서
	// Settings::loadRetropanguiConf()가 "매 ES 시작마다" 직접 읽도록 함 - 비동기
	// 동기화 의존성 제거.
	{
		std::string confVal = cfgReadKey(rpConfPath(), "emulationstation.Language");
		auto lang_list = std::make_shared<OptionListComponent<std::string>>(
			mWindow, _("LANGUAGE"), false);
		struct { const char* value; const char* label; } langOptions[] = {
			{ "en_US", "English (US)" },
			{ "ko_KR", "한국어 (Korean)" },
			{ "ja_JP", "日本語 (Japanese)" },
			{ "zh_CN", "中文简体 (Chinese)" },
		};
		bool anySelected = false;
		bool isFirst = true;
		for (auto& opt : langOptions) {
			bool sel = (std::string(opt.value) == confVal) || (isFirst && confVal.empty());
			isFirst = false;
			if (sel) anySelected = true;
			lang_list->add(opt.label, opt.value, sel);
		}
		if (!anySelected)
			lang_list->add(langOptions[0].label, langOptions[0].value, true);
		std::string effectiveOrig = lang_list->getSelected();
		s->addWithLabel(_("LANGUAGE"), lang_list);
		s->addSaveFunc([lang_list, effectiveOrig] {
			std::string newVal = lang_list->getSelected();
			cfgWriteKey(rpConfPath(), "emulationstation.Language", newVal, false);
			// emulationstation.* 항목은 항상 Settings 메모리도 같이 갱신 - 누락되면
			// 같은 세션 안에서 반영이 안 되고 재부팅해야만 적용되는 회귀가 생김
			// (SaveStatePreview 사례로 실기기에서 확인됨).
			Settings::getInstance()->setString("Language", newVal);
		});
		checks->push_back({ [lang_list, effectiveOrig]{ return lang_list->getSelected() != effectiveOrig; }, "es" });
	}

	// 2026-07-06: 아래 7개는 GuiMenu.cpp에 하드코딩돼 있던 단순 토글/리스트를 YAML로
	// 옮겼다가(이제 다시 네이티브로) - conf_key는 Settings:: 키 이름과 정확히 일치해야
	// emulationstation.* 브릿지(Settings::loadRetropanguiConf(), apply_retropangui_conf.sh)가
	// 작동함.
	// 2026-07-11: 아래 둘은 C++ 시절 실제 기본값이 true였는데(Settings.cpp
	// QuickSystemSelect/ShowHelpPrompts) YAML 이관 때 default: 필드를 안 넣어서
	// fallback("false")을 그대로 타고 있었음 - conf 파일에 이 키가 아직 없는 기기에서
	// UI SETTINGS를 한 번이라도 열었다 닫으면 조용히 false로 저장돼버려서, 특히
	// ShowHelpPrompts는 하단 도움말 표시줄이 통째로 사라지는 버그로 이어졌음
	// (todo-20260710-helpbar-missing.html). 기본값 "true"를 반드시 유지할 것.
	// YAML→네이티브 이관(ui_settings): rp.quick_system_select — QUICK SYSTEM SELECT
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.QuickSystemSelect", "true");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto qss_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("QUICK SYSTEM SELECT"), qss_sw);
		s->addSaveFunc([qss_sw] {
			bool newVal = qss_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.QuickSystemSelect", newVal ? "true" : "false", false);
			Settings::getInstance()->setBool("QuickSystemSelect", newVal);
		});
		// restart: none
	}

	// YAML→네이티브 이관(ui_settings): rp.show_help_prompts — ON-SCREEN HELP
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.ShowHelpPrompts", "true");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto shp_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("ON-SCREEN HELP"), shp_sw);
		s->addSaveFunc([shp_sw] {
			bool newVal = shp_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.ShowHelpPrompts", newVal ? "true" : "false", false);
			Settings::getInstance()->setBool("ShowHelpPrompts", newVal);
		});
		// restart: none
	}

	// YAML→네이티브 이관(ui_settings): rp.show_hidden_files — SHOW HIDDEN FILES
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.ShowHiddenFiles", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto shf_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("SHOW HIDDEN FILES"), shf_sw);
		s->addSaveFunc([shf_sw] {
			bool newVal = shf_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.ShowHiddenFiles", newVal ? "true" : "false", false);
			Settings::getInstance()->setBool("ShowHiddenFiles", newVal);
		});
		// restart: none
	}

	// YAML→네이티브 이관(ui_settings): rp.use_fullscreen_paging — USE FULL SCREEN PAGING FOR LB/RB
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.UseFullscreenPaging", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto ufp_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("USE FULL SCREEN PAGING FOR LB/RB"), ufp_sw);
		s->addSaveFunc([ufp_sw] {
			bool newVal = ufp_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.UseFullscreenPaging", newVal ? "true" : "false", false);
			Settings::getInstance()->setBool("UseFullscreenPaging", newVal);
		});
		// restart: none
	}

	// YAML→네이티브 이관(ui_settings): rp.save_gamelists_mode — SAVE METADATA
	{
		std::string confVal = cfgReadKey(rpConfPath(), "emulationstation.SaveGamelistsMode");
		auto sgm_list = std::make_shared<OptionListComponent<std::string>>(
			mWindow, _("SAVE METADATA"), false);
		const char* sgmOptions[] = { "on exit", "always", "never" };
		bool anySelected = false;
		bool isFirst = true;
		for (auto& opt : sgmOptions) {
			bool sel = (std::string(opt) == confVal) || (isFirst && confVal.empty());
			isFirst = false;
			if (sel) anySelected = true;
			sgm_list->add(opt, opt, sel);
		}
		if (!anySelected)
			sgm_list->add(sgmOptions[0], sgmOptions[0], true);
		s->addWithLabel(_("SAVE METADATA"), sgm_list);
		s->addSaveFunc([sgm_list] {
			std::string newVal = sgm_list->getSelected();
			cfgWriteKey(rpConfPath(), "emulationstation.SaveGamelistsMode", newVal, false);
			Settings::getInstance()->setString("SaveGamelistsMode", newVal);
		});
		// restart: none
	}

	// YAML→네이티브 이관(ui_settings): rp.parse_gamelist_only — PARSE GAMESLISTS ONLY
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.ParseGamelistOnly", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto pgo_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("PARSE GAMESLISTS ONLY"), pgo_sw);
		s->addSaveFunc([pgo_sw] {
			bool newVal = pgo_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.ParseGamelistOnly", newVal ? "true" : "false", false);
			Settings::getInstance()->setBool("ParseGamelistOnly", newVal);
		});
		// restart: none
	}

	// YAML→네이티브 이관(ui_settings): rp.local_art — SEARCH FOR LOCAL ART
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.LocalArt", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto la_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("SEARCH FOR LOCAL ART"), la_sw);
		s->addSaveFunc([la_sw] {
			bool newVal = la_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.LocalArt", newVal ? "true" : "false", false);
			Settings::getInstance()->setBool("LocalArt", newVal);
		});
		// restart: none
	}

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

	// DEBUG MODE - retropangui.conf의 system.debug. RetroArch(rpui-launcher.py
	// debug_enabled())/Kodi(S99emulationstation DEBUG_FLAG)는 실행할 때마다
	// 이 값을 직접 읽어서 --verbose/--debug로 반영하므로 재시작 없이도 다음
	// 실행부터 바로 적용되지만, ES 자신의 로그 레벨은 --debug 플래그로 기동
	// 시점에만 정해지므로 반영하려면 ES 재시작이 필요 - SHOW BUNDLED GAMES와
	// 동일한 재시작 확인 패턴을 그대로 씀.
	{
		auto debug_mode = std::make_shared<SwitchComponent>(mWindow);
		bool origDebug = cfgReadKey(rpConfPath(), "system.debug", "false") != "false";
		debug_mode->setState(origDebug);
		s->addWithLabel(_("DEBUG MODE"), debug_mode);
		s->addSaveFunc([this, debug_mode, origDebug] {
			bool newState = debug_mode->getState();
			if (newState == origDebug) return;
			mWindow->pushGui(new GuiMsgBox(mWindow,
				_("ES 재시작이 필요합니다.\n지금 재시작하시겠습니까?"),
				_("OK"), [newState] {
					cfgWriteKey(rpConfPath(), "system.debug", newState ? "true" : "false", false);
					quitES(QuitMode::RESTART);
				},
				_("CANCEL"), [debug_mode, origDebug] {
					debug_mode->setState(origDebug);
				}
			));
		});
	}

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

// 2026-07-12: OUTPUT RESOLUTION 목록에서 각 해상도가 16:9/16:10/4:3 중
// 뭔지 바로 알아보게 라벨을 붙임 - 표준 비율에 충분히 가까우면(1% 이내)
// 그 이름을, 아니면 계산된 비율 숫자를 그대로 보여줌.
static std::string aspectLabel(long w, long h)
{
	if (w <= 0 || h <= 0) return "";
	double ratio = (double)w / (double)h;
	// 2026-07-12: 울트라와이드 모니터 대응. 21:9는 실제로는 정확히
	// 21:9(2.333)가 아니라 패널마다 2.37~2.40 근처로 제각각이라(2560x1080,
	// 3440x1440, 3840x1600 등) 아래 표준 목록의 1% 오차보다 넓게(6%) 따로 봄.
	if (std::abs(ratio - 21.0 / 9.0) / (21.0 / 9.0) < 0.06)
		return "21:9";
	struct { double r; const char* name; } known[] = {
		{ 16.0 / 9.0,  "16:9" },
		{ 16.0 / 10.0, "16:10" },
		{ 4.0 / 3.0,   "4:3" },
		{ 32.0 / 9.0,  "32:9" },
	};
	for (auto& k : known)
		if (std::abs(ratio - k.r) / k.r < 0.01)
			return k.name;
	char buf[32];
	snprintf(buf, sizeof(buf), "%.2f:1", ratio);
	return buf;
}

// retropangui.conf 에 쓰고, retroarch.cfg 에도 즉시 반영 (부팅 대기 없이 효과)
static void raCfgSet(const std::string& key, const std::string& value)
{
	cfgWriteKey(rpConfPath(), "global." + key, value, false); // retropangui.conf: 따옴표 없음
	cfgWriteKey(raCfgPath(),  key,             value, true);  // retroarch.cfg:    따옴표 있음
}

// ---------------------------------------------------------------------------
// restart 레벨 병합 헬퍼 (setSaveWithRestartChecks에서 사용)
// ---------------------------------------------------------------------------

static std::string strongerRestart(const std::string& a, const std::string& b)
{
	if (a == "system" || b == "system") return "system";
	if (a == "es"     || b == "es")     return "es";
	return "none";
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

// GAME SETTINGS — 에뮬레이터(코어) 선택 + 비디오/게임 옵션(네이티브) + RetroAchievements
void GuiMenu::openGameSettings()
{
	auto s = new GuiSettings(mWindow, _("GAME SETTINGS"));
	auto checks = std::make_shared<std::vector<RestartCheck>>();

	// 2026-07-11: 순서 재배치 - RETROACHIEVEMENTS 맨 위, SCRAPER/EMULATOR
	// SETTINGS는 맨 아래(자주 안 쓰는 설정이라는 판단, 사용자 요청).
	addSubmenuEntry(s, _("RETROACHIEVEMENTS"), [this] { openRetroAchievements(); });

	// 2026-07-10: video_settings/bundlegame_settings/game_settings 3블록을 하나로
	// 통합했던 YAML을 그대로 네이티브로 이관 - 아이템 순서는 원래 파일 순서
	// (video_smooth/video_scale_integer → video_driver 이하) 그대로 유지.
	// YAML→네이티브 이관(game_settings): rp.video_smooth — SMOOTH SCALING
	{
		std::string orig = cfgReadKey(rpConfPath(), "global.video_smooth", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto vs_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("SMOOTH SCALING"), vs_sw);
		s->addSaveFunc([vs_sw] {
			cfgWriteKey(rpConfPath(), "global.video_smooth", vs_sw->getState() ? "true" : "false", false);
		});
		// restart: none
	}

	// YAML→네이티브 이관(game_settings): rp.video_scale_integer — INTEGER SCALING
	{
		std::string orig = cfgReadKey(rpConfPath(), "global.video_scale_integer", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto vsi_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("INTEGER SCALING"), vsi_sw);
		s->addSaveFunc([vsi_sw] {
			cfgWriteKey(rpConfPath(), "global.video_scale_integer", vsi_sw->getState() ? "true" : "false", false);
		});
		// restart: none
	}

	// YAML→네이티브 이관(game_settings): rp.video_driver — VIDEO DRIVER
	{
		std::string confVal = cfgReadKey(rpConfPath(), "global.video_driver");
		auto vd_list = std::make_shared<OptionListComponent<std::string>>(
			mWindow, _("VIDEO DRIVER"), false);
		struct { const char* value; const char* label; } vdOptions[] = {
			{ "vulkan", "Vulkan (권장)" },
			{ "gl",     "OpenGL ES (호환성 폴백)" },
		};
		bool anySelected = false;
		bool isFirst = true;
		for (auto& opt : vdOptions) {
			bool sel = (std::string(opt.value) == confVal) || (isFirst && confVal.empty());
			isFirst = false;
			if (sel) anySelected = true;
			vd_list->add(opt.label, opt.value, sel);
		}
		if (!anySelected)
			vd_list->add(vdOptions[0].label, vdOptions[0].value, true);
		s->addWithLabel(_("VIDEO DRIVER"), vd_list);
		s->addSaveFunc([vd_list] {
			cfgWriteKey(rpConfPath(), "global.video_driver", vd_list->getSelected(), false);
		});
		// restart: none
	}

	// YAML→네이티브 이관(game_settings): rp.rewind — REWIND
	{
		std::string orig = cfgReadKey(rpConfPath(), "global.rewind_enable", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto rw_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("REWIND"), rw_sw);
		s->addSaveFunc([rw_sw] {
			cfgWriteKey(rpConfPath(), "global.rewind_enable", rw_sw->getState() ? "true" : "false", false);
		});
		// restart: none
	}

	// YAML→네이티브 이관(game_settings): rp.savestate_auto_save — AUTO SAVE STATE
	{
		std::string orig = cfgReadKey(rpConfPath(), "global.savestate_auto_save", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto sas_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("AUTO SAVE STATE"), sas_sw);
		s->addSaveFunc([sas_sw] {
			cfgWriteKey(rpConfPath(), "global.savestate_auto_save", sas_sw->getState() ? "true" : "false", false);
		});
		// restart: none
	}

	// YAML→네이티브 이관(game_settings): rp.savestate_auto_load — AUTO LOAD STATE
	{
		std::string orig = cfgReadKey(rpConfPath(), "global.savestate_auto_load", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto sal_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("AUTO LOAD STATE"), sal_sw);
		s->addSaveFunc([sal_sw] {
			cfgWriteKey(rpConfPath(), "global.savestate_auto_load", sal_sw->getState() ? "true" : "false", false);
		});
		// restart: none
	}

	// 2026-07-17: 게임 실행 전 세이브 스테이트 목록+썸네일 화면. 실행 흐름을
	// 가로채는 기능이라 설계 문서대로 기본 꺼짐이었으나, 2026-07-20 기본값을
	// 켜짐으로 변경.
	// YAML→네이티브 이관(game_settings): rp.savestate_preview — SHOW SAVE STATES BEFORE LAUNCH
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.SaveStatePreview", "true");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto ssp_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("SHOW SAVE STATES BEFORE LAUNCH"), ssp_sw);
		s->addSaveFunc([ssp_sw] {
			bool newVal = ssp_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.SaveStatePreview", newVal ? "true" : "false", false);
			// emulationstation.* 토글은 항상 Settings 메모리도 같이 갱신 - 누락되면
			// 같은 세션 안에서 반영이 안 되고 재부팅해야만 적용되는 회귀가 실기기에서
			// 확인됨(이 항목이 그 최초 사례).
			Settings::getInstance()->setBool("SaveStatePreview", newVal);
		});
		// restart: none
	}

	// 2026-07-12: SHOW BUNDLED GAMES - 예전엔 YAML toggle(restart: none) +
	// apply_retropangui_conf.sh가 rpui-bundlegame show/hide를 호출하고 그
	// 안에서 killall emulationstation으로 강제 종료했음. killall(외부
	// SIGTERM)은 타이밍에 따라 ES가 GPU/DRM 작업 도중 끊길 수 있어서 다음
	// 실행 때 화면에 아무것도 안 그려지는(DRM plane 없음) 문제가 실기기에서
	// 확인됨 - ES 메뉴 자체의 RESTART/재시작들이 쓰는 quitES()(ES 스스로
	// SDL_QUIT을 큐에 넣고 메인 루프 안전한 지점에서 정리 후 종료)로
	// 교체. apply_retropangui_conf.sh가 백그라운드(&)로 실행되는 것과
	// 달리, 여기서는 rpui-bundlegame을 동기 호출해서 gamelist.xml 갱신이
	// 끝난 뒤에만 재시작하도록(레이스 방지) 순서를 보장함.
	if (isBinAvailable("rpui-bundlegame"))
	{
		auto bundlegame_show = std::make_shared<SwitchComponent>(mWindow);
		bool origBundleShow = cfgReadKey(rpConfPath(), "system.bundlegame_show", "true") != "false";
		bundlegame_show->setState(origBundleShow);
		s->addWithLabel(_("SHOW BUNDLED GAMES"), bundlegame_show);
		// 2026-07-21: 아무 안내 없이 바로 ES를 재시작시켜서 크래시로 오인되던
		// 문제(사용자 지적) - OUTPUT RESOLUTION과 동일한 확인 다이얼로그로
		// 통일. CANCEL을 누르면 conf도 안 쓰고 재시작도 안 하고, 스위치
		// 표시도 원래 값으로 되돌림(OUTPUT RESOLUTION은 리스트라 다음에
		// 메뉴 들어가면 알아서 원복되지만, 스위치는 같은 화면에서 계속
		// 보이므로 즉시 되돌려야 함).
		s->addSaveFunc([this, bundlegame_show, origBundleShow] {
			bool newState = bundlegame_show->getState();
			if (newState == origBundleShow) return;
			mWindow->pushGui(new GuiMsgBox(mWindow,
				_("ES 재시작이 필요합니다.\n지금 재시작하시겠습니까?"),
				_("OK"), [newState, origBundleShow] {
					cfgWriteKey(rpConfPath(), "system.bundlegame_show", newState ? "true" : "false", false);
					::system(newState ? "rpui-bundlegame show" : "rpui-bundlegame hide");
					quitES(QuitMode::RESTART);
				},
				_("CANCEL"), [bundlegame_show, origBundleShow] {
					bundlegame_show->setState(origBundleShow);
				}
			));
		});
	}

	// 백그라운드 인덱싱
	auto background_indexing = std::make_shared<SwitchComponent>(mWindow);
	background_indexing->setState(Settings::getInstance()->getBool("BackgroundIndexing"));
	s->addWithLabel(_("INDEX FILES DURING SCREENSAVER"), background_indexing);
	s->addSaveFunc([background_indexing] { Settings::getInstance()->setBool("BackgroundIndexing", background_indexing->getState()); });

	// RetroPangui: 게임 리스트 갱신(전체 시스템) - 각 롬 폴더를 재스캔해서
	// gamelist.xml에 없는 게임만 등록(기존 항목 유지). 진행 상황은
	// GuiGamelistRefresh가 시스템별로 화면에 표시. ES 재시작 없이 반영됨.
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow, _("UPDATE GAMELISTS"),
			Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
		row.makeAcceptInputHandler([this] {
			std::vector<SystemData*> systems;
			for (auto sys : SystemData::sSystemVector)
				if (sys->isGameSystem() && !sys->isCollection())
					systems.push_back(sys);
			mWindow->pushGui(new GuiGamelistRefresh(mWindow, systems));
		});
		s->addRow(row);
	}

	// RetroPangui: 바이오스 체크 - share/bios/의 시스템별 필수/선택 바이오스
	// 존재+md5를 검사해 색상 목록으로 표시(GuiBiosCheck). "코어가 조용히 안
	// 뜨는" 원인 1순위를 로그 안 뒤지고 확인하기 위한 메뉴
	// (todo-20260714-bios-check-menu.html).
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow, _("BIOS CHECK"),
			Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
		row.makeAcceptInputHandler([this] {
			mWindow->pushGui(new GuiBiosCheck(mWindow));
		});
		s->addRow(row);
	}

	addSubmenuEntry(s, _("SCRAPER"), [this] { openScraperSettings(); });
	addSubmenuEntry(s, _("EMULATOR SETTINGS"), [this] { openEmulatorSettings(); });

	setSaveWithRestartChecks(s, checks);
	mWindow->pushGui(s);
}

// CONTROLLER SETTINGS — 입력 설정 + 버튼 방식 + 드라이버/통합 컨트롤(네이티브)
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

	// 2026-07-11: JOYPAD DRIVER 제거 - 실제 기본값은 udev인데 라벨은 "linuxraw
	// (기본)"이라 되어 있어서 오해를 주고 있었고, 일반 사용자가 판단할 필요 없는
	// 설정이라는 사용자 결정으로 삭제. retroarch.cfg의 input_joypad_driver=udev는
	// 그대로 유지됨.
	// YAML→네이티브 이관(advanced_settings, 구 ADVANCED SETTINGS): rp.menu_unified_controls — UNIFIED CONTROLS
	{
		std::string orig = cfgReadKey(rpConfPath(), "global.menu_unified_controls", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto muc_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("UNIFIED CONTROLS"), muc_sw);
		s->addSaveFunc([muc_sw] {
			cfgWriteKey(rpConfPath(), "global.menu_unified_controls", muc_sw->getState() ? "true" : "false", false);
		});
		// restart: none
	}

	// 2026-07-17: ES 메뉴 이동/선택 시 짧은 진동 피드백
	// YAML→네이티브 이관(advanced_settings): rp.menu_rumble — MENU RUMBLE
	{
		std::string orig = cfgReadKey(rpConfPath(), "emulationstation.MenuRumble", "true");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto mr_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("MENU RUMBLE"), mr_sw);
		s->addSaveFunc([mr_sw] {
			bool newVal = mr_sw->getState();
			cfgWriteKey(rpConfPath(), "emulationstation.MenuRumble", newVal ? "true" : "false", false);
			Settings::getInstance()->setBool("MenuRumble", newVal);
		});
		// restart: none
	}

	// 세기 슬라이더 - 조절 중 즉시 그 세기로 진동 피드백. PS2 패드류는 모터 특성상
	// 저세기 단펄스가 무감각해 사용자별 튜닝 필요(2026-07-17 사용자 요청).
	// YAML→네이티브 이관(advanced_settings): rp.menu_rumble_strength — MENU RUMBLE STRENGTH
	{
		std::string raw = cfgReadKey(rpConfPath(), "emulationstation.MenuRumbleStrength");
		float orig = 10.f;
		if (!raw.empty()) { try { orig = std::stof(raw); } catch (...) {} }
		auto mrs_sl = std::make_shared<SliderComponent>(mWindow, 10.f, 100.f, 10.f, "%");
		mrs_sl->setValue(orig);
		// 조절 즉시 Settings에 반영 + 그 세기로 바로 진동을 울려서 사용자가 움직이면서
		// 느낌을 확인할 수 있게 함(어느 패드로 조작 중인지는 여기서 모르므로 rumbleAll).
		mrs_sl->setChangedCallback([](float val) {
			Settings::getInstance()->setInt("MenuRumbleStrength", (int)Math::round(val));
			InputManager::getInstance()->rumbleAll(1.0f, 90);
		});
		s->addWithLabel(_("MENU RUMBLE STRENGTH"), mrs_sl);
		s->addSaveFunc([mrs_sl] {
			cfgWriteKey(rpConfPath(), "emulationstation.MenuRumbleStrength", std::to_string((int)mrs_sl->getValue()), false);
		});
		// restart: none
	}

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
		bool explicitlySet = !origIdxStr.empty();
		if (explicitlySet) { try { origIdx = std::stoi(origIdxStr); } catch (...) {} }
		int numJoy = SDL_NumJoysticks();
		// 2026-07-12: retroarch.cfg에 input_playerN_joypad_index가 명시적으로
		// 없으면(보통 그렇다 - RetroArch가 연결 순서대로 자동 배정하고 이
		// 값을 파일에 쓰지 않음) 지금까지 항상 "None"으로 보이던 버그.
		// RetroArch의 기본 배정 규칙(플레이어 N → SDL 조이스틱 인덱스 N-1)을
		// 그대로 따라서, 그 인덱스에 실제로 패드가 꽂혀있으면 그걸 기본
		// 선택값으로 보여줌.
		if (!explicitlySet && (p - 1) < numJoy)
			origIdx = p - 1;

		std::string rowLabel = _("PLAYER ") + std::to_string(p) + _(" CONTROLLER");
		auto padList = std::make_shared< OptionListComponent<std::string> >(mWindow, rowLabel, false);

		padList->add("None", "-1", origIdx < 0);
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

		std::string effectiveOrig = std::to_string(origIdx);
		s->addWithLabel(rowLabel, padList);
		s->addSaveFunc([padList, confKey, effectiveOrig] {
			std::string newVal = padList->getSelected();
			if (newVal == effectiveOrig)
				return;
			cfgWriteKey(raCfgPath(), confKey, newVal, false);
		});
	}

	setSaveWithRestartChecks(s, checks);
	mWindow->pushGui(s);
}

void GuiMenu::openStorageSettings()
{
	mWindow->pushGui(new GuiStorageSelect(mWindow));
}

// NETWORK — SYSTEM SETTINGS 안의 서브메뉴. IP 표시(네이티브) + SSH/SAMBA/WIFI
// 토글(예전 YAML network_settings 블록을 네이티브로 이관 - SYSTEM SETTINGS
// 최상위엔 안 풀리고 여기서만 보여주던 배치를 그대로 유지) + WiFi 스캔·연결
// 화면(실시간 데이터라 정적 목록으로 표현 불가, 기존 GuiWifiSelect 재사용).
void GuiMenu::openNetworkSettings()
{
	auto s = new GuiSettings(mWindow, _("NETWORK"));
	auto checks = std::make_shared<std::vector<RestartCheck>>();

	auto ipText = std::make_shared<TextComponent>(mWindow, getCurrentIpAddressWithLink(),
		Font::get(FONT_SIZE_MEDIUM, FONT_PATH_LIGHT), 0x777777FF, ALIGN_RIGHT);
	s->addWithLabel(_("CURRENT IP ADDRESS"), ipText);

	// YAML→네이티브 이관(network_settings): rp.ssh — SSH
	{
		std::string orig = cfgReadKey(rpConfPath(), "system.ssh", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto ssh_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("SSH"), ssh_sw);
		s->addSaveFunc([ssh_sw] {
			cfgWriteKey(rpConfPath(), "system.ssh", ssh_sw->getState() ? "true" : "false", false);
		});
		// restart: none
	}

	// YAML→네이티브 이관(network_settings): rp.samba — SAMBA
	{
		std::string orig = cfgReadKey(rpConfPath(), "system.samba", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto samba_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("SAMBA"), samba_sw);
		s->addSaveFunc([samba_sw] {
			cfgWriteKey(rpConfPath(), "system.samba", samba_sw->getState() ? "true" : "false", false);
		});
		// restart: none
	}

	// YAML→네이티브 이관(network_settings): rp.wifi — WIFI
	{
		std::string orig = cfgReadKey(rpConfPath(), "system.wifi.enabled", "false");
		bool state = (orig == "true" || orig == "1" || orig == "yes" || orig == "on");
		auto wifi_sw = std::make_shared<SwitchComponent>(mWindow, state);
		s->addWithLabel(_("WIFI"), wifi_sw);
		s->addSaveFunc([wifi_sw] {
			cfgWriteKey(rpConfPath(), "system.wifi.enabled", wifi_sw->getState() ? "true" : "false", false);
		});
		// restart: none
	}

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
			// 2026-07-25: 연결 안 된 상태의 화면 표시용 placeholder "None"이
			// 그대로 설정 파일에 저장되던 버그 - 실제로 뭔가 입력/연결된
			// 경우에만 저장.
			if (ssidCur->empty() || *ssidCur == "None") return;
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

		addSubmenuEntry(s, _("SELECT WIFI NETWORK"), [this, ssidText, ssidCur, pwText, pwCur] {
			auto gws = new GuiWifiSelect(mWindow);
			mWindow->pushGui(gws);
			// RetroPangui: 반드시 pushGui 이후에 호출 - GuiWifiSelect 생성자
			// 안에서 부르면 스택 순서 버그로 크래시(GuiWifiSelect.h 주석 참고).
			gws->openInitialPopup();
			// 2026-07-25: SELECT WIFI NETWORK에서 새로 연결해도 위쪽 SSID/비밀번호
			// 행이 화면 진입 시점 값 그대로 굳어있던 문제 - 이 팝업이 닫히는
			// 시점(GuiSettings 공통 패턴, addSaveFunc은 accept/back 상관없이
			// 항상 실행됨)에 다시 읽어서 갱신. enableNetwork()는 fork+exec로
			// 비동기 연결이라 완전히 연결되기 전에 닫으면 아직 예전 상태로
			// 보일 수 있음(수 초 뒤 재진입하면 반영됨) - 딱 그 순간의 실시간
			// 반영까지는 보장 못 함.
			gws->addSaveFunc([ssidText, ssidCur, pwText, pwCur] {
				bool wConnected = false;
				std::string wSsid, wIp;
				GuiWifiSelect::readStatus(wConnected, wSsid, wIp);
				if (wConnected && !wSsid.empty()) {
					*ssidCur = wSsid;
					ssidText->setValue(wSsid);
				}
				// 2026-07-25: wifi.conf는 share가 아니라 /var/lib/retropangui(루트
				// overlay)로 이동함 - exFAT는 Unix 권한을 실제로 강제 못 해서
				// 평문 비밀번호를 두기엔 부적절했음(rpui_wifi.c 주석 참고).
				std::string psk = cfgReadKey("/var/lib/retropangui/wifi.conf", "psk");
				if (!psk.empty()) {
					*pwCur = psk;
					pwText->setValue(std::string(psk.size(), '*'));
				}
			});
		});
	}

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
			{ autoLabel, "auto" },
		};
		// 2026-07-12: 고정 목록 대신 hdmi-set-resolution.py --list로 지금
		// 연결된 모니터의 EDID가 실제로 신고한 해상도 후보를 그대로 보여줌
		// (걸러내지 않음 - 새로고침율 안전 필터는 auto 경로에만 적용됨).
		// 그 아래엔 항상 존재가 보장된 CEA 표준 해상도 두 개를 폴백으로 둠.
		{
			FILE* p = popen("/usr/share/retropangui/hdmi-set-resolution.py --list 2>/dev/null", "r");
			if (p)
			{
				std::string json;
				char buf[512];
				while (fgets(buf, sizeof(buf), p)) json += buf;
				pclose(p);

				size_t pos = 0;
				while ((pos = json.find("\"name\": \"", pos)) != std::string::npos)
				{
					size_t nameStart = pos + 9;
					size_t nameEnd = json.find('"', nameStart);
					if (nameEnd == std::string::npos) break;
					std::string name = json.substr(nameStart, nameEnd - nameStart);

					size_t chunkEnd = json.find("\"name\": \"", nameEnd);
					std::string chunk = json.substr(nameEnd, (chunkEnd == std::string::npos ? json.size() : chunkEnd) - nameEnd);

					auto findInt = [&chunk](const char* key) -> long {
						std::string needle = std::string("\"") + key + "\": ";
						size_t p2 = chunk.find(needle);
						if (p2 == std::string::npos) return -1;
						return strtol(chunk.c_str() + p2 + needle.size(), nullptr, 10);
					};
					long width = findInt("width");
					long height = findInt("height");
					long refreshRounded = findInt("refresh_rounded");
					bool preferred = chunk.find("\"preferred\": true") != std::string::npos;

					if (width > 0 && height > 0)
					{
						std::string label = std::to_string(width) + "x" + std::to_string(height)
							+ " " + aspectLabel(width, height)
							+ " @" + std::to_string(refreshRounded) + "Hz"
							+ (preferred ? " (모니터 선호)" : "");
						resOptions.push_back({ label, name });
					}
					pos = nameEnd;
				}
			}
		}
		// 2026-07-12: 항상 존재가 보장된 CEA/VESA 표준 해상도 폴백 - 종횡비별로
		// 하나씩(16:9/16:10/4:3). CVT-RB로 새로 계산한 값은 검증이 안 돼서
		// 안 씀(1600x900 등을 시도했다가 역산하니 새로고침율이 틀려서 뺐음) -
		// 여기 셋 다 업계 표준값으로 역산 검증까지 마친 것만 남김.
		// 2026-07-12 정정: 값 이름이 "1920x1080p60hz"/"1280x720p60hz"였는데
		// 이건 U-Boot vout= 표기이지 실제 DRM 커넥터 모드 이름이 아님 -
		// odroid-drm-fbset -showmodes로 확인한 실제 이름("1080p60hz"/
		// "720p60hz")으로 정정. 존재하지 않는 이름이라 이 메뉴에서 골라도
		// 조용히 적용 실패하던 버그(실기기에서 재현 확인).
		resOptions.push_back({ "1920x1080 16:9 @60Hz",  "1080p60hz" });
		resOptions.push_back({ "1280x720 16:9 @60Hz",   "720p60hz" });
		resOptions.push_back({ "1920x1200 16:10 @60Hz", "fallback_1920x1200p60hz" });
		// 16:10 저해상도 사다리 - VESA DMT CVT-RB 표준, 역산 검증 완료(2026-07-13)
		resOptions.push_back({ "1680x1050 16:10 @60Hz", "fallback_1680x1050p60hz" });
		resOptions.push_back({ "1440x900 16:10 @60Hz",  "fallback_1440x900p60hz" });
		resOptions.push_back({ "1280x800 16:10 @60Hz",  "fallback_1280x800p60hz" });
		resOptions.push_back({ "1024x768 4:3 @60Hz",    "fallback_1024x768p60hz" });
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

	// YAML→네이티브 이관(system_settings): rp.hostname — HOSTNAME
	{
		std::string orig = cfgReadKey(rpConfPath(), "system.hostname");
		auto curVal = std::make_shared<std::string>(orig);

		auto lbl = std::make_shared<TextComponent>(mWindow, _("HOSTNAME"),
			Font::get(FONT_SIZE_MEDIUM), 0x777777FF);
		auto valText = std::make_shared<TextComponent>(mWindow, orig,
			Font::get(FONT_SIZE_MEDIUM, FONT_PATH_LIGHT), 0x777777FF, ALIGN_RIGHT);

		ComponentListRow row;
		row.addElement(lbl, true);
		row.addElement(valText, false);

		Window* hostnameWindow = mWindow;
		row.makeAcceptInputHandler([hostnameWindow, valText, curVal] {
			hostnameWindow->pushGui(new GuiArcadeVirtualKeyboard(hostnameWindow, _("HOSTNAME"),
				*curVal,
				[valText, curVal](const std::string& v) {
					*curVal = v;
					valText->setValue(v);
				}));
		});
		s->addRow(row);

		s->addSaveFunc([curVal] {
			cfgWriteKey(rpConfPath(), "system.hostname", *curVal, false);
		});
		// restart: none
	}

	// YAML→네이티브 이관(system_settings): rp.timezone — TIMEZONE
	{
		std::string confVal = cfgReadKey(rpConfPath(), "system.timezone");
		auto tz_list = std::make_shared<OptionListComponent<std::string>>(
			mWindow, _("TIMEZONE"), false);
		struct { const char* value; const char* label; } tzOptions[] = {
			{ "Asia/Seoul",          "Seoul (KST)" },
			{ "America/New_York",    "New York (EST)" },
			{ "Europe/Paris",        "Paris (CET)" },
			{ "America/Los_Angeles", "Los Angeles (PST)" },
		};
		bool anySelected = false;
		bool isFirst = true;
		for (auto& opt : tzOptions) {
			bool sel = (std::string(opt.value) == confVal) || (isFirst && confVal.empty());
			isFirst = false;
			if (sel) anySelected = true;
			tz_list->add(opt.label, opt.value, sel);
		}
		if (!anySelected)
			tz_list->add(tzOptions[0].label, tzOptions[0].value, true);
		s->addWithLabel(_("TIMEZONE"), tz_list);
		s->addSaveFunc([tz_list] {
			cfgWriteKey(rpConfPath(), "system.timezone", tz_list->getSelected(), false);
		});
		// restart: none
	}
	// HDMI 출력 해상도(OUTPUT RESOLUTION)는 잘못 고르면 화면이 안 보이게 될 수
	// 있는 위험한 설정이라 위 두 항목과 달리 이 함수 위쪽에 이미 확인+되돌리기
	// 포함 네이티브로 구현돼 있음 - A/B 버튼전환(SWAP BUTTONS A/B)과 동일한 패턴.

	// NETWORK 서브메뉴 — SSH/SAMBA/WIFI 토글 + IP + WiFi 선택을 한 화면으로 묶음.
	// 2026-07-10: 예전엔 이 항목들이 SYSTEM SETTINGS에 flat하게 다 풀려있었음
	// ("구 NETWORK SETTINGS" 주석은 "예전엔 진짜 서브메뉴였다가 flat으로
	// 합쳐졌다"는 뜻이었는데, 문서에서 반대로("이미 서브메뉴로 존재") 오독됐던
	// 걸 재확인 과정에서 발견 - todo-20260704-wifi-menu-polish.html 참고.
	addSubmenuEntry(s, _("NETWORK"), [this] { openNetworkSettings(); });

	// power saver
	// 2026-07-22: 각 옵션 뜻을 풀어쓴 긴 영문 설명을 항목명 자체로 쓰던 것
	// 제거(불필요하다는 사용자 피드백) - 짧은 라벨로 되돌리고 대신 번역이
	// 제대로 붙게 함. PowerSaver.cpp 기준 실제 동작: 유휴 상태에서 화면을
	// 얼마나 뜸하게 다시 그릴지(ms) 정하는 값이 커질수록 절전 효과가 큼
	// (disabled=-1은 그 기능 자체를 끔 = 절전 없음, instant=200ms는 대기
	// 간격은 가장 짧지만 대신 전환 애니메이션/캐러셀 이동/효과음을 아예
	// 꺼서(아래 addSaveFunc) 렌더링 자체를 줄이는 방식이라 종합적으로는
	// 가장 공격적인 절전 모드).
	struct PowerSaverOption { const char* value; const char* label; };
	static const PowerSaverOption psOptions[] = {
		{ "disabled", "DISABLED" },
		{ "default",  "DEFAULT" },
		{ "enhanced", "ENHANCED" },
		{ "instant",  "INSTANT" },
	};
	auto power_saver = std::make_shared< OptionListComponent<std::string> >(mWindow, _("POWER SAVER MODES"), false);
	for (auto& opt : psOptions)
		power_saver->add(_(opt.label), opt.value, Settings::getInstance()->getString("PowerSaverMode") == opt.value);
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

namespace
{
	struct EmulatorSettingsSystemInfo
	{
		std::string name;
		std::string fullname;
		std::vector<CoreInfo> cores;
	};

	// es_systems.xml을 직접 파싱해서 게임 유무와 무관하게 코어가 정의된
	// 모든 시스템을 나열한다. SystemData::sSystemVector는 게임이 하나도
	// 없는 시스템을 아예 만들지 않고 버리므로(SystemData.cpp
	// loadSystem() - 메인 캐러셀에 빈 시스템이 안 뜨게 하는 의도적 동작,
	// 손대지 않음) EMULATOR SETTINGS는 그 목록을 못 쓴다. "게임 유무와
	// 상관없이 시스템에 설치된 모든 코어를 조절할 수 있어야 한다"는
	// 요구사항 때문에 이 화면만 별도로 XML을 직접 읽는다(2026-08-10,
	// todo-20260810-system-default-core-conf-gap.html).
	std::vector<EmulatorSettingsSystemInfo> loadAllSystemCoresFromXml()
	{
		std::vector<EmulatorSettingsSystemInfo> result;

		pugi::xml_document doc;
		if (!doc.load_file(SystemData::getConfigPath(false).c_str()))
			return result;

		pugi::xml_node systemList = doc.child("systemList");
		for (pugi::xml_node system = systemList.child("system"); system; system = system.next_sibling("system"))
		{
			std::string name = system.child("name").text().get();
			pugi::xml_node coresNode = system.child("cores");
			if (name.empty() || !coresNode)
				continue;

			EmulatorSettingsSystemInfo info;
			info.name = name;
			info.fullname = system.child("fullname").text().get();

			for (pugi::xml_node coreNode = coresNode.child("core"); coreNode; coreNode = coreNode.next_sibling("core"))
			{
				CoreInfo core;
				core.name = coreNode.attribute("name").as_string();
				core.fullname = coreNode.attribute("fullname").as_string();
				if (core.fullname.empty())
					core.fullname = core.name;
				core.module_id = coreNode.attribute("module_id").as_string();
				core.priority = coreNode.attribute("priority").as_int(999);
				if (!core.name.empty())
					info.cores.push_back(core);
			}

			if (info.cores.empty())
				continue;

			std::sort(info.cores.begin(), info.cores.end(),
				[](const CoreInfo& a, const CoreInfo& b) { return a.priority < b.priority; });

			result.push_back(info);
		}

		return result;
	}
}

// 시스템 전체 기본 코어 선택 화면 - retropangui.conf의 system.<system>.core=
// override를 실제로 읽고 쓴다. 예전엔 systems.json priority만 보고 "Current
// Default"를 표시했고, 저장도 존재하지 않는 스크립트
// (es_systems_updater.sh, 빌드머신/실기기 어디에도 없었음)를 호출하는
// 죽은 코드라 항상 조용히 실패했음 - 재부팅하면 원래대로 돌아가는 상태로
// 오래 방치돼 있었음. GuiMetaDataEd.cpp의 EMULATOR 드롭다운,
// rpui-launcher.py의 resolve_core_override_from_conf()와 동일한 conf
// 키를 공유해야 셋이 서로 다른 값을 보여주는 일이 없다(2026-08-10,
// todo-20260810-system-default-core-conf-gap.html).
void GuiMenu::openEmulatorSettings()
{
	auto s = new GuiSettings(mWindow, _("EMULATOR SETTINGS"));

	// 게임 유무와 무관하게 코어가 정의된 시스템 전체를 대상으로 함
	for (const auto& sysInfo : loadAllSystemCoresFromXml())
	{
		const std::vector<CoreInfo>& cores = sysInfo.cores;
		std::string systemName = sysInfo.name;
		std::string confKey = "system." + systemName + ".core";
		std::string overrideModuleId = cfgReadKey(rpConfPath(), confKey, "");

		// "(Default)" 라벨 = 시스템에 내장된 고정 기준값(systems.json priority
		// 1). retropangui.conf로 사용자가 뭘 골라 저장하든 이 라벨은 절대
		// 안 움직임 - "default"가 매번 "지금 고른 값"과 같아지면 그 단어
		// 자체가 무의미해짐(2026-08-10 사용자 지적). "지금 실제로 적용
		//중인 값"은 아래 currentSelection으로 별도 관리 - 선택자 커서
		// 위치로만 표현하고 텍스트 라벨은 안 붙임.
		std::string factoryDefault;
		for (const auto& core : cores)
		{
			if (core.priority == 1)
			{
				factoryDefault = core.name;
				break;
			}
		}

		std::string currentSelection = factoryDefault;
		if (!overrideModuleId.empty())
		{
			for (const auto& core : cores)
			{
				if (core.module_id == overrideModuleId)
				{
					currentSelection = core.name;
					break;
				}
			}
		}

		auto emulatorList = std::make_shared<OptionListComponent<std::string>>(mWindow,
			_("DEFAULT EMULATOR"), false);

		for (const auto& core : cores)
		{
			std::string label = core.fullname;
			if (core.name == factoryDefault)
				label += " (Default)";

			emulatorList->add(label, core.name, core.name == currentSelection);
		}

		s->addWithLabel(Utils::String::toUpper(systemName), emulatorList);
		s->addSaveFunc([systemName, confKey, cores, emulatorList, currentSelection] {
			std::string selectedCore = emulatorList->getSelected();

			// Only update if selection changed
			if (selectedCore == currentSelection)
			{
				LOG(LogDebug) << "No change in default emulator for " << systemName << ", skipping update";
				return;
			}

			std::string moduleId;
			for (const auto& core : cores)
			{
				if (core.name == selectedCore)
				{
					moduleId = core.module_id;
					break;
				}
			}
			if (moduleId.empty())
			{
				LOG(LogError) << "Unknown core selected for " << systemName << ": " << selectedCore;
				return;
			}

			cfgWriteKey(rpConfPath(), confKey, moduleId, false);
			LOG(LogInfo) << "System default core saved: " << confKey << "=" << moduleId;
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
	prompts.push_back(HelpPrompt("up/down", _("CHOOSE")));

	// RetroPangui: InputConfig::getActionButton()로 통일(중복 삼항연산자 제거)
	prompts.push_back(HelpPrompt(InputConfig::getActionButton("accept"), _("SELECT")));
	prompts.push_back(HelpPrompt("start", _("CLOSE")));
	return prompts;
}
