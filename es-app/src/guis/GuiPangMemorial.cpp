#include "guis/GuiPangMemorial.h"

#include "components/ButtonComponent.h"
#include "components/MenuComponent.h"

#define OUTER_PADDING_PX 30
#define INNER_GAP_PX 30
#define VERTICAL_PADDING_PX 24
#define PANG_MEMORIAL_IMAGE "/usr/share/retropangui/pangui.png"

// 5열: [좌측여백][텍스트][간격][사진][우측여백]. ComponentGrid는 셀 크기가
// 내용물보다 크면 가운데로 밀어버려서 "여유 공간에 기대는" 방식으로는
// 원하는 쪽에만 여백이 생기지 않음 - 그래서 각 칸의 폭을 실제 내용물 크기와
// 정확히 같게 맞추고, 여백 자체를 별도의 빈 칸으로 명시한다.
GuiPangMemorial::GuiPangMemorial(Window* window) : GuiComponent(window),
	mBackground(window, ":/frame.png"), mGrid(window, Vector2i(5, 2))
{
	const float infoWidth = Renderer::getScreenWidth() * 0.32f;
	const float photoWidth = Renderer::getScreenWidth() * 0.17f;

	// RetroPangUI라는 이름의 유래 - 2017년 가을 창원 진해구에서 실종된
	// 반려견 "팡이"의 기록. 연락 유도 목적이 아니라 그냥 남겨두는 기록이라
	// 명시적으로 요청받음(2026-07-24) - 문구도 그 취지에 맞게 담담하게.
	const std::string text =
		"ㅇ 이름 : 팡이\n"
		"ㅇ 생년월 : 2013년 11월생\n"
		"ㅇ 견종 : 시고르자브종\n\n"
		"2017년 가을, 창원 진해구 집 대문에 잠깐 묶여 있다가 실종. (동네 아이들 전언 - 어떤 할머니가 데려갔다고 함)\n\n"
		"ㅇ 팡이 메일주소 : pang.e1311@outlook.com";

	mInfo = std::make_shared<TextComponent>(mWindow, text, Font::get(FONT_SIZE_SMALL), 0x777777FF, ALIGN_LEFT);
	mInfo->setSize(infoWidth, 0);
	mGrid.setEntry(mInfo, Vector2i(1, 0), false, false);

	// ComponentGrid::updateCellComponent()는 origin(0,0) 기준으로 셀 좌상단에
	// 배치하는 걸 전제로 위치를 계산함 - origin을 (0.5,0.5)로 바꾸면 계산된
	// 좌표에서 이미지 크기의 절반만큼 위·왼쪽으로 밀려서 카드 밖으로 튀어나감
	// (2026-07-24 실기기에서 실측 확인 - GuiSaveStates의 mThumbnail은 그리드가
	// 아니라 addChild+수동 setPosition이라 origin 0.5를 써도 안전했던 것과
	// 대비됨). 그리드에 넣는 컴포넌트는 기본 origin을 유지해야 함.
	mPhoto = std::make_shared<ImageComponent>(mWindow);
	mPhoto->setImage(PANG_MEMORIAL_IMAGE);
	mPhoto->setMaxSize(photoWidth, photoWidth);
	mGrid.setEntry(mPhoto, Vector2i(3, 0), false, false);

	mButtons.push_back(std::make_shared<ButtonComponent>(mWindow, "OK", "OK", [this] { delete this; }));
	mAcceleratorFunc = mButtons.front()->getPressedFunc();
	mButtonGrid = makeButtonGrid(mWindow, mButtons);
	mGrid.setEntry(mButtonGrid, Vector2i(0, 1), true, false, Vector2i(5, 1), GridFlags::BORDER_TOP);

	const float contentHeight = Math::max(mPhoto->getSize().y(), mInfo->getSize().y()) + VERTICAL_PADDING_PX * 2;
	const float contentWidth = OUTER_PADDING_PX + infoWidth + INNER_GAP_PX + mPhoto->getSize().x() + OUTER_PADDING_PX;
	setSize(contentWidth, contentHeight + mButtonGrid->getSize().y());

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
	mGrid.setColWidthPerc(0, OUTER_PADDING_PX / mSize.x(), false);
	mGrid.setColWidthPerc(1, mInfo->getSize().x() / mSize.x(), false);
	mGrid.setColWidthPerc(2, INNER_GAP_PX / mSize.x(), false);
	mGrid.setColWidthPerc(3, mPhoto->getSize().x() / mSize.x(), false);
	mGrid.setColWidthPerc(4, OUTER_PADDING_PX / mSize.x(), false);
	mGrid.setRowHeightPerc(1, mButtonGrid->getSize().y() / mSize.y());

	mGrid.onSizeChanged();

	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));
}

std::vector<HelpPrompt> GuiPangMemorial::getHelpPrompts()
{
	return mGrid.getHelpPrompts();
}
