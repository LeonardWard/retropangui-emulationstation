//EmulationStation, a graphical front-end for ROM browsing. Created by Alec "Aloshi" Lofquist.
//http://www.aloshi.com

#include "guis/GuiChangelog.h"
#include "guis/GuiDetectDevice.h"
#include "guis/GuiInfoPopup.h"
#include "guis/GuiInputConfig.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiStorageSelect.h"
#include "utils/FileSystemUtil.h"
#include "utils/ProfilingUtil.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "EmulationStation.h"
#include "InputConfig.h"
#include "InputManager.h"
#include "LocaleES.h"
#include "Log.h"
#include "MameNames.h"
#include "MusicManager.h"
#include "platform.h"
#include "PowerSaver.h"
#include "ScraperCmdLine.h"
#include "Settings.h"
#include "SystemData.h"
#include "SystemScreenSaver.h"
#include <SDL_events.h>
#include <dirent.h>
#include <unistd.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <SDL_main.h>
#include <SDL_timer.h>
#include <iostream>
#include <time.h>
#ifdef WIN32
#include <Windows.h>
#endif

#include <FreeImage.h>
#include "guis/GuiOtaUpdate.h"
#include "HttpReq.h"
#include "AudioManager.h"
#include "VolumeControl.h"
#include <future>
#include <fstream>
#include <csignal>

bool scrape_cmdline = false;

// 2026-07-13: 모니터 핫스왑 대응. hdmi-hotplug(udev)가 "다른 모니터로 교체"를
// 감지하면 ES에 SIGUSR1을 보냄 - ES 프로세스를 죽이지 않고(메뉴 위치 등
// 사용자 화면 상태 유지) 비디오만 재초기화해서 새 모니터에 맞는 해상도를
// 다시 잡기 위함. 시그널 핸들러에서는 async-safe하게 플래그만 세우고, 실제
// 처리는 메인 루프가 프레임 사이에서 수행(launchGame 복귀 경로와 동일 패턴).
static volatile sig_atomic_t gDisplayResetRequested = 0;
static void displayResetSignalHandler(int) { gDisplayResetRequested = 1; }

bool parseArgs(int argc, char* argv[])
{
	Utils::FileSystem::setExePath(argv[0]);

	// We need to process --home before any call to Settings::getInstance(), because settings are loaded from homepath
	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "--home") == 0)
		{
			if(i >= argc - 1)
			{
				std::cerr << "Invalid home path supplied.";
				return false;
			}

			Utils::FileSystem::setHomePath(argv[i + 1]);
			break;
		}
	}

	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "--monitor") == 0)
		{
			if (i >= argc - 1)
			{
				std::cerr << "Invalid monitor supplied.";
				return false;
			}

			int monitor = atoi(argv[i + 1]);
			i++; // skip the argument value
			Settings::getInstance()->setInt("MonitorID", monitor);
		}else if(strcmp(argv[i], "--resolution") == 0)
		{
			if(i >= argc - 2)
			{
				std::cerr << "Invalid resolution supplied.";
				return false;
			}

			int width = atoi(argv[i + 1]);
			int height = atoi(argv[i + 2]);
			i += 2; // skip the argument value
			Settings::getInstance()->setInt("WindowWidth", width);
			Settings::getInstance()->setInt("WindowHeight", height);
		}else if(strcmp(argv[i], "--screensize") == 0)
		{
			if(i >= argc - 2)
			{
				std::cerr << "Invalid screensize supplied.";
				return false;
			}

			int width = atoi(argv[i + 1]);
			int height = atoi(argv[i + 2]);
			i += 2; // skip the argument value
			Settings::getInstance()->setInt("ScreenWidth", width);
			Settings::getInstance()->setInt("ScreenHeight", height);
		}else if(strcmp(argv[i], "--screenoffset") == 0)
		{
			if(i >= argc - 2)
			{
				std::cerr << "Invalid screenoffset supplied.";
				return false;
			}

			int x = atoi(argv[i + 1]);
			int y = atoi(argv[i + 2]);
			i += 2; // skip the argument value
			Settings::getInstance()->setInt("ScreenOffsetX", x);
			Settings::getInstance()->setInt("ScreenOffsetY", y);
		}else if (strcmp(argv[i], "--screenrotate") == 0)
		{
			if (i >= argc - 1)
			{
				std::cerr << "Invalid screenrotate supplied.";
				return false;
			}

			int rotate = atoi(argv[i + 1]);
			++i; // skip the argument value
			Settings::getInstance()->setInt("ScreenRotate", rotate);
		}else if(strcmp(argv[i], "--gamelist-only") == 0)
		{
			Settings::getInstance()->setBool("ParseGamelistOnly", true);
		}else if(strcmp(argv[i], "--ignore-gamelist") == 0)
		{
			Settings::getInstance()->setBool("IgnoreGamelist", true);
		}else if(strcmp(argv[i], "--show-hidden-files") == 0)
		{
			Settings::getInstance()->setBool("ShowHiddenFiles", true);
		}else if(strcmp(argv[i], "--draw-framerate") == 0)
		{
			Settings::getInstance()->setBool("DrawFramerate", true);
		}else if(strcmp(argv[i], "--no-exit") == 0)
		{
			Settings::getInstance()->setBool("ShowExit", false);
		}else if(strcmp(argv[i], "--no-confirm-quit") == 0)
		{
			Settings::getInstance()->setBool("ConfirmQuit", false);
		}else if(strcmp(argv[i], "--no-splash") == 0)
		{
			Settings::getInstance()->setBool("SplashScreen", false);
		}else if(strcmp(argv[i], "--debug") == 0)
		{
			Settings::getInstance()->setBool("Debug", true);
			Settings::getInstance()->setBool("HideConsole", false);
			Log::setReportingLevel(LogDebug);
		}else if(strcmp(argv[i], "--fullscreen-borderless") == 0)
		{
			Settings::getInstance()->setBool("FullscreenBorderless", true);
		}else if(strcmp(argv[i], "--windowed") == 0)
		{
			Settings::getInstance()->setBool("Windowed", true);
		}else if(strcmp(argv[i], "--vsync") == 0)
		{
			bool vsync = (strcmp(argv[i + 1], "on") == 0 || strcmp(argv[i + 1], "1") == 0) ? true : false;
			Settings::getInstance()->setBool("VSync", vsync);
			i++; // skip vsync value
		}else if(strcmp(argv[i], "--scrape") == 0)
		{
			scrape_cmdline = true;
		}else if(strcmp(argv[i], "--max-vram") == 0)
		{
			int maxVRAM = atoi(argv[i + 1]);
			Settings::getInstance()->setInt("MaxVRAM", maxVRAM);
		}
		else if (strcmp(argv[i], "--force-kiosk") == 0)
		{
			Settings::getInstance()->setBool("ForceKiosk", true);
		}
		else if (strcmp(argv[i], "--force-kid") == 0)
		{
			Settings::getInstance()->setBool("ForceKid", true);
		}
		else if (strcmp(argv[i], "--force-disable-filters") == 0)
		{
			Settings::getInstance()->setBool("ForceDisableFilters", true);
		}
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
		{
#ifdef WIN32
			// This is a bit of a hack, but otherwise output will go to nowhere
			// when the application is compiled with the "WINDOWS" subsystem (which we usually are).
			// If you're an experienced Windows programmer and know how to do this
			// the right way, please submit a pull request!
			AttachConsole(ATTACH_PARENT_PROCESS);
			freopen("CONOUT$", "wb", stdout);
#endif
			std::cout <<
				"EmulationStation, a graphical front-end for ROM browsing.\n"
				"Written by Alec \"Aloshi\" Lofquist.\n"
				"Version " << PROGRAM_VERSION_STRING << ", built " << PROGRAM_BUILT_STRING << "\n"
				"Command line arguments:\n"
				"\nGeometry settings:\n"
				"--resolution WIDTH HEIGHT      try and force a particular resolution\n"
				"--screenrotate N               rotate a quarter turn clockwise for each N\n"
				"--screensize WIDTH HEIGHT      for a canvas smaller than the full resolution,\n"
				"                               or if rotating into portrait mode\n"
				"--screenoffset X Y             move the canvas by x,y pixels\n"
				"--fullscreen-borderless        borderless fullscreen window\n"
				"--windowed                     not fullscreen, should be used with --resolution\n"
				"--monitor N                    monitor index (0-)\n"
				"\nGame and settings visibility in ES and behaviour of ES:\n"
				"--force-disable-filters        force the UI to ignore applied filters on\n"
				"                               gamelist (p)\n"
				"--force-kid                    force the UI mode to be Kid\n"
				"--force-kiosk                  force the UI mode to be Kiosk\n"
				"--no-confirm-quit              omit confirm dialog on actions of quit menu\n"
				"--no-exit                      don't show the exit option in the menu\n"
				"--no-splash                    don't show the splash screen\n"
				"\nGamelist related:\n"
				"--gamelist-only                use gamelist.xml as trusted source and do not\n"
				"                               check any path entries of gamelist.xml (p)\n"
				"--ignore-gamelist              do not read gamelist.xml files (useful for\n"
				"                               troubleshooting)\n"
				"\nAdvanced settings:\n"
				"--debug                        more logging, show console on Windows. Enables\n"
				"                               these keyboard shortcuts with left CTRL-key:\n"
				"                               +G: Toggle Gridlayout boundary boxes\n"
				"                               +I: Toggle image boundary box\n"
				"                               +R: Reload all UI views (theme, gamelist, system)\n"
				"                               +T: Toggle textcomponent boundary box\n"
				"--draw-framerate               display the framerate (p)\n"
				"--max-vram SIZE                maximum VRAM to use in MB before swapping,\n"
				"                               use 0 for unlimited (p)\n"
				"--show-hidden-files            show also hidden files of filesystem, no effect\n"
				"                               if --gamelist-only is also set (p)\n"
				"--vsync 1|0                    turn vsync on (1) or off (0) (default is on)\n"
				"\nGeneric switches:\n"
				"--help, -h                     summon a sentient, angry tuba\n\n"
				"--home PATH                    directory to use as home folder for\n"
				"                               .emulationstation/es_settings.cfg, aso.\n"
				"                               Subfolder .emulationstation/ will be created.\n"
				"\nScrape mode:\n"
				"--scrape                       scrape using command line interface\n\n"
				"Note: Switches marked (p) will be persisted in es_settings.cfg when any\n"
				"setting is changed via EmulationStation UI.\n\n"
				"Please refer to the online documentation for additional information:\n"
				"https://retropie.org.uk/docs/EmulationStation/\n";
			return false; //exit after printing help
		}
	}

	return true;
}

bool verifyHomeFolderExists()
{
	//make sure the config directory exists
	std::string home = Utils::FileSystem::getHomePath();
	std::string configDir = home + "/.emulationstation";
	if(!Utils::FileSystem::exists(configDir))
	{
		std::cout << "Creating config directory \"" << configDir << "\"\n";
		Utils::FileSystem::createDirectory(configDir);
		if(!Utils::FileSystem::exists(configDir))
		{
			std::cerr << "Config directory could not be created!\n";
			return false;
		}
	}

	return true;
}

// Returns true if everything is OK,
bool loadSystemConfigFile(Window* window, std::string* errorString)
{
	errorString->clear();

	if(!SystemData::loadConfig(window))
	{
		LOG(LogError) << "Error while parsing systems configuration file!";
		*errorString = _("IT LOOKS LIKE YOUR SYSTEMS CONFIGURATION FILE HAS NOT BEEN SET UP OR IS INVALID. YOU'LL NEED TO DO THIS BY HAND, UNFORTUNATELY.\n\n"
			"VISIT EMULATIONSTATION.ORG FOR MORE INFORMATION.");
		return false;
	}

	if(SystemData::sSystemVector.size() == 0)
	{
		LOG(LogError) << "No systems found! Does at least one system have a game present? (check that extensions match!)\n(Also, make sure you've updated your es_systems.cfg for XML!)";
		*errorString = _("WE CAN'T FIND ANY SYSTEMS!\n"
			"CHECK THAT YOUR PATHS ARE CORRECT IN THE SYSTEMS CONFIGURATION FILE, "
			"AND YOUR GAME DIRECTORY HAS AT LEAST ONE GAME WITH THE CORRECT EXTENSION.\n\n"
			"VISIT EMULATIONSTATION.ORG FOR MORE INFORMATION.");
		return false;
	}

	return true;
}

//called on exit, assuming we get far enough to have the log initialized
void onExit()
{
	Log::close();
}

int main(int argc, char* argv[])
{
	std::locale::global(std::locale("C"));

	if(!parseArgs(argc, argv))
		return 0;

	// only show the console on Windows if HideConsole is false
#ifdef WIN32
	// MSVC has a "SubSystem" option, with two primary options: "WINDOWS" and "CONSOLE".
	// In "WINDOWS" mode, no console is automatically created for us.  This is good,
	// because we can choose to only create the console window if the user explicitly
	// asks for it, preventing it from flashing open and then closing.
	// In "CONSOLE" mode, a console is always automatically created for us before we
	// enter main. In this case, we can only hide the console after the fact, which
	// will leave a brief flash.
	// TL;DR: You should compile ES under the "WINDOWS" subsystem.
	// I have no idea how this works with non-MSVC compilers.
	if(!Settings::getInstance()->getBool("HideConsole"))
	{
		// we want to show the console
		// if we're compiled in "CONSOLE" mode, this is already done.
		// if we're compiled in "WINDOWS" mode, no console is created for us automatically;
		// the user asked for one, so make one and then hook stdin/stdout/sterr up to it
		if(AllocConsole()) // should only pass in "WINDOWS" mode
		{
			freopen("CONIN$", "r", stdin);
			freopen("CONOUT$", "wb", stdout);
			freopen("CONOUT$", "wb", stderr);
		}
	}else{
		// we want to hide the console
		// if we're compiled with the "WINDOWS" subsystem, this is already done.
		// if we're compiled with the "CONSOLE" subsystem, a console is already created;
		// it'll flash open, but we hide it nearly immediately
		if(GetConsoleWindow()) // should only pass in "CONSOLE" mode
			ShowWindow(GetConsoleWindow(), SW_HIDE);
	}
#endif

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_Initialise();
#endif

	//if ~/.emulationstation doesn't exist and cannot be created, bail
	if(!verifyHomeFolderExists())
		return 1;

	//start the logger
	Log::init();
	Log::open();
	LOG(LogInfo) << "EmulationStation - v" << PROGRAM_VERSION_STRING << ", built " << PROGRAM_BUILT_STRING;
	Log::flush(); // ensure version line is on disk even if we crash during early init

	//always close the log on exit
	atexit(&onExit);

	// RetroPangui: Log::open() 이후에 retropangui.conf 적용 (LOG 메시지가 정상적으로 기록되도록)
	Settings::getInstance()->loadRetropanguiConf();

	// RetroPangui: Initialize locale
	std::string language = Settings::getInstance()->getString("Language");
	LOG(LogInfo) << "main() : language read from Settings just before LocaleES::init() = [" << language << "]";
	LocaleES::init(language);

	Window window;
	SystemScreenSaver screensaver(&window);
	PowerSaver::init();
	ViewController::init(&window);
	CollectionSystemManager::init(&window);
	MameNames::init();
	window.pushGui(ViewController::get());

	bool splashScreen = Settings::getInstance()->getBool("SplashScreen");

	if(!scrape_cmdline)
	{
		if(!window.init())
		{
			LOG(LogError) << "Window failed to initialize!";
			return 1;
		}

		if (splashScreen)
		{
			std::string progressText = "Loading system config...";
			window.renderLoadingScreen(progressText);
		}
	}

	Log::flush(); // before loading system config

	std::string errorMsg;
	if(!loadSystemConfigFile(splashScreen ? &window : nullptr, &errorMsg))
	{
		// something went terribly wrong
		if(errorMsg.empty())
		{
			LOG(LogError) << "Unknown error occured while parsing system config file.";
			if(!scrape_cmdline)
				Renderer::deinit();
			return 1;
		}

		// we can't handle es_systems.cfg file problems inside ES itself, so display the error message then quit
		window.pushGui(new GuiMsgBox(&window,
			errorMsg,
			_("QUIT"), [] {
				SDL_Event* quit = new SDL_Event();
				quit->type = SDL_QUIT;
				SDL_PushEvent(quit);
			}));
	}

	//run the command line scraper then quit
	if(scrape_cmdline)
	{
		return run_scraper_cmdline();
	}

	Log::flush(); // system config loaded OK — about to preload game lists

	// preload what we can right away instead of waiting for the user to select it
	// this makes for no delays when accessing content, but a longer startup time
	ViewController::get()->preload();
	Log::flush(); // preload complete

	if(splashScreen)
		window.renderLoadingScreen("Done.");

	InputManager::getInstance()->init();

	// 백그라운드 OTA 자동 버전 체크
	auto otaAutoFuture = std::async(std::launch::async, []() -> std::pair<std::string,std::string> {
		std::string serverUrl, curVer;
		{
			std::ifstream f("/etc/retropangui-ota.conf");
			if (!f.good()) return {"", ""};
			std::getline(f, serverUrl);
			serverUrl.erase(serverUrl.find_last_not_of(" \t\r\n") + 1);
			if (serverUrl.empty()) return {"", ""};
		}
		{
			std::ifstream f("/etc/retropangui-version");
			if (f.good()) {
				std::getline(f, curVer);
				curVer.erase(curVer.find_last_not_of(" \t\r\n") + 1);
			}
		}
		auto req = std::make_shared<HttpReq>(serverUrl + "/version");
		for (int i = 0; i < 50 && req->status() == HttpReq::REQ_IN_PROGRESS; i++)
			SDL_Delay(100);
		if (req->status() != HttpReq::REQ_SUCCESS) return {"", ""};
		std::string serverVer = req->getContent();
		serverVer.erase(serverVer.find_last_not_of(" \t\r\n") + 1);
		return {curVer, serverVer};
	});

	//choose which GUI to open depending on if an input configuration already exists
	if(errorMsg.empty())
	{
		auto showChangelogIfPending = [&window]() {
			if(Utils::FileSystem::exists("/etc/.ota-changelog-pending"))
				window.pushGui(new GuiChangelog(&window));
		};

		if(Utils::FileSystem::exists(InputManager::getConfigPath()) && InputManager::getInstance()->getNumConfiguredDevices() > 0)
		{
			ViewController::get()->goToStart();
			showChangelogIfPending();
		}else{
			window.pushGui(new GuiDetectDevice(&window, true, [&window, showChangelogIfPending] {
				ViewController::get()->goToStart();
				showChangelogIfPending();
			}));
		}
	}

	// RetroPangui: 외부 저장장치 감지 콜백 등록 (storage-mgr IPC 기반)
	window.setStorageDetectedCallback([&window](const std::string& label, const std::string& /*id*/) {
		std::string msg = label.empty()
			? "외부 저장장치가 감지됐습니다.\n게임과 세이브 파일을 여기에 저장하시겠습니까?"
			: label + " 이(가) 감지됐습니다.\n게임과 세이브 파일을 여기에 저장하시겠습니까?";

		window.pushGui(new GuiMsgBox(&window, msg,
			"예",     [&window]() { window.pushGui(new GuiStorageSelect(&window)); },
			"아니오", nullptr));
	});

	// RetroPangui: 런타임 중 핫플러그된 미매핑 컨트롤러 알림 (부팅 시 최초 스캔은 트리거 안 됨)
	// 2026-07-18 정정: Y/N 확인창 자체를 없앰(사용자 지적) - 패드가 지금 실제로
	// 연결돼 있으면 GuiDetectDevice의 "버튼 꾹 누르기"가 그 자체로 확인 동작이라
	// 별도 Y/N이 군더더기였고, 이벤트가 뜬 시점과 실제 화면이 뜨는 시점 사이에
	// 이미 연결 해제됐으면(케이블 불량 등) 아예 아무것도 안 띄워서 "입력 장치가
	// 하나도 없는데 Y/N 대화상자만 화면에 남아 영원히 못 닫히는" 상황 자체를
	// 없앰. 키보드/마우스는 세지 않고 진짜 게임패드가 있을 때만 화면 표시.
	window.setUnconfiguredJoystickCallback([&window](InputConfig* /*config*/) {
		if (InputManager::getInstance()->getNumJoysticks() > 0)
			window.pushGui(new GuiDetectDevice(&window, false, nullptr));
	});

	// RetroPangui: 이미 매핑된 패드가 런타임 중 연결/해제될 때 짧은 OSD 알림
	// (부팅 시 이미 꽂혀 있던 패드나 미매핑 패드는 InputManager에서 걸러짐)
	window.setJoystickNotificationCallback([&window](const std::string& name, bool connected) {
		std::string msg = name + (connected ? " 연결됨" : " 연결 해제됨");
		window.setInfoPopup(new GuiInfoPopup(&window, msg, 3000));
	});

	// RetroPangui: USB/블루투스 오디오 장치 연결/해제 OSD 알림 (조이스틱과 동일한 패턴 -
	// 부팅 시 이미 연결돼 있던 장치는 Window::checkAudioDeviceChange()에서 걸러짐)
	window.setAudioDeviceNotificationCallback([&window](const std::string& name, bool connected) {
		std::string msg = name + (connected ? " 연결됨" : " 연결 해제됨");
		window.setInfoPopup(new GuiInfoPopup(&window, msg, 3000));
	});

	// RetroPangui: 배경 음악 시작 (BackgroundMusic=false거나 <share>/music 비어 있으면 no-op)
	MusicManager::getInstance()->start();

	// 모니터 핫스왑 시 hdmi-hotplug가 보내는 비디오 재초기화 요청 수신
	signal(SIGUSR1, displayResetSignalHandler);

	int lastTime = SDL_GetTicks();
	int ps_time = SDL_GetTicks();

	bool running = true;

	while(running)
	{
		// 모니터 교체(SIGUSR1) - ES를 유지한 채 비디오만 재초기화.
		// launchGame()의 게임 복귀 시퀀스(deinit → 해상도 재적용 → init,
		// d1e5719)와 동일한 순서를 그대로 따름 - GUI 스택은 건드리지
		// 않으므로 사용자가 보던 화면(메뉴 위치 등)이 유지됨.
		if(gDisplayResetRequested)
		{
			gDisplayResetRequested = 0;
			LOG(LogInfo) << "monitor hotplug: display reset requested - reinitializing video only";
			MusicManager::getInstance()->stop();
			AudioManager::getInstance()->deinit();
			VolumeControl::getInstance()->deinit();
			InputManager::getInstance()->deinit();
			window.deinit();
			// 새 모니터의 EDID 기준으로 해상도 재적용 (3단 폴백 - 교체
			// 직후엔 화면이 안 보여서 사람이 개입할 수 없으므로 어떤
			// 모니터든 반드시 화면이 나오는 데까지 자동으로 내려감)
			system(
				"HDMI_MODE=\"$(python3 /usr/share/retropangui/hdmi-set-resolution.py "
				"2>>/var/log/hdmi-resolution.log)\"; "
				"[ -z \"$HDMI_MODE\" ] && HDMI_MODE=\"1080p60hz\"; "
				"odroid-drm-fbset -outputmode \"$HDMI_MODE\" 2>/dev/null "
				"|| odroid-drm-fbset -outputmode 1080p60hz 2>/dev/null "
				"|| odroid-drm-fbset -outputmode 720p60hz 2>/dev/null || true");
			window.init();
			InputManager::getInstance()->init();
			VolumeControl::getInstance()->init();
			MusicManager::getInstance()->start();
			lastTime = SDL_GetTicks();
		}
		SDL_Event event;
		bool ps_standby = PowerSaver::getState() && (int) SDL_GetTicks() - ps_time > PowerSaver::getMode();

		if(ps_standby ? SDL_WaitEventTimeout(&event, PowerSaver::getTimeout()) : SDL_PollEvent(&event))
		{
			do
			{
				InputManager::getInstance()->parseEvent(event, &window);

				if(event.type == SDL_QUIT)
				{
					// 디버깅용 임시 로그(2026-07-05) — 메뉴 진입/퇴장만으로 종료되는 원인 추적.
					// quitMode가 quitES()에서 이미 로그되지 않았다면(기본값 QUIT) quitES()를
					// 거치지 않고 SDL_QUIT이 직접 큐에 들어온 경우(예: 외부 시그널)임을 알 수 있음.
					LOG(LogWarning) << "main loop: received SDL_QUIT, current quitMode=" << (int)quitMode;
					running = false;
				}
			} while(SDL_PollEvent(&event));

			// triggered if exiting from SDL_WaitEvent due to event
			if (ps_standby)
				// show as if continuing from last event
				lastTime = SDL_GetTicks();

			// reset counter
			ps_time = SDL_GetTicks();
		}
		else if (ps_standby)
		{
			// If exitting SDL_WaitEventTimeout due to timeout. Trail considering
			// timeout as an event
			ps_time = SDL_GetTicks();
		}

		// 자동 OTA 체크 결과 처리 (한 번만)
		if (otaAutoFuture.valid() &&
		    otaAutoFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
		{
			auto res = otaAutoFuture.get();
			const std::string& curVer   = res.first;
			const std::string& serverVer = res.second;
			if (!serverVer.empty() && serverVer > curVer)
			{
				std::string msg = "새 버전이 있습니다.\n현재: " + curVer +
				                  "  →  새 버전: " + serverVer +
				                  "\n\n지금 업데이트하시겠습니까?";
				window.pushGui(new GuiMsgBox(&window, msg,
					"업데이트", [&window, serverVer]() {
						std::string serverUrl2, device2 = "odroidc5";
						{
							std::ifstream cf("/etc/retropangui-ota.conf");
							if (cf.good()) {
								std::getline(cf, serverUrl2);
								serverUrl2.erase(serverUrl2.find_last_not_of(" \t\r\n") + 1);
								std::string d; std::getline(cf, d);
								d.erase(d.find_last_not_of(" \t\r\n") + 1);
								if (!d.empty()) device2 = d;
							}
						}
						auto dl_fn = [serverUrl2, device2]() -> int {
							std::string cmd = "/usr/share/retropangui/ota-update.sh " + serverUrl2 + " " + device2;
							int ret = ::system(cmd.c_str());
							return WEXITSTATUS(ret);
						};
						auto done_fn = [&window, serverVer](bool success) {
							if (success)
								window.pushGui(new GuiMsgBox(&window,
									"업데이트 준비 완료!\n새 버전: " + serverVer +
									"\n\n지금 재부팅하시겠습니까?",
									"재부팅", []() { ::system("reboot"); },
									"나중에", nullptr));
							else
								window.pushGui(new GuiMsgBox(&window,
									"업데이트 실패.\n메뉴에서 다시 시도하세요.",
									"확인", nullptr));
						};
						window.pushGui(new GuiOtaDownload(&window, dl_fn, done_fn));
					},
					"나중에", nullptr));
			}
		}

		if(window.isSleeping())
		{
			lastTime = SDL_GetTicks();
			SDL_Delay(1); // this doesn't need to be accurate, we're just giving up our CPU time until something wakes us up
			continue;
		}

		int curTime = SDL_GetTicks();
		int deltaTime = curTime - lastTime;
		lastTime = curTime;

		// cap deltaTime if it ever goes negative
		if(deltaTime < 0)
			deltaTime = 1000;

		window.update(deltaTime);
		MusicManager::getInstance()->update(); // 트랙 종료 감지 → 다음 곡
		window.render();
		Renderer::swapBuffers();

		Log::flush();
	}

	while(window.peekGui() != ViewController::get())
		delete window.peekGui();

	InputManager::getInstance()->deinit();
	window.deinit();

	MameNames::deinit();
	CollectionSystemManager::deinit();
	SystemData::deleteSystems();

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_DeInitialise();
#endif

	processQuitMode();

	ProfileDump();

	LOG(LogInfo) << "EmulationStation cleanly shutting down.";

	return 0;
}
