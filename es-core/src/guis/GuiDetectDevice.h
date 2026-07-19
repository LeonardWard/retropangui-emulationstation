#pragma once
#ifndef ES_CORE_GUIS_GUI_DETECT_DEVICE_H
#define ES_CORE_GUIS_GUI_DETECT_DEVICE_H

#include "components/ComponentGrid.h"
#include "components/NinePatchComponent.h"
#include "GuiComponent.h"

class TextComponent;

class GuiDetectDevice : public GuiComponent
{
public:
	// autoDismissIfConfigured: 핫플러그(미설정 패드 연결)로 자동으로 뜬 창에서만 true.
	// 버튼을 누른 패드가 이미 매핑돼 있으면 창을 스스로 닫음 - 메뉴의 CONFIGURE INPUT은
	// 의도적 재매핑이므로 false 유지(true면 매핑된 패드를 다시 매핑할 방법이 없어짐).
	GuiDetectDevice(Window* window, bool firstRun, const std::function<void()>& doneCallback,
		bool autoDismissIfConfigured = false);

	bool input(InputConfig* config, Input input) override;
	void update(int deltaTime) override;
	void onSizeChanged() override;

private:
	bool mFirstRun;
	bool mAutoDismissIfConfigured;
	InputConfig* mHoldingConfig;
	int mHoldTime;

	NinePatchComponent mBackground;
	ComponentGrid mGrid;

	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<TextComponent> mMsg1;
	std::shared_ptr<TextComponent> mMsg2;
	std::shared_ptr<TextComponent> mDeviceInfo;
	std::shared_ptr<TextComponent> mDeviceHeld;

	std::function<void()> mDoneCallback;
};

#endif // ES_CORE_GUIS_GUI_DETECT_DEVICE_H
