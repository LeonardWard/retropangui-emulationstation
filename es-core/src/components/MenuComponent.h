#pragma once
#ifndef ES_CORE_COMPONENTS_MENU_COMPONENT_H
#define ES_CORE_COMPONENTS_MENU_COMPONENT_H

#include "components/ComponentGrid.h"
#include "components/ComponentList.h"
#include "components/NinePatchComponent.h"
#include "components/TextComponent.h"
#include "utils/StringUtil.h"

class ButtonComponent;
class ImageComponent;

std::shared_ptr<ComponentGrid> makeButtonGrid(Window* window, const std::vector< std::shared_ptr<ButtonComponent> >& buttons);
std::shared_ptr<ImageComponent> makeArrow(Window* window);

#define TITLE_VERT_PADDING (Renderer::getScreenHeight()*0.0637f)

// 메뉴 제목 글자 크기 — 기존 FONT_SIZE_LARGE에서 15% 축소(2026-07-05)
#define MENU_TITLE_FONT_SIZE ((unsigned int)(FONT_SIZE_LARGE * 0.85f))

class MenuComponent : public GuiComponent
{
public:
	MenuComponent(Window* window, const char* title, const std::shared_ptr<Font>& titleFont = Font::get(MENU_TITLE_FONT_SIZE));

	void onSizeChanged() override;

	inline void addRow(const ComponentListRow& row, bool setCursorHere = false) { mList->addRow(row, setCursorHere); updateSize(); }

	inline void addWithLabel(const std::string& label, const std::shared_ptr<GuiComponent>& comp, bool setCursorHere = false, bool invert_when_selected = true)
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow, Utils::String::toUpper(label), Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
		row.addElement(comp, false, invert_when_selected);
		addRow(row, setCursorHere);
	}

	// 2026-08-16: 라벨 밑에 작은 설명 문구를 붙이는 배리언트(바토세라 스타일) -
	// addWithLabel()의 라벨 자리(단일 TextComponent)를, 라벨+설명 2줄을 세로로
	// 쌓은 ComponentGrid로 대체해서 끼워넣는다. ComponentList::getRowHeight()는
	// 행 안 요소 중 가장 큰 getSize().y()를 그대로 쓰므로(다른 행은 여전히
	// 한 줄 높이 그대로) 별도 구조 변경 없이 이 행만 자동으로 키가 커진다.
	inline void addWithDescription(const std::string& label, const std::string& description,
		const std::shared_ptr<GuiComponent>& comp, bool setCursorHere = false, bool invert_when_selected = true)
	{
		ComponentListRow row;

		auto labelText = std::make_shared<TextComponent>(mWindow, Utils::String::toUpper(label),
			Font::get(FONT_SIZE_MEDIUM), 0x777777FF);
		auto descText = std::make_shared<TextComponent>(mWindow, description,
			Font::get(FONT_SIZE_SMALL), 0x888888FF);
		float labelH = labelText->getSize().y();
		float descH  = descText->getSize().y();

		auto labelGrid = std::make_shared<ComponentGrid>(mWindow, Vector2i(1, 2));
		labelGrid->setEntry(labelText, Vector2i(0, 0), false, true);
		labelGrid->setEntry(descText,  Vector2i(0, 1), false, true);
		labelGrid->setRowHeightPerc(0, labelH / (labelH + descH), false);
		labelGrid->setRowHeightPerc(1, descH  / (labelH + descH), false);
		labelGrid->setSize(labelText->getSize().x(), labelH + descH);

		row.addElement(labelGrid, true);
		row.addElement(comp, false, invert_when_selected);
		addRow(row, setCursorHere);
	}

	void addButton(const std::string& label, const std::string& helpText, const std::function<void()>& callback);

	void setTitle(const char* title, const std::shared_ptr<Font>& font);

	inline void setCursorToList() { mGrid.setCursorTo(mList); }
	inline void setCursorToButtons() { assert(mButtonGrid); mGrid.setCursorTo(mButtonGrid); }

	virtual std::vector<HelpPrompt> getHelpPrompts() override;

private:
	void updateSize();
	void updateGrid();
	float getButtonGridHeight() const;

	NinePatchComponent mBackground;
	ComponentGrid mGrid;

	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<ComponentList> mList;
	std::shared_ptr<ComponentGrid> mButtonGrid;
	std::vector< std::shared_ptr<ButtonComponent> > mButtons;
};

#endif // ES_CORE_COMPONENTS_MENU_COMPONENT_H
