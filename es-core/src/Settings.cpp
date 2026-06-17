#include "Settings.h"

#include "utils/FileSystemUtil.h"
#include "Log.h"
#include "Scripting.h"
#include "platform.h"
#include <pugixml.hpp>
#include <algorithm>
#include <vector>
#include <fstream>
#include <sys/stat.h>

Settings* Settings::sInstance = NULL;

// these values are NOT saved to es_settings.xml
// since they're set through command-line arguments, and not the in-program settings menu
std::vector<const char*> settings_dont_save {
	"Debug",
	"DebugGrid",
	"DebugText",
	"DebugImage",
	"ForceKid",
	"ForceKiosk",
	"IgnoreGamelist",
	"HideConsole",
	"ShowExit",
	"ConfirmQuit",
	"SplashScreen",
	"VSync",
	"FullscreenBorderless",
	"Windowed",
	"WindowWidth",
	"WindowHeight",
	"ScreenWidth",
	"ScreenHeight",
	"ScreenOffsetX",
	"ScreenOffsetY",
	"ScreenRotate",
	"MonitorID"
};

Settings::Settings()
{
	setDefaults();
	loadFile();
}

Settings* Settings::getInstance()
{
	if(sInstance == NULL)
		sInstance = new Settings();

	return sInstance;
}

void Settings::setDefaults()
{
	mBoolMap.clear();
	mIntMap.clear();

	mBoolMap["BackgroundJoystickInput"] = false;
	mBoolMap["ParseGamelistOnly"] = false;
	mBoolMap["ShowHiddenFiles"] = false;
	mBoolMap["DrawFramerate"] = false;
	mBoolMap["ShowExit"] = true;
	mBoolMap["ConfirmQuit"] = true;
	mBoolMap["FullscreenBorderless"] = false;
	mBoolMap["Windowed"] = false;
	mBoolMap["SplashScreen"] = true;
	mStringMap["StartupSystem"] = "";
	mBoolMap["DisableKidStartMenu"] = true;

	mBoolMap["VSync"] = true;

	mBoolMap["EnableSounds"] = true;
	// 배경 음악(BGM): <share>/music 폴더의 음악 파일을 셔플 재생 (MusicManager)
	mBoolMap["BackgroundMusic"] = true;
	mBoolMap["ShowHelpPrompts"] = true;
	mBoolMap["DoublePressRemovesFromFavs"] = false;
	mBoolMap["ScrapeRatings"] = true;
	mBoolMap["IgnoreGamelist"] = false;
	mBoolMap["HideConsole"] = true;
	mBoolMap["QuickSystemSelect"] = true;
	mBoolMap["MoveCarousel"] = true;

	mBoolMap["ThreadedLoading"] = false;

	mBoolMap["Debug"] = false;
	mBoolMap["DebugGrid"] = false;
	mBoolMap["DebugText"] = false;
	mBoolMap["DebugImage"] = false;

	mIntMap["ScreenSaverTime"] = 5 * Settings::ONE_MINUTE_IN_MS;
	mIntMap["SystemSleepTime"] = 0 * Settings::ONE_MINUTE_IN_MS;
	mBoolMap["SystemSleepTimeHintDisplayed"] = false;
	mIntMap["ScraperResizeWidth"] = 400;
	mIntMap["ScraperResizeHeight"] = 0;
	#ifdef _RPI_
		mIntMap["MaxVRAM"] = 80;
	#else
		mIntMap["MaxVRAM"] = 100;
	#endif

	// RetroPangui: instant 기본 — fade는 시스템 전환 시 블랙 플래시 유발
	mStringMap["TransitionStyle"] = "instant";
	mStringMap["ThemeSet"] = "";
	mStringMap["ScreenSaverBehavior"] = "dim";
	mStringMap["Scraper"] = "TheGamesDB";
	mStringMap["GamelistViewStyle"] = "automatic";
	// RetroPangui: always 기본 — never면 playcount/lastplayed가 gamelist.xml에 기록되지 않고,
	// on exit은 전원을 바로 끄는 기기에서 저장 시점이 보장되지 않음
	mStringMap["SaveGamelistsMode"] = "always";

	// RetroPangui: Paths for multi-core support
	// Priority: 1. Environment variable, 2. Build-time default, 3. Empty string
	// This ensures ES works even without es_settings.cfg

	// RetroArch binary path
	const char* retroarchEnv = std::getenv("RETROARCH_PATH");
	if (retroarchEnv && retroarchEnv[0] != '\0') {
		mStringMap["RetroArchPath"] = retroarchEnv;
	} else {
#ifdef RETROPANGUI_RETROARCH_PATH
		mStringMap["RetroArchPath"] = RETROPANGUI_RETROARCH_PATH;
#else
		mStringMap["RetroArchPath"] = "";
#endif
	}

	// Libretro cores directory
	const char* coresEnv = std::getenv("LIBRETRO_CORES_PATH");
	if (coresEnv && coresEnv[0] != '\0') {
		mStringMap["LibretroCoresPath"] = coresEnv;
	} else {
#ifdef RETROPANGUI_CORES_PATH
		mStringMap["LibretroCoresPath"] = RETROPANGUI_CORES_PATH;
#else
		mStringMap["LibretroCoresPath"] = "";
#endif
	}

	// Core-specific config directory (user-specific, environment variable only)
	const char* coreConfigEnv = std::getenv("CORE_CONFIG_PATH");
	mStringMap["CoreConfigPath"] = (coreConfigEnv && coreConfigEnv[0] != '\0') ? coreConfigEnv : "";

	// RetroPangui: Language support
	mStringMap["Language"] = "en_US";

	// RetroPangui: Fallback font for CJK characters
	mStringMap["FallbackFont"] = "";

	// RetroPangui: List display mode (ALL/SCRAPED/AUTO)
	mStringMap["ShowFolders"] = "AUTO";

	// RetroPangui: Button Layout (nintendo, sony, xbox) — xbox 기본 (A=확인, B=취소)
	mStringMap["ButtonLayout"] = "xbox";

	mBoolMap["ScreenSaverControls"] = true;
	mStringMap["ScreenSaverGameInfo"] = "never";
	mBoolMap["StretchVideoOnScreenSaver"] = false;
	mStringMap["PowerSaverMode"] = "disabled";

	mIntMap["ScreenSaverSwapMediaTimeout"] = 10000;
	mBoolMap["SlideshowScreenSaverStretch"] = false;
	mStringMap["SlideshowScreenSaverBackgroundAudioFile"] = Utils::FileSystem::getHomePath() + "/.emulationstation/slideshow/audio/slideshow_bg.wav";
	mBoolMap["SlideshowScreenSaverCustomMediaSource"] = false;
	mStringMap["SlideshowScreenSaverMediaDir"] = Utils::FileSystem::getHomePath() + "/.emulationstation/slideshow/media";
	mStringMap["SlideshowScreenSaverImageFilter"] = ".png,.jpg";
	mStringMap["SlideshowScreenSaverVideoFilter"] = ".mp4,.avi";
	mBoolMap["SlideshowScreenSaverRecurse"] = false;

	// This setting only applies to raspberry pi but set it for all platforms so
	// we don't get a warning if we encounter it on a different platform
	mBoolMap["VideoOmxPlayer"] = false;
	#ifdef _OMX_
		// we're defaulting to OMX Player for full screen video on the Pi
		mBoolMap["ScreenSaverOmxPlayer"] = true;
		// use OMX Player defaults
		mStringMap["SubtitleFont"] = "/usr/share/fonts/truetype/freefont/FreeSans.ttf";
		mStringMap["SubtitleItalicFont"] = "/usr/share/fonts/truetype/freefont/FreeSansOblique.ttf";
		mIntMap["SubtitleSize"] = 55;
		mStringMap["SubtitleAlignment"] = "left";
	#else
		mBoolMap["ScreenSaverOmxPlayer"] = false;
	#endif

	mIntMap["ScreenSaverSwapVideoTimeout"] = 30000;

	mBoolMap["VideoAudio"] = true;
	mBoolMap["ScreenSaverVideoMute"] = false;
	mStringMap["VlcScreenSaverResolution"] = "original";
	// Audio out device for Video playback using OMX player.
	mStringMap["OMXAudioDev"] = "both";
	mIntMap["RandomCollectionMaxGames"] = 0; // 0 == no limit
	std::map<std::string, int> m1;
	mMapIntMap["RandomCollectionSystemsAuto"] = m1;
	std::map<std::string, int> m2;
	mMapIntMap["RandomCollectionSystemsCustom"] = m2;
	std::map<std::string, int> m3;
	mMapIntMap["RandomCollectionSystems"] = m3;
	mStringMap["RandomCollectionExclusionCollection"] = "";
	mStringMap["CollectionSystemsAuto"] = "";
	mStringMap["CollectionSystemsCustom"] = "";
	mStringMap["DefaultScreenSaverCollection"] = "";
	mBoolMap["CollectionShowSystemInfo"] = true;
	mBoolMap["SortAllSystems"] = false;
	mBoolMap["UseCustomCollectionsSystem"] = true;
	mBoolMap["BackgroundIndexing"] = false;

	mBoolMap["LocalArt"] = false;

	// Audio out device for volume control
	#ifdef _RPI_
		mStringMap["AudioDevice"] = "HDMI";
	#else
		mStringMap["AudioDevice"] = "Master";
	#endif

	mStringMap["AudioCard"] = "default";
	mStringMap["UIMode"] = "Full";
	mStringMap["UIMode_passkey"] = "uuddlrlrba";
	mBoolMap["ForceKiosk"] = false;
	mBoolMap["ForceKid"] = false;
	mBoolMap["ForceDisableFilters"] = false;

	mIntMap["WindowWidth"]   = 0;
	mIntMap["WindowHeight"]  = 0;
	mIntMap["ScreenWidth"]   = 0;
	mIntMap["ScreenHeight"]  = 0;
	mIntMap["ScreenOffsetX"] = 0;
	mIntMap["ScreenOffsetY"] = 0;
	mIntMap["ScreenRotate"]  = 0;
	mIntMap["MonitorID"] = 0;

	mBoolMap["UseFullscreenPaging"] = false;

	mBoolMap["IgnoreLeadingArticles"] = false;
	//No spaces!  Order is important!
	//"The A Squad" given [a,an,the] will sort as "A Squad", but given [the,a,an] will sort as "Squad"
	mStringMap["LeadingArticles"] = "a,an,the";
}

template <typename K, typename V>
void saveMap(pugi::xml_document& doc, std::map<K, V>& map, const char* type)
{
	for(auto iter = map.cbegin(); iter != map.cend(); iter++)
	{
		// key is on the "don't save" list, so don't save it
		if(std::find(settings_dont_save.cbegin(), settings_dont_save.cend(), iter->first) != settings_dont_save.cend())
			continue;

		pugi::xml_node node = doc.append_child(type);
		node.append_attribute("name").set_value(iter->first.c_str());
		node.append_attribute("value").set_value(iter->second);
	}
}

void Settings::saveFile()
{
	LOG(LogDebug) << "Settings::saveFile() : Saving Settings to file.";
	const std::string path = Utils::FileSystem::getHomePath() + "/.emulationstation/es_settings.cfg";

	pugi::xml_document doc;

	saveMap<std::string, bool>(doc, mBoolMap, "bool");
	saveMap<std::string, int>(doc, mIntMap, "int");
	saveMap<std::string, float>(doc, mFloatMap, "float");

	//saveMap<std::string, std::string>(doc, mStringMap, "string");
	for(auto iter = mStringMap.cbegin(); iter != mStringMap.cend(); iter++)
	{
		pugi::xml_node node = doc.append_child("string");
		node.append_attribute("name").set_value(iter->first.c_str());
		node.append_attribute("value").set_value(iter->second.c_str());
	}

	for(auto &m : mMapIntMap)
	{
		pugi::xml_node node = doc.append_child("map");
		node.append_attribute("name").set_value(m.first.c_str());
		std::string datatype = "int";
		node.append_attribute("type").set_value(datatype.c_str());
		for(auto &intMap : m.second) // intMap is a <string, int> map
		{
			pugi::xml_node entry = node.append_child(datatype.c_str());
			entry.append_attribute("name").set_value(intMap.first.c_str());
			entry.append_attribute("value").set_value(intMap.second);
		}
	}

	doc.save_file(path.c_str());

	// retropangui.conf 에 있던 emulationstation.* 키도 현재 값으로 동기화
	saveRetropanguiConf();

	Scripting::fireEvent("config-changed");
	Scripting::fireEvent("settings-changed");
}

void Settings::loadFile()
{
	const std::string path = Utils::FileSystem::getHomePath() + "/.emulationstation/es_settings.cfg";

	if(!Utils::FileSystem::exists(path))
		return;

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(path.c_str());
	if(!result)
	{
		LOG(LogError) << "Could not parse Settings file!\n   " << result.description();
		return;
	}

	// apply_retropangui_conf.sh는 <config> 래퍼 안에 저장, ES saveFile은 루트 레벨에 저장
	// 둘 다 읽을 수 있도록 <config> 래퍼 유무에 따라 루트 노드 선택
	pugi::xml_node root = doc.child("config") ? doc.child("config") : doc;

	for(pugi::xml_node node = root.child("bool"); node; node = node.next_sibling("bool"))
		setBool(node.attribute("name").as_string(), node.attribute("value").as_bool());
	for(pugi::xml_node node = root.child("int"); node; node = node.next_sibling("int"))
		setInt(node.attribute("name").as_string(), node.attribute("value").as_int());
	for(pugi::xml_node node = root.child("float"); node; node = node.next_sibling("float"))
		setFloat(node.attribute("name").as_string(), node.attribute("value").as_float());
	for(pugi::xml_node node = root.child("string"); node; node = node.next_sibling("string"))
		setString(node.attribute("name").as_string(), node.attribute("value").as_string());

	for(pugi::xml_node node = root.child("map"); node; node = node.next_sibling("map"))
	{
		std::string mapName = node.attribute("name").as_string();
		std::string mapType = node.attribute("type").as_string();
		if (mapType == "int") {
			// only supporting int value maps currently
			std::map<std::string, int> _map;
			for(pugi::xml_node entry : node.children(mapType.c_str()))
			{
				_map[entry.attribute("name").as_string()] = entry.attribute("value").as_int();
			}
			setMap(mapName, _map);
		} else {
			LOG(LogWarning) << "Map: '" << mapName << "'. Unsupported data type '"<< mapType <<"'. Value ignored!";
		}
	}

	processBackwardCompatibility();
}

// RETROPANGUI_SHARE 환경 변수 → /share → ~/share 순서로 탐색
// (C5에서는 S99emulationstation이 RETROPANGUI_SHARE=/retropangui/share 를 export)
static std::string retropanguiConfPath()
{
	struct stat st;
	std::string sharePath;
	const char* env = getenv("RETROPANGUI_SHARE");
	if (env && env[0] != '\0')
		sharePath = env;
	else if (stat("/share", &st) == 0 && S_ISDIR(st.st_mode))
		sharePath = "/share";
	else
	{
		const char* home = getenv("HOME");
		sharePath = home ? std::string(home) + "/share" : "/share";
	}
	return sharePath + "/system/retropangui.conf";
}

void Settings::loadRetropanguiConf()
{
	std::string confPath = retropanguiConfPath();
	std::ifstream f(confPath);
	if (!f.is_open())
	{
		LOG(LogInfo) << "retropangui.conf not found at " << confPath << " — skipping";
		return;
	}

	LOG(LogInfo) << "Loading emulationstation settings from " << confPath;

	static const std::string PREFIX = "emulationstation.";
	std::string line;
	while (std::getline(f, line))
	{
		if (line.empty() || line[0] == '#') continue;
		if (line.substr(0, PREFIX.size()) != PREFIX) continue;

		auto eq = line.find('=');
		if (eq == std::string::npos) continue;

		std::string key = line.substr(PREFIX.size(), eq - PREFIX.size());
		// trim key
		while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();

		std::string val = line.substr(eq + 1);
		// trim value
		while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
		while (!val.empty() && (val.back()  == ' ' || val.back()  == '\t')) val.pop_back();

		if (val.empty()) continue;

		// 타입 판별: 기존 맵에 있는 키를 기준으로 적용
		if (mBoolMap.count(key))
		{
			setBool(key, val == "1" || val == "true" || val == "yes");
		}
		else if (mIntMap.count(key))
		{
			int intVal = std::stoi(val);
			// ScreenSaverTime: retropangui.conf 는 초 단위, Settings 는 ms 단위
			if (key == "ScreenSaverTime")
				intVal *= 1000;
			setInt(key, intVal);
		}
		else if (mFloatMap.count(key))
		{
			setFloat(key, std::stof(val));
		}
		else
		{
			// 키가 없거나 string 으로 처리
			setString(key, val);
		}

		// conf 에 존재한 키는 saveFile() 시 현재 값으로 역기록 (메뉴 변경 유지)
		mRetropanguiKeys.insert(key);

		LOG(LogDebug) << "retropangui.conf → " << key << " = " << val;
	}
}

// retropangui.conf 에 존재하는 emulationstation.* 키를 현재 설정값으로 갱신.
// 부팅 시 conf 가 es_settings.cfg 를 덮어쓰므로, 역기록이 없으면 메뉴에서
// 바꾼 값이 재부팅마다 conf 값으로 되돌아감 (conf 가 마스터 설정).
void Settings::saveRetropanguiConf()
{
	if (mRetropanguiKeys.empty())
		return;

	std::string confPath = retropanguiConfPath();
	std::ifstream fin(confPath);
	if (!fin.is_open())
		return;

	static const std::string PREFIX = "emulationstation.";
	std::vector<std::string> lines;
	std::string line;
	bool changed = false;

	while (std::getline(fin, line))
	{
		if (!line.empty() && line[0] != '#' && line.substr(0, PREFIX.size()) == PREFIX)
		{
			auto eq = line.find('=');
			if (eq != std::string::npos)
			{
				std::string key = line.substr(PREFIX.size(), eq - PREFIX.size());
				while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();

				if (mRetropanguiKeys.count(key))
				{
					std::string val;
					bool known = true;
					if (mBoolMap.count(key))
						val = mBoolMap[key] ? "true" : "false";
					else if (mIntMap.count(key))
					{
						int v = mIntMap[key];
						// ScreenSaverTime: Settings 는 ms 단위, conf 는 초 단위
						if (key == "ScreenSaverTime")
							v /= 1000;
						val = std::to_string(v);
					}
					else if (mFloatMap.count(key))
						val = std::to_string(mFloatMap[key]);
					else if (mStringMap.count(key))
						val = mStringMap[key];
					else
						known = false;

					if (known)
					{
						std::string newLine = PREFIX + key + "=" + val;
						if (newLine != line)
						{
							line = newLine;
							changed = true;
						}
					}
				}
			}
		}
		lines.push_back(line);
	}
	fin.close();

	if (!changed)
		return;

	std::ofstream fout(confPath);
	if (!fout.is_open())
	{
		LOG(LogError) << "saveRetropanguiConf: cannot write " << confPath;
		return;
	}
	for (auto& l : lines)
		fout << l << "\n";

	LOG(LogDebug) << "saveRetropanguiConf: updated " << confPath;
}


void Settings::setMap(const std::string& key, const std::map<std::string, int>& map)
{
	mMapIntMap[key] = map;
}

const std::map<std::string, int> Settings::getMap(const std::string& key)
{
	if(mMapIntMap.find(key) == mMapIntMap.cend())
	{
		LOG(LogError) << "Tried to use undefined setting " << key << "!";
		std::map<std::string, int> empty;
		return empty;

	}
	return mMapIntMap[key];
}


template<typename Map>
void Settings::renameSetting(Map& map, std::string&& oldName, std::string&& newName)
{
	typename Map::const_iterator it = map.find(oldName);
	if (it != map.end()) {
		map[newName] = it->second;
		map.erase(it);
	}
}

void Settings::processBackwardCompatibility()
{
	{	// SaveGamelistsOnExit -> SaveGamelistsMode
		std::map<std::string, bool>::const_iterator it = mBoolMap.find("SaveGamelistsOnExit");
		if (it != mBoolMap.end()) {
			mStringMap["SaveGamelistsMode"] = it->second ? "on exit" : "never";
			mBoolMap.erase(it);
		}
	}

	{ // ScreenSaverSlideShow Image -> Media
		renameSetting<std::map<std::string, int>>(mIntMap, std::string("ScreenSaverSwapImageTimeout"), std::string("ScreenSaverSwapMediaTimeout"));
		renameSetting<std::map<std::string, bool>>(mBoolMap, std::string("SlideshowScreenSaverCustomImageSource"), std::string("SlideshowScreenSaverCustomMediaSource"));
		renameSetting<std::map<std::string, std::string>>(mStringMap, std::string("SlideshowScreenSaverImageDir"), std::string("SlideshowScreenSaverMediaDir"));
	}
}


//Print a warning message if the setting we're trying to get doesn't already exist in the map, then return the value in the map.
#define SETTINGS_GETSET(type, mapName, getMethodName, setMethodName) type Settings::getMethodName(const std::string& name) \
{ \
	if(mapName.find(name) == mapName.cend()) \
	{ \
		LOG(LogError) << "Tried to use unset setting " << name << "!"; \
	} \
	return mapName[name]; \
} \
void Settings::setMethodName(const std::string& name, type value) \
{ \
	mapName[name] = value; \
}

SETTINGS_GETSET(bool, mBoolMap, getBool, setBool);
SETTINGS_GETSET(int, mIntMap, getInt, setInt);
SETTINGS_GETSET(float, mFloatMap, getFloat, setFloat);
SETTINGS_GETSET(const std::string&, mStringMap, getString, setString);
