#pragma once
#ifndef ES_APP_SYSTEM_SCREEN_SAVER_H
#define ES_APP_SYSTEM_SCREEN_SAVER_H

#include "Window.h"
#include <thread>

class ImageComponent;
class Sound;
class VideoComponent;

// Screensaver implementation for main window
class SystemScreenSaver : public Window::ScreenSaver
{
public:
	SystemScreenSaver(Window* window);
	virtual ~SystemScreenSaver();

	virtual void startScreenSaver(SystemData* system=NULL);
	virtual void stopScreenSaver(bool toResume=false);
	virtual void renderScreenSaver();
	virtual bool allowSleep();
	virtual void update(int deltaTime);
	virtual bool isScreenSaverActive();

	virtual FileData* getCurrentGame();
	virtual void selectGame(bool launch);
	virtual bool inputDuringScreensaver(InputConfig* config, Input input);

private:
	void changeMediaItem(bool next = true);
	void pickGameListNode(const char *nodeName);
	void prepareScreenSaverMedia(const char *nodeName, std::string& path);
	void pickRandomVideo(std::string& path, bool keepSame = false);
	void pickRandomGameListImage(std::string& path, bool keepSame = false);
	void pickRandomCustomMedia(std::string& path);
	void setVideoScreensaver(std::string& path);
	void setImageScreensaver(std::string& path);
	// RetroPangui: "web stream" 모드 - ES 내장 VLC 콜백 디코딩이 라이브 네트워크
	// 스트림에서 화면 깨짐을 일으켜서(2026-07-23 실기기 확인), launchGame()과
	// 동일한 패턴(window/input/audio deinit → 외부 프로세스 → 재init)으로
	// mpv를 직접 DRM에 재생시킴. 이 호출은 사용자 입력이 들어올 때까지
	// 블로킹됨(게임 실행과 동일).
	void runWebStreamScreensaver(const std::string& url);
	bool isFileVideo(std::string& path);
	std::vector<std::string> getCustomMediaFiles(const std::string &mediaDir);
	void getAllGamelistNodes();
	void getAllGamelistNodesForSystem(SystemData* system);
	void backgroundIndexing();
	void setBackground();
	void handleScreenSaverEditingCollection();
	void input(InputConfig* config, Input input);

	enum STATE {
		STATE_INACTIVE,
		STATE_FADE_OUT_WINDOW,
		STATE_FADE_IN_VIDEO,
		STATE_SCREENSAVER_ACTIVE
	};

private:
	VideoComponent*		mVideoScreensaver;
	ImageComponent*		mImageScreensaver;
	Window*			mWindow;
	SystemData*		mSystem;
	STATE			mState;
	float			mOpacity;
	int			mTimer;
	FileData*		mCurrentGame;
	FileData*		mPreviousGame;
	int			mSwapTimeout;
	std::shared_ptr<Sound>	mBackgroundAudio;
	bool			mStopBackgroundAudio;
	std::vector<FileData*>	mAllFiles;
	std::vector<std::string> mCustomMediaFiles;
	// RetroPangui: "web stream" 모드 - startScreenSaver()(Window::render() 도중
	// 호출됨)에서 바로 블로킹 실행하면 재진입 문제가 있어, update()(Window::update()
	// 도중 호출, render()와 별개 시점)에서 처리하도록 넘겨주는 대기값
	std::string mPendingWebStreamUrl;
	int			mAllFilesSize;
	std::thread*		mThread;
	bool			mExit;
	std::string 		mRegularEditingCollection;
};

#endif // ES_APP_SYSTEM_SCREEN_SAVER_H
