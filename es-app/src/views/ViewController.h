#pragma once
#ifndef ES_APP_VIEWS_VIEW_CONTROLLER_H
#define ES_APP_VIEWS_VIEW_CONTROLLER_H

#include "renderers/Renderer.h"
#include "FileData.h"
#include "GuiComponent.h"
#include <vector>

class IGameListView;
class SystemData;
class SystemView;

// Used to smoothly transition the camera between multiple views (e.g. from system to system, from gamelist to gamelist).
class ViewController : public GuiComponent
{
public:
	static void init(Window* window);
	static ViewController* get();

	virtual ~ViewController();

	// Try to completely populate the GameListView map.
	// Caches things so there's no pauses during transitions.
	void preload();

	// If a basic view detected a metadata change, it can request to recreate
	// the current gamelist view (as it may change to be detailed).
	void reloadGameListView(IGameListView* gamelist, bool reloadTheme = false);
	inline void reloadGameListView(SystemData* system, bool reloadTheme = false) { reloadGameListView(getGameListView(system).get(), reloadTheme); }
	void reloadAll(bool themeChanged = false); // Reload everything with a theme.  When the "ThemeSet" setting changes, themeChanged is true.

	// Navigation.
	void goToNextGameList();
	void goToPrevGameList();
	void goToGameList(SystemData* system);
	void goToSystemView(SystemData* system);
	void goToStart();
	void ReloadAndGoToStart();

	void onFileChanged(FileData* file, FileChangeType change);

	// Plays a nice launch effect and launches the game at the end of it.
	// Once the game terminates, plays a return effect.
	// entrySlot: 세이브 스테이트 미리보기(GuiSaveStates)에서 고른 슬롯
	// (-1 = 스테이트 없이 실행). skipSaveStatePreview: GuiSaveStates 콜백에서
	// 되돌아온 호출임을 표시 - 미리보기 화면을 또 띄우는 재귀를 막는다.
	void launch(FileData* game, Vector3f centerCameraOn = Vector3f(Renderer::getScreenWidth() / 2.0f, Renderer::getScreenHeight() / 2.0f, 0), int entrySlot = -1, bool skipSaveStatePreview = false);

	bool input(InputConfig* config, Input input) override;
	void update(int deltaTime) override;
	void render(const Transform4x4f& parentTrans) override;

	enum ViewMode
	{
		NOTHING,
		START_SCREEN,
		SYSTEM_SELECT,
		GAME_LIST
	};

	enum GameListViewType
	{
		AUTOMATIC,
		BASIC,
		DETAILED,
		GRID,
		VIDEO
	};

	ViewController::GameListViewType getGameListViewType();

	struct State
	{
		ViewMode viewing;

		inline SystemData* getSystem() const { assert(viewing == GAME_LIST || viewing == SYSTEM_SELECT); return system; }

	private:
		friend ViewController;
		SystemData* system;
	};

	inline const State& getState() const { return mState; }

	virtual std::vector<HelpPrompt> getHelpPrompts() override;
	virtual HelpStyle getHelpStyle() override;

	std::shared_ptr<IGameListView> getGameListView(SystemData* system);
	std::shared_ptr<SystemView> getSystemListView();
	void removeGameListView(SystemData* system);

private:
	ViewController(Window* window);
	static ViewController* sInstance;

	void playViewTransition();
	int getSystemId(SystemData* system);

	std::shared_ptr<GuiComponent> mCurrentView;
	std::map< SystemData*, std::shared_ptr<IGameListView> > mGameListViews;
	std::shared_ptr<SystemView> mSystemListView;

	Transform4x4f mCamera;
	float mFadeOpacity;
	bool mLockInput;

	State mState;
};

#endif // ES_APP_VIEWS_VIEW_CONTROLLER_H
