#include "guis/GuiGamelistRefresh.h"

#include "components/TextComponent.h"
#include "resources/Font.h"
#include "utils/StringUtil.h"
#include "views/ViewController.h"
#include "LocaleES.h"
#include "renderers/Renderer.h"
#include "SystemData.h"
#include "Window.h"

GuiGamelistRefresh::GuiGamelistRefresh(Window* window, const std::vector<SystemData*>& systems)
	: GuiComponent(window), mBackground(window, ":/frame.png"),
	  mSystems(systems), mIndex(0), mTotalAdded(0), mTotalRemoved(0), mFailed(false), mDone(false)
{
	addChild(&mBackground);

	mTitle = std::make_shared<TextComponent>(mWindow, _("UPDATE GAMELISTS"),
		Font::get(FONT_SIZE_MEDIUM), 0x555555FF, ALIGN_CENTER);
	mText = std::make_shared<TextComponent>(mWindow, "",
		Font::get(FONT_SIZE_SMALL), 0x777777FF, ALIGN_LEFT);
	mText->setVerticalAlignment(ALIGN_TOP);
	addChild(mTitle.get());
	addChild(mText.get());

	// 시스템 수에 맞춰 높이 결정(제목 + 시스템별 한 줄 + 요약 두 줄)
	float lineHeight = Font::get(FONT_SIZE_SMALL)->getLetterHeight() * 1.8f;
	float height = Font::get(FONT_SIZE_MEDIUM)->getLetterHeight() * 2.0f
	             + lineHeight * (float)(mSystems.size() + 3);
	float maxHeight = Renderer::getScreenHeight() * 0.85f;
	if (height > maxHeight)
		height = maxHeight;

	setSize(Renderer::getScreenWidth() * 0.45f, height);
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2,
	            (Renderer::getScreenHeight() - mSize.y()) / 2);
}

void GuiGamelistRefresh::onSizeChanged()
{
	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));

	const float padX = mSize.x() * 0.05f;
	const float padY = mSize.y() * 0.05f;

	mTitle->setSize(mSize.x() - padX * 2, 0);
	mTitle->setPosition(padX, padY);
	mText->setPosition(padX, padY + mTitle->getSize().y() * 1.6f);
	mText->setSize(mSize.x() - padX * 2, mSize.y() - mText->getPosition().y() - padY);
}

void GuiGamelistRefresh::appendLine(const std::string& line)
{
	if (!mLog.empty())
		mLog += "\n";
	mLog += line;
	mText->setText(mLog);
}

void GuiGamelistRefresh::update(int deltaTime)
{
	GuiComponent::update(deltaTime);

	if (mDone)
		return;

	// 프레임당 시스템 하나 - 처리한 줄이 즉시 화면에 그려진 뒤 다음 시스템으로
	if (mIndex < mSystems.size())
	{
		SystemData* sys = mSystems.at(mIndex);
		int removed = 0;
		int added = sys->refreshGamelist(&removed);
		if (added > 0)
			mTotalAdded += added;
		if (removed > 0)
			mTotalRemoved += removed;
		if (added > 0 || removed > 0)
			mChanged.push_back(sys); // 삭제만 있어도 뷰를 다시 그려야 반영됨
		if (added < 0)
			mFailed = true;

		std::string line = sys->getFullName() + " ... ";
		if (added < 0)
			line += _("FAILED");
		else
		{
			line += "+" + std::to_string(added);
			if (removed > 0)
				line += " -" + std::to_string(removed);
		}
		appendLine(line);
		mIndex++;
		return;
	}

	// 전체 완료: 바뀐 시스템 뷰만 다시 로드
	for (auto sys : mChanged)
		ViewController::get()->reloadGameListView(sys);

	appendLine("");
	std::string summary = Utils::String::replace(_("DONE. %i NEW GAMES ADDED"),
		"%i", std::to_string(mTotalAdded));
	if (mTotalRemoved > 0)
		summary += ", " + Utils::String::replace(_("%i REMOVED"),
			"%i", std::to_string(mTotalRemoved));
	if (mFailed)
		summary += " - " + std::string(_("GAMELIST UPDATE FAILED"));
	appendLine(summary);

	mDone = true;
	updateHelpPrompts();
}

bool GuiGamelistRefresh::input(InputConfig* config, Input input)
{
	// 진행 중에는 입력을 전부 삼켜서 도중 취소로 인한 어중간한 상태를 막는다
	if (!mDone)
		return true;

	if ((config->isMappedToAction("accept", input) || config->isMappedToAction("back", input)) && input.value)
	{
		delete this;
		return true;
	}
	return true;
}

std::vector<HelpPrompt> GuiGamelistRefresh::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	if (mDone)
		prompts.push_back(HelpPrompt(InputConfig::getActionButton("back"), "close"));
	return prompts;
}
