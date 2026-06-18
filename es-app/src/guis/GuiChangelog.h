#pragma once
#ifndef ES_APP_GUIS_GUI_CHANGELOG_H
#define ES_APP_GUIS_GUI_CHANGELOG_H

#include "GuiComponent.h"
#include "Window.h"
#include "components/NinePatchComponent.h"
#include "components/ScrollableContainer.h"
#include "components/TextComponent.h"

// OTA 업데이트 완료 후 1회 표시되는 변경 내역 팝업.
// /etc/.ota-changelog-pending 플래그 파일이 존재할 때 표시되며,
// 닫으면 해당 파일을 삭제한다.
class GuiChangelog : public GuiComponent
{
public:
    explicit GuiChangelog(Window* window);
    bool input(InputConfig* config, Input input) override;
    void onSizeChanged() override;

private:
    void close();

    NinePatchComponent  mBackground;
    TextComponent       mTitle;
    TextComponent       mVersion;
    ScrollableContainer mScrollContainer;
    TextComponent       mBody;
    TextComponent       mFooter;
};

#endif // ES_APP_GUIS_GUI_CHANGELOG_H
