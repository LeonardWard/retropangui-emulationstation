#pragma once
#ifndef ES_APP_GUIS_GUI_SAVE_STATES_H
#define ES_APP_GUIS_GUI_SAVE_STATES_H

#include "GuiComponent.h"
#include "components/NinePatchComponent.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ComponentList;
class ImageComponent;
class TextComponent;
class FileData;
class Window;

struct SaveStateInfo
{
	int slot;                  // 0 = <이름>.state, N = <이름>.stateN
	std::string statePath;     // 스테이트 파일 전체 경로
	std::string thumbnailPath; // statePath + ".png" (없으면 빈 문자열)
	std::string label;         // "SLOT 0 — 2026-07-17 01:23" 형식
};

// RetroPangUI: 게임 실행 직전에 끼어들어 세이브 스테이트 슬롯 목록을 보여주고
// 하나를 골라 이어하거나(--entryslot) 새로 시작하게 하는 화면. Recalbox
// GuiSaveStates와 같은 UX(세로 리스트 + 단일 썸네일 패널, 그리드 아님) -
// todo-20260706-savestate-preview.html의 확정 설계.
class GuiSaveStates : public GuiComponent
{
public:
	// launchCb: 사용자가 고른 슬롯 번호로 호출됨. -1 = 스테이트 없이 새로 시작.
	GuiSaveStates(Window* window, FileData* game, const std::function<void(int)>& launchCb);

	// 이 게임의 스테이트 슬롯 목록(슬롯 번호 오름차순). ViewController가
	// "스테이트가 하나라도 있나" 판단할 때도 이 함수를 씀.
	static std::vector<SaveStateInfo> scanSaveStates(FileData* game);

	bool input(InputConfig* config, Input input) override;
	void onSizeChanged() override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	void updateThumbnail();

	NinePatchComponent mBackground;
	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<ComponentList> mList;
	std::shared_ptr<ImageComponent> mThumbnail;
	std::vector<SaveStateInfo> mStates;
	std::function<void(int)> mLaunchCb;
};

#endif // ES_APP_GUIS_GUI_SAVE_STATES_H
