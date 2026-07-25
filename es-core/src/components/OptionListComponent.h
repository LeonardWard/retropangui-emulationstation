#pragma once
#ifndef ES_CORE_COMPONENTS_OPTION_LIST_COMPONENT_H
#define ES_CORE_COMPONENTS_OPTION_LIST_COMPONENT_H

#include "GuiComponent.h"
#include "InputConfig.h"
#include "Log.h"
#include "Window.h"
#include <type_traits>

//Used to display a list of options.
//Can select one or multiple options.

// if !multiSelect
// * <- curEntry ->

// always
// * press a -> open full list

#define CHECKED_PATH ":/checkbox_checked.svg"
#define UNCHECKED_PATH ":/checkbox_unchecked.svg"

template<typename T>
class OptionListComponent : public GuiComponent
{
private:
	struct OptionListData
	{
		std::string name;
		T object;
		bool selected;
	};

	class OptionListPopup : public GuiComponent
	{
	private:
		MenuComponent mMenu;
		OptionListComponent<T>* mParent;

	public:
		OptionListPopup(Window* window, OptionListComponent<T>* parent, const std::string& title) : GuiComponent(window),
			mMenu(window, title.c_str()), mParent(parent)
		{
			auto font = Font::get(FONT_SIZE_MEDIUM);
			ComponentListRow row;

			// for select all/none
			std::vector<ImageComponent*> checkboxes;

			for(auto it = mParent->mEntries.begin(); it != mParent->mEntries.end(); it++)
			{
				row.elements.clear();
				row.addElement(std::make_shared<TextComponent>(mWindow, Utils::String::toUpper(it->name), font, 0x777777FF), true);

				OptionListData& e = *it;

				if(mParent->mMultiSelect)
				{
					// add checkbox
					auto checkbox = std::make_shared<ImageComponent>(mWindow);
					checkbox->setImage(it->selected ? CHECKED_PATH : UNCHECKED_PATH);
					checkbox->setResize(0, font->getLetterHeight());
					row.addElement(checkbox, false);

					// input handler
					// update checkbox state & selected value
					row.makeAcceptInputHandler([this, &e, checkbox]
					{
						e.selected = !e.selected;
						checkbox->setImage(e.selected ? CHECKED_PATH : UNCHECKED_PATH);
						mParent->onSelectedChanged();
					});

					// for select all/none
					checkboxes.push_back(checkbox.get());
				}else{
					// input handler for non-multiselect
					// update selected value and close
					row.makeAcceptInputHandler([this, &e]
					{
						if (!mParent->mEntries.empty())
							mParent->mEntries.at(mParent->getSelectedId()).selected = false;
						e.selected = true;
						mParent->onSelectedChanged();
						delete this;
					});
				}

				// also set cursor to this row if we're not multi-select and this row is selected
				mMenu.addRow(row, (!mParent->mMultiSelect && it->selected));
			}

			mMenu.addButton("BACK", "accept", [this] { delete this; });

			if(mParent->mMultiSelect)
			{
				mMenu.addButton("SELECT ALL", "select all", [this, checkboxes] {
					for(unsigned int i = 0; i < mParent->mEntries.size(); i++)
					{
						mParent->mEntries.at(i).selected = true;
						checkboxes.at(i)->setImage(CHECKED_PATH);
					}
					mParent->onSelectedChanged();
				});

				mMenu.addButton("SELECT NONE", "select none", [this, checkboxes] {
					for(unsigned int i = 0; i < mParent->mEntries.size(); i++)
					{
						mParent->mEntries.at(i).selected = false;
						checkboxes.at(i)->setImage(UNCHECKED_PATH);
					}
					mParent->onSelectedChanged();
				});
			}

			mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, Renderer::getScreenHeight() * 0.15f);
			addChild(&mMenu);
		}

		~OptionListPopup()
		{
			// RetroPangui: open()의 mPopupOpen 가드와 짝 - 이 팝업이 닫히는
			// 모든 경로(accept 선택, BACK 버튼, back 액션)에서 공통으로
			// 거치므로 여기 한 곳에서만 풀어주면 됨.
			mParent->mPopupOpen = false;
		}

		bool input(InputConfig* config, Input input) override
		{
			if(config->isMappedToAction("back", input) && input.value != 0)
			{
				delete this;
				return true;
			}

			return GuiComponent::input(config, input);
		}

		std::vector<HelpPrompt> getHelpPrompts() override
		{
			auto prompts = mMenu.getHelpPrompts();
			prompts.push_back(HelpPrompt(InputConfig::getActionButton("back"), "back"));
			return prompts;
		}
	};

public:
	OptionListComponent(Window* window, const std::string& name, bool multiSelect = false) : GuiComponent(window), mMultiSelect(multiSelect), mName(name),
		 mText(window), mLeftArrow(window), mRightArrow(window)
	{
		auto font = Font::get(FONT_SIZE_MEDIUM, FONT_PATH_LIGHT);
		mText.setFont(font);
		mText.setColor(0x777777FF);
		mText.setHorizontalAlignment(ALIGN_CENTER);
		addChild(&mText);

		mLeftArrow.setResize(0, mText.getFont()->getLetterHeight());
		mRightArrow.setResize(0, mText.getFont()->getLetterHeight());

		if(mMultiSelect)
		{
			mRightArrow.setImage(":/arrow.svg");
			addChild(&mRightArrow);
		}else{
			mLeftArrow.setImage(":/option_arrow.svg");
			mLeftArrow.setFlipX(true);
			addChild(&mLeftArrow);

			mRightArrow.setImage(":/option_arrow.svg");
			addChild(&mRightArrow);
		}

		setSize(mLeftArrow.getSize().x() + mRightArrow.getSize().x(), font->getHeight());
	}

	// handles positioning/resizing of text and arrows
	void onSizeChanged() override
	{
		mLeftArrow.setResize(0, mText.getFont()->getLetterHeight());
		mRightArrow.setResize(0, mText.getFont()->getLetterHeight());

		if(mSize.x() < (mLeftArrow.getSize().x() + mRightArrow.getSize().x()))
			LOG(LogWarning) << "OptionListComponent too narrow!";

		mText.setSize(mSize.x() - mLeftArrow.getSize().x() - mRightArrow.getSize().x(), mText.getFont()->getHeight());

		// position
		mLeftArrow.setPosition(0, (mSize.y() - mLeftArrow.getSize().y()) / 2);
		mText.setPosition(mLeftArrow.getPosition().x() + mLeftArrow.getSize().x(), (mSize.y() - mText.getSize().y()) / 2);
		mRightArrow.setPosition(mText.getPosition().x() + mText.getSize().x(), (mSize.y() - mRightArrow.getSize().y()) / 2);
	}

	bool input(InputConfig* config, Input input) override
	{
		if(input.value != 0)
		{
			if(config->isMappedToAction("accept", input))
			{
				open();
				return true;
			}
			// RetroPangui: mEntries가 비어있으면(예: 스캔 결과 0개) getSelectedId()가
			// "선택된 항목 없음 - 0으로 기본 처리"하고 경고만 남긴 채 0을 반환하는데,
			// 그 0으로 곧장 mEntries.at(0)을 호출하는 아래 코드들이 빈 컨테이너에서
			// std::out_of_range를 던져 앱 전체가 죽었음(2026-07-25 실기기 확인 -
			// WiFi 네트워크 재선택 시 재현). 빈 목록에서는 애초에 옮길 항목이 없으니
			// 조용히 무시.
			if(!mMultiSelect && !mEntries.empty())
			{
				if(config->isMappedLike("left", input))
				{
					// move selection to previous
					unsigned int i = getSelectedId();
					int next = (int)i - 1;
					if(next < 0)
						next += (int)mEntries.size();

					mEntries.at(i).selected = false;
					mEntries.at(next).selected = true;
					onSelectedChanged();
					return true;

				}else if(config->isMappedLike("right", input))
				{
					// move selection to next
					unsigned int i = getSelectedId();
					int next = (i + 1) % mEntries.size();
					mEntries.at(i).selected = false;
					mEntries.at(next).selected = true;
					onSelectedChanged();
					return true;

				}
			}
		}
		return GuiComponent::input(config, input);
	}

	std::vector<T> getSelectedObjects()
	{
		std::vector<T> ret;
		for(auto it = mEntries.cbegin(); it != mEntries.cend(); it++)
		{
			if(it->selected)
				ret.push_back(it->object);
		}

		return ret;
	}

	T getSelected()
	{
		assert(mMultiSelect == false);
		auto selected = getSelectedObjects();
		assert(selected.size() == 1);
		// RetroPangui: release 빌드는 NDEBUG로 위 assert가 컴파일에서 빠지므로,
		// mEntries가 비어있거나(예: 스캔 결과 0개) 아무 항목도 selected=true가
		// 아닌 상태에서 selected.at(0)이 std::out_of_range를 던져 앱이 죽는
		// 경로가 있었음(2026-07-25, getSelectedId()의 동일 계열 버그와 같은
		// 실기기 크래시에서 발견 - OptionListComponent::input()도 참고).
		// 기본 생성값을 안전하게 반환.
		if (selected.empty())
		{
			LOG(LogWarning) << "OptionListComponent::getSelected() - no selected element found, returning default";
			return T();
		}
		return selected.at(0);
	}

	// RetroPangui: Add getValue() for compatibility with GuiMetaDataEd
	// Only works when T is std::string
	template<typename U = T>
	typename std::enable_if<std::is_same<U, std::string>::value, std::string>::type
	getValue() const
	{
		// For single-select mode, return the selected object as string
		if (mMultiSelect)
		{
			// For multi-select, return empty (not used in metadata editing)
			return "";
		}

		// Find selected entry
		for (const auto& entry : mEntries)
		{
			if (entry.selected)
				return entry.object;
		}
		return ""; // No selection
	}

	// For non-string types, return empty string (not used in metadata editing)
	template<typename U = T>
	typename std::enable_if<!std::is_same<U, std::string>::value, std::string>::type
	getValue() const
	{
		return "";
	}

	// RetroPangui: Add setValue() for compatibility with GuiMetaDataEd
	// Only works when T is std::string
	template<typename U = T>
	typename std::enable_if<std::is_same<U, std::string>::value, void>::type
	setValue(const std::string& val)
	{
		// Deselect all
		for (auto& entry : mEntries)
			entry.selected = false;

		// Select the matching entry
		for (auto& entry : mEntries)
		{
			if (entry.object == val)
			{
				entry.selected = true;
				onSelectedChanged();
				return;
			}
		}

		// If not found, select first entry as default
		if (!mEntries.empty())
		{
			mEntries[0].selected = true;
			onSelectedChanged();
		}
	}

	// For non-string types, do nothing
	template<typename U = T>
	typename std::enable_if<!std::is_same<U, std::string>::value, void>::type
	setValue(const std::string& val)
	{
		// Do nothing for non-string types
	}

	void add(const std::string& name, const T& obj, bool selected)
	{
		OptionListData e;
		e.name = name;
		e.object = obj;
		e.selected = selected;

		mEntries.push_back(e);
		onSelectedChanged();
	}

	void selectAll()
	{
		for(unsigned int i = 0; i < mEntries.size(); i++)
		{
			mEntries.at(i).selected = true;
		}
		onSelectedChanged();
	}

	void selectNone()
	{
		for(unsigned int i = 0; i < mEntries.size(); i++)
		{
			mEntries.at(i).selected = false;
		}
		onSelectedChanged();
	}

	// 2026-07-11: 화면 진입 즉시 선택 팝업을 띄우고 싶은 화면(예: WIFI
	// 네트워크 설정)을 위해 private open()을 밖에서 부를 수 있게 감쌈.
	void openPopup() { open(); }

private:
	unsigned int getSelectedId()
	{
		assert(mMultiSelect == false);
		for(unsigned int i = 0; i < mEntries.size(); i++)
		{
			if(mEntries.at(i).selected)
				return i;
		}

		LOG(LogWarning) << "OptionListComponent::getSelectedId() - no selected element found, defaulting to 0 (name=" << mName << " size=" << mEntries.size() << ")";
		return 0;
	}

	void open()
	{
		if (mPopupOpen)
			return;
		mPopupOpen = true;
		mWindow->pushGui(new OptionListPopup(mWindow, this, mName));
	}

	void onSelectedChanged()
	{
		if(mMultiSelect)
		{
			// display # selected
			std::stringstream ss;
			ss << getSelectedObjects().size() << " SELECTED";
			mText.setText(ss.str());
			mText.setSize(0, mText.getSize().y());
			setSize(mText.getSize().x() + mRightArrow.getSize().x() + 24, mText.getSize().y());
			if(mParent) // hack since theres no "on child size changed" callback atm...
				mParent->onSizeChanged();
		}else{
			// display currently selected + l/r cursors
			for(auto it = mEntries.cbegin(); it != mEntries.cend(); it++)
			{
				if(it->selected)
				{
					mText.setText(Utils::String::toUpper(it->name));
					mText.setSize(0, mText.getSize().y());
					setSize(mText.getSize().x() + mLeftArrow.getSize().x() + mRightArrow.getSize().x() + 24, mText.getSize().y());
					if(mParent) // hack since theres no "on child size changed" callback atm...
						mParent->onSizeChanged();
					break;
				}
			}
		}
	}

	std::vector<HelpPrompt> getHelpPrompts() override
	{
		std::vector<HelpPrompt> prompts;
		if(!mMultiSelect)
			prompts.push_back(HelpPrompt("left/right", "change"));

		// RetroPangui: InputConfig::getActionButton()로 통일(중복 삼항연산자 제거)
		prompts.push_back(HelpPrompt(InputConfig::getActionButton("accept"), "select"));
		return prompts;
	}

	bool mMultiSelect;

	std::string mName;
	TextComponent mText;
	ImageComponent mLeftArrow;
	ImageComponent mRightArrow;

	std::vector<OptionListData> mEntries;

	// RetroPangui: open()이 연타/버튼 반복입력(약 1초 뒤 재입력 등)으로 두 번
	// 불려서 같은 OptionListComponent에 대해 OptionListPopup이 중복으로
	// 뜨는 버그가 있었음(2026-07-25, WiFi SSID 재선택 크래시로 실기기에서
	// 확인 - 먼저 뜬 팝업이 한 번도 안 닫힌 채 화면 스택에 좀비로 남았다가,
	// 나중에(이 컴포넌트/list가 이미 소멸된 뒤) 그 좀비가 뒤늦게 입력을
	// 받아 이미 죽은 mParent를 참조 - use-after-free). 팝업이 이미 떠있는
	// 동안엔 open()을 무시.
	bool mPopupOpen = false;
};

#endif // ES_CORE_COMPONENTS_OPTION_LIST_COMPONENT_H
