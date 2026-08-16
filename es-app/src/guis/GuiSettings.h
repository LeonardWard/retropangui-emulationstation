#pragma once
#ifndef ES_APP_GUIS_GUI_SETTINGS_H
#define ES_APP_GUIS_GUI_SETTINGS_H

#include "components/MenuComponent.h"

// This is just a really simple template for a GUI that calls some save functions when closed.
class GuiSettings : public GuiComponent
{
public:
	GuiSettings(Window* window, const char* title);
	virtual ~GuiSettings(); // just calls save();

	void save();
	inline void addRow(const ComponentListRow& row) { mMenu.addRow(row); };
	inline void addWithLabel(const std::string& label, const std::shared_ptr<GuiComponent>& comp) { mMenu.addWithLabel(label, comp); };
	// 라벨 밑에 작은 설명 문구를 붙이는 배리언트 - 필요한 항목에만 골라서 사용
	inline void addWithDescription(const std::string& label, const std::string& description, const std::shared_ptr<GuiComponent>& comp) { mMenu.addWithDescription(label, description, comp); };
	inline void addSaveFunc(const std::function<void()>& func) { mSaveFuncs.push_back(func); };

	void setOnSave(const std::function<void()>& func) { mOnSaveFunc = func; };
	void executeSaveFuncs();

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	HelpStyle getHelpStyle() override;

private:
	MenuComponent mMenu;
	std::vector< std::function<void()> > mSaveFuncs;
	std::function<void()> mOnSaveFunc;
};

#endif // ES_APP_GUIS_GUI_SETTINGS_H
