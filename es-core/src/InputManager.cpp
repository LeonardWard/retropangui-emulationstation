#include "InputManager.h"

#include "utils/FileSystemUtil.h"
#include "CECInput.h"
#include "Log.h"
#include "platform.h"
#include "Scripting.h"
#include "Window.h"
#include <pugixml.hpp>
#include <SDL.h>
#include <iostream>
#include <assert.h>

#define KEYBOARD_GUID_STRING "-1"
#define CEC_GUID_STRING      "-2"

// SO HEY POTENTIAL POOR SAP WHO IS TRYING TO MAKE SENSE OF ALL THIS (by which I mean my future self)
// There are like four distinct IDs used for joysticks (crazy, right?)
// 1. Device index - this is the "lowest level" identifier, and is just the Nth joystick plugged in to the system (like /dev/js#).
//    It can change even if the device is the same, and is only used to open joysticks (required to receive SDL events).
// 2. SDL_JoystickID - this is an ID for each joystick that is supposed to remain consistent between plugging and unplugging.
//    ES doesn't care if it does, though.
// 3. "Device ID" - this is something I made up and is what InputConfig's getDeviceID() returns.
//    This is actually just an SDL_JoystickID (also called instance ID), but -1 means "keyboard" instead of "error."
// 4. Joystick GUID - this is some squashed version of joystick vendor, version, and a bunch of other device-specific things.
//    It should remain the same across runs of the program/system restarts/device reordering and is what I use to identify which joystick to load.

// hack for cec support
int SDL_USER_CECBUTTONDOWN = -1;
int SDL_USER_CECBUTTONUP   = -1;

InputManager* InputManager::mInstance = NULL;

InputManager::InputManager() : mKeyboardInputConfig(NULL), mBootGraceUntil(0)
{
}

InputManager::~InputManager()
{
	deinit();
}

InputManager* InputManager::getInstance()
{
	if(!mInstance)
		mInstance = new InputManager();

	return mInstance;
}

void InputManager::init()
{
	if(initialized())
		deinit();

	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
		Settings::getInstance()->getBool("BackgroundJoystickInput") ? "1" : "0");
	// Don't enable the HIDAPI drivers by default, it will break the existing configurations
	// for a few controller types, since the names and the input mappings are different.
#if !defined(_WIN32)
#if	SDL_VERSION_ATLEAST(2,0,9)
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
#endif
#endif
	SDL_InitSubSystem(SDL_INIT_JOYSTICK);
	SDL_JoystickEventState(SDL_ENABLE);

	// first, open all currently present joysticks
	int numJoysticks = SDL_NumJoysticks();
	for(int i = 0; i < numJoysticks; i++)
	{
		addJoystickByDeviceIndex(i);
	}

	// 위 스캔에서 열어놓은 장치들에 대해 SDL이 지연된 SDL_JOYDEVICEADDED를 메인 루프
	// 진입 후에야 큐에서 내보내는 경우를 대비한 유예 시간 - 그 안에는 연결 알림 스킵
	mBootGraceUntil = SDL_GetTicks() + 2000;

	mKeyboardInputConfig = new InputConfig(DEVICE_KEYBOARD, "Keyboard", KEYBOARD_GUID_STRING);
	loadInputConfig(mKeyboardInputConfig);

	SDL_USER_CECBUTTONDOWN = SDL_RegisterEvents(2);
	SDL_USER_CECBUTTONUP   = SDL_USER_CECBUTTONDOWN + 1;
	CECInput::init();
	mCECInputConfig = new InputConfig(DEVICE_CEC, "CEC", CEC_GUID_STRING);
	loadInputConfig(mCECInputConfig);
}

void InputManager::addJoystickByDeviceIndex(int id, Window* window)
{
	assert(id > -1);
	assert(id < SDL_NumJoysticks());

	// open joystick & add to our list
	SDL_Joystick* joy = SDL_JoystickOpen(id);
	assert(joy);

	// add it to our list so we can close it again later
	SDL_JoystickID joyId = SDL_JoystickInstanceID(joy);
	mJoysticks[joyId] = joy;

	char guid[65];
	SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), guid, 65);

	// create the InputConfig
	mInputConfigs[joyId] = new InputConfig(joyId, SDL_JoystickName(joy), guid);

	// add Vendor and Product IDs
	mInputConfigs[joyId]->setVendorId(SDL_JoystickGetVendor(joy));
	mInputConfigs[joyId]->setProductId(SDL_JoystickGetProduct(joy));

	if(!loadInputConfig(mInputConfigs[joyId]))
	{
		LOG(LogInfo) << "Added unconfigured joystick '" << SDL_JoystickName(joy) << "' (GUID: " << guid << ", instance ID: " << joyId << ", device index: " << id << ").";
		// window가 있을 때만(= 부팅 시 최초 enumerate가 아니라 런타임 핫플러그일 때만) 알림 —
		// init()의 최초 스캔은 window 없이 호출되므로 부팅 시엔 트리거되지 않음(첫 실행 위자드와 중복 방지).
		if (window)
			window->onUnconfiguredJoystick(mInputConfigs[joyId]);
	}else{
		LOG(LogInfo) << "Added known joystick '" << SDL_JoystickName(joy) << "' (instance ID: " << joyId << ", device index: " << id << ")";
		if (window && SDL_GetTicks() > mBootGraceUntil)
			window->onJoystickNotification(mInputConfigs[joyId]->getDeviceName(), true);
	}

	// set up the prevAxisValues
	int numAxes = SDL_JoystickNumAxes(joy);
	mPrevAxisValues[joyId] = new int[numAxes];
	std::fill(mPrevAxisValues[joyId], mPrevAxisValues[joyId] + numAxes, 0); //initialize array to 0

	// RetroPangui: 축별 쉬는 값 기준점 - 연결 시점에 실제 값을 읽어서 채움
	// (스틱은 대개 0, 일부 컨트롤러의 트리거 축은 -32767 근처에서 쉼)
	mAxisRestValues[joyId] = new int[numAxes];
	for (int i = 0; i < numAxes; i++)
		mAxisRestValues[joyId][i] = SDL_JoystickGetAxis(joy, i);
}

void InputManager::removeJoystickByJoystickID(SDL_JoystickID joyId, Window* window)
{
	assert(joyId != -1);

	// delete old prevAxisValues
	auto axisIt = mPrevAxisValues.find(joyId);
	delete[] axisIt->second;
	mPrevAxisValues.erase(axisIt);

	// RetroPangui: delete old axisRestValues
	auto restIt = mAxisRestValues.find(joyId);
	delete[] restIt->second;
	mAxisRestValues.erase(restIt);

	// delete old InputConfig (매핑돼 있었는지, 알림에 쓸 이름은 erase 전에 확보)
	auto it = mInputConfigs.find(joyId);
	bool wasConfigured = it->second->isConfigured();
	std::string deviceName = it->second->getDeviceName();
	delete it->second;
	mInputConfigs.erase(it);

	// close the joystick
	auto joyIt = mJoysticks.find(joyId);
	LOG(LogInfo) << "Removed joystick '" << SDL_JoystickName(joyIt->second) << "' (instance ID: " << joyId << ")";
	SDL_JoystickClose(joyIt->second);
	mJoysticks.erase(joyIt);

	if (window && wasConfigured && SDL_GetTicks() > mBootGraceUntil)
		window->onJoystickNotification(deviceName, false);
}

void InputManager::deinit()
{
	if(!initialized())
		return;

	for(auto iter = mJoysticks.cbegin(); iter != mJoysticks.cend(); iter++)
	{
		SDL_JoystickClose(iter->second);
	}
	mJoysticks.clear();

	for(auto iter = mInputConfigs.cbegin(); iter != mInputConfigs.cend(); iter++)
	{
		delete iter->second;
	}
	mInputConfigs.clear();

	for(auto iter = mPrevAxisValues.cbegin(); iter != mPrevAxisValues.cend(); iter++)
	{
		delete[] iter->second;
	}
	mPrevAxisValues.clear();

	// RetroPangui: delete axisRestValues
	for(auto iter = mAxisRestValues.cbegin(); iter != mAxisRestValues.cend(); iter++)
	{
		delete[] iter->second;
	}
	mAxisRestValues.clear();

	if(mKeyboardInputConfig != NULL)
	{
		delete mKeyboardInputConfig;
		mKeyboardInputConfig = NULL;
	}

	if(mCECInputConfig != NULL)
	{
		delete mCECInputConfig;
		mCECInputConfig = NULL;
	}

	CECInput::deinit();

	SDL_JoystickEventState(SDL_DISABLE);
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

int InputManager::getNumJoysticks() { return (int)mJoysticks.size(); }

int InputManager::getAxisCountByDevice(SDL_JoystickID id)
{
	return SDL_JoystickNumAxes(mJoysticks[id]);
}

int InputManager::getButtonCountByDevice(SDL_JoystickID id)
{
	if(id == DEVICE_KEYBOARD)
		return 120; //it's a lot, okay.
	else if(id == DEVICE_CEC)
#ifdef HAVE_CECLIB
		return CEC::CEC_USER_CONTROL_CODE_MAX;
#else // HAVE_LIBCEF
		return 0;
#endif // HAVE_CECLIB
	else
		return SDL_JoystickNumButtons(mJoysticks[id]);
}

void InputManager::rumble(SDL_JoystickID deviceId, float strength, int durationMs)
{
	if(!Settings::getInstance()->getBool("MenuRumble"))
		return;

	auto it = mJoysticks.find(deviceId); // 키보드(-1)/CEC(-2)는 여기서 걸러짐
	if(it == mJoysticks.end() || it->second == nullptr)
		return;

	// 사용자 세기 설정(메뉴 슬라이더, 10~100%)을 곱해서 최종 세기 결정 -
	// 호출부는 상대 비율(이동 0.6/선택 1.0)만 넘긴다.
	strength *= Settings::getInstance()->getInt("MenuRumbleStrength") / 100.f;
	if(strength < 0.f) strength = 0.f;
	if(strength > 1.f) strength = 1.f;

	// 두 모터 모두 구동 - PS2(DualShock) 계열은 작은 모터(weak)가 on/off 방식이라
	// 짧은 "톡" 느낌을 담당하고, 큰 모터(strong)는 회전 시동 시간이 필요해 저세기
	// 단펄스로는 체감이 안 됨(2026-07-17 Twin USB 실기기 확인 - strong 18%/40ms는
	// 무감각, weak 100%는 뚜렷). 진동 미지원 장치는 SDL이 -1을 돌려주고 끝.
	Uint16 mag = (Uint16)(strength * 0xFFFF);
	SDL_JoystickRumble(it->second, mag, mag, (Uint32)durationMs);
}

void InputManager::rumbleAll(float strength, int durationMs)
{
	// 세기 슬라이더 조절 중 즉시 피드백용 - 어느 패드로 조작 중인지 슬라이더
	// 콜백에서는 알 수 없어서 열려 있는 패드 전부에 보냄.
	for(auto it = mJoysticks.cbegin(); it != mJoysticks.cend(); ++it)
		rumble(it->first, strength, durationMs);
}

InputConfig* InputManager::getInputConfigByDevice(int device)
{
	if(device == DEVICE_KEYBOARD)
		return mKeyboardInputConfig;
	else if(device == DEVICE_CEC)
		return mCECInputConfig;
	else
		return mInputConfigs[device];
}

bool InputManager::parseEvent(const SDL_Event& ev, Window* window)
{
	bool causedEvent = false;
	switch(ev.type)
	{
	case SDL_JOYAXISMOTION:
	{
		// RetroPangui: 쉬는 값 기준점 대비 편차로 데드존 판정 - 스틱(기준점≈0)은
		// 기존과 동일하게 동작하고, 쉬는 값이 극단(예: -32767)인 트리거 축만
		// 실질적으로 달라짐(항상 "눌림"으로 오판되던 문제 수정)
		int rest = mAxisRestValues[ev.jaxis.which][ev.jaxis.axis];
		int delta = ev.jaxis.value - rest;
		int prevDelta = mPrevAxisValues[ev.jaxis.which][ev.jaxis.axis] - rest;

		//if it switched boundaries
		if((abs(delta) > DEADZONE) != (abs(prevDelta) > DEADZONE))
		{
			int normValue;
			if(abs(delta) <= DEADZONE)
				normValue = 0;
			else
				if(delta > 0)
					normValue = 1;
				else
					normValue = -1;

			window->input(getInputConfigByDevice(ev.jaxis.which), Input(ev.jaxis.which, TYPE_AXIS, ev.jaxis.axis, normValue, false));
			causedEvent = true;
		}

		mPrevAxisValues[ev.jaxis.which][ev.jaxis.axis] = ev.jaxis.value;
		return causedEvent;
	}

	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
		window->input(getInputConfigByDevice(ev.jbutton.which), Input(ev.jbutton.which, TYPE_BUTTON, ev.jbutton.button, ev.jbutton.state == SDL_PRESSED, false));
		return true;

	case SDL_JOYHATMOTION:
		window->input(getInputConfigByDevice(ev.jhat.which), Input(ev.jhat.which, TYPE_HAT, ev.jhat.hat, ev.jhat.value, false));
		return true;

	case SDL_KEYDOWN:
		if(ev.key.keysym.sym == SDLK_BACKSPACE && SDL_IsTextInputActive())
		{
			window->textInput("\b");
		}

		if(ev.key.repeat)
			return false;

		if(ev.key.keysym.sym == SDLK_F4)
		{
			SDL_Event* quit = new SDL_Event();
			quit->type = SDL_QUIT;
			SDL_PushEvent(quit);
			return false;
		}

		window->input(getInputConfigByDevice(DEVICE_KEYBOARD), Input(DEVICE_KEYBOARD, TYPE_KEY, ev.key.keysym.sym, 1, false));
		return true;

	case SDL_KEYUP:
		window->input(getInputConfigByDevice(DEVICE_KEYBOARD), Input(DEVICE_KEYBOARD, TYPE_KEY, ev.key.keysym.sym, 0, false));
		return true;

	case SDL_TEXTINPUT:
		window->textInput(ev.text.text);
		break;

	case SDL_JOYDEVICEADDED:
		addJoystickByDeviceIndex(ev.jdevice.which, window); // ev.jdevice.which is a device index
		return true;

	case SDL_JOYDEVICEREMOVED:
		removeJoystickByJoystickID(ev.jdevice.which, window); // ev.jdevice.which is an SDL_JoystickID (instance ID)
		return false;
	}

	if((ev.type == (unsigned int)SDL_USER_CECBUTTONDOWN) || (ev.type == (unsigned int)SDL_USER_CECBUTTONUP))
	{
		window->input(getInputConfigByDevice(DEVICE_CEC), Input(DEVICE_CEC, TYPE_CEC_BUTTON, ev.user.code, ev.type == (unsigned int)SDL_USER_CECBUTTONDOWN, false));
		return true;
	}

	return false;
}

bool InputManager::loadInputConfig(InputConfig* config)
{
	std::string path = getConfigPath();
	if(!Utils::FileSystem::exists(path))
		return false;

	pugi::xml_document doc;
	pugi::xml_parse_result res = doc.load_file(path.c_str());

	if(!res)
	{
		LOG(LogError) << "Error parsing input config: " << res.description();
		return false;
	}

	pugi::xml_node root = doc.child("inputList");
	if(!root)
		return false;

	pugi::xml_node configNode = root.find_child_by_attribute("inputConfig", "deviceGUID", config->getDeviceGUIDString().c_str());
	if(!configNode)
		configNode = root.find_child_by_attribute("inputConfig", "deviceName", config->getDeviceName().c_str());
	if(!configNode)
		return false;

	config->loadFromXML(configNode);
	return true;
}

//used in an "emergency" where no keyboard config could be loaded from the inputmanager config file
//allows the user to select to reconfigure in menus if this happens without having to delete es_input.cfg manually
void InputManager::loadDefaultKBConfig()
{
	InputConfig* cfg = getInputConfigByDevice(DEVICE_KEYBOARD);

	cfg->clear();
	cfg->mapInput("up", Input(DEVICE_KEYBOARD, TYPE_KEY, SDLK_UP, 1, true));
	cfg->mapInput("down", Input(DEVICE_KEYBOARD, TYPE_KEY, SDLK_DOWN, 1, true));
	cfg->mapInput("left", Input(DEVICE_KEYBOARD, TYPE_KEY, SDLK_LEFT, 1, true));
	cfg->mapInput("right", Input(DEVICE_KEYBOARD, TYPE_KEY, SDLK_RIGHT, 1, true));

	cfg->mapInput("a", Input(DEVICE_KEYBOARD, TYPE_KEY, SDLK_RETURN, 1, true));
	cfg->mapInput("b", Input(DEVICE_KEYBOARD, TYPE_KEY, SDLK_ESCAPE, 1, true));
	cfg->mapInput("start", Input(DEVICE_KEYBOARD, TYPE_KEY, SDLK_F1, 1, true));
	cfg->mapInput("select", Input(DEVICE_KEYBOARD, TYPE_KEY, SDLK_F2, 1, true));

	cfg->mapInput("leftshoulder", Input(DEVICE_KEYBOARD, TYPE_KEY, SDLK_LEFTBRACKET, 1, true));
	cfg->mapInput("rightshoulder", Input(DEVICE_KEYBOARD, TYPE_KEY, SDLK_RIGHTBRACKET, 1, true));
}

void InputManager::writeDeviceConfig(InputConfig* config)
{
	assert(initialized());

	std::string path = getConfigPath();

	pugi::xml_document doc;

	if(Utils::FileSystem::exists(path))
	{
		// merge files
		pugi::xml_parse_result result = doc.load_file(path.c_str());
		if(!result)
		{
			LOG(LogError) << "Error parsing input config: " << result.description();
		}
		else
		{
			// successfully loaded, delete the old entry if it exists
			pugi::xml_node root = doc.child("inputList");
			if(root)
			{
				// if inputAction @type=onfinish is set, let onfinish command take care for creating input configuration.
				// we just put the input configuration into a temporary input config file.
				pugi::xml_node actionnode = root.find_child_by_attribute("inputAction", "type", "onfinish");
				if(actionnode)
				{
					path = getTemporaryConfigPath();
					doc.reset();
					root = doc.append_child("inputList");
				}
				else
				{
					pugi::xml_node oldEntry = root.find_child_by_attribute("inputConfig", "deviceGUID",
											  config->getDeviceGUIDString().c_str());
					if(oldEntry)
					{
						root.remove_child(oldEntry);
					}
					oldEntry = root.find_child_by_attribute("inputConfig", "deviceName",
															config->getDeviceName().c_str());
					if(oldEntry)
					{
						root.remove_child(oldEntry);
					}
				}
			}
		}
	}

	pugi::xml_node root = doc.child("inputList");
	if(!root)
		root = doc.append_child("inputList");

	config->writeToXML(root);
	doc.save_file(path.c_str());

	Scripting::fireEvent("config-changed");
	Scripting::fireEvent("controls-changed");

	// execute any onFinish commands and re-load the config for changes
	doOnFinish();
	loadInputConfig(config);
}

void InputManager::doOnFinish()
{
	assert(initialized());
	std::string path = getConfigPath();
	pugi::xml_document doc;

	if(Utils::FileSystem::exists(path))
	{
		pugi::xml_parse_result result = doc.load_file(path.c_str());
		if(!result)
		{
			LOG(LogError) << "Error parsing input config: " << result.description();
		}
		else
		{
			pugi::xml_node root = doc.child("inputList");
			if(root)
			{
				root = root.find_child_by_attribute("inputAction", "type", "onfinish");
				if(root)
				{
					for(pugi::xml_node command = root.child("command"); command;
							command = command.next_sibling("command"))
					{
						std::string tocall = command.text().get();

						LOG(LogInfo) << "	" << tocall;
						std::cout << "==============================================\ninput config finish command:\n";
						int exitCode = runSystemCommand(tocall);
						std::cout << "==============================================\n";

						if(exitCode != 0)
						{
							LOG(LogWarning) << "...launch terminated with nonzero exit code " << exitCode << "!";
						}
					}
				}
			}
		}
	}
}

std::string InputManager::getConfigPath()
{
	std::string path = Utils::FileSystem::getHomePath();
	path += "/.emulationstation/es_input.cfg";
	return path;
}

std::string InputManager::getTemporaryConfigPath()
{
	std::string path = Utils::FileSystem::getHomePath();
	path += "/.emulationstation/es_temporaryinput.cfg";
	return path;
}

bool InputManager::initialized() const
{
	return mKeyboardInputConfig != NULL;
}

int InputManager::getNumConfiguredDevices()
{
	int num = 0;
	for(auto it = mInputConfigs.cbegin(); it != mInputConfigs.cend(); it++)
	{
		if(it->second->isConfigured())
			num++;
	}

	if(mKeyboardInputConfig->isConfigured())
		num++;

	if(mCECInputConfig->isConfigured())
		num++;

	return num;
}

std::string InputManager::getDeviceGUIDString(int deviceId)
{
	if(deviceId == DEVICE_KEYBOARD)
		return KEYBOARD_GUID_STRING;

	if(deviceId == DEVICE_CEC)
		return CEC_GUID_STRING;

	auto it = mJoysticks.find(deviceId);
	if(it == mJoysticks.cend())
	{
		LOG(LogError) << "getDeviceGUIDString - deviceId " << deviceId << " not found!";
		return "something went horribly wrong";
	}

	char guid[65];
	SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(it->second), guid, 65);
	return std::string(guid);
}
