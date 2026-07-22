#pragma once
#ifndef ES_APP_VIEWS_SYSTEM_VIEW_H
#define ES_APP_VIEWS_SYSTEM_VIEW_H

#include "components/IList.h"
#include "components/TextComponent.h"
#include "resources/Font.h"
#include "GuiComponent.h"
#include <memory>

class AnimatedImageComponent;
class FileData;
class SystemData;

enum CarouselType : unsigned int
{
	HORIZONTAL = 0,
	VERTICAL = 1,
	VERTICAL_WHEEL = 2,
	HORIZONTAL_WHEEL = 3
};

struct SystemViewData
{
	std::shared_ptr<GuiComponent> logo;
	std::vector<GuiComponent*> backgroundExtras;

	// RetroPangui: RECENTLY PLAYED 카드(rp-card-N)처럼 이름으로 찾아서 값을 갱신해야
	// 하는 extra만 별도 보관 - backgroundExtras와 같은 포인터를 가리키므로 소유권/삭제는
	// backgroundExtras 쪽에서만 수행(여긴 조회용 raw pointer만 들고 있음).
	std::vector<std::pair<std::string, GuiComponent*>> namedExtras;
};

struct SystemViewCarousel
{
	CarouselType type;
	Vector2f pos;
	Vector2f size;
	Vector2f origin;
	float logoScale;
	float logoRotation;
	Vector2f logoRotationOrigin;
	Alignment logoAlignment;
	unsigned int color;
	unsigned int colorEnd;
	bool colorGradientHorizontal;
	int maxLogoCount; // number of logos shown on the carousel
	Vector2f logoSize;
	float zIndex;
};

class SystemView : public IList<SystemViewData, SystemData*>
{
public:
	SystemView(Window* window);

	virtual void onShow() override;
	virtual void onHide() override;

	void goToSystem(SystemData* system, bool animate);

	bool input(InputConfig* config, Input input) override;
	void update(int deltaTime) override;
	void render(const Transform4x4f& parentTrans) override;

	void onThemeChanged(const std::shared_ptr<ThemeData>& theme);

	std::vector<HelpPrompt> getHelpPrompts() override;
	virtual HelpStyle getHelpStyle() override;

protected:
	void onCursorChanged(const CursorState& state) override;

private:
	void populate();
	void getViewElements(const std::shared_ptr<ThemeData>& theme);
	void getDefaultElements(void);
	void getCarouselFromTheme(const ThemeData::ThemeElement* elem);

	void renderCarousel(const Transform4x4f& parentTrans);
	void renderExtras(const Transform4x4f& parentTrans, float lower, float upper);
	void renderInfoBar(const Transform4x4f& trans);
	void renderFade(const Transform4x4f& trans);

	// RetroPangui: RECENTLY PLAYED 카드(rp-card-1..N) 이미지/이름을 매 프레임 최신 상태로 갱신
	// system: 지금 보고 있는 시스템 - 이 시스템 소속 게임만 필터링하기 위함(2026-07-06)
	void updateRecentlyPlayed(SystemViewData& data, SystemData* system);

	// RetroPangui: 하단 푸터 우측 bgmTitle 텍스트에 현재 재생 트랙 제목을 매 프레임 반영
	void updateBgmTitle(SystemViewData& data);

	// RetroPangui: 메인 화면 우측 상단 시계(clock-time/clock-date, 테마 쪽 이름 있는 extra) -
	// 1초에 한 번만 텍스트를 다시 그려서 매 프레임 setText() 비용을 피함(2026-07-23)
	void updateClock(SystemViewData& data, int deltaTime);
	int mClockAccumulator;


	SystemViewCarousel mCarousel;
	TextComponent mSystemInfo;
	// RetroPangui: 게임 수 숫자만 표시 — 테마가 text 요소
	// gameCountNumber를 선언한 경우에만 표시 (레이블은 테마에서 직접 그림)
	TextComponent mGameCountNumber;
	bool mHasGameCountNumber;
	// RetroPangui: 즐겨찾기 수 숫자만 표시 — 테마가 text 요소
	// favoriteCountNumber를 선언한 경우에만 표시 (레이블은 테마에서 직접 그림)
	TextComponent mFavoriteCountNumber;
	bool mHasFavoriteCountNumber;

	// unit is list index
	float mCamOffset;
	float mExtrasCamOffset;
	float mExtrasFadeOpacity;

	bool mViewNeedsReload;
	bool mShowing;
};

#endif // ES_APP_VIEWS_SYSTEM_VIEW_H
