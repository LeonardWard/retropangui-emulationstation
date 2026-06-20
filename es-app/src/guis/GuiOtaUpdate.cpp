#include "guis/GuiOtaUpdate.h"
#include "renderers/Renderer.h"
#include "resources/Font.h"

GuiOtaDownload::GuiOtaDownload(Window* window,
                               std::function<int()> download_fn,
                               std::function<void(bool)> done_fn)
	: GuiComponent(window),
	  mBackground(window, ":/frame.png"),
	  mMsg(std::make_shared<TextComponent>(window,
	       "업데이트 다운로드 중...\n잠시 기다려주세요.",
	       Font::get(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_CENTER)),
	  mFuture(std::async(std::launch::async, download_fn)),
	  mDoneFn(done_fn)
{
	float w = Renderer::getScreenWidth() * 0.5f;
	float h = Renderer::getScreenHeight() * 0.2f;
	setSize(w, h);
	setPosition((Renderer::getScreenWidth() - w) / 2.0f,
	            (Renderer::getScreenHeight() - h) / 2.0f);

	addChild(&mBackground);
	addChild(mMsg.get());
}

void GuiOtaDownload::onSizeChanged()
{
	mBackground.setSize(mSize);
	mMsg->setSize(mSize.x() - 40.0f, 0);
	mMsg->setPosition(20.0f, (mSize.y() - mMsg->getSize().y()) / 2.0f);
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
	  mMsg(std::make_shared<TextComponent>(window,
	       "버전 확인 중...\n잠시 기다려주세요.",
	       Font::get(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_CENTER)),
	  mFuture(std::async(std::launch::async, check_fn)),
	  mDoneFn(done_fn)
{
	float w = Renderer::getScreenWidth() * 0.5f;
	float h = Renderer::getScreenHeight() * 0.2f;
	setSize(w, h);
	setPosition((Renderer::getScreenWidth() - w) / 2.0f,
	            (Renderer::getScreenHeight() - h) / 2.0f);

	addChild(&mBackground);
	addChild(mMsg.get());
}

void GuiOtaCheck::onSizeChanged()
{
	mBackground.setSize(mSize);
	mMsg->setSize(mSize.x() - 40.0f, 0);
	mMsg->setPosition(20.0f, (mSize.y() - mMsg->getSize().y()) / 2.0f);
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
