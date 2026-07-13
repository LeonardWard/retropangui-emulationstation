#pragma once
#ifndef ES_APP_GUIS_GUI_GAMELIST_REFRESH_H
#define ES_APP_GUIS_GUI_GAMELIST_REFRESH_H

#include "GuiComponent.h"
#include "components/NinePatchComponent.h"

#include <memory>
#include <string>
#include <vector>

class SystemData;
class TextComponent;

// RetroPangui: "게임 리스트 갱신" 진행 창.
// 프레임당 시스템 하나씩 SystemData::refreshGamelist()를 실행해 UI를 살려둔 채
// 시스템별 신규 등록 수를 실시간으로 보여준다. 완료 후 B로 닫는다.
class GuiGamelistRefresh : public GuiComponent
{
public:
	GuiGamelistRefresh(Window* window, const std::vector<SystemData*>& systems);

	void update(int deltaTime) override;
	bool input(InputConfig* config, Input input) override;
	void onSizeChanged() override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	void appendLine(const std::string& line);

	NinePatchComponent mBackground;
	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<TextComponent> mText;

	std::vector<SystemData*> mSystems;
	std::vector<SystemData*> mChanged;
	std::string mLog;
	size_t mIndex;
	int mTotalAdded;
	bool mFailed;
	bool mDone;
};

#endif // ES_APP_GUIS_GUI_GAMELIST_REFRESH_H
