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
// 사진과 기록을 보여줌. GuiMsgBox와 같은 프레임/버튼 스타일을 따르되, 좌측에
// 텍스트(좌정렬 필드 목록) · 우측에 사진을 배치한 상단 행 + 버튼 행 구성.
// ComponentGrid는 셀 크기와 내용물 크기가 정확히 같지 않으면 가운데 정렬로
// 밀어버리므로(2026-07-24 실기기에서 사진이 카드 오른쪽 끝에 딱 붙어버리는
// 문제로 실측), 여백을 "여유 공간에 기대는 방식"이 아니라 좌측여백/텍스트/
// 간격/사진/우측여백 5개 열의 정확한 픽셀 폭으로 직접 명시한다.
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
