#include "LocaleES.h"
#include "guis/GuiGeneralScreensaverOptions.h"

#include "components/OptionListComponent.h"
#include "components/SliderComponent.h"
#include "components/SwitchComponent.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiSlideshowScreensaverOptions.h"
#include "guis/GuiVideoScreensaverOptions.h"
#include "Settings.h"
#include "utils/FileSystemUtil.h"

#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

namespace
{
	// RetroPangui: GuiMenu.cpp의 getSharePath()/cfgReadKey()와 동일한 경량
	// key=value 파서 - 헤더로 안 빼고 여기 로컬로 둠(이 파일에서만 필요한
	// "뉴스 티커 API 키가 하나라도 있나" 체크용, 2026-07-23).
	std::string getSharePathLocal()
	{
		const char* env = std::getenv("RETROPANGUI_SHARE");
		if (env && env[0] != '\0')
			return env;
		struct stat st;
		if (stat("/share", &st) == 0 && S_ISDIR(st.st_mode))
			return "/share";
		const char* home = std::getenv("HOME");
		return home ? std::string(home) + "/share" : "/share";
	}

	std::string cfgReadKeyLocal(const std::string& fullKey)
	{
		std::ifstream f(getSharePathLocal() + "/system/retropangui.conf");
		if (!f.is_open())
			return "";
		std::string line;
		while (std::getline(f, line))
		{
			if (line.empty() || line[0] == '#')
				continue;
			auto eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			std::string k = line.substr(0, eq);
			while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
			if (k != fullKey)
				continue;
			std::string v = line.substr(eq + 1);
			while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '"')) v.erase(v.begin());
			while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '"' || v.back() == '\r')) v.pop_back();
			return v;
		}
		return "";
	}

	// 뉴스 티커 API 키(ticker.*) 6개 중 하나라도 값이 있으면 true
	bool anyTickerApiKeySet()
	{
		static const char* kKeys[] = {
			"ticker.naver_client_id", "ticker.naver_client_secret",
			"ticker.krx_api_key",
			"ticker.weather_service_key", "ticker.weather_nx", "ticker.weather_ny",
		};
		for (const char* key : kKeys)
			if (!cfgReadKeyLocal(key).empty())
				return true;
		return false;
	}
}

GuiGeneralScreensaverOptions::GuiGeneralScreensaverOptions(Window* window, const char* title) : GuiScreensaverOptions(window, title)
{
	// screensaver time
	auto screensaver_time = std::make_shared<SliderComponent>(mWindow, 0.f, 30.f, 1.f, "m");
	screensaver_time->setValue((float)(Settings::getInstance()->getInt("ScreenSaverTime") / Settings::ONE_MINUTE_IN_MS));
	addWithLabel(_("SCREENSAVER AFTER"), screensaver_time);
	addSaveFunc([screensaver_time] {
		Settings::getInstance()->setInt("ScreenSaverTime", (int)Math::round(screensaver_time->getValue()) * Settings::ONE_MINUTE_IN_MS);
		PowerSaver::updateTimeouts();
	});

	// Allow ScreenSaver Controls - ScreenSaverControls
	auto ss_controls = std::make_shared<SwitchComponent>(mWindow);
	ss_controls->setState(Settings::getInstance()->getBool("ScreenSaverControls"));
	addWithLabel(_("SCREENSAVER CONTROLS"), ss_controls);
	addSaveFunc([ss_controls] { Settings::getInstance()->setBool("ScreenSaverControls", ss_controls->getState()); });

	// screensaver behavior
	auto screensaver_behavior = std::make_shared< OptionListComponent<std::string> >(mWindow, _("SCREENSAVER BEHAVIOR"), false);
	std::vector<std::string> screensavers;
	screensavers.push_back("dim");
	screensavers.push_back("black");
	screensavers.push_back("random video");
	screensavers.push_back("slideshow");
	screensavers.push_back("web stream external");
	// RetroPangui: "온디바이스 브라우징"은 wpewebkit/cog가 실제로 설치된
	// 빌드에서만 메뉴에 노출 - C5(Mali-G310)는 GPU 버퍼공유 크래시로 상류
	// 이슈가 해결될 때까지 defconfig에서 아예 안 빌드함(2026-07-23).
	// cog 바이너리 유무로만 판단해서, 나중에 x86 등 다른 보드가 wpewebkit을
	// 빌드에 포함하면 이 ES 소스를 안 고쳐도 자동으로 다시 나타남.
	if (Utils::FileSystem::exists("/usr/bin/cog"))
		screensavers.push_back("web stream ondevice");
	// RetroPangui: "뉴스 티커"도 마찬가지로 조건부 노출 - ticker.* API 키
	// 6개가 전부 비어있으면(TICKER SETTINGS 메뉴에서 아무것도 입력 안 한
	// 기본 상태) 메뉴에서 숨김. 키를 하나라도 입력하면 바로 나타남(2026-07-23).
	if (anyTickerApiKeySet())
		screensavers.push_back("news ticker");
	for(auto it = screensavers.cbegin(); it != screensavers.cend(); it++)
		screensaver_behavior->add(*it, *it, Settings::getInstance()->getString("ScreenSaverBehavior") == *it);
	addWithLabel(_("SCREENSAVER BEHAVIOR"), screensaver_behavior);
	addSaveFunc([this, screensaver_behavior] {
		if (Settings::getInstance()->getString("ScreenSaverBehavior") != "random video" && screensaver_behavior->getSelected() == "random video") {
			// if before it wasn't risky but now there's a risk of problems, show warning
			mWindow->pushGui(new GuiMsgBox(mWindow,
			_("The \"Random Video\" screensaver shows videos from your gamelist.\n\nIf you do not have videos, or if in several consecutive attempts the games it selects don't have videos it will default to black.\n\nMore options in the \"UI Settings\" > \"Video Screensaver\" menu."),
				_("OK"), [] { return; }));
		}
		Settings::getInstance()->setString("ScreenSaverBehavior", screensaver_behavior->getSelected());
		PowerSaver::updateTimeouts();
	});

	ComponentListRow row;

	// show filtered menu
	row.elements.clear();
	row.addElement(std::make_shared<TextComponent>(mWindow, _("VIDEO SCREENSAVER SETTINGS"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
	row.addElement(makeArrow(mWindow), false);
	row.makeAcceptInputHandler(std::bind(&GuiGeneralScreensaverOptions::openVideoScreensaverOptions, this));
	addRow(row);

	row.elements.clear();
	row.addElement(std::make_shared<TextComponent>(mWindow, _("SLIDESHOW SCREENSAVER SETTINGS"), Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
	row.addElement(makeArrow(mWindow), false);
	row.makeAcceptInputHandler(std::bind(&GuiGeneralScreensaverOptions::openSlideshowScreensaverOptions, this));
	addRow(row);

	// system sleep time
	float stepw = 5.f;
	float max =  120.f;
	auto system_sleep_time = std::make_shared<SliderComponent>(mWindow, 0.f, max, stepw, "m");
	system_sleep_time->setValue((float)(Settings::getInstance()->getInt("SystemSleepTime") / Settings::ONE_MINUTE_IN_MS));
	addWithLabel(_("SYSTEM SLEEP AFTER"), system_sleep_time);
	addSaveFunc([this, system_sleep_time, screensaver_time, max, stepw] {
		if (screensaver_time->getValue() > system_sleep_time->getValue() && system_sleep_time->getValue() > 0) {
			int steps = Math::min(1 + (int)(screensaver_time->getValue() / stepw), (int)(max/stepw));
			int adj_system_sleep_time = steps*stepw;
			system_sleep_time->setValue((float)adj_system_sleep_time);
			std::string msg = "";
			if (!Settings::getInstance()->getBool("SystemSleepTimeHintDisplayed")) {
				msg += _("One time note: Enabling the system sleep time will trigger user-defined scripts.");
				msg += _("\nPlease see Retropie/Emulationstation Wiki on events for details.");
				Settings::getInstance()->setBool("SystemSleepTimeHintDisplayed", true);
			}
			if (msg.length() > 0) {
				msg += "\n\n";
			}
			msg += _("The system sleep delay is enabled, but is less than or equal to the screen saver start delay.");
			msg	+= _("\n\nAdjusted system sleep time to ") + std::to_string(adj_system_sleep_time) + _(" minutes.");
			mWindow->pushGui(new GuiMsgBox(mWindow, msg, _("OK"), [] { return; }));
		}
		Settings::getInstance()->setInt("SystemSleepTime", (int)Math::round(system_sleep_time->getValue()) * Settings::ONE_MINUTE_IN_MS);
	});
}

GuiGeneralScreensaverOptions::~GuiGeneralScreensaverOptions()
{
}

void GuiGeneralScreensaverOptions::openVideoScreensaverOptions() {
	mWindow->pushGui(new GuiVideoScreensaverOptions(mWindow, _("VIDEO SCREENSAVER")));
}

void GuiGeneralScreensaverOptions::openSlideshowScreensaverOptions() {
    mWindow->pushGui(new GuiSlideshowScreensaverOptions(mWindow, _("SLIDESHOW SCREENSAVER")));
}
