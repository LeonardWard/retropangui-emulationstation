#include "views/SystemView.h"

#include "InputConfig.h"
#include "InputManager.h"

#include "animations/LambdaAnimation.h"
#include "components/ImageComponent.h"
#include "components/ScrollableContainer.h"
#include "guis/GuiMsgBox.h"
#include "views/UIModeController.h"
#include "views/ViewController.h"
#include "LocaleES.h"
#include "Log.h"
#include "MusicManager.h"
#include "Scripting.h"
#include "Settings.h"
#include "SystemData.h"
#include "Window.h"

// buffer values for scrolling velocity (left, stopped, right)
const int logoBuffersLeft[] = { -5, -2, -1 };
const int logoBuffersRight[] = { 1, 2, 5 };

SystemView::SystemView(Window* window) : IList<SystemViewData, SystemData*>(window, LIST_SCROLL_STYLE_SLOW, LIST_ALWAYS_LOOP),
										 mViewNeedsReload(true),
										 mSystemInfo(window, _("SYSTEM INFO"), Font::get(FONT_SIZE_SMALL), 0x33333300, ALIGN_CENTER),
										 mGameCountNumber(window, "", Font::get(FONT_SIZE_LARGE), 0x33333300, ALIGN_CENTER),
										 mHasGameCountNumber(false),
										 mFavoriteCountNumber(window, "", Font::get(FONT_SIZE_LARGE), 0x33333300, ALIGN_CENTER),
										 mHasFavoriteCountNumber(false)
{
	mCamOffset = 0;
	mExtrasCamOffset = 0;
	mExtrasFadeOpacity = 0.0f;

	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	populate();
}

void SystemView::populate()
{
	mEntries.clear();

	for(auto it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); it++)
	{
		const std::shared_ptr<ThemeData>& theme = (*it)->getTheme();

		if(mViewNeedsReload)
			getViewElements(theme);

		if((*it)->isVisible())
		{
			Entry e;
			e.name = (*it)->getName();
			e.object = *it;

			// make logo
			const ThemeData::ThemeElement* logoElem = theme->getElement("system", "logo", "image");
			if(logoElem)
			{
				std::string path = logoElem->get<std::string>("path");
				std::string defaultPath = logoElem->has("default") ? logoElem->get<std::string>("default") : "";
				if((!path.empty() && ResourceManager::getInstance()->fileExists(path))
				   || (!defaultPath.empty() && ResourceManager::getInstance()->fileExists(defaultPath)))
				{
					ImageComponent* logo = new ImageComponent(mWindow, false, false);
					logo->setMaxSize(mCarousel.logoSize * mCarousel.logoScale);
					logo->applyTheme(theme, "system", "logo", ThemeFlags::PATH | ThemeFlags::COLOR);
					logo->setRotateByTargetSize(true);
					e.data.logo = std::shared_ptr<GuiComponent>(logo);
				}
			}
			if (!e.data.logo)
			{
				// no logo in theme; use text
				TextComponent* text = new TextComponent(mWindow,
					(*it)->getName(),
					Font::get(FONT_SIZE_LARGE),
					0x000000FF,
					ALIGN_CENTER);
				text->setSize(mCarousel.logoSize * mCarousel.logoScale);
				text->applyTheme((*it)->getTheme(), "system", "logoText", ThemeFlags::FONT_PATH | ThemeFlags::FONT_SIZE | ThemeFlags::COLOR | ThemeFlags::FORCE_UPPERCASE | ThemeFlags::LINE_SPACING | ThemeFlags::TEXT);
				e.data.logo = std::shared_ptr<GuiComponent>(text);

				if (mCarousel.type == VERTICAL || mCarousel.type == VERTICAL_WHEEL)
				{
					text->setHorizontalAlignment(mCarousel.logoAlignment);
					text->setVerticalAlignment(ALIGN_CENTER);
				} else {
					text->setHorizontalAlignment(ALIGN_CENTER);
					text->setVerticalAlignment(mCarousel.logoAlignment);
				}
			}

			if (mCarousel.type == VERTICAL || mCarousel.type == VERTICAL_WHEEL)
			{
				if (mCarousel.logoAlignment == ALIGN_LEFT)
					e.data.logo->setOrigin(0, 0.5);
				else if (mCarousel.logoAlignment == ALIGN_RIGHT)
					e.data.logo->setOrigin(1.0, 0.5);
				else
					e.data.logo->setOrigin(0.5, 0.5);
			} else {
				if (mCarousel.logoAlignment == ALIGN_TOP)
					e.data.logo->setOrigin(0.5, 0);
				else if (mCarousel.logoAlignment == ALIGN_BOTTOM)
					e.data.logo->setOrigin(0.5, 1);
				else
					e.data.logo->setOrigin(0.5, 0.5);
			}

			Vector2f denormalized = mCarousel.logoSize * e.data.logo->getOrigin();
			e.data.logo->setPosition(denormalized.x(), denormalized.y(), 0.0);
			// delete any existing extras
			for (auto extra : e.data.backgroundExtras)
				delete extra;
			e.data.backgroundExtras.clear();
			e.data.namedExtras.clear();

			// make background extras
			// RetroPangui: RECENTLY PLAYED 카드(rp-card-N)처럼 이름으로 다시 찾아야 하는
			// extra가 생겨서 이름도 같이 보관(namedExtras) - backgroundExtras는 기존처럼
			// z-index 정렬/렌더/삭제용 GuiComponent* 목록 그대로 유지.
			for (auto& kv : ThemeData::makeExtras((*it)->getTheme(), "system", mWindow))
			{
				e.data.backgroundExtras.push_back(kv.second);
				e.data.namedExtras.push_back(kv);
			}

			// sort the extras by z-index
			std::stable_sort(e.data.backgroundExtras.begin(), e.data.backgroundExtras.end(),  [](GuiComponent* a, GuiComponent* b) {
				return b->getZIndex() > a->getZIndex();
			});

			this->add(e);
		}
	}
	if (mEntries.size() == 0)
	{
		// Something is wrong, there is not a single system to show, check if UI mode is not full
		if (!UIModeController::getInstance()->isUIModeFull())
		{
			Settings::getInstance()->setString("UIMode", "Full");
			mWindow->pushGui(new GuiMsgBox(mWindow, _("The selected UI mode has nothing to show,\n returning to UI mode: FULL"), _("OK"), nullptr));
		}
	}
}

void SystemView::goToSystem(SystemData* system, bool animate)
{
	setCursor(system);

	if(!animate)
		finishAnimation(0);
}

bool SystemView::input(InputConfig* config, Input input)
{
	if(input.value != 0)
	{
		if(config->getDeviceId() == DEVICE_KEYBOARD && input.value && input.id == SDLK_r && SDL_GetModState() & KMOD_LCTRL && Settings::getInstance()->getBool("Debug"))
		{
			LOG(LogInfo) << " Reloading all";
			ViewController::get()->reloadAll();
			return true;
		}

		switch (mCarousel.type)
		{
		case VERTICAL:
		case VERTICAL_WHEEL:
			if (config->isMappedLike("up", input))
			{
				InputManager::getInstance()->rumbleNav(config->getDeviceId());
				listInput(-1);
				return true;
			}
			if (config->isMappedLike("down", input))
			{
				InputManager::getInstance()->rumbleNav(config->getDeviceId());
				listInput(1);
				return true;
			}
			break;
		case HORIZONTAL:
		case HORIZONTAL_WHEEL:
		default:
			if (config->isMappedLike("left", input))
			{
				InputManager::getInstance()->rumbleNav(config->getDeviceId());
				listInput(-1);
				return true;
			}
			if (config->isMappedLike("right", input))
			{
				InputManager::getInstance()->rumbleNav(config->getDeviceId());
				listInput(1);
				return true;
			}
			break;
		}

		if(config->isMappedToAction("accept", input))
		{
			stopScrolling();
			ViewController::get()->goToGameList(getSelected());
			return true;
		}
		if (config->isMappedTo("x", input))
		{
			// get random system
			// go to system
			setCursor(SystemData::getRandomSystem());
			return true;
		}
	}else{
		if(config->isMappedLike("left", input) ||
			config->isMappedLike("right", input) ||
			config->isMappedLike("up", input) ||
			config->isMappedLike("down", input))
			listInput(0);
		Scripting::fireEvent("system-select", this->IList::getSelected()->getName(), "input");
		if(!UIModeController::getInstance()->isUIModeKid() && config->isMappedTo("select", input) && Settings::getInstance()->getBool("ScreenSaverControls"))
		{
			mWindow->startScreenSaver();
			mWindow->renderScreenSaver();
			return true;
		}
	}

	return GuiComponent::input(config, input);
}

void SystemView::update(int deltaTime)
{
	listUpdate(deltaTime);
	// RetroPangui: extras는 자식 컴포넌트가 아니라 update가 전달되지 않으므로
	// 현재 시스템의 extras를 직접 갱신 (scrollable text 자동 스크롤 등)
	if(mCursor >= 0 && mCursor < (int)mEntries.size())
	{
		for(auto extra : mEntries.at(mCursor).data.backgroundExtras)
			extra->update(deltaTime);
		updateRecentlyPlayed(mEntries.at(mCursor).data, mEntries.at(mCursor).object);
		updateBgmTitle(mEntries.at(mCursor).data);
	}
	GuiComponent::update(deltaTime);
}

// RetroPangui: RECENTLY PLAYED 카드(rp-card-1..N, 테마 쪽 이름 있는 extra)에 최근 플레이한
// 게임의 썸네일/이름을 채워줌 - 몇 장을 보여줄지/배치/스타일은 테마(retropangui-slate)
// 책임이고, ES는 데이터(경로/이름)만 이름으로 조회 가능한 extra에 넘겨줌(bgmTitle과 동일 원칙).
// 이름 있는 extra를 못 찾으면(테마가 아직 rp-card-N을 안 뒀으면) 조용히 스킵.
static GuiComponent* findNamedExtra(SystemViewData& data, const std::string& name)
{
	for (auto& kv : data.namedExtras)
		if (kv.first == name)
			return kv.second;
	return nullptr;
}

// RetroPangui: 하단 푸터 우측 bgmTitle 텍스트에 현재 재생 트랙 제목 반영
// (2026-07-06, 게임리스트 사이드바에서 메인 화면 푸터로 이동 - 게임리스트에선
// 더 이상 표시 안 함, ES는 값만 넘겨주고 표시 위치/스타일은 테마 책임 원칙 동일)
void SystemView::updateBgmTitle(SystemViewData& data)
{
	GuiComponent* bgmTitleExtra = findNamedExtra(data, "bgmTitle");
	if (bgmTitleExtra == nullptr)
		return;

	auto& music = MusicManager::getInstance();
	std::string title = music->isPlaying() ? music->getCurrentTrackTitle() : "";
	bgmTitleExtra->setValue(title);
}

void SystemView::updateRecentlyPlayed(SystemViewData& data, SystemData* system)
{
	// 테마(retropangui-slate)의 rp-card-1..6(디자인 목업 System View.png 기준 6장)에 맞춤
	// - 카드 수가 바뀌면 여기 숫자도 같이 조정
	static const int MAX_RECENT_CARDS = 6;

	SystemData* recentSystem = nullptr;
	for (auto sys : SystemData::sSystemVector)
	{
		if (sys->getName() == "recent")
		{
			recentSystem = sys;
			break;
		}
	}

	// 2026-07-06: "recent"는 전체 시스템을 합친 자동 컬렉션이라 필터링 없이 쓰면
	// 지금 보고 있는 시스템과 무관하게 항상 똑같은 목록이 보임("모든 시스템에
	// 동일하게 나타난다" 피드백으로 발견) - 컬렉션 안의 각 항목은 원본 게임을
	// 감싸는 래퍼(CollectionFileData)라 getSourceFileData()로 원래 게임을 찾아
	// 그 게임이 지금 보고 있는 시스템 소속인 것만 남김.
	std::vector<FileData*> games;
	if (recentSystem != nullptr && system != nullptr)
	{
		std::vector<FileData*> allRecent = recentSystem->getRootFolder()->getFilesRecursive(GAME, true);
		for (FileData* game : allRecent)
		{
			if (game->getSourceFileData()->getSystem() == system)
				games.push_back(game);
		}
		if ((int)games.size() > MAX_RECENT_CARDS)
			games.resize(MAX_RECENT_CARDS);
	}

	// RetroPangui: update()가 매 프레임 호출하므로 위 스캔 자체는 어쩔 수 없이 매번
	// 돌지만, 그 결과가 지난 프레임과 똑같으면 아래 setImage()/setVisible() 호출은
	// 건너뜀 - 썸네일이 없는 카드(예: 롬이 1개뿐인 시스템)에서 매 프레임 이미지 없는
	// setImage("")가 반복 호출되며 깜빡이던 문제 수정(2026-07-18).
	static SystemData* sLastSystem = nullptr;
	static std::vector<FileData*> sLastGames;
	if (system == sLastSystem && games == sLastGames)
		return;
	sLastSystem = system;
	sLastGames = games;

	// 최근 플레이한 게임 수만큼만 카드를 보여줌 - 빈 슬롯을 놔두지 않고 아예 숨김
	for (int i = 0; i < MAX_RECENT_CARDS; ++i)
	{
		bool hasGame = i < (int)games.size();
		std::string cardName = "rp-card-" + std::to_string(i + 1);

		if (auto img = dynamic_cast<ImageComponent*>(findNamedExtra(data, cardName)))
		{
			img->setVisible(hasGame);
			if (hasGame)
				img->setImage(games[i]->getThumbnailPath());
		}

		// 테마가 카드별 이름 텍스트(예: rp-card-1-name)를 아직 안 뒀으면 nullptr - 안전하게 스킵
		if (auto nameExtra = findNamedExtra(data, cardName + "-name"))
		{
			nameExtra->setVisible(hasGame);
			if (hasGame)
				nameExtra->setValue(games[i]->getDisplayName());
		}

		// 테마가 카드별 그림자(예: rp-card-1-shadow)를 뒀으면 카드와 동일하게 보임/숨김
		// 처리 - 안 뒀으면 nullptr이라 안전하게 스킵.
		if (auto shadowExtra = findNamedExtra(data, cardName + "-shadow"))
			shadowExtra->setVisible(hasGame);
	}
	// 플레이 이력이 하나도 없어도 rp-header("RECENTLY PLAYED" 제목)는 그대로 둠 -
	// 카드만 하나도 없이 제목만 남는 게 의도된 동작(사용자 확인, 2026-07-06).
}

void SystemView::onCursorChanged(const CursorState& /*state*/)
{
	// update help style
	updateHelpPrompts();

	// RetroPangui: 도착한 시스템의 scrollable text extra는 처음부터 다시 스크롤
	if(mCursor >= 0 && mCursor < (int)mEntries.size())
	{
		for(auto extra : mEntries.at(mCursor).data.backgroundExtras)
		{
			if(auto sc = dynamic_cast<ScrollableContainer*>(extra))
				sc->reset();
		}
	}

	float startPos = mCamOffset;

	float posMax = (float)mEntries.size();
	float target = (float)mCursor;

	// what's the shortest way to get to our target?
	// it's one of these...

	float endPos = target; // directly
	float dist = abs(endPos - startPos);

	if(abs(target + posMax - startPos) < dist)
		endPos = target + posMax; // loop around the end (0 -> max)
	if(abs(target - posMax - startPos) < dist)
		endPos = target - posMax; // loop around the start (max - 1 -> -1)


	// animate mSystemInfo's opacity (fade out, wait, fade back in)

	cancelAnimation(1);
	cancelAnimation(2);

	std::string transition_style = Settings::getInstance()->getString("TransitionStyle");
	bool goFast = transition_style == "instant";
	const float infoStartOpacity = mSystemInfo.getOpacity() / 255.f;

	Animation* infoFadeOut = new LambdaAnimation(
		[infoStartOpacity, this] (float t)
	{
		const unsigned char op = (unsigned char)(Math::lerp(infoStartOpacity, 0.f, t) * 255);
		mSystemInfo.setOpacity(op);
		mGameCountNumber.setOpacity(op);
		mFavoriteCountNumber.setOpacity(op);
	}, (int)(infoStartOpacity * (goFast ? 10 : 150)));

	unsigned int gameCount = getSelected()->getDisplayedGameCount();
	unsigned int favoriteCount = getSelected()->getDisplayedFavoriteCount();

	// also change the text after we've fully faded out
	setAnimation(infoFadeOut, 0, [this, gameCount, favoriteCount] {
		std::stringstream ss;

		if (!getSelected()->isGameSystem())
			ss << _("CONFIGURATION");
		else {
			// RetroPangui: Translate game count text
			if (gameCount == 1)
				ss << gameCount << " " << _("GAME AVAILABLE");
			else
				ss << gameCount << " " << _("GAMES AVAILABLE");
		}

		mSystemInfo.setText(ss.str());

		// RetroPangui: 게임 수 숫자 요소 갱신 (레이블은 테마에서 직접 그림)
		mGameCountNumber.setText(getSelected()->isGameSystem() ? std::to_string(gameCount) : "");

		// RetroPangui: 즐겨찾기 수 숫자 요소 갱신 (레이블은 테마에서 직접 그림)
		mFavoriteCountNumber.setText(getSelected()->isGameSystem() ? std::to_string(favoriteCount) : "");
	}, false, 1);

	Animation* infoFadeIn = new LambdaAnimation(
		[this](float t)
	{
		const unsigned char op = (unsigned char)(Math::lerp(0.f, 1.f, t) * 255);
		mSystemInfo.setOpacity(op);
		mGameCountNumber.setOpacity(op);
		mFavoriteCountNumber.setOpacity(op);
	}, goFast ? 10 : 300);

	// wait 600ms to fade in
	setAnimation(infoFadeIn, goFast ? 0 : 2000, nullptr, false, 2);

	// no need to animate transition, we're not going anywhere (probably mEntries.size() == 1)
	if(endPos == mCamOffset && endPos == mExtrasCamOffset)
		return;

	Animation* anim;
	bool move_carousel = Settings::getInstance()->getBool("MoveCarousel");
	if(transition_style == "fade")
	{
		float startExtrasFade = mExtrasFadeOpacity;
		anim = new LambdaAnimation(
			[this, startExtrasFade, startPos, endPos, posMax, move_carousel](float t)
		{
			t -= 1;
			float f = Math::lerp(startPos, endPos, t*t*t + 1);
			if(f < 0)
				f += posMax;
			if(f >= posMax)
				f -= posMax;

			this->mCamOffset = move_carousel ? f : endPos;

			t += 1;
			if(t < 0.3f)
				this->mExtrasFadeOpacity = Math::lerp(0.0f, 1.0f, t / 0.3f + startExtrasFade);
			else if(t < 0.7f)
				this->mExtrasFadeOpacity = 1.0f;
			else
				this->mExtrasFadeOpacity = Math::lerp(1.0f, 0.0f, (t - 0.7f) / 0.3f);

			if(t > 0.5f)
				this->mExtrasCamOffset = endPos;

		}, 500);
	} else if (transition_style == "slide") {
		// slide
		anim = new LambdaAnimation(
			[this, startPos, endPos, posMax, move_carousel](float t)
		{
			t -= 1;
			float f = Math::lerp(startPos, endPos, t*t*t + 1);
			if(f < 0)
				f += posMax;
			if(f >= posMax)
				f -= posMax;

			this->mCamOffset = move_carousel ? f : endPos;
			this->mExtrasCamOffset = f;
		}, 500);
	} else {
		// instant
		anim = new LambdaAnimation(
			[this, startPos, endPos, posMax, move_carousel ](float t)
		{
			t -= 1;
			float f = Math::lerp(startPos, endPos, t*t*t + 1);
			if(f < 0)
				f += posMax;
			if(f >= posMax)
				f -= posMax;

			this->mCamOffset = move_carousel ? f : endPos;
			this->mExtrasCamOffset = endPos;
		}, move_carousel ? 500 : 1);
	}


	setAnimation(anim, 0, nullptr, false, 0);
}

void SystemView::render(const Transform4x4f& parentTrans)
{
	if(size() == 0)
		return;  // nothing to render

	Transform4x4f trans = getTransform() * parentTrans;

	auto systemInfoZIndex = mSystemInfo.getZIndex();
	auto minMax = std::minmax(mCarousel.zIndex, systemInfoZIndex);

	renderExtras(trans, INT16_MIN, minMax.first);
	renderFade(trans);

	if (mCarousel.zIndex > mSystemInfo.getZIndex()) {
		renderInfoBar(trans);
	} else {
		renderCarousel(trans);
	}

	renderExtras(trans, minMax.first, minMax.second);

	if (mCarousel.zIndex > mSystemInfo.getZIndex()) {
		renderCarousel(trans);
	} else {
		renderInfoBar(trans);
	}

	renderExtras(trans, minMax.second, INT16_MAX);
}

std::vector<HelpPrompt> SystemView::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	if (mCarousel.type == VERTICAL || mCarousel.type == VERTICAL_WHEEL)
		prompts.push_back(HelpPrompt("up/down", _("CHOOSE")));
	else
		prompts.push_back(HelpPrompt("left/right", _("CHOOSE")));

	// RetroPangui: InputConfig::getActionButton()로 통일(중복 삼항연산자 제거)
	prompts.push_back(HelpPrompt(InputConfig::getActionButton("accept"), _("SELECT")));
	prompts.push_back(HelpPrompt("x", _("RANDOM")));

	if (!UIModeController::getInstance()->isUIModeKid() && Settings::getInstance()->getBool("ScreenSaverControls"))
		prompts.push_back(HelpPrompt("select", _("LAUNCH SCREENSAVER")));

	return prompts;
}

HelpStyle SystemView::getHelpStyle()
{
	HelpStyle style;
	style.applyTheme(mEntries.at(mCursor).object->getTheme(), "system");
	return style;
}

void  SystemView::onThemeChanged(const std::shared_ptr<ThemeData>& /*theme*/)
{
	LOG(LogDebug) << "SystemView::onThemeChanged()";
	mViewNeedsReload = true;
	populate();
}

//  Get the ThemeElements that make up the SystemView.
void  SystemView::getViewElements(const std::shared_ptr<ThemeData>& theme)
{
	LOG(LogDebug) << "SystemView::getViewElements()";

	getDefaultElements();

	if (!theme->hasView("system"))
		return;

	const ThemeData::ThemeElement* carouselElem = theme->getElement("system", "systemcarousel", "carousel");
	if (carouselElem)
		getCarouselFromTheme(carouselElem);

	const ThemeData::ThemeElement* sysInfoElem = theme->getElement("system", "systemInfo", "text");
	if (sysInfoElem)
		mSystemInfo.applyTheme(theme, "system", "systemInfo", ThemeFlags::ALL);

	// RetroPangui: 게임 수 숫자 요소 (테마에 선언된 경우에만 표시)
	const ThemeData::ThemeElement* countNumElem = theme->getElement("system", "gameCountNumber", "text");
	mHasGameCountNumber = (countNumElem != nullptr);
	if (countNumElem)
		mGameCountNumber.applyTheme(theme, "system", "gameCountNumber", ThemeFlags::ALL);

	// RetroPangui: 즐겨찾기 수 숫자 요소 (테마에 선언된 경우에만 표시)
	const ThemeData::ThemeElement* favCountNumElem = theme->getElement("system", "favoriteCountNumber", "text");
	mHasFavoriteCountNumber = (favCountNumElem != nullptr);
	if (favCountNumElem)
		mFavoriteCountNumber.applyTheme(theme, "system", "favoriteCountNumber", ThemeFlags::ALL);

	mViewNeedsReload = false;
}

//  Render system carousel
void SystemView::renderCarousel(const Transform4x4f& trans)
{
	// background box behind logos
	Transform4x4f carouselTrans = trans;
	carouselTrans.translate(Vector3f(mCarousel.pos.x(), mCarousel.pos.y(), 0.0));
	carouselTrans.translate(Vector3f(mCarousel.origin.x() * mCarousel.size.x() * -1, mCarousel.origin.y() * mCarousel.size.y() * -1, 0.0f));

	Vector2f clipPos(carouselTrans.translation().x(), carouselTrans.translation().y());
	Renderer::pushClipRect(Vector2i((int)clipPos.x(), (int)clipPos.y()), Vector2i((int)mCarousel.size.x(), (int)mCarousel.size.y()));

	Renderer::setMatrix(carouselTrans);
	Renderer::drawRect(0.0f, 0.0f, mCarousel.size.x(), mCarousel.size.y(), mCarousel.color, mCarousel.colorEnd, mCarousel.colorGradientHorizontal);

	// draw logos
	Vector2f logoSpacing(0.0, 0.0); // NB: logoSpacing will include the size of the logo itself as well!
	float xOff = 0.0;
	float yOff = 0.0;

	switch (mCarousel.type)
	{
		case VERTICAL_WHEEL:
			yOff = (mCarousel.size.y() - mCarousel.logoSize.y()) / 2.f - (mCamOffset * logoSpacing[1]);
			if (mCarousel.logoAlignment == ALIGN_LEFT)
				xOff = mCarousel.logoSize.x() / 10.f;
			else if (mCarousel.logoAlignment == ALIGN_RIGHT)
				xOff = mCarousel.size.x() - (mCarousel.logoSize.x() * 1.1f);
			else
				xOff = (mCarousel.size.x() - mCarousel.logoSize.x()) / 2.f;
			break;
		case VERTICAL:
			logoSpacing[1] = ((mCarousel.size.y() - (mCarousel.logoSize.y() * mCarousel.maxLogoCount)) / (mCarousel.maxLogoCount)) + mCarousel.logoSize.y();
			yOff = (mCarousel.size.y() - mCarousel.logoSize.y()) / 2.f - (mCamOffset * logoSpacing[1]);

			if (mCarousel.logoAlignment == ALIGN_LEFT)
				xOff = mCarousel.logoSize.x() / 10.f;
			else if (mCarousel.logoAlignment == ALIGN_RIGHT)
				xOff = mCarousel.size.x() - (mCarousel.logoSize.x() * 1.1f);
			else
				xOff = (mCarousel.size.x() - mCarousel.logoSize.x()) / 2;
			break;
		case HORIZONTAL_WHEEL:
			xOff = (mCarousel.size.x() - mCarousel.logoSize.x()) / 2 - (mCamOffset * logoSpacing[1]);
			if (mCarousel.logoAlignment == ALIGN_TOP)
				yOff = mCarousel.logoSize.y() / 10;
			else if (mCarousel.logoAlignment == ALIGN_BOTTOM)
				yOff = mCarousel.size.y() - (mCarousel.logoSize.y() * 1.1f);
			else
				yOff = (mCarousel.size.y() - mCarousel.logoSize.y()) / 2;
			break;
		case HORIZONTAL:
		default:
			logoSpacing[0] = ((mCarousel.size.x() - (mCarousel.logoSize.x() * mCarousel.maxLogoCount)) / (mCarousel.maxLogoCount)) + mCarousel.logoSize.x();
			xOff = (mCarousel.size.x() - mCarousel.logoSize.x()) / 2.f - (mCamOffset * logoSpacing[0]);

			if (mCarousel.logoAlignment == ALIGN_TOP)
				yOff = mCarousel.logoSize.y() / 10.f;
			else if (mCarousel.logoAlignment == ALIGN_BOTTOM)
				yOff = mCarousel.size.y() - (mCarousel.logoSize.y() * 1.1f);
			else
				yOff = (mCarousel.size.y() - mCarousel.logoSize.y()) / 2.f;
			break;
	}

	int center = (int)(mCamOffset);
	int logoCount = Math::min(mCarousel.maxLogoCount, (int)mEntries.size());

	// Adding texture loading buffers depending on scrolling speed and status
	int bufferIndex = getScrollingVelocity() + 1;
	int bufferLeft = logoBuffersLeft[bufferIndex];
	int bufferRight = logoBuffersRight[bufferIndex];
	if (logoCount == 1)
	{
		bufferLeft = 0;
		bufferRight = 0;
	}

	for (int i = center - logoCount / 2 + bufferLeft; i <= center + logoCount / 2 + bufferRight; i++)
	{
		int index = i;
		while (index < 0)
			index += (int)mEntries.size();
		while (index >= (int)mEntries.size())
			index -= (int)mEntries.size();

		Transform4x4f logoTrans = carouselTrans;
		logoTrans.translate(Vector3f(i * logoSpacing[0] + xOff, i * logoSpacing[1] + yOff, 0));

		float distance = i - mCamOffset;

		float scale = 1.0f + ((mCarousel.logoScale - 1.0f) * (1.0f - fabs(distance)));
		scale = Math::min(mCarousel.logoScale, Math::max(1.0f, scale));
		scale /= mCarousel.logoScale;

		int opacity = (int)Math::round(0x80 + ((0xFF - 0x80) * (1.0f - fabs(distance))));
		opacity = Math::max((int) 0x80, opacity);

		const std::shared_ptr<GuiComponent> &comp = mEntries.at(index).data.logo;
		if (mCarousel.type == VERTICAL_WHEEL || mCarousel.type == HORIZONTAL_WHEEL) {
			comp->setRotationDegrees(mCarousel.logoRotation * distance);
			comp->setRotationOrigin(mCarousel.logoRotationOrigin);
		}
		comp->setScale(scale);
		comp->setOpacity((unsigned char)opacity);
		comp->render(logoTrans);
	}
	Renderer::popClipRect();
}

void SystemView::renderInfoBar(const Transform4x4f& trans)
{
	Renderer::setMatrix(trans);
	mSystemInfo.render(trans);
	if (mHasGameCountNumber)
		mGameCountNumber.render(trans);
	if (mHasFavoriteCountNumber)
		mFavoriteCountNumber.render(trans);
}

// Draw background extras
void SystemView::renderExtras(const Transform4x4f& trans, float lower, float upper)
{
	int extrasCenter = (int)mExtrasCamOffset;

	// Adding texture loading buffers depending on scrolling speed and status
	int bufferIndex = getScrollingVelocity() + 1;

	Renderer::pushClipRect(Vector2i::Zero(), Vector2i((int)mSize.x(), (int)mSize.y()));

	for (int i = extrasCenter + logoBuffersLeft[bufferIndex]; i <= extrasCenter + logoBuffersRight[bufferIndex]; i++)
	{
		int index = i;
		while (index < 0)
			index += (int)mEntries.size();
		while (index >= (int)mEntries.size())
			index -= (int)mEntries.size();

		//Only render selected system when not showing
		if (mShowing || index == mCursor)
		{
			Transform4x4f extrasTrans = trans;
			if (mCarousel.type == HORIZONTAL || mCarousel.type == HORIZONTAL_WHEEL)
				extrasTrans.translate(Vector3f((i - mExtrasCamOffset) * mSize.x(), 0, 0));
			else
				extrasTrans.translate(Vector3f(0, (i - mExtrasCamOffset) * mSize.y(), 0));

			Renderer::pushClipRect(Vector2i((int)extrasTrans.translation()[0], (int)extrasTrans.translation()[1]),
								   Vector2i((int)mSize.x(), (int)mSize.y()));
			SystemViewData data = mEntries.at(index).data;
			for (unsigned int j = 0; j < data.backgroundExtras.size(); j++) {
				GuiComponent *extra = data.backgroundExtras[j];
				if (extra->getZIndex() >= lower && extra->getZIndex() < upper) {
					extra->render(extrasTrans);
				}
			}
			Renderer::popClipRect();
		}
	}
	Renderer::popClipRect();
}

void SystemView::renderFade(const Transform4x4f& trans)
{
	// fade extras if necessary
	if (mExtrasFadeOpacity)
	{
		unsigned int fadeColor = 0x00000000 | (unsigned char)(mExtrasFadeOpacity * 255);
		Renderer::setMatrix(trans);
		Renderer::drawRect(0.0f, 0.0f, mSize.x(), mSize.y(), fadeColor, fadeColor);
	}
}

// Populate the system carousel with the legacy values
void  SystemView::getDefaultElements(void)
{
	// Carousel
	mCarousel.type = HORIZONTAL;
	mCarousel.logoAlignment = ALIGN_CENTER;
	mCarousel.size.x() = mSize.x();
	mCarousel.size.y() = 0.2325f * mSize.y();
	mCarousel.pos.x() = 0.0f;
	mCarousel.pos.y() = 0.5f * (mSize.y() - mCarousel.size.y());
	mCarousel.origin.x() = 0.0f;
	mCarousel.origin.y() = 0.0f;
	mCarousel.color = 0xFFFFFFD8;
	mCarousel.colorEnd = 0xFFFFFFD8;
	mCarousel.colorGradientHorizontal = true;
	mCarousel.logoScale = 1.2f;
	mCarousel.logoRotation = 7.5;
	mCarousel.logoRotationOrigin.x() = -5;
	mCarousel.logoRotationOrigin.y() = 0.5;
	mCarousel.logoSize.x() = 0.25f * mSize.x();
	mCarousel.logoSize.y() = 0.155f * mSize.y();
	mCarousel.maxLogoCount = 3;
	mCarousel.zIndex = 40;

	// System Info Bar
	mSystemInfo.setSize(mSize.x(), mSystemInfo.getFont()->getLetterHeight()*2.2f);
	mSystemInfo.setPosition(0, (mCarousel.pos.y() + mCarousel.size.y() - 0.2f));
	mSystemInfo.setBackgroundColor(0xDDDDDDD8);
	mSystemInfo.setRenderBackground(true);
	mSystemInfo.setFont(Font::get((int)(0.035f * mSize.y()), Font::getDefaultPath()));
	mSystemInfo.setColor(0x000000FF);
	mSystemInfo.setZIndex(50);
	mSystemInfo.setDefaultZIndex(50);
}

void SystemView::getCarouselFromTheme(const ThemeData::ThemeElement* elem)
{
	if (elem->has("type"))
	{
		if (!(elem->get<std::string>("type").compare("vertical")))
			mCarousel.type = VERTICAL;
		else if (!(elem->get<std::string>("type").compare("vertical_wheel")))
			mCarousel.type = VERTICAL_WHEEL;
		else if (!(elem->get<std::string>("type").compare("horizontal_wheel")))
			mCarousel.type = HORIZONTAL_WHEEL;
		else
			mCarousel.type = HORIZONTAL;
	}
	if (elem->has("size"))
		mCarousel.size = elem->get<Vector2f>("size") * mSize;
	if (elem->has("pos"))
		mCarousel.pos = elem->get<Vector2f>("pos") * mSize;
	if (elem->has("origin"))
		mCarousel.origin = elem->get<Vector2f>("origin");
	if (elem->has("color"))
	{
		mCarousel.color = elem->get<unsigned int>("color");
		mCarousel.colorEnd = mCarousel.color;
	}
	if (elem->has("colorEnd"))
		mCarousel.colorEnd = elem->get<unsigned int>("colorEnd");
	if (elem->has("gradientType"))
		mCarousel.colorGradientHorizontal = !(elem->get<std::string>("gradientType").compare("horizontal"));
	if (elem->has("logoScale"))
		mCarousel.logoScale = elem->get<float>("logoScale");
	if (elem->has("logoSize"))
		mCarousel.logoSize = elem->get<Vector2f>("logoSize") * mSize;
	if (elem->has("maxLogoCount"))
		mCarousel.maxLogoCount = (int)Math::round(elem->get<float>("maxLogoCount"));
	if (elem->has("zIndex"))
		mCarousel.zIndex = elem->get<float>("zIndex");
	if (elem->has("logoRotation"))
		mCarousel.logoRotation = elem->get<float>("logoRotation");
	if (elem->has("logoRotationOrigin"))
		mCarousel.logoRotationOrigin = elem->get<Vector2f>("logoRotationOrigin");
	if (elem->has("logoAlignment"))
	{
		if (!(elem->get<std::string>("logoAlignment").compare("left")))
			mCarousel.logoAlignment = ALIGN_LEFT;
		else if (!(elem->get<std::string>("logoAlignment").compare("right")))
			mCarousel.logoAlignment = ALIGN_RIGHT;
		else if (!(elem->get<std::string>("logoAlignment").compare("top")))
			mCarousel.logoAlignment = ALIGN_TOP;
		else if (!(elem->get<std::string>("logoAlignment").compare("bottom")))
			mCarousel.logoAlignment = ALIGN_BOTTOM;
		else
			mCarousel.logoAlignment = ALIGN_CENTER;
	}
}

void SystemView::onShow()
{
	mShowing = true;
}

void SystemView::onHide()
{
	mShowing = false;
}
