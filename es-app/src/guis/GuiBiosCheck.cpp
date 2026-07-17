#include "guis/GuiBiosCheck.h"

#include "components/ComponentList.h"
#include "components/TextComponent.h"
#include "resources/Font.h"
#include "renderers/Renderer.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "LocaleES.h"
#include "Log.h"
#include "Window.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#define BIOS_DEFINITION_FILE "/usr/share/retropangui/bios-check.json"

// 상태 색상 - 정상/주의/누락 (Recalbox식 3단계 Green/Yellow/Red 모델)
#define COLOR_OK      0x44AA44FF
#define COLOR_WARN    0xCC9933FF
#define COLOR_MISSING 0xCC3333FF
#define COLOR_HEADER  0x8899AAFF
#define COLOR_TEXT    0x777777FF

// RETROPANGUI_SHARE 환경 변수 → /share → ~/share 순서로 탐색
// (MusicManager.cpp getMusicDirectory()와 동일 규칙)
static std::string getShareRoot()
{
	const char* env = getenv("RETROPANGUI_SHARE");
	if (env && env[0] != '\0')
		return std::string(env);

	if (Utils::FileSystem::isDirectory("/share"))
		return "/share";

	const char* home = getenv("HOME");
	return home ? std::string(home) + "/share" : "/share";
}

// popen("md5sum ...") 첫 토큰. ES 자체에 md5 구현이 없어서 busybox md5sum을
// 빌려 씀 - 바이오스 파일은 수십KB~수MB라 프레임당 1개 동기 호출로 충분.
static std::string md5OfFile(const std::string& path)
{
	// 작은따옴표 이스케이프: ' → '\''
	std::string escaped;
	for (size_t i = 0; i < path.size(); ++i)
	{
		if (path[i] == '\'')
			escaped += "'\\''";
		else
			escaped += path[i];
	}

	FILE* p = popen(("md5sum '" + escaped + "' 2>/dev/null").c_str(), "r");
	if (p == nullptr)
		return "";

	char buf[64] = {0};
	size_t n = fread(buf, 1, 32, p);
	pclose(p);
	if (n != 32)
		return "";
	return Utils::String::toLower(std::string(buf, 32));
}

GuiBiosCheck::GuiBiosCheck(Window* window)
	: GuiComponent(window), mBackground(window, ":/frame.png"),
	  mIndex(0), mDone(false), mOkCount(0), mWarnCount(0), mMissingCount(0)
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

	loadDefinitions();
	updateSummaryChips();

	setSize(Renderer::getScreenWidth() * 0.72f, Renderer::getScreenHeight() * 0.82f);
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2,
	            (Renderer::getScreenHeight() - mSize.y()) / 2);
}

void GuiBiosCheck::loadDefinitions()
{
	std::ifstream f(BIOS_DEFINITION_FILE);
	if (!f.is_open())
	{
		LOG(LogError) << "GuiBiosCheck: 정의 파일 없음 - " << BIOS_DEFINITION_FILE;
		mDetail->setText(_("BIOS DEFINITION FILE NOT FOUND"));
		mDone = true;
		return;
	}

	std::stringstream ss;
	ss << f.rdbuf();

	rapidjson::Document doc;
	doc.Parse(ss.str().c_str());
	if (doc.HasParseError() || !doc.IsObject())
	{
		LOG(LogError) << "GuiBiosCheck: JSON 파싱 실패 - "
			<< (doc.HasParseError() ? rapidjson::GetParseError_En(doc.GetParseError()) : "루트가 오브젝트 아님");
		mDetail->setText(_("BIOS DEFINITION FILE NOT FOUND"));
		mDone = true;
		return;
	}

	for (auto sys = doc.MemberBegin(); sys != doc.MemberEnd(); ++sys)
	{
		// "_comment" 같은 메타 키는 건너뜀
		if (!sys->value.IsObject() || !sys->value.HasMember("bios"))
			continue;

		const std::string sysKey = sys->name.GetString();
		const std::string sysName = (sys->value.HasMember("name") && sys->value["name"].IsString())
			? sys->value["name"].GetString() : sysKey;

		const rapidjson::Value& biosArr = sys->value["bios"];
		if (!biosArr.IsArray())
			continue;

		for (auto it = biosArr.Begin(); it != biosArr.End(); ++it)
		{
			if (!it->IsObject() || !it->HasMember("path") || !(*it)["path"].IsString())
				continue;

			BiosEntry e;
			e.system = sysKey;
			e.systemName = sysName;
			e.path = (*it)["path"].GetString();
			e.mandatory = it->HasMember("mandatory") && (*it)["mandatory"].IsBool() && (*it)["mandatory"].GetBool();
			e.hashMandatory = it->HasMember("hashMandatory") && (*it)["hashMandatory"].IsBool() && (*it)["hashMandatory"].GetBool();
			if (it->HasMember("note") && (*it)["note"].IsString())
				e.note = (*it)["note"].GetString();
			if (it->HasMember("md5") && (*it)["md5"].IsArray())
				for (auto m = (*it)["md5"].Begin(); m != (*it)["md5"].End(); ++m)
					if (m->IsString())
						e.md5.push_back(Utils::String::toLower(m->GetString()));

			e.status = BiosStatus::Missing;
			mEntries.push_back(e);
		}
	}

	if (mEntries.empty())
	{
		mDetail->setText(_("BIOS DEFINITION FILE NOT FOUND"));
		mDone = true;
	}
}

void GuiBiosCheck::checkEntry(BiosEntry& e)
{
	const std::string full = getShareRoot() + "/bios/" + e.path;

	if (!Utils::FileSystem::exists(full))
	{
		e.status = e.mandatory ? BiosStatus::Missing : BiosStatus::Warning;
		e.statusText = e.mandatory ? "MISSING" : "MISSING (OPTIONAL)";
		e.detail = e.note;
		return;
	}

	if (e.md5.empty())
	{
		e.status = BiosStatus::Ok;
		e.statusText = "FOUND";
		e.detail = e.note;
		return;
	}

	const std::string hash = md5OfFile(full);
	if (hash.empty())
	{
		e.status = BiosStatus::Warning;
		e.statusText = "MD5 CHECK FAILED";
		e.detail = e.note;
		return;
	}

	for (size_t i = 0; i < e.md5.size(); ++i)
	{
		if (e.md5[i] == hash)
		{
			e.status = BiosStatus::Ok;
			e.statusText = "OK";
			e.detail = e.note;
			return;
		}
	}

	e.status = e.hashMandatory ? BiosStatus::Missing : BiosStatus::Warning;
	e.statusText = "MD5 MISMATCH";
	// 어떤 파일이 온 건지 사용자가 추적할 수 있게 실측 해시를 상세줄에 노출
	e.detail = e.note + (e.note.empty() ? "" : " · ") + "md5 " + hash;
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
	if (!mDone && mIndex < mEntries.size())
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

	// 프레임당 1개 파일 - md5 계산이 프레임을 잡아먹지 않게 (GuiGamelistRefresh 관례)
	if (mIndex < mEntries.size())
	{
		BiosEntry& e = mEntries.at(mIndex);
		checkEntry(e);
		if (e.status == BiosStatus::Ok) mOkCount++;
		else if (e.status == BiosStatus::Warning) mWarnCount++;
		else mMissingCount++;
		addResultRow(e);
		updateSummaryChips();
		mDetail->setColor(COLOR_TEXT);
		mDetail->setText(std::string(_("SCANNING...")) + " " + std::to_string(mIndex + 1)
			+ " / " + std::to_string(mEntries.size()));
		mIndex++;
		return;
	}

	writeReport();
	mDone = true;
	updateDetail();
	updateHelpPrompts();
}

void GuiBiosCheck::writeReport()
{
	const std::string path = getShareRoot() + "/system/bios_report.txt";
	std::ofstream out(path);
	if (!out.is_open())
	{
		LOG(LogWarning) << "GuiBiosCheck: 리포트 저장 실패 - " << path;
		return;
	}

	out << "RetroPangUI BIOS report\n";
	for (size_t i = 0; i < mEntries.size(); ++i)
	{
		const BiosEntry& e = mEntries.at(i);
		const char* tag = (e.status == BiosStatus::Ok) ? "[OK]  "
			: (e.status == BiosStatus::Warning) ? "[WARN]" : "[MISS]";
		out << tag << " " << e.system << " " << e.path << " — " << e.statusText;
		if (!e.note.empty())
			out << " (" << e.note << ")";
		out << "\n";
	}
	out << "\nOK " << mOkCount << " / WARNING " << mWarnCount
		<< " / MISSING " << mMissingCount << "\n";
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

	if (input.value != 0 && (config->isMappedTo("a", input) || config->isMappedTo("b", input)))
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
		prompts.push_back(HelpPrompt("b", _("BACK")));
	}
	return prompts;
}
