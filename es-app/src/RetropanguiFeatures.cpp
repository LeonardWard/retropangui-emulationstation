#include "RetropanguiFeatures.h"

#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <sys/stat.h>

// YAML 들여쓰기 레벨 (공백 수 기준):
//  0  menus:
//  2    - id: <menu>           ← 새 FeatureMenu
//  4      label/parent/items:  ← FeatureMenu 필드
//  6      - id: <item>         ← 새 FeatureItem
//  8        label/type/...     ← FeatureItem 필드
//  10         - value: <v>     ← 새 FeatureOption
//  12           label: ...     ← FeatureOption 필드

static std::string getYmlPath()
{
	struct stat st;
	// RETROPANGUI_SHARE 환경 변수 → /share → ~/share 순서로 탐색
	std::string share;
	const char* env = getenv("RETROPANGUI_SHARE");
	if (env && env[0] != '\0')
		share = env;
	else if (stat("/share", &st) == 0 && S_ISDIR(st.st_mode))
		share = "/share";
	else {
		const char* home = getenv("HOME");
		share = home ? std::string(home) + "/share" : "/share";
	}
	std::string userPath = share + "/system/retropangui_features.yml";
	if (stat(userPath.c_str(), &st) == 0)
		return userPath;
	return "/usr/share/retropangui/retropangui_features.yml";
}

static int indentOf(const std::string& line)
{
	int n = 0;
	for (char c : line) {
		if (c == ' ') n++;
		else break;
	}
	return n;
}

static std::string trimVal(std::string s)
{
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
	while (!s.empty() &&  s.front() == '"') s.erase(s.begin());
	// 인라인 주석 제거: 따옴표 없는 값에서 ' #...' 부분 제거
	size_t hash = s.find(" #");
	if (hash != std::string::npos && (s.empty() || s.front() != '"'))
		s = s.substr(0, hash);
	while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t')) s.pop_back();
	while (!s.empty() &&  s.back()  == '"') s.pop_back();
	return s;
}

std::vector<FeatureMenu> RetropanguiFeatures::load()
{
	std::string path = getYmlPath();
	std::ifstream f(path);
	if (!f.is_open()) return {};

	std::vector<FeatureMenu> menus;
	FeatureMenu curMenu;
	FeatureItem curItem;
	FeatureOption curOpt;
	bool hasMenu = false, hasItem = false, hasOpt = false;

	auto commitOpt = [&]() {
		if (hasOpt) { curItem.options.push_back(curOpt); curOpt = {}; hasOpt = false; }
	};
	auto commitItem = [&]() {
		commitOpt();
		if (hasItem) { curMenu.items.push_back(curItem); curItem = {}; hasItem = false; }
	};
	auto commitMenu = [&]() {
		commitItem();
		if (hasMenu) { menus.push_back(curMenu); curMenu = {}; hasMenu = false; }
	};

	std::string line;
	while (std::getline(f, line))
	{
		// 공백만 있는 줄 / 주석 무시
		size_t s = 0;
		while (s < line.size() && line[s] == ' ') s++;
		if (s == line.size() || line[s] == '#') continue;

		int indent = indentOf(line);
		std::string trimmed = line.substr(s);

		bool hasDash = (trimmed.size() >= 2 && trimmed[0] == '-' && trimmed[1] == ' ');
		std::string content = hasDash ? trimmed.substr(2) : trimmed;

		size_t colon = content.find(':');
		if (colon == std::string::npos) continue;

		std::string key = content.substr(0, colon);
		while (!key.empty() && key.back() == ' ') key.pop_back();
		std::string val = (colon + 1 < content.size()) ? trimVal(content.substr(colon + 1)) : "";

		if (indent == 0) {
			// "menus:" — 최상위, 아무 처리 불필요
			continue;
		}

		if (indent == 2 && hasDash && key == "id") {
			commitMenu();
			curMenu.id = val;
			hasMenu = true;
			continue;
		}

		if (indent == 4) {
			if      (key == "label" ) curMenu.label  = val;
			else if (key == "parent") curMenu.parent = val;
			// "items:" → 다음 indent-6 항목들이 FeatureItem 으로 처리됨
			continue;
		}

		if (indent == 6 && hasDash && key == "id") {
			commitItem();
			curItem = FeatureItem();  // restart 기본값 "none" 포함
			curItem.id = val;
			hasItem = true;
			continue;
		}

		if (indent == 8) {
			if      (key == "label"   ) curItem.label       = val;
			else if (key == "type"    ) curItem.type        = val;
			else if (key == "conf_key") curItem.conf_key    = val;
			else if (key == "exec"    ) curItem.exec        = val;
			else if (key == "feedback_msg") curItem.feedback_msg = val;
			else if (key == "restart" ) curItem.restart     = val;
			else if (key == "default" ) curItem.default_val = val;
			else if (key == "unit"    ) curItem.unit        = val;
			else if (key == "min"  ) { try { curItem.min  = std::stof(val); } catch (...) {} }
			else if (key == "max"  ) { try { curItem.max  = std::stof(val); } catch (...) {} }
			else if (key == "step" ) { try { curItem.step = std::stof(val); } catch (...) {} }
			// "options:" → 다음 indent-10 항목들이 FeatureOption
			continue;
		}

		if (indent == 10 && hasDash && key == "value") {
			commitOpt();
			curOpt = {};
			curOpt.value = val;
			hasOpt = true;
			continue;
		}

		if (indent == 12) {
			if      (key == "value") curOpt.value = val;
			else if (key == "label") curOpt.label = val;
			continue;
		}
	}

	commitMenu();
	return menus;
}
