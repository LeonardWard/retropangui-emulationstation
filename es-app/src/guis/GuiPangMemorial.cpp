#include "guis/GuiPangMemorial.h"

#include "components/ButtonComponent.h"
#include "components/MenuComponent.h"

#define HORIZONTAL_PADDING_PX 20
#define PANG_MEMORIAL_IMAGE "/usr/share/retropangui/pangui.png"

GuiPangMemorial::GuiPangMemorial(Window* window) : GuiComponent(window),
	mBackground(window, ":/frame.png"), mGrid(window, Vector2i(1, 3))
{
	const float width = Math::max(Renderer::getScreenWidth() * 0.4f, Renderer::getScreenWidth() * 0.24f);

	mPhoto = std::make_shared<ImageComponent>(mWindow);
	mPhoto->setImage(PANG_MEMORIAL_IMAGE);
	mPhoto->setOrigin(0.5f, 0.5f);
	mPhoto->setMaxSize(width * 0.6f, width * 0.6f);
	mGrid.setEntry(mPhoto, Vector2i(0, 0), false, false);

	// RetroPangUI라는 이름의 유래 - 2017년 가을 창원 진해구에서 실종된
	// 반려견 "팡이"의 기록. 연락 유도 목적이 아니라 그냥 남겨두는 기록이라
	// 명시적으로 요청받음(2026-07-24) - 문구도 그 취지에 맞게 담담하게.
	const std::string text =
		"팡이\n"
		"2013년 11월생 · 시고르자브종\n\n"
		"창원 진해구에서 2017년 가을,\n"
		"집 대문에 잠깐 묶여 있다가 실종.\n"
		"(동네 아이들 전언에 의하면 어떤 할머니가 데려갔다고 함)\n\n"
		"pang.e1311@outlook.com";

	mInfo = std::make_shared<TextComponent>(mWindow, text, Font::get(FONT_SIZE_SMALL), 0x777777FF, ALIGN_CENTER);
	mGrid.setEntry(mInfo, Vector2i(0, 1), false, false);

	mButtons.push_back(std::make_shared<ButtonComponent>(mWindow, "OK", "OK", [this] { delete this; }));
	mAcceleratorFunc = mButtons.front()->getPressedFunc();
	mButtonGrid = makeButtonGrid(mWindow, mButtons);
	mGrid.setEntry(mButtonGrid, Vector2i(0, 2), true, false, Vector2i(1, 1), GridFlags::BORDER_TOP);

	mInfo->setSize(width, 0);
	const float photoHeight = mPhoto->getSize().y();
	const float infoHeight = mInfo->getSize().y() * 1.15f;
	setSize(width + HORIZONTAL_PADDING_PX * 2, photoHeight + infoHeight + mButtonGrid->getSize().y());

	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2.0f, (Renderer::getScreenHeight() - mSize.y()) / 2.0f);

	addChild(&mBackground);
	addChild(&mGrid);
}

bool GuiPangMemorial::input(InputConfig* config, Input input)
{
	if (mAcceleratorFunc && config->isMappedToAction("back", input) && input.value != 0)
	{
		mAcceleratorFunc();
		return true;
	}
	return GuiComponent::input(config, input);
}

void GuiPangMemorial::onSizeChanged()
{
	mGrid.setSize(mSize);
	mGrid.setRowHeightPerc(0, mPhoto->getSize().y() / mSize.y(), false);
	mGrid.setRowHeightPerc(2, mButtonGrid->getSize().y() / mSize.y());

	mInfo->setSize(mSize.x() - HORIZONTAL_PADDING_PX * 2, mGrid.getRowHeight(1));
	mGrid.onSizeChanged();

	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));
}

std::vector<HelpPrompt> GuiPangMemorial::getHelpPrompts()
{
	return mGrid.getHelpPrompts();
}
