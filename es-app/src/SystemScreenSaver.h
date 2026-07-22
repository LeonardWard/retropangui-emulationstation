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
	// RetroPangui: "web stream" 모드 - wpewebkit/cog로 C5가 직접 웹페이지를
	// DRM에 렌더링(클라우드 서버 없는 온디바이스 구현, 2026-07-23). URL
	// 목록/순환 주기는 retropangui.conf를 스크립트가 직접 읽으므로 여기서는
	// 인자 없이 launchGame()과 동일한 패턴(window/input/audio deinit → 외부
	// 프로세스 → 재init)으로 스크립트만 실행. 사용자 입력이 들어올 때까지
	// 블로킹됨(게임 실행과 동일).
	void runWebStreamScreensaver();
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
	// 도중 호출, render()와 별개 시점)에서 처리하도록 넘겨주는 대기 플래그
	bool mPendingWebStreamScreensaver = false;
	int			mAllFilesSize;
	std::thread*		mThread;
	bool			mExit;
	std::string 		mRegularEditingCollection;
};

#endif // ES_APP_SYSTEM_SCREEN_SAVER_H
