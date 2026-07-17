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

// 상태 색상 - Ok/Warning/Missing (todo-20260714-bios-check-menu.html의
// Recalbox식 3단계 Green/Yellow/Red 모델)
#define COLOR_OK      0x44AA44FF
#define COLOR_WARN    0xCC9933FF
#define COLOR_MISSING 0xCC3333FF

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

	mList = std::make_shared<ComponentList>(mWindow);
	addChild(mList.get());

	mSummary = std::make_shared<TextComponent>(mWindow, _("SCANNING..."),
		Font::get(FONT_SIZE_SMALL), 0x777777FF, ALIGN_CENTER);
	addChild(mSummary.get());

	loadDefinitions();

	setSize(Renderer::getScreenWidth() * 0.7f, Renderer::getScreenHeight() * 0.8f);
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2,
	            (Renderer::getScreenHeight() - mSize.y()) / 2);
}

void GuiBiosCheck::loadDefinitions()
{
	std::ifstream f(BIOS_DEFINITION_FILE);
	if (!f.is_open())
	{
		LOG(LogError) << "GuiBiosCheck: 정의 파일 없음 - " << BIOS_DEFINITION_FILE;
		mSummary->setText(_("BIOS DEFINITION FILE NOT FOUND"));
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
		mSummary->setText(_("BIOS DEFINITION FILE NOT FOUND"));
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
		mSummary->setText(_("BIOS DEFINITION FILE NOT FOUND"));
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
		return;
	}

	if (e.md5.empty())
	{
		e.status = BiosStatus::Ok;
		e.statusText = "FOUND";
		return;
	}

	const std::string hash = md5OfFile(full);
	if (hash.empty())
	{
		e.status = BiosStatus::Warning;
		e.statusText = "MD5 CHECK FAILED";
		return;
	}

	for (size_t i = 0; i < e.md5.size(); ++i)
	{
		if (e.md5[i] == hash)
		{
			e.status = BiosStatus::Ok;
			e.statusText = "OK";
			return;
		}
	}

	e.status = e.hashMandatory ? BiosStatus::Missing : BiosStatus::Warning;
	e.statusText = "MD5 MISMATCH";
}

void GuiBiosCheck::addResultRow(const BiosEntry& e)
{
	unsigned int color = COLOR_OK;
	if (e.status == BiosStatus::Warning) color = COLOR_WARN;
	else if (e.status == BiosStatus::Missing) color = COLOR_MISSING;

	std::string text = e.systemName + " · " + e.path + " — " + e.statusText;
	if (!e.note.empty())
		text += " (" + e.note + ")";

	ComponentListRow row;
	row.addElement(std::make_shared<TextComponent>(mWindow, text,
		Font::get(FONT_SIZE_SMALL), color), true);
	mList->addRow(row);
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
		mIndex++;
		return;
	}

	writeReport();

	mSummary->setText("OK " + std::to_string(mOkCount)
		+ " · WARNING " + std::to_string(mWarnCount)
		+ " · MISSING " + std::to_string(mMissingCount)
		+ "  (bios_report.txt)");
	mDone = true;
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

	const float summaryH = Font::get(FONT_SIZE_SMALL)->getLetterHeight() * 2.0f;
	mSummary->setSize(mSize.x() - padX * 2, 0);
	mSummary->setPosition(padX, mSize.y() - padY - summaryH);

	const float listTop = padY + mTitle->getSize().y() * 1.6f;
	mList->setPosition(padX, listTop);
	mList->setSize(mSize.x() - padX * 2, mSummary->getPosition().y() - listTop - padY);
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
