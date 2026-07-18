#include "guis/GuiSaveStates.h"

#include "components/ComponentList.h"
#include "components/ImageComponent.h"
#include "components/TextComponent.h"
#include "resources/Font.h"
#include "renderers/Renderer.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "FileData.h"
#include "InputConfig.h"
#include "LocaleES.h"
#include "SystemData.h"
#include "Window.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>

// RETROPANGUI_SHARE 환경 변수 → /share → ~/share 순서로 탐색
// (MusicManager.cpp getMusicDirectory()와 동일 규칙)
static std::string getSavesDirectory()
{
	const char* env = getenv("RETROPANGUI_SHARE");
	if (env && env[0] != '\0')
		return std::string(env) + "/saves";

	if (Utils::FileSystem::isDirectory("/share"))
		return "/share/saves";

	const char* home = getenv("HOME");
	return (home ? std::string(home) + "/share" : "/share") + "/saves";
}

std::vector<SaveStateInfo> GuiSaveStates::scanSaveStates(FileData* game)
{
	std::vector<SaveStateInfo> states;

	const std::string stem = Utils::FileSystem::getStem(game->getPath());
	const std::string dir = getSavesDirectory() + "/" + game->getSystem()->getName();
	const std::string prefix = stem + ".state";

	Utils::FileSystem::stringList files = Utils::FileSystem::getDirContent(dir);
	for (Utils::FileSystem::stringList::const_iterator it = files.cbegin(); it != files.cend(); ++it)
	{
		const std::string name = Utils::FileSystem::getFileName(*it);
		if (name.compare(0, prefix.size(), prefix) != 0)
			continue;

		// ".state" 뒤가 전부 숫자일 때만 슬롯으로 인정 - ".state.png"(썸네일),
		// ".state.auto"(자동저장, --entryslot 대상이 아님) 등은 제외
		const std::string suffix = name.substr(prefix.size());
		int slot = 0;
		if (!suffix.empty())
		{
			bool digits = true;
			for (size_t i = 0; i < suffix.size(); ++i)
				if (suffix[i] < '0' || suffix[i] > '9') { digits = false; break; }
			if (!digits)
				continue;
			slot = atoi(suffix.c_str());
		}

		SaveStateInfo info;
		info.slot = slot;
		info.statePath = *it;
		info.thumbnailPath = Utils::FileSystem::exists(*it + ".png") ? (*it + ".png") : "";

		struct stat st;
		if (stat(it->c_str(), &st) == 0)
		{
			char buf[32];
			struct tm* t = localtime(&st.st_mtime);
			strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", t);
			info.label = "SLOT " + std::to_string(slot) + " — " + buf;
		}
		else
			info.label = "SLOT " + std::to_string(slot);

		states.push_back(info);
	}

	std::sort(states.begin(), states.end(),
		[](const SaveStateInfo& a, const SaveStateInfo& b) { return a.slot < b.slot; });

	return states;
}

GuiSaveStates::GuiSaveStates(Window* window, FileData* game, const std::function<void(int)>& launchCb)
	: GuiComponent(window), mBackground(window, ":/frame.png"), mLaunchCb(launchCb)
{
	addChild(&mBackground);

	mStates = scanSaveStates(game);

	mTitle = std::make_shared<TextComponent>(mWindow, _("SAVE STATES"),
		Font::get(FONT_SIZE_MEDIUM), 0x555555FF, ALIGN_CENTER);
	addChild(mTitle.get());

	mList = std::make_shared<ComponentList>(mWindow);
	addChild(mList.get());

	mThumbnail = std::make_shared<ImageComponent>(mWindow);
	addChild(mThumbnail.get());

	// 첫 행: 스테이트 없이 새로 시작 (slot -1)
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow, _("START NEW GAME"),
			Font::get(FONT_SIZE_SMALL), 0x777777FF), true);
		mList->addRow(row);
	}
	for (size_t i = 0; i < mStates.size(); ++i)
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow, mStates.at(i).label,
			Font::get(FONT_SIZE_SMALL), 0x777777FF), true);
		mList->addRow(row);
	}

	mList->setCursorChangedCallback([this](CursorState) { updateThumbnail(); });

	// RetroPangui: 이 화면은 ComponentGrid 없이 mList를 직접 addChild()하는
	// 구조라, 다른 화면들처럼 grid가 자동으로 넘겨주는 onFocusGained() 호출이
	// 없어서 mList가 계속 mFocused=false로 남아 선택 바(커서 하이라이트)가
	// 안 그려지고 있었음(사용자 발견) - 명시적으로 포커스를 줘서 고침.
	mList->onFocusGained();

	setSize(Renderer::getScreenWidth() * 0.75f, Renderer::getScreenHeight() * 0.7f);
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2,
	            (Renderer::getScreenHeight() - mSize.y()) / 2);

	updateThumbnail();
}

void GuiSaveStates::updateThumbnail()
{
	// 행 0은 "새로 시작"이라 썸네일 없음
	const int cursor = mList->getCursorId();
	std::string path;
	if (cursor >= 1 && cursor <= (int)mStates.size())
		path = mStates.at(cursor - 1).thumbnailPath;
	mThumbnail->setImage(path);
}

void GuiSaveStates::onSizeChanged()
{
	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));

	const float padX = mSize.x() * 0.04f;
	const float padY = mSize.y() * 0.05f;

	mTitle->setSize(mSize.x() - padX * 2, 0);
	mTitle->setPosition(padX, padY);

	const float listTop = padY + mTitle->getSize().y() * 1.6f;
	const float listWidth = (mSize.x() - padX * 2) * 0.55f;
	mList->setPosition(padX, listTop);
	mList->setSize(listWidth, mSize.y() - listTop - padY);

	// 우측 45% 영역 가운데에 썸네일 - setMaxSize라 비율 유지된 채 영역 안에 맞음
	const float thumbAreaX = padX + listWidth + padX;
	const float thumbAreaW = mSize.x() - thumbAreaX - padX;
	const float thumbAreaH = mSize.y() - listTop - padY;
	mThumbnail->setOrigin(0.5f, 0.5f);
	mThumbnail->setPosition(thumbAreaX + thumbAreaW / 2, listTop + thumbAreaH / 2);
	mThumbnail->setMaxSize(thumbAreaW, thumbAreaH);
}

bool GuiSaveStates::input(InputConfig* config, Input input)
{
	// RetroPangui: isMappedTo("a"/"b")는 물리 위치(East/South) 고정이라 ButtonLayout
	// 설정(SWAP BUTTONS A/B)을 무시함 - isMappedToAction("accept"/"back")으로 교체해서
	// 다른 화면들과 Accept/Back 물리 버튼이 항상 일치하도록 통일(사용자 지적).
	if (input.value != 0 && config->isMappedToAction("back", input))
	{
		delete this; // 실행 취소 - 게임을 시작하지 않고 화면만 닫음
		return true;
	}

	if (input.value != 0 && config->isMappedToAction("accept", input))
	{
		const int cursor = mList->getCursorId();
		const int slot = (cursor >= 1 && cursor <= (int)mStates.size())
			? mStates.at(cursor - 1).slot : -1;
		// 콜백(ViewController::launch)이 새 화면 전환/애니메이션을 시작하므로
		// 이 화면을 먼저 지우고 나서 호출 - delete 후 멤버 접근 금지
		auto cb = mLaunchCb;
		delete this;
		cb(slot);
		return true;
	}

	return mList->input(config, input);
}

std::vector<HelpPrompt> GuiSaveStates::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt("up/down", _("CHOOSE")));
	prompts.push_back(HelpPrompt(InputConfig::getActionButton("accept"), _("LAUNCH")));
	prompts.push_back(HelpPrompt(InputConfig::getActionButton("back"), _("BACK")));
	return prompts;
}
