#pragma once
#ifndef ES_APP_GUIS_GUI_OTA_UPDATE_H
#define ES_APP_GUIS_GUI_OTA_UPDATE_H

#include "GuiComponent.h"
#include "components/NinePatchComponent.h"
#include "components/TextComponent.h"
#include <future>
#include <functional>
#include <string>

// OTA 다운로드 진행 화면.
// 생성 즉시 백그라운드 스레드에서 download_fn() 실행.
// 완료되면 자신을 Window에서 제거하고 done_fn(success) 호출.
class GuiOtaDownload : public GuiComponent
{
public:
	GuiOtaDownload(Window* window,
	               std::function<int()> download_fn,
	               std::function<void(bool)> done_fn);

	void update(int dt) override;
	void onSizeChanged() override;
	bool input(InputConfig* config, Input input) override { return true; }

private:
	NinePatchComponent mBackground;
	std::shared_ptr<TextComponent> mMsg;
	std::future<int> mFuture;
	std::function<void(bool)> mDoneFn;
};

#endif // ES_APP_GUIS_GUI_OTA_UPDATE_H
