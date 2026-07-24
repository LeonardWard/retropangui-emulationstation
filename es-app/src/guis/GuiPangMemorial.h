#pragma once
#ifndef ES_APP_GUIS_GUI_PANG_MEMORIAL_H
#define ES_APP_GUIS_GUI_PANG_MEMORIAL_H

#include "components/ComponentGrid.h"
#include "components/ImageComponent.h"
#include "components/NinePatchComponent.h"
#include "GuiComponent.h"

class ButtonComponent;
class TextComponent;

// 이스터에그(코나미 커맨드) 전용 팝업 - 프로젝트 이름의 유래가 된 반려견 "팡이"의
// 사진과 기록을 보여줌. GuiMsgBox와 같은 프레임/버튼 스타일을 따르되 사진 한 장이
// 추가된 3행(사진/텍스트/버튼) 그리드 구성.
class GuiPangMemorial : public GuiComponent
{
public:
	explicit GuiPangMemorial(Window* window);

	bool input(InputConfig* config, Input input) override;
	void onSizeChanged() override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	NinePatchComponent mBackground;
	ComponentGrid mGrid;

	std::shared_ptr<ImageComponent> mPhoto;
	std::shared_ptr<TextComponent> mInfo;
	std::vector< std::shared_ptr<ButtonComponent> > mButtons;
	std::shared_ptr<ComponentGrid> mButtonGrid;
	std::function<void()> mAcceleratorFunc;
};

#endif // ES_APP_GUIS_GUI_PANG_MEMORIAL_H
