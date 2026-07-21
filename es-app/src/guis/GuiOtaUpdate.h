#pragma once
#ifndef ES_APP_GUIS_GUI_OTA_UPDATE_H
#define ES_APP_GUIS_GUI_OTA_UPDATE_H

#include "GuiComponent.h"
#include "Window.h"
#include "components/AnimatedImageComponent.h"
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
	               std::function<void(bool)> done_fn,
	               const std::string& message = "업데이트 다운로드 중...\n잠시 기다려주세요.");

	void update(int dt) override;
	void onSizeChanged() override;
	bool input(InputConfig* config, Input input) override { return true; }

private:
	NinePatchComponent mBackground;
	std::shared_ptr<AnimatedImageComponent> mSpinner;
	std::shared_ptr<TextComponent> mMsg;
	std::future<int> mFuture;
	std::function<void(bool)> mDoneFn;
};

// 버전 확인 대기 화면.
// 생성 즉시 백그라운드 스레드에서 check_fn() 실행.
// 완료되면 자신을 Window에서 제거하고 done_fn(serverVer) 호출. 실패 시 빈 문자열.
class GuiOtaCheck : public GuiComponent
{
public:
	GuiOtaCheck(Window* window,
	            std::function<std::string()> check_fn,
	            std::function<void(std::string)> done_fn);

	void update(int dt) override;
	void onSizeChanged() override;
	bool input(InputConfig* config, Input input) override { return true; }

private:
	NinePatchComponent mBackground;
	std::shared_ptr<AnimatedImageComponent> mSpinner;
	std::shared_ptr<TextComponent> mMsg;
	std::future<std::string> mFuture;
	std::function<void(std::string)> mDoneFn;
};

#endif // ES_APP_GUIS_GUI_OTA_UPDATE_H
