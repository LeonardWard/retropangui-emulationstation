
#pragma once
#ifndef ES_APP_VIEWS_GAME_LIST_ISIMPLE_GAME_LIST_VIEW_H
#define ES_APP_VIEWS_GAME_LIST_ISIMPLE_GAME_LIST_VIEW_H

#include "components/ImageComponent.h"
#include "components/TextComponent.h"
#include "views/gamelist/IGameListView.h"
#include <stack>
#include <utility>

class ISimpleGameListView : public IGameListView
{
public:
	ISimpleGameListView(Window* window, FileData* root);
	virtual ~ISimpleGameListView() {}

	// Called when a new file is added, a file is removed, a file's metadata changes, or a file's children are sorted.
	// NOTE: FILE_SORTED is only reported for the topmost FileData, where the sort started.
	//       Since sorts are recursive, that FileData's children probably changed too.
	virtual void onFileChanged(FileData* file, FileChangeType change) override;

	// Called whenever the theme changes.
	virtual void onThemeChanged(const std::shared_ptr<ThemeData>& theme) override;

	virtual FileData* getCursor() override = 0;
	virtual void setCursor(FileData*, bool refreshListCursorPos = false) override = 0;
	virtual int getViewportTop() override = 0;
	virtual void setViewportTop(int index) override = 0;

	virtual bool input(InputConfig* config, Input input) override;
	virtual void launch(FileData* game) override = 0;

protected:
	static const int DESCRIPTION_SCROLL_DELAY = 5 * 1000; // five secs

	virtual std::string getQuickSystemSelectRightButton() = 0;
	virtual std::string getQuickSystemSelectLeftButton() = 0;
	virtual void populateList(const std::vector<FileData*>& files) = 0;

	// 이름으로 theme extra 조회 (없으면 nullptr) — bgmTitle처럼 동적으로 값을 갱신할 extra를 찾을 때 사용
	GuiComponent* findThemeExtraByName(const std::string& name) const;

	// L2/R2로 메뉴 없이 바로 다음/이전 "글자 그룹"(이름 정렬 기준 첫 글자)으로
	// 커서 점프. direction: -1=이전 그룹, +1=다음 그룹. 한글 완성형(가~힣)은
	// 초성 블록으로 그룹화되어 가나다 순으로 점프됨.
	void jumpToAdjacentLetterGroup(int direction);

	TextComponent mHeaderText;
	ImageComponent mHeaderImage;
	ImageComponent mBackground;

	std::vector<std::pair<std::string, GuiComponent*>> mThemeExtras;

	std::stack<FileData*> mCursorStack;
};

#endif // ES_APP_VIEWS_GAME_LIST_ISIMPLE_GAME_LIST_VIEW_H
