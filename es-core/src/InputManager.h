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
	// RetroPangui: 사용자가 마법사로 잡은 매핑 전용 파일. 시스템 기본값
	// es_input.cfg는 메이저 OTA 때 번들로 리셋되므로, 사용자 매핑은 이 파일에
	// 분리 저장해서 영구 보존한다. 읽기는 사용자 파일 우선, 쓰기는 항상 여기로.
	static std::string getUserConfigPath();
	static std::string getTemporaryConfigPath();

	void init();
	void deinit();

	int getNumJoysticks();
	int getAxisCountByDevice(int deviceId);
	int getButtonCountByDevice(int deviceId);
	int getNumConfiguredDevices();

	std::string getDeviceGUIDString(int deviceId);

	// 메뉴 조작감용 짧은 진동 펄스. deviceId가 키보드/CEC(-1/-2)거나 장치가
	// 진동 미지원이면 조용히 무시. strength는 상대 비율(0.0~1.0)이고 실제 세기는
	// MenuRumbleStrength 설정(%)이 곱해져 결정됨. 두 모터 모두 구동.
	void rumble(SDL_JoystickID deviceId, float strength, int durationMs);
	// 열려 있는 모든 패드에 rumble() - 세기 슬라이더 즉시 피드백용.
	void rumbleAll(float strength, int durationMs);
	// 공용 프리셋 - 이동(메뉴/캐러셀/롬리스트)과 선택을 모든 훅 지점에서
	// 동일한 느낌으로 통일 (상대비율/길이의 단일 진실 공급원).
	// RetroPangui: 기존 0.6/60ms는 모터 회전 시동 시간을 못 채워서 사실상
	// 전혀 안 느껴지는 문제가 있었음(2026-07-24 실기기 확인 - 슬라이더의
	// 1.0/90ms는 확실히 느껴지는데 이동 진동은 전무). GuiArcadeVirtualKeyboard가
	// 예전에 이미 같은 이유로 select 세기를 그대로 갖다 쓴 전례가 있어서,
	// 그 판단을 여기 기본값에도 반영 - select보다 살짝 약하게 남기되(구분감
	// 유지) 확실히 체감되는 선(0.9/80ms)으로 상향.
	void rumbleNav(SDL_JoystickID deviceId)    { rumble(deviceId, 0.9f, 80); }
	void rumbleSelect(SDL_JoystickID deviceId) { rumble(deviceId, 1.0f, 90); }

	InputConfig* getInputConfigByDevice(int deviceId);

	bool parseEvent(const SDL_Event& ev, Window* window);
};

#endif // ES_CORE_INPUT_MANAGER_H
