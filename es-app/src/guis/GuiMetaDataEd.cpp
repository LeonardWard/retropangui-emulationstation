#include "guis/GuiMetaDataEd.h"

#include <stdlib.h>
#include "components/ButtonComponent.h"
#include "components/ComponentList.h"
#include "components/DateTimeComponent.h"
#include "components/DateTimeEditComponent.h"
#include "components/MenuComponent.h"
#include "components/OptionListComponent.h"
#include "components/RatingComponent.h"
#include "components/SwitchComponent.h"
#include "components/TextComponent.h"
#include "guis/GuiGameScraper.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiTextEditPopup.h"
#include "resources/Font.h"
#include "utils/StringUtil.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "FileData.h"
#include "FileFilterIndex.h"
#include "Gamelist.h"
#include "SystemData.h"
#include "Window.h"
#include "Log.h"
#include "LocaleES.h"
#include <fstream>
#include <sys/stat.h>

// retropangui.conf의 system.<system>.core= 시스템 기본 코어 override를 읽는다.
// rpui-launcher.py의 resolve_core_override_from_conf()와 동일한 조회를
// C++쪽에서도 해야 함 - 안 그러면 이 EMULATOR 드롭다운의 "(Default)" 표시가
// systems.json priority만 보고 매겨져서, retropangui.conf로 시스템 기본
// 코어를 바꿔도 실제 실행되는 코어와 라벨이 어긋난다(2026-08-10,
// todo-20260810-system-default-core-conf-gap.html).
//
// share 경로 탐색 순서(RETROPANGUI_SHARE 환경변수 → /share → ~/share)는
// GuiMenu.cpp의 getSharePath()와 반드시 동일해야 함 - S99emulationstation이
// RETROPANGUI_SHARE=/retropangui/share를 export해서 ES를 띄우므로, 이 순서를
// 안 지키면(예: ~/share만 확인) 실기기에서 조용히 항상 빈 값만 돌려주게 된다.
static std::string getRetropanguiShareSystemPath()
{
	const char* env = getenv("RETROPANGUI_SHARE");
	if (env && env[0] != '\0')
		return std::string(env) + "/system";

	struct stat st;
	if (stat("/share", &st) == 0 && S_ISDIR(st.st_mode))
		return "/share/system";

	const char* home = getenv("HOME");
	return (home ? std::string(home) : "") + "/share/system";
}

static std::string getSystemDefaultCoreModuleId(const std::string& systemName)
{
	std::string confPath = getRetropanguiShareSystemPath() + "/retropangui.conf";
	std::ifstream f(confPath);
	if (!f.is_open())
		return "";

	std::string prefix = "system." + systemName + ".core=";
	std::string line;
	while (std::getline(f, line))
	{
		if (line.compare(0, prefix.size(), prefix) == 0)
			return Utils::String::trim(line.substr(prefix.size()));
	}
	return "";
}

GuiMetaDataEd::GuiMetaDataEd(Window* window, MetaDataList* md, const std::vector<MetaDataDecl>& mdd, ScraperSearchParams scraperParams,
	const std::string& /*header*/, std::function<void()> saveCallback, std::function<void()> deleteFunc) : GuiComponent(window),
	mScraperParams(scraperParams),

	mBackground(window, ":/frame.png"),
	mGrid(window, Vector2i(1, 3)),

	mMetaDataDecl(mdd),
	mMetaData(md),
	mSavedCallback(saveCallback),
	mDeleteFunc(deleteFunc)
{
	addChild(&mBackground);
	addChild(&mGrid);

	mHeaderGrid = std::make_shared<ComponentGrid>(mWindow, Vector2i(1, 5));

	mTitle = std::make_shared<TextComponent>(mWindow, _("EDIT METADATA"), Font::get(FONT_SIZE_LARGE), 0x555555FF, ALIGN_CENTER);
	std::string tgt = md->getType() == GAME_METADATA ? "GAME" : "FOLDER";
	std::string subt = tgt + ": " + Utils::String::toUpper(Utils::FileSystem::getFileName(scraperParams.game->getPath()));
	mSubtitle = std::make_shared<TextComponent>(mWindow, subt, Font::get(FONT_SIZE_SMALL), 0x777777FF, ALIGN_CENTER);
	mHeaderGrid->setEntry(mTitle, Vector2i(0, 1), false, true);
	mHeaderGrid->setEntry(mSubtitle, Vector2i(0, 3), false, true);

	mGrid.setEntry(mHeaderGrid, Vector2i(0, 0), false, true);

	mList = std::make_shared<ComponentList>(mWindow);
	mGrid.setEntry(mList, Vector2i(0, 1), true, true);

	// populate list
	for(auto iter = mdd.cbegin(); iter != mdd.cend(); iter++)
	{
		// don't add statistics
		if(iter->isStatistic)
			continue;

		std::shared_ptr<GuiComponent> ed;

		// create ed and add it (and any related components) to mMenu
		// ed's value will be set below
		ComponentListRow row;
		auto lblTxt = Utils::String::toUpper(iter->displayName);
		if (iter->type == MD_DATE) {
			lblTxt += " (" + DateTimeComponent::getDateformatTip() + ")";
		}
		auto lbl = std::make_shared<TextComponent>(mWindow, lblTxt, Font::get(FONT_SIZE_SMALL), 0x777777FF);
		row.addElement(lbl, true); // label

		switch(iter->type)
		{
		case MD_BOOL:
			{
				ed = std::make_shared<SwitchComponent>(window);
				row.addElement(ed, false, true);
				break;
			}
		case MD_RATING:
			{
				ed = std::make_shared<RatingComponent>(window);
				const float height = lbl->getSize().y() * 0.71f;
				ed->setSize(0, height);
				row.addElement(ed, false, true);

				auto spacer = std::make_shared<GuiComponent>(mWindow);
				spacer->setSize(Renderer::getScreenWidth() * 0.0025f, 0);
				row.addElement(spacer, false);

				// pass input to the actual RatingComponent instead of the spacer
				row.input_handler = std::bind(&GuiComponent::input, ed.get(), std::placeholders::_1, std::placeholders::_2);

				break;
			}
		case MD_DATE:
			{
				ed = std::make_shared<DateTimeEditComponent>(window);
				row.addElement(ed, false);

				auto spacer = std::make_shared<GuiComponent>(mWindow);
				spacer->setSize(Renderer::getScreenWidth() * 0.0025f, 0);
				row.addElement(spacer, false);

				// pass input to the actual DateTimeEditComponent instead of the spacer
				row.input_handler = std::bind(&GuiComponent::input, ed.get(), std::placeholders::_1, std::placeholders::_2);

				break;
			}
		case MD_TIME:
			{
				ed = std::make_shared<DateTimeEditComponent>(window, DateTimeEditComponent::DISP_RELATIVE_TO_NOW);
				row.addElement(ed, false);
				break;
			}
		case MD_MULTILINE_STRING:
		default:
			{
				// RetroPangui: Special handling for "core" field - use OptionList
				if (iter->key == "core" && md->getType() == GAME_METADATA)
				{
					auto system = scraperParams.system;
					std::vector<CoreInfo> availableCores = system->getCores();

					if (!availableCores.empty())
					{
						auto coreList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("EMULATOR"), false);

						// Add "SYSTEM DEFAULT" option
						std::string currentCore = mMetaData->get("core");
						coreList->add(_("SYSTEM DEFAULT"), "", currentCore.empty());

						// "(Default)" 라벨 - retropangui.conf에 시스템 기본 코어
						// override가 있으면 그 module_id를 우선 쓰고, 없으면
						// priority 1 코어로 폴백(priorities.conf와 동일 순서).
						std::string defaultModuleId = getSystemDefaultCoreModuleId(system->getName());

						// Add all available cores
						for (const auto& core : availableCores)
						{
							std::string label = core.fullname;
							bool isDefault = defaultModuleId.empty() ? (core.priority == 1)
							                                          : (core.module_id == defaultModuleId);
							if (isDefault)
								label += " (Default)";

							bool selected = (!currentCore.empty() && currentCore == core.name);
							coreList->add(label, core.name, selected);
						}

						ed = coreList;
						row.addElement(ed, false, true);

						auto spacer = std::make_shared<GuiComponent>(mWindow);
						spacer->setSize(Renderer::getScreenWidth() * 0.0025f, 0);
						row.addElement(spacer, false);

						// Input handler - pass all input to the OptionListComponent
						row.input_handler = std::bind(&GuiComponent::input, ed.get(), std::placeholders::_1, std::placeholders::_2);
					}
					else
					{
						// If no cores available, show N/A as text
						ed = std::make_shared<TextComponent>(window, "N/A",
							Font::get(FONT_SIZE_SMALL, FONT_PATH_LIGHT), 0x777777FF, ALIGN_RIGHT);
						row.addElement(ed, true);
					}
				}
				else
				{
					// MD_STRING (default handling)
					ed = std::make_shared<TextComponent>(window, "", Font::get(FONT_SIZE_SMALL, FONT_PATH_LIGHT), 0x777777FF, ALIGN_RIGHT);
					const float height = lbl->getSize().y() * 0.71f;
					ed->setSize(0, height);
					row.addElement(ed, true);

					auto spacer = std::make_shared<GuiComponent>(mWindow);
					spacer->setSize(Renderer::getScreenWidth() * 0.005f, 0);
					row.addElement(spacer, false);

					auto bracket = std::make_shared<ImageComponent>(mWindow);
					bracket->setImage(":/arrow.svg");
					bracket->setResize(Vector2f(0, lbl->getFont()->getLetterHeight()));
					row.addElement(bracket, false);

					bool multiLine = iter->type == MD_MULTILINE_STRING;
					const std::string title = iter->displayPrompt;
					auto updateVal = [ed](const std::string& newVal) { ed->setValue(newVal); }; // ok callback (apply new value to ed)
					row.makeAcceptInputHandler([this, title, ed, updateVal, multiLine] {
						mWindow->pushGui(new GuiTextEditPopup(mWindow, title, ed->getValue(), updateVal, multiLine));
					});
				}
				break;
			}
		}

		assert(ed);
		mList->addRow(row);
		// RetroPangui: Skip setValue for core field (already set during creation)
		if (iter->key != "core") {
			ed->setValue(mMetaData->get(iter->key));
		}
		mEditors.push_back(ed);
	}

	std::vector< std::shared_ptr<ButtonComponent> > buttons;

	if(md->getType() == GAME_METADATA && !scraperParams.system->hasPlatformId(PlatformIds::PLATFORM_IGNORE))
		buttons.push_back(std::make_shared<ButtonComponent>(mWindow, _("SCRAPE"), _("scrape"), std::bind(&GuiMetaDataEd::fetch, this)));

	buttons.push_back(std::make_shared<ButtonComponent>(mWindow, _("SAVE"), _("save"), [&] { save(); delete this; }));
	buttons.push_back(std::make_shared<ButtonComponent>(mWindow, _("CANCEL"), _("cancel"), [&] { delete this; }));

	if(mDeleteFunc)
	{
		auto deleteFileAndSelf = [&] { mDeleteFunc(); delete this; };
		auto deleteBtnFunc = [this, deleteFileAndSelf] { mWindow->pushGui(new GuiMsgBox(mWindow, _("THIS WILL DELETE THE ACTUAL GAME FILE(S)!\nARE YOU SURE?"), _("YES"), deleteFileAndSelf, _("NO"), nullptr)); };
		buttons.push_back(std::make_shared<ButtonComponent>(mWindow, _("DELETE"), _("delete"), deleteBtnFunc));
	}

	mButtons = makeButtonGrid(mWindow, buttons);
	mGrid.setEntry(mButtons, Vector2i(0, 2), true, false);

	// resize + center
	float width = (float)Math::min(Renderer::getScreenHeight(), (int)(Renderer::getScreenWidth() * 0.90f));
	setSize(width, Renderer::getScreenHeight() * 0.82f);
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2, (Renderer::getScreenHeight() - mSize.y()) / 2);
}

void GuiMetaDataEd::onSizeChanged()
{
	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));

	mGrid.setSize(mSize);

	const float titleHeight = mTitle->getFont()->getLetterHeight();
	const float subtitleHeight = mSubtitle->getFont()->getLetterHeight();
	const float titleSubtitleSpacing = mSize.y() * 0.03f;

	mGrid.setRowHeightPerc(0, (titleHeight + titleSubtitleSpacing + subtitleHeight + TITLE_VERT_PADDING) / mSize.y());
	mGrid.setRowHeightPerc(2, mButtons->getSize().y() / mSize.y());

	mHeaderGrid->setRowHeightPerc(1, titleHeight / mHeaderGrid->getSize().y());
	mHeaderGrid->setRowHeightPerc(2, titleSubtitleSpacing / mHeaderGrid->getSize().y());
	mHeaderGrid->setRowHeightPerc(3, subtitleHeight / mHeaderGrid->getSize().y());
}

void GuiMetaDataEd::save()
{
	// remove game from index
	mScraperParams.system->getIndex()->removeFromIndex(mScraperParams.game);

	assert(mMetaDataDecl.size() >= mEditors.size());
	// there may be less editfields than metadata entries as
	// statistic md fields are not shown to the user.
	// md statistic fields are not necessarily at the end of the md list
	int edIdx = 0;
	for(auto &mdd : mMetaDataDecl)
	{
		if(!mdd.isStatistic) {
			std::string value;

			// RetroPangui: Special handling for core field - cast to OptionListComponent
			if (mdd.key == "core") {
				auto coreList = std::dynamic_pointer_cast<OptionListComponent<std::string>>(mEditors.at(edIdx));
				if (coreList) {
					value = coreList->getSelected();
				} else {
					value = "";
				}
			} else {
				value = mEditors.at(edIdx)->getValue();
			}

			mMetaData->set(mdd.key, value);
			edIdx++;
		}
	}

	// enter game in index
	mScraperParams.system->getIndex()->addToIndex(mScraperParams.game);

	if(mSavedCallback)
		mSavedCallback();

	// update respective Collection Entries
	CollectionSystemManager::get()->refreshCollectionSystems(mScraperParams.game);

	// RetroPangui: Always save metadata immediately when editing
	// Don't rely on SaveGamelistsMode setting for manual edits
	updateGamelist(mScraperParams.system);
}

void GuiMetaDataEd::fetch()
{
	GuiGameScraper* scr = new GuiGameScraper(mWindow, mScraperParams, std::bind(&GuiMetaDataEd::fetchDone, this, std::placeholders::_1));
	mWindow->pushGui(scr);
}

void GuiMetaDataEd::fetchDone(const ScraperSearchResult& result)
{
	assert(mMetaDataDecl.size() >= mEditors.size());
	int edIdx = 0;
	for(auto &mdd : mMetaDataDecl)
	{
		if(mdd.isStatistic)
			continue;

		mEditors.at(edIdx)->setValue(result.mdl.get(mdd.key));
		edIdx++;
	}
}

void GuiMetaDataEd::close(bool closeAllWindows)
{
	bool dirty = hasChanges();

	std::function<void()> closeFunc;
	if(!closeAllWindows)
	{
		closeFunc = [this] { delete this; };
	}else{
		Window* window = mWindow;
		closeFunc = [window, this] {
			while(window->peekGui() != ViewController::get())
				delete window->peekGui();
		};
	}

	if(dirty)
	{
		// changes were made, ask if the user wants to save them
		mWindow->pushGui(new GuiMsgBox(mWindow,
			_("SAVE CHANGES?"),
			_("YES"), [this, closeFunc] { save(); closeFunc(); },
			_("NO"), closeFunc
		));
	}else{
		closeFunc();
	}
}

bool GuiMetaDataEd::hasChanges()
{
	assert(mMetaDataDecl.size() >= mEditors.size());
	// find out if the user made any changes
	int edIdx = 0;
	for(auto &mdd : mMetaDataDecl)
	{
		if(!mdd.isStatistic)
		{
			std::string gamelistVal = mMetaData->get(mdd.key);
			std::string editorVal;

			// RetroPangui: Special handling for core field - cast to OptionListComponent
			if (mdd.key == "core") {
				auto coreList = std::dynamic_pointer_cast<OptionListComponent<std::string>>(mEditors.at(edIdx));
				if (coreList) {
					editorVal = coreList->getSelected();
				} else {
					editorVal = "";
				}
			} else {
				editorVal = mEditors.at(edIdx)->getValue();
			}
			edIdx++;
			if (mdd.key == "rating")
			{
				// needed to catch "0", "0.0" or ".<d>" (and "1.0") from gamelist string rating
				// getValue() of RatingComponent returns "0" for floats 0, 0.0; "0.<d>" for .<d>
				// and "1" for float 1.0
				// convert to float and compare to avoid false "Save Changes" prompt
				bool ok;
				if (to_float(gamelistVal, ok) != to_float(editorVal, ok))
					return true;
			}
			else
			{
				// string compare
				if (gamelistVal != editorVal)
					return true;
			}
		}
	}
	return false;
}

float GuiMetaDataEd::to_float(const std::string& str, bool& ok)
{
	errno = 0;
	char* end = nullptr;
	float f = std::strtof(str.c_str(), &end);
	ok = !str.empty() && !*end && errno == 0;
	if (!ok)
		LOG(LogWarning) << "Conversion of input string '" << str << "' to float failed or is incomplete. Return value: " << f;
	return f;
}

bool GuiMetaDataEd::input(InputConfig* config, Input input)
{
	if(GuiComponent::input(config, input))
		return true;

	const bool isStart = config->isMappedTo("start", input);
	if(input.value != 0 && (config->isMappedToAction("back", input) || isStart))
	{
		close(isStart);
		return true;
	}

	return false;
}

std::vector<HelpPrompt> GuiMetaDataEd::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts = mGrid.getHelpPrompts();
	prompts.push_back(HelpPrompt(InputConfig::getActionButton("back"), _("BACK")));
	prompts.push_back(HelpPrompt("start", _("CLOSE")));
	return prompts;
}
