#include "guis/GuiOtaUpdate.h"
#include "components/ImageComponent.h"
#include "renderers/Renderer.h"
#include "resources/Font.h"
#include <algorithm>

// BusyComponent와 동일한 회전 애니메이션 프레임 재사용 (GuiBtPairing.cpp와 같은 패턴) -
// 다운로드/버전확인 팝업이 좁아 글자가 잘린다는 실기기 피드백(2026-07-22)으로
// 스피너를 추가하고 박스도 키움.
static AnimationFrame OTA_SPINNER_ANIM_FRAMES[] = {
	{":/busy_0.svg", 300},
	{":/busy_1.svg", 300},
	{":/busy_2.svg", 300},
	{":/busy_3.svg", 300},
};
static const AnimationDef OTA_SPINNER_ANIM_DEF = { OTA_SPINNER_ANIM_FRAMES, 4, true };

GuiOtaDownload::GuiOtaDownload(Window* window,
                               std::function<int()> download_fn,
                               std::function<void(bool)> done_fn,
                               const std::string& message)
	: GuiComponent(window),
	  mBackground(window, ":/frame.png"),
	  mSpinner(std::make_shared<AnimatedImageComponent>(window)),
	  mMsg(std::make_shared<TextComponent>(window,
	       message,
	       Font::get(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_CENTER)),
	  mFuture(std::async(std::launch::async, download_fn)),
	  mDoneFn(done_fn)
{
	mSpinner->load(&OTA_SPINNER_ANIM_DEF);

	float w = Renderer::getScreenWidth() * 0.55f;
	float h = Renderer::getScreenHeight() * 0.35f;
	setSize(w, h);
	setPosition((Renderer::getScreenWidth() - w) / 2.0f,
	            (Renderer::getScreenHeight() - h) / 2.0f);

	addChild(&mBackground);
	addChild(mSpinner.get());
	addChild(mMsg.get());
}

void GuiOtaDownload::onSizeChanged()
{
	mBackground.setSize(mSize);

	float spinnerSize = std::min(mSize.x(), mSize.y()) * 0.22f;
	mSpinner->setSize(spinnerSize, spinnerSize);
	mSpinner->setPosition((mSize.x() - spinnerSize) / 2.0f, mSize.y() * 0.12f);

	mMsg->setSize(mSize.x() - 40.0f, 0);
	mMsg->setPosition(20.0f, mSpinner->getPosition().y() + spinnerSize + 16.0f);
}

void GuiOtaDownload::update(int dt)
{
	GuiComponent::update(dt);

	if (mFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
		return;

	int result = mFuture.get();
	bool success = (result == 0);

	// 자신을 제거한 후 done 콜백 호출 (done_fn이 새 GUI를 pushGui할 수 있으므로)
	mWindow->removeGui(this);
	mDoneFn(success);
	delete this;
}

GuiOtaCheck::GuiOtaCheck(Window* window,
                         std::function<std::string()> check_fn,
                         std::function<void(std::string)> done_fn)
	: GuiComponent(window),
	  mBackground(window, ":/frame.png"),
	  mSpinner(std::make_shared<AnimatedImageComponent>(window)),
	  mMsg(std::make_shared<TextComponent>(window,
	       "버전 확인 중...\n잠시 기다려주세요.",
	       Font::get(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_CENTER)),
	  mFuture(std::async(std::launch::async, check_fn)),
	  mDoneFn(done_fn)
{
	mSpinner->load(&OTA_SPINNER_ANIM_DEF);

	float w = Renderer::getScreenWidth() * 0.55f;
	float h = Renderer::getScreenHeight() * 0.35f;
	setSize(w, h);
	setPosition((Renderer::getScreenWidth() - w) / 2.0f,
	            (Renderer::getScreenHeight() - h) / 2.0f);

	addChild(&mBackground);
	addChild(mSpinner.get());
	addChild(mMsg.get());
}

void GuiOtaCheck::onSizeChanged()
{
	mBackground.setSize(mSize);

	float spinnerSize = std::min(mSize.x(), mSize.y()) * 0.22f;
	mSpinner->setSize(spinnerSize, spinnerSize);
	mSpinner->setPosition((mSize.x() - spinnerSize) / 2.0f, mSize.y() * 0.12f);

	mMsg->setSize(mSize.x() - 40.0f, 0);
	mMsg->setPosition(20.0f, mSpinner->getPosition().y() + spinnerSize + 16.0f);
}

void GuiOtaCheck::update(int dt)
{
	GuiComponent::update(dt);

	if (mFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
		return;

	std::string result = mFuture.get();

	mWindow->removeGui(this);
	mDoneFn(result);
	delete this;
}
