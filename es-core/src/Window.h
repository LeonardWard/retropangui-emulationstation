#pragma once
#ifndef ES_CORE_WINDOW_H
#define ES_CORE_WInDOW_H

#include "HelpPrompt.h"
#include "InputConfig.h"
#include "Settings.h"

#include <functional>
#include <memory>

class SystemData;
class FileData;
class Font;
class GuiComponent;
class HelpComponent;
class ImageComponent;
class InputConfig;
class TextCache;
class Transform4x4f;
struct HelpStyle;

class Window
{
public:
	class ScreenSaver {
	public:
		virtual void startScreenSaver(SystemData* system=NULL) = 0;
		virtual void stopScreenSaver(bool toResume=false) = 0;
		virtual void renderScreenSaver() = 0;
		virtual bool allowSleep() = 0;
		virtual void update(int deltaTime) = 0;
		virtual bool isScreenSaverActive() = 0;
		virtual FileData* getCurrentGame() = 0;
		virtual void selectGame(bool launch) = 0;
		virtual bool inputDuringScreensaver(InputConfig* config, Input input) = 0;
	};

	class InfoPopup {
	public:
		virtual void render(const Transform4x4f& parentTrans) = 0;
		virtual void stop() = 0;
		virtual ~InfoPopup() {};
	};

	Window();
	~Window();

	void pushGui(GuiComponent* gui);
	void removeGui(GuiComponent* gui);
	GuiComponent* peekGui();
	inline int getGuiStackSize() { return (int)mGuiStack.size(); }

	void textInput(const char* text);
	void input(InputConfig* config, Input input);
	void update(int deltaTime);
	void render();

	bool init();
	void deinit();

	void normalizeNextUpdate();

	inline bool isSleeping() const { return mSleeping; }
	bool getAllowSleep();
	void setAllowSleep(bool sleep);

	void renderLoadingScreen(std::string text, float percent = -1, unsigned char opacity = 255);

	void renderHelpPromptsEarly(); // used to render HelpPrompts before a fade
	void setHelpPrompts(const std::vector<HelpPrompt>& prompts, const HelpStyle& style);

	void setScreenSaver(ScreenSaver* screenSaver) { mScreenSaver = screenSaver; }
	void setInfoPopup(InfoPopup* infoPopup) { delete mInfoPopup; mInfoPopup = infoPopup; }
	inline void stopInfoPopup() { if (mInfoPopup) mInfoPopup->stop(); };

	void setStorageDetectedCallback(std::function<void(const std::string& label, const std::string& id)> cb) { mStorageDetectedCallback = cb; }

	// 런타임 중 핫플러그된 미매핑 컨트롤러 알림 (InputManager::addJoystickByDeviceIndex에서 호출)
	void setUnconfiguredJoystickCallback(std::function<void(InputConfig*)> cb) { mUnconfiguredJoystickCallback = cb; }
	void onUnconfiguredJoystick(InputConfig* config) { if (mUnconfiguredJoystickCallback) mUnconfiguredJoystickCallback(config); }

	// 이미 매핑된(es_input.cfg에 등록된) 패드가 런타임 중 연결/해제될 때 OSD 알림
	// (InputManager::addJoystickByDeviceIndex/removeJoystickByJoystickID에서 호출)
	void setJoystickNotificationCallback(std::function<void(const std::string& name, bool connected)> cb) { mJoystickNotificationCallback = cb; }
	void onJoystickNotification(const std::string& name, bool connected) { if (mJoystickNotificationCallback) mJoystickNotificationCallback(name, connected); }

	// 2026-07-16: USB/블루투스 오디오 장치 연결/해제 OSD 알림 - 조이스틱과 동일한
	// 패턴이지만 SDL 이벤트가 없어서(오디오는 핫플러그 콜백이 없음) checkNewStorage()
	// 처럼 폴링 방식(/proc/asound/cards + discovery.json)으로 감지한다.
	void setAudioDeviceNotificationCallback(std::function<void(const std::string& name, bool connected)> cb) { mAudioDeviceNotificationCallback = cb; }

	// 이스터에그(코나미 커맨드) 완성 시 호출 - es-app에서 실제 팝업 GUI를 등록
	void setEasterEggCallback(std::function<void()> cb) { mEasterEggCallback = cb; }

	void startScreenSaver(SystemData* system=NULL);
	bool cancelScreenSaver();
	void renderScreenSaver();

private:
	void onSleep();
	void onWake();

	// Returns true if at least one component on the stack is processing
	bool isProcessing();

	HelpComponent*	mHelp;
	ImageComponent* mBackgroundOverlay;
	ScreenSaver*	mScreenSaver;
	InfoPopup*	mInfoPopup;
	bool		mRenderScreenSaver;

	std::vector<GuiComponent*> mGuiStack;

	std::vector< std::shared_ptr<Font> > mDefaultFonts;

	int mFrameTimeElapsed;
	int mFrameCountElapsed;
	int mAverageDeltaTime;

	std::unique_ptr<TextCache> mFrameDataText;

	bool mNormalizeNextUpdate;

	bool mAllowSleep;
	bool mSleeping;
	unsigned int mTimeSinceLastInput;

	bool mRenderedHelpPrompts;

	int  mStorageCheckTimer;
	std::function<void(const std::string& label, const std::string& id)> mStorageDetectedCallback;
	void checkNewStorage();

	std::function<void(InputConfig*)> mUnconfiguredJoystickCallback;
	std::function<void(const std::string& name, bool connected)> mJoystickNotificationCallback;

	int mAudioDeviceCheckTimer;
	std::vector<std::string> mKnownAudioDevices; // 지난 체크 시점의 (라벨) 스냅샷
	bool mAudioDeviceFirstCheck; // 최초 1회는 알림 없이 스냅샷만(부팅 시 이미 꽂힌 것들 걸러내기)
	std::function<void(const std::string& name, bool connected)> mAudioDeviceNotificationCallback;
	void checkAudioDeviceChange();

	// 이스터에그: 어느 화면에서든(메뉴 입력을 가로채지 않고 그냥 관찰만) 코나미
	// 커맨드를 완성하면 콜백 호출 - 정상 내비게이션엔 전혀 영향 없음(input()이
	// 이 함수 호출 후에도 그대로 peekGui()로 넘어감). 실제로 띄우는 GUI(팡이
	// 기록 팝업)는 es-app 쪽 클래스라 es-core에서 직접 참조할 수 없으므로,
	// 다른 알림들(storage/joystick 등)과 동일하게 콜백 등록 패턴을 씀.
	std::vector<std::string> mEasterEggSequence;
	size_t mEasterEggProgress;
	std::function<void()> mEasterEggCallback;
	void checkEasterEggInput(InputConfig* config, Input input);
};

#endif // ES_CORE_WINDOW_H
