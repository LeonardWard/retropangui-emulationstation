#include "views/gamelist/ISimpleGameListView.h"

#include <cctype>
#include "views/UIModeController.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "Scripting.h"
#include "Settings.h"
#include "Sound.h"
#include "SystemData.h"

// L2/R2 글자 점프용 "그룹 키" 계산. ASCII는 대문자 코드값 그대로, 한글
// 완성형 음절(U+AC00~U+D7A3)은 초성 블록(0~18, ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ
// 순)로 그룹화 - 유니코드 한글 음절은 이 순서로 인코딩돼 있어서 나눗셈만으로
// 가나다 순 그룹 판별이 됨(2026-07-10). 그 외 멀티바이트 문자는 코드포인트
// 자체로 대략 그룹화.
static int getJumpGroupKey(const std::string& sortName)
{
	if (sortName.empty())
		return 0;

	unsigned char c0 = (unsigned char)sortName[0];

	// ASCII (기존 GuiGamelistOptions::jumpToLetter()와 동일한 관례)
	if (c0 < 0x80)
		return (int)toupper(c0);

	// UTF-8 첫 코드포인트 디코딩 (2~4바이트)
	unsigned int codepoint = 0;
	int extraBytes = 0;
	if ((c0 & 0xE0) == 0xC0)      { codepoint = c0 & 0x1F; extraBytes = 1; }
	else if ((c0 & 0xF0) == 0xE0) { codepoint = c0 & 0x0F; extraBytes = 2; }
	else if ((c0 & 0xF8) == 0xF0) { codepoint = c0 & 0x07; extraBytes = 3; }
	else return 0x100; // 잘못된 시작 바이트 - 전부 한 그룹으로

	for (int i = 1; i <= extraBytes && i < (int)sortName.size(); i++)
	{
		unsigned char cc = (unsigned char)sortName[i];
		if ((cc & 0xC0) != 0x80) { codepoint = 0; break; } // 깨진 시퀀스
		codepoint = (codepoint << 6) | (cc & 0x3F);
	}

	// 한글 완성형 음절(가~힣) - 초성 인덱스로 그룹화 (가나다 순과 일치)
	if (codepoint >= 0xAC00 && codepoint <= 0xD7A3)
	{
		int choseong = (int)((codepoint - 0xAC00) / (21 * 28));
		return 0x200 + choseong; // ASCII 그룹(0x100 이하) 뒤에 오도록 오프셋
	}

	// 그 외 유니코드 문자 - 코드포인트로 대략 그룹화
	return 0x300 + (int)(codepoint & 0xFFFF);
}

ISimpleGameListView::ISimpleGameListView(Window* window, FileData* root) : IGameListView(window, root),
	mHeaderText(window), mHeaderImage(window), mBackground(window)
{
	mHeaderText.setText("Logo Text");
	mHeaderText.setSize(mSize.x(), 0);
	mHeaderText.setPosition(0, 0);
	mHeaderText.setHorizontalAlignment(ALIGN_CENTER);
	mHeaderText.setDefaultZIndex(50);

	mHeaderImage.setResize(0, mSize.y() * 0.185f);
	mHeaderImage.setOrigin(0.5f, 0.0f);
	mHeaderImage.setPosition(mSize.x() / 2, 0);
	mHeaderImage.setDefaultZIndex(50);

	mBackground.setResize(mSize.x(), mSize.y());
	mBackground.setDefaultZIndex(0);

	addChild(&mHeaderText);
	addChild(&mBackground);
}

void ISimpleGameListView::onThemeChanged(const std::shared_ptr<ThemeData>& theme)
{
	using namespace ThemeFlags;
	mBackground.applyTheme(theme, getName(), "background", ALL);
	mHeaderImage.applyTheme(theme, getName(), "logo", ALL);
	mHeaderText.applyTheme(theme, getName(), "logoText", ALL);

	// Remove old theme extras
	for (auto& extra : mThemeExtras)
	{
		removeChild(extra.second);
		delete extra.second;
	}
	mThemeExtras.clear();

	// Add new theme extras
	mThemeExtras = ThemeData::makeExtras(theme, getName(), mWindow);
	for (auto& extra : mThemeExtras)
	{
		addChild(extra.second);
	}

	if(mHeaderImage.hasImage())
	{
		removeChild(&mHeaderText);
		addChild(&mHeaderImage);
	}else{
		addChild(&mHeaderText);
		removeChild(&mHeaderImage);
	}
}

void ISimpleGameListView::onFileChanged(FileData* /*file*/, FileChangeType /*change*/)
{
	// we could be tricky here to be efficient;
	// but this shouldn't happen very often so we'll just always repopulate
	FileData* cursor = getCursor();
	if (!cursor->isPlaceHolder()) {
		populateList(cursor->getParent()->getChildrenListToDisplay());
		setCursor(cursor);
	}
	else
	{
		populateList(mRoot->getChildrenListToDisplay());
		setCursor(cursor);
	}
}

bool ISimpleGameListView::input(InputConfig* config, Input input)
{
	if(input.value != 0)
	{
		if(config->isMappedToAction("accept", input))
		{
			FileData* cursor = getCursor();
			if(cursor->getType() == GAME)
			{
				Sound::getFromTheme(getTheme(), getName(), "launch")->play();
				launch(cursor);
			}else{
				// it's a folder
				if(cursor->getChildren().size() > 0)
				{
					mCursorStack.push(cursor);
					populateList(cursor->getChildrenListToDisplay());
					FileData* cursor = getCursor();
					setCursor(cursor);
				}
			}

			return true;
		}else if(config->isMappedToAction("back", input))
		{
			if(mCursorStack.size())
			{
				populateList(mCursorStack.top()->getParent()->getChildrenListToDisplay());
				setCursor(mCursorStack.top());
				mCursorStack.pop();
				Sound::getFromTheme(getTheme(), getName(), "back")->play();
			}else{
				onFocusLost();
				SystemData* systemToView = getCursor()->getSystem();
				if (systemToView->isCollection())
				{
					systemToView = CollectionSystemManager::get()->getSystemToView(systemToView);
				}
				ViewController::get()->goToSystemView(systemToView);
			}

			return true;
		}else if(config->isMappedLike(getQuickSystemSelectRightButton(), input))
		{
			if(Settings::getInstance()->getBool("QuickSystemSelect"))
			{
				onFocusLost();
				ViewController::get()->goToNextGameList();
				return true;
			}
		}else if(config->isMappedLike(getQuickSystemSelectLeftButton(), input))
		{
			if(Settings::getInstance()->getBool("QuickSystemSelect"))
			{
				onFocusLost();
				ViewController::get()->goToPrevGameList();
				return true;
			}
		}else if (config->isMappedTo("x", input))
		{
			if (mRoot->getSystem()->isGameSystem())
			{
				// go to random system game
				FileData* randomGame = getCursor()->getSystem()->getRandomGame();
				if (randomGame)
				{
					setCursor(randomGame);
				}
				return true;
			}
		}else if (config->isMappedTo("y", input) && !UIModeController::getInstance()->isUIModeKid())
		{
			if(mRoot->getSystem()->isGameSystem())
			{
				if (CollectionSystemManager::get()->toggleGameInCollection(getCursor()))
				{
					return true;
				}
			}
		}else if (config->isMappedTo("l2", input))
		{
			jumpToAdjacentLetterGroup(-1);
			return true;
		}else if (config->isMappedTo("r2", input))
		{
			jumpToAdjacentLetterGroup(1);
			return true;
		}
	}

	FileData* cursor = getCursor();
	SystemData* system = this->mRoot->getSystem();
    	if (system != NULL) {
            Scripting::fireEvent("game-select", system->getName(), cursor->getPath(), cursor->getName(), "input");
        }
	else
	{
	    Scripting::fireEvent("game-select", "NULL", "NULL", "NULL", "input");
	}
	return IGameListView::input(config, input);
}

GuiComponent* ISimpleGameListView::findThemeExtraByName(const std::string& name) const
{
	for (auto& extra : mThemeExtras)
		if (extra.first == name)
			return extra.second;
	return nullptr;
}

void ISimpleGameListView::jumpToAdjacentLetterGroup(int direction)
{
	FileData* cursor = getCursor();
	if (cursor == nullptr || cursor->getParent() == nullptr)
		return;

	const std::vector<FileData*>& files = cursor->getParent()->getChildrenListToDisplay();
	if (files.empty())
		return;

	int idx = -1;
	for (size_t i = 0; i < files.size(); i++)
	{
		if (files[i] == cursor) { idx = (int)i; break; }
	}
	if (idx < 0)
		return;

	int curKey = getJumpGroupKey(cursor->getSortName());
	int i = idx;

	if (direction > 0)
	{
		// 현재 그룹을 지나 다음 그룹의 시작으로
		while (i < (int)files.size() - 1 && getJumpGroupKey(files[i]->getSortName()) == curKey)
			i++;
	}
	else
	{
		// 현재 그룹의 시작까지 먼저 올라간 다음, 그 앞 그룹의 시작까지 한 번 더
		while (i > 0 && getJumpGroupKey(files[i - 1]->getSortName()) == curKey)
			i--;
		if (i > 0)
		{
			i--;
			int prevKey = getJumpGroupKey(files[i]->getSortName());
			while (i > 0 && getJumpGroupKey(files[i - 1]->getSortName()) == prevKey)
				i--;
		}
	}

	setCursor(files[i]);
}
