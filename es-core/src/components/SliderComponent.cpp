#include "LocaleES.h"
#include "components/SliderComponent.h"

#include "resources/Font.h"

#define MOVE_REPEAT_DELAY 500
#define MOVE_REPEAT_RATE 40

SliderComponent::SliderComponent(Window* window, float min, float max, float increment, const std::string& suffix) : GuiComponent(window),
	mMin(min), mMax(max), mSingleIncrement(increment), mMoveRate(0), mKnob(window), mSuffix(suffix)
{
	assert((min - max) != 0);

	// some sane default value
	mValue = (max + min) / 2;

	mKnob.setOrigin(0.5f, 0.5f);
	mKnob.setImage(":/slider_knob.svg");

	setSize(Renderer::getScreenWidth() * 0.15f, Font::get(FONT_SIZE_MEDIUM)->getLetterHeight());
}

bool SliderComponent::input(InputConfig* config, Input input)
{
	// 조이패드 hat은 4방향이 전부 같은 hat 번호를 공유해서, InputConfig::
	// isMappedTo()가 hat 중립(놓음, value==0) 이벤트를 그 hat에 매핑된
	// "모든" 방향에 대해 true로 판정함(release를 각 방향에 전파하기 위한
	// 의도된 설계) — 그래서 위/아래를 놓는 이벤트도 isMappedLike("left"/
	// "right")에 걸려 여기서 소비돼버림. 이 슬라이더가 실제로 좌우로
	// 움직이는 중이 아니었으면(mMoveRate == 0) 그 release는 이 슬라이더와
	// 무관한(위/아래) 것이니 소비하지 말고 그대로 통과시켜야 함 — 안 그러면
	// ComponentList까지 release가 전달 안 돼 리스트 스크롤이 멈추지 않고
	// 계속 미끄러지는 버그가 생김(2026-07-05, 패드에서만 재현·키보드는
	// 방향키가 서로 다른 keycode라 무관해서 재현 안 됐음).
	if(config->isMappedLike("left", input))
	{
		if(input.value)
		{
			setValue(mValue - mSingleIncrement);
			mMoveRate = -mSingleIncrement;
			mMoveAccumulator = -MOVE_REPEAT_DELAY;
			return true;
		}
		if(mMoveRate != 0)
		{
			mMoveRate = 0;
			return true;
		}
	}
	if(config->isMappedLike("right", input))
	{
		if(input.value)
		{
			setValue(mValue + mSingleIncrement);
			mMoveRate = mSingleIncrement;
			mMoveAccumulator = -MOVE_REPEAT_DELAY;
			return true;
		}
		if(mMoveRate != 0)
		{
			mMoveRate = 0;
			return true;
		}
	}

	return GuiComponent::input(config, input);
}

void SliderComponent::update(int deltaTime)
{
	if(mMoveRate != 0)
	{
		mMoveAccumulator += deltaTime;
		while(mMoveAccumulator >= MOVE_REPEAT_RATE)
		{
			setValue(mValue + mMoveRate);
			mMoveAccumulator -= MOVE_REPEAT_RATE;
		}
	}

	GuiComponent::update(deltaTime);
}

void SliderComponent::render(const Transform4x4f& parentTrans)
{
	Transform4x4f trans = parentTrans * getTransform();
	Renderer::setMatrix(trans);

	// render suffix
	if(mValueCache)
		mFont->renderTextCache(mValueCache.get());

	float width = mSize.x() - mKnob.getSize().x() - (mValueCache ? mValueCache->metrics.size.x() + 4 : 0);

	//render line
	const float lineWidth = 2;
	Renderer::drawRect(mKnob.getSize().x() / 2, mSize.y() / 2 - lineWidth / 2, width, lineWidth, 0x777777FF, 0x777777FF);

	//render knob
	mKnob.render(trans);

	GuiComponent::renderChildren(trans);
}

void SliderComponent::setValue(float value)
{
	mValue = value;
	if(mValue < mMin)
		mValue = mMin;
	else if(mValue > mMax)
		mValue = mMax;

	onValueChanged();
}

float SliderComponent::getValue()
{
	return mValue;
}

void SliderComponent::onSizeChanged()
{
	if(!mSuffix.empty())
		mFont = Font::get((int)(mSize.y()), FONT_PATH_LIGHT);

	onValueChanged();
}

void SliderComponent::onValueChanged()
{
	// update suffix textcache
	if(mFont)
	{
		std::stringstream ss;
		ss << std::fixed;
		ss.precision(0);
		ss << mValue;
		ss << mSuffix;
		const std::string val = ss.str();

		ss.str("");
		ss.clear();
		ss << std::fixed;
		ss.precision(0);
		ss << mMax;
		ss << mSuffix;
		const std::string max = ss.str();

		Vector2f textSize = mFont->sizeText(max);
		mValueCache = std::shared_ptr<TextCache>(mFont->buildTextCache(val, mSize.x() - textSize.x(), (mSize.y() - textSize.y()) / 2, 0x777777FF));
		mValueCache->metrics.size[0] = textSize.x(); // fudge the width
	}

	// update knob position/size
	mKnob.setResize(0, mSize.y() * 0.7f);
	float lineLength = mSize.x() - mKnob.getSize().x() - (mValueCache ? mValueCache->metrics.size.x() + 4 : 0);
	mKnob.setPosition(((mValue + mMin) / mMax) * lineLength + mKnob.getSize().x()/2, mSize.y() / 2);

	if(mChangedCallback)
		mChangedCallback(mValue);
}

std::vector<HelpPrompt> SliderComponent::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt("left/right", _("CHANGE")));
	return prompts;
}
