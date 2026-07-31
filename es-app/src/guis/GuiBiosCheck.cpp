#include "guis/GuiBiosCheck.h"

#include "components/ComponentList.h"
#include "components/TextComponent.h"
#include "resources/Font.h"
#include "renderers/Renderer.h"
#include "utils/StringUtil.h"
#include "LocaleES.h"
#include "Log.h"
#include "Window.h"

#include <rapidjson/document.h>

#define BIOS_CHECK_SCRIPT "python3 /usr/share/retropangui/bios-check.py 2>/dev/null"

// 상태 색상 - 정상/주의/누락 (Recalbox식 3단계 Green/Yellow/Red 모델)
#define COLOR_OK      0x44AA44FF
#define COLOR_WARN    0xCC9933FF
#define COLOR_MISSING 0xCC3333FF
#define COLOR_HEADER  0x8899AAFF
#define COLOR_TEXT    0x777777FF

GuiBiosCheck::GuiBiosCheck(Window* window)
	: GuiComponent(window), mBackground(window, ":/frame.png"),
	  mPipe(nullptr), mTotal(0), mIndex(0), mDone(false),
	  mOkCount(0), mWarnCount(0), mMissingCount(0)
{
	addChild(&mBackground);

	mTitle = std::make_shared<TextComponent>(mWindow, _("BIOS CHECK"),
		Font::get(FONT_SIZE_MEDIUM), 0x555555FF, ALIGN_CENTER);
	addChild(mTitle.get());

	// 요약 칩 3개 - 스캔 중에도 실시간으로 숫자가 올라가며 색으로 구분됨
	mChipOk = std::make_shared<TextComponent>(mWindow, "", Font::get(FONT_SIZE_SMALL), COLOR_OK, ALIGN_LEFT);
	mChipWarn = std::make_shared<TextComponent>(mWindow, "", Font::get(FONT_SIZE_SMALL), COLOR_WARN, ALIGN_LEFT);
	mChipMiss = std::make_shared<TextComponent>(mWindow, "", Font::get(FONT_SIZE_SMALL), COLOR_MISSING, ALIGN_LEFT);
	addChild(mChipOk.get());
	addChild(mChipWarn.get());
	addChild(mChipMiss.get());

	mList = std::make_shared<ComponentList>(mWindow);
	mList->setCursorChangedCallback([this](CursorState) { updateDetail(); });
	addChild(mList.get());

	mDetail = std::make_shared<TextComponent>(mWindow, _("SCANNING..."),
		Font::get(FONT_SIZE_SMALL), COLOR_TEXT, ALIGN_CENTER);
	addChild(mDetail.get());

	startScan();
	updateSummaryChips();

	setSize(Renderer::getScreenWidth() * 0.72f, Renderer::getScreenHeight() * 0.82f);
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2,
	            (Renderer::getScreenHeight() - mSize.y()) / 2);
}

GuiBiosCheck::~GuiBiosCheck()
{
	// 스캔 도중 이 GUI가 파괴되는 경로가 생기더라도(예: ES 종료) 파이프를
	// 반드시 닫아 좀비 프로세스/fd 누수를 막는다. input()이 스캔 중엔
	// 닫기를 막아두지만 방어적으로 둔다.
	if (mPipe != nullptr)
		pclose(mPipe);
}

// mPipe에서 한 줄 읽는다. 개별 항목 detail 텍스트가 아무리 길어도 넉넉한
// 고정 버퍼(bios-check.json의 note는 실측상 수백 바이트를 넘지 않음).
static bool readLine(FILE* pipe, std::string& out)
{
	if (pipe == nullptr)
		return false;
	char buf[8192];
	if (fgets(buf, sizeof(buf), pipe) == nullptr)
		return false;
	out = buf;
	while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
		out.pop_back();
	return true;
}

void GuiBiosCheck::startScan()
{
	mPipe = popen(BIOS_CHECK_SCRIPT, "r");
	if (mPipe == nullptr)
	{
		LOG(LogError) << "GuiBiosCheck: bios-check.py 실행 실패";
		mDetail->setText(_("BIOS DEFINITION FILE NOT FOUND"));
		mDone = true;
		return;
	}

	std::string headerLine;
	if (!readLine(mPipe, headerLine))
	{
		LOG(LogError) << "GuiBiosCheck: bios-check.py 헤더 줄 없음";
		pclose(mPipe);
		mPipe = nullptr;
		mDetail->setText(_("BIOS DEFINITION FILE NOT FOUND"));
		mDone = true;
		return;
	}

	rapidjson::Document header;
	header.Parse(headerLine.c_str());
	if (header.HasParseError() || !header.IsObject() || !header.HasMember("total") || !header["total"].IsUint())
	{
		LOG(LogError) << "GuiBiosCheck: bios-check.py 헤더 파싱 실패 - " << headerLine;
		pclose(mPipe);
		mPipe = nullptr;
		mDetail->setText(_("BIOS DEFINITION FILE NOT FOUND"));
		mDone = true;
		return;
	}

	mTotal = header["total"].GetUint();
	if (mTotal == 0)
	{
		pclose(mPipe);
		mPipe = nullptr;
		mDetail->setText(_("BIOS DEFINITION FILE NOT FOUND"));
		mDone = true;
	}
}

bool GuiBiosCheck::readNextEntry(BiosEntry& e)
{
	std::string line;
	if (!readLine(mPipe, line))
		return false;

	rapidjson::Document doc;
	doc.Parse(line.c_str());
	if (doc.HasParseError() || !doc.IsObject())
	{
		LOG(LogError) << "GuiBiosCheck: 항목 파싱 실패 - " << line;
		return false;
	}

	e.system     = doc.HasMember("system") && doc["system"].IsString() ? doc["system"].GetString() : "";
	e.systemName = doc.HasMember("systemName") && doc["systemName"].IsString() ? doc["systemName"].GetString() : e.system;
	e.path       = doc.HasMember("path") && doc["path"].IsString() ? doc["path"].GetString() : "";
	e.statusText = doc.HasMember("statusText") && doc["statusText"].IsString() ? doc["statusText"].GetString() : "";
	e.detail     = doc.HasMember("detail") && doc["detail"].IsString() ? doc["detail"].GetString() : "";

	const std::string statusStr = doc.HasMember("status") && doc["status"].IsString() ? doc["status"].GetString() : "";
	if (statusStr == "ok")           e.status = BiosStatus::Ok;
	else if (statusStr == "missing") e.status = BiosStatus::Missing;
	else                              e.status = BiosStatus::Warning;

	return true;
}

void GuiBiosCheck::addSystemHeaderRow(const std::string& name)
{
	ComponentListRow row;
	row.addElement(std::make_shared<TextComponent>(mWindow, Utils::String::toUpper(name),
		Font::get(FONT_SIZE_SMALL), COLOR_HEADER), true);
	mList->addRow(row);
	mRowEntry.push_back(-1);
}

void GuiBiosCheck::addResultRow(const BiosEntry& e)
{
	if (e.system != mLastSystem)
	{
		addSystemHeaderRow(e.systemName);
		mLastSystem = e.system;
	}

	unsigned int color = COLOR_OK;
	if (e.status == BiosStatus::Warning) color = COLOR_WARN;
	else if (e.status == BiosStatus::Missing) color = COLOR_MISSING;

	ComponentListRow row;
	// [●상태점] [파일명(남는 폭 전부)] [한국어 상태 라벨] - 색으로 한눈에 구분
	row.addElement(std::make_shared<TextComponent>(mWindow, "  ● ",
		Font::get(FONT_SIZE_SMALL), color), false);
	row.addElement(std::make_shared<TextComponent>(mWindow, e.path,
		Font::get(FONT_SIZE_SMALL), COLOR_TEXT), true);
	row.addElement(std::make_shared<TextComponent>(mWindow, _(e.statusText.c_str()),
		Font::get(FONT_SIZE_SMALL), color), false);
	mList->addRow(row);
	mRowEntry.push_back((int)mIndex);
}

void GuiBiosCheck::updateSummaryChips()
{
	mChipOk->setText("● " + std::string(_("OK")) + " " + std::to_string(mOkCount));
	mChipWarn->setText("● " + std::string(_("WARNING")) + " " + std::to_string(mWarnCount));
	mChipMiss->setText("● " + std::string(_("MISSING")) + " " + std::to_string(mMissingCount));

	// 세 칩을 가운데 정렬로 나란히 배치 - 폭이 숫자에 따라 변해서 매번 재계산
	const float y = mTitle->getPosition().y() + mTitle->getSize().y() * 1.35f;
	const float gap = Font::get(FONT_SIZE_SMALL)->sizeText("MM").x();
	float wOk = Font::get(FONT_SIZE_SMALL)->sizeText(mChipOk->getValue()).x();
	float wWarn = Font::get(FONT_SIZE_SMALL)->sizeText(mChipWarn->getValue()).x();
	float wMiss = Font::get(FONT_SIZE_SMALL)->sizeText(mChipMiss->getValue()).x();
	float total = wOk + wWarn + wMiss + gap * 2;
	float x = (mSize.x() - total) / 2.0f;
	mChipOk->setPosition(x, y);
	mChipWarn->setPosition(x + wOk + gap, y);
	mChipMiss->setPosition(x + wOk + gap + wWarn + gap, y);
}

void GuiBiosCheck::updateDetail()
{
	if (!mDone && mIndex < mTotal)
		return; // 스캔 중엔 진행 표시를 유지

	const int cursor = mList->getCursorId();
	if (cursor < 0 || cursor >= (int)mRowEntry.size() || mRowEntry[cursor] < 0)
	{
		mDetail->setText("");
		return;
	}

	const BiosEntry& e = mEntries.at(mRowEntry[cursor]);
	unsigned int color = COLOR_OK;
	if (e.status == BiosStatus::Warning) color = COLOR_WARN;
	else if (e.status == BiosStatus::Missing) color = COLOR_MISSING;
	mDetail->setColor(color);
	mDetail->setText(e.detail.empty() ? std::string(_(e.statusText.c_str())) : e.detail);
}

void GuiBiosCheck::update(int deltaTime)
{
	GuiComponent::update(deltaTime);

	if (mDone)
		return;

	// 프레임당 1줄 - bios-check.py가 한 항목 검사할 때마다 stdout에 flush한
	// 줄을 그때그때 읽으므로, md5 계산 시간이 여러 프레임에 자연히 분산됨
	// (GuiGamelistRefresh 관례와 동일한 체감).
	if (mIndex < mTotal)
	{
		BiosEntry e;
		if (!readNextEntry(e))
		{
			// 스크립트가 예상보다 일찍 끝남(에러 등) - 남은 항목은 포기하고 종료
			LOG(LogWarning) << "GuiBiosCheck: bios-check.py가 " << mIndex << "/" << mTotal << "에서 끊김";
			mTotal = mIndex;
		}
		else
		{
			mEntries.push_back(e);
			if (e.status == BiosStatus::Ok) mOkCount++;
			else if (e.status == BiosStatus::Warning) mWarnCount++;
			else mMissingCount++;
			addResultRow(e);
			updateSummaryChips();
			mDetail->setColor(COLOR_TEXT);
			mDetail->setText(std::string(_("SCANNING...")) + " " + std::to_string(mIndex + 1)
				+ " / " + std::to_string(mTotal));
			mIndex++;
			return;
		}
	}

	if (mPipe != nullptr)
	{
		pclose(mPipe);
		mPipe = nullptr;
	}
	mDone = true;
	updateDetail();
	updateHelpPrompts();
}

void GuiBiosCheck::onSizeChanged()
{
	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));

	const float padX = mSize.x() * 0.04f;
	const float padY = mSize.y() * 0.04f;

	mTitle->setSize(mSize.x() - padX * 2, 0);
	mTitle->setPosition(padX, padY);

	const float detailH = Font::get(FONT_SIZE_SMALL)->getLetterHeight() * 2.2f;
	mDetail->setSize(mSize.x() - padX * 2, 0);
	mDetail->setPosition(padX, mSize.y() - padY - detailH);

	const float chipH = Font::get(FONT_SIZE_SMALL)->getLetterHeight() * 2.0f;
	const float listTop = mTitle->getPosition().y() + mTitle->getSize().y() * 1.35f + chipH;
	mList->setPosition(padX, listTop);
	mList->setSize(mSize.x() - padX * 2, mDetail->getPosition().y() - listTop - padY * 0.5f);

	updateSummaryChips();
}

bool GuiBiosCheck::input(InputConfig* config, Input input)
{
	// 스캔 중에는 스크롤만 허용 - 도중 닫기로 인한 어중간한 리포트를 막는다
	if (!mDone)
	{
		if (config->isMappedLike("up", input) || config->isMappedLike("down", input))
			return mList->input(config, input);
		return true;
	}

	// RetroPangui: isMappedToAction()으로 통일 - 물리 버튼 자체는 동일(East/South
	// 둘 다 받아주므로 동작 변화 없음), 다른 화면들과 표기 일관성 목적.
	if (input.value != 0 && (config->isMappedToAction("accept", input) || config->isMappedToAction("back", input)))
	{
		delete this;
		return true;
	}

	return mList->input(config, input);
}

std::vector<HelpPrompt> GuiBiosCheck::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	if (mDone)
	{
		prompts.push_back(HelpPrompt("up/down", _("CHOOSE")));
		prompts.push_back(HelpPrompt(InputConfig::getActionButton("back"), _("BACK")));
	}
	return prompts;
}
