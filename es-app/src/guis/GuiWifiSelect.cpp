#include "LocaleES.h"
#include "guis/GuiWifiSelect.h"
#include "guis/GuiArcadeVirtualKeyboard.h"
#include "components/OptionListComponent.h"
#include "components/TextComponent.h"
#include "resources/Font.h"
#include <fstream>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

// status.json 단순 파싱 — 외부 JSON 라이브러리 없음 (GuiStorageSelect.cpp와 동일 패턴)
// 포맷은 rpui-wifi 데몬이 직접 기록하므로 구조가 고정됨
static std::string jsonVal(const std::string& line, const std::string& key)
{
	std::string needle = "\"" + key + "\": \"";
	size_t pos = line.find(needle);
	if (pos == std::string::npos) return {};
	pos += needle.size();
	size_t end = line.find('"', pos);
	return end == std::string::npos ? std::string() : line.substr(pos, end - pos);
}

// retropangui.conf 경로/읽기 — GuiMenu.cpp의 rpConfPath()/cfgReadKey()와 동일
// 로직을 간단히 중복(그쪽은 static이라 외부에서 못 부름).
static std::string wifiConfPath()
{
	const char* env = getenv("RETROPANGUI_SHARE");
	std::string base = (env && env[0] != '\0') ? env : "";
	if (base.empty()) {
		struct stat st;
		if (stat("/share", &st) == 0 && S_ISDIR(st.st_mode))
			base = "/share";
		else {
			const char* home = getenv("HOME");
			base = home ? std::string(home) + "/share" : "/share";
		}
	}
	return base + "/system/retropangui.conf";
}

static std::string wifiConfRead(const std::string& key)
{
	std::ifstream f(wifiConfPath());
	if (!f.is_open()) return {};
	std::string line;
	while (std::getline(f, line)) {
		if (line.empty() || line[0] == '#') continue;
		auto eq = line.find('=');
		if (eq == std::string::npos) continue;
		if (line.substr(0, eq) != key) continue;
		std::string v = line.substr(eq + 1);
		while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
		return v;
	}
	return {};
}

void GuiWifiSelect::readStatus(bool& connected, std::string& ssid, std::string& ip)
{
	connected = false;
	ssid.clear();
	ip.clear();

	std::ifstream f("/tmp/retropangui-wifi-status.json");
	if (!f.is_open()) return;

	std::string line;
	while (std::getline(f, line)) {
		if (line.find("\"connected\": true") != std::string::npos)
			connected = true;

		std::string v;
		if (!(v = jsonVal(line, "ssid")).empty()) ssid = v;
		if (!(v = jsonVal(line, "ip")).empty())   ip = v;
	}
}

std::vector<std::string> GuiWifiSelect::scanNetworks()
{
	std::vector<std::string> result;

	FILE* p = popen("rpui-wifi scanlist 2>/dev/null", "r");
	if (!p) return result;

	char buf[256];
	while (fgets(buf, sizeof(buf), p)) {
		std::string line(buf);
		size_t q1 = line.find('"');
		if (q1 == std::string::npos) continue;
		size_t q2 = line.find('"', q1 + 1);
		if (q2 == std::string::npos) continue;

		std::string val = line.substr(q1 + 1, q2 - q1 - 1);
		if (!val.empty() && val != "networks")
			result.push_back(val);
	}
	pclose(p);
	return result;
}

// argv 배열로 직접 실행(fork+execlp) — 쉘을 거치지 않아 비밀번호에 특수문자가 있어도 안전
void GuiWifiSelect::enableNetwork(const std::string& ssid, const std::string& psk)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("rpui-wifi", "rpui-wifi", "enable", ssid.c_str(), psk.c_str(), (char*)nullptr);
		_exit(127);
	} else if (pid > 0) {
		waitpid(pid, nullptr, 0);
	}
}

GuiWifiSelect::GuiWifiSelect(Window* window)
	: GuiSettings(window, "WIFI 설정")
{
	bool connected = false;
	std::string curSsid, curIp;
	readStatus(connected, curSsid, curIp);

	std::string statusLabel = connected
		? ("연결됨: " + curSsid + " (" + curIp + ")")
		: std::string("연결 안 됨");
	auto statusText = std::make_shared<TextComponent>(window, statusLabel,
		Font::get(FONT_SIZE_SMALL), 0x777777FF);
	addWithLabel("상태", statusText);

	// 2026-07-11: curSsid와 일치하는 항목이 없으면(아직 연결 전 등) 아무것도
	// selected=true가 안 돼서 GuiStorageSelect/GuiBtDevices와 동일한 원인으로
	// Back 시 크래시함 - 매칭 안 되면 첫 항목을 강제 선택.
	auto networks = scanNetworks();
	auto list = std::make_shared<OptionListComponent<std::string>>(window, "네트워크", false);
	bool curMatches = false;
	for (const auto& n : networks)
		if (n == curSsid) { curMatches = true; break; }
	for (size_t i = 0; i < networks.size(); i++)
		list->add(networks[i], networks[i], curMatches ? (networks[i] == curSsid) : (i == 0));
	if (networks.empty())
		list->add("검색된 네트워크 없음", "", true);
	addWithLabel(_("SSID"), list);
	// 방금 위에서 강제로 골라둔 "기본 선택값" - 사용자가 실제로 다른 걸
	// 고르지 않았다면 이 값과 같을 것이므로, 그 경우엔 연결 시도를
	// 하면 안 됨(아래 addSaveFunc 참고).
	std::string effectiveOrig = list->getSelected();

	// 2026-07-11: "메뉴 안에 메뉴" 느낌이 나던 것 - 화면에 들어오자마자
	// SSID 목록 팝업을 바로 띄워서 한 번 더 클릭 안 해도 되게 함.
	list->openPopup();

	Window* win = window;
	addSaveFunc([list, win, effectiveOrig] {
		const std::string ssid = list->getSelected();
		// 2026-07-11: 위의 크래시 수정으로 뭔가는 항상 selected가 되기
		// 때문에, 예전처럼 "선택 안 됨=ssid.empty()"로는 더 이상 "사용자가
		// 실제로 다른 네트워크를 고름"을 판별할 수 없음 - 기본값과 달라진
		// 경우에만 연결 시도(빈 화면 들어왔다 그냥 나가도 재연결 시도가
		// 걸리던 문제 방지).
		if (ssid.empty() || ssid == effectiveOrig)
			return;

		// 2026-07-11: NETWORK 화면(SSID/SSID PASSWORD 행)에서 미리 입력해둔
		// 비밀번호가 있으면 다시 안 물어보고 바로 그걸로 연결 시도.
		std::string savedPsk = wifiConfRead("system.wifi_password");
		if (!savedPsk.empty()) {
			GuiWifiSelect::enableNetwork(ssid, savedPsk);
			return;
		}

		win->pushGui(new GuiArcadeVirtualKeyboard(win, "WIFI 비밀번호", "",
			[ssid](const std::string& psk) {
				GuiWifiSelect::enableNetwork(ssid, psk);
			}));
	});
}
