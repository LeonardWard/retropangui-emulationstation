#pragma once
#ifndef ES_CORE_INPUT_MANAGER_H
#define ES_CORE_INPUT_MANAGER_H

#include <SDL_joystick.h>
#include <map>
#include <string>

class InputConfig;
class Window;
union SDL_Event;

//you should only ever instantiate one of these, by the way
class InputManager
{
private:
	InputManager();

	static InputManager* mInstance;

	static const int DEADZONE = 23000;

	void loadDefaultKBConfig();

	std::map<SDL_JoystickID, SDL_Joystick*> mJoysticks;
	std::map<SDL_JoystickID, InputConfig*> mInputConfigs;
	InputConfig* mKeyboardInputConfig;
	InputConfig* mCECInputConfig;

	std::map<SDL_JoystickID, int*> mPrevAxisValues;

	// init()의 최초 스캔 직후 잠깐 동안은 연결 알림을 띄우지 않음 - SDL이 부팅 시
	// 이미 열려 있던 장치에 대한 지연된 SDL_JOYDEVICEADDED를 메인 루프 진입 후에야
	// 뒤늦게 큐에서 꺼내 주는 경우가 있어, 그 이벤트가 window!=nullptr 상태로 다시
	// 처리되면서 부팅 직후 스팸성 알림이 뜨는 걸 막기 위함.
	Uint32 mBootGraceUntil;

	bool initialized() const;

	void addJoystickByDeviceIndex(int id, Window* window = nullptr);
	void removeJoystickByJoystickID(SDL_JoystickID id, Window* window = nullptr);
	bool loadInputConfig(InputConfig* config); // returns true if successfully loaded, false if not (or didn't exist)

public:
	virtual ~InputManager();

	static InputManager* getInstance();

	void writeDeviceConfig(InputConfig* config);
	void doOnFinish();
	static std::string getConfigPath();
	static std::string getTemporaryConfigPath();

	void init();
	void deinit();

	int getNumJoysticks();
	int getAxisCountByDevice(int deviceId);
	int getButtonCountByDevice(int deviceId);
	int getNumConfiguredDevices();

	std::string getDeviceGUIDString(int deviceId);

	InputConfig* getInputConfigByDevice(int deviceId);

	bool parseEvent(const SDL_Event& ev, Window* window);
};

#endif // ES_CORE_INPUT_MANAGER_H
