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
	// RetroPangui: 축별 "쉬는 값" 기준점 - 연결 시점의 실제 축 값으로 채움.
	// 아날로그 스틱은 보통 0으로 쉬지만, 일부 컨트롤러(예: 특정 Xbox 계열)의
	// L2/R2 트리거 축은 raw joystick API에서 -32767 근처에서 쉬는 경우가 있어
	// DEADZONE을 0 기준으로만 적용하면 항상 "눌린 상태"로 오판됨 - 이 기준점
	// 대비 편차로 데드존을 계산해서 바로잡음(스틱류는 기준점이 이미 0이라
	// 기존 동작과 동일, 트리거류만 실질적으로 바뀜).
	std::map<SDL_JoystickID, int*> mAxisRestValues;

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

	// 메뉴 조작감용 짧은 진동 펄스. deviceId가 키보드/CEC(-1/-2)거나 장치가
	// 진동 미지원이면 조용히 무시. strength 0.0~1.0 (저주파 모터만 사용).
	void rumble(SDL_JoystickID deviceId, float strength, int durationMs);

	InputConfig* getInputConfigByDevice(int deviceId);

	bool parseEvent(const SDL_Event& ev, Window* window);
};

#endif // ES_CORE_INPUT_MANAGER_H
