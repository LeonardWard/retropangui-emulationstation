#include "guis/GuiWifiSelect.h"
#include "guis/GuiArcadeVirtualKeyboard.h"
#include "components/OptionListComponent.h"
#include "components/TextComponent.h"
#include "resources/Font.h"
#include <fstream>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>

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

	auto networks = scanNetworks();
	auto list = std::make_shared<OptionListComponent<std::string>>(window, "네트워크", false);
	for (const auto& n : networks)
		list->add(n, n, n == curSsid);
	if (networks.empty())
		list->add("검색된 네트워크 없음", "", false);
	addWithLabel("SSID", list);

	Window* win = window;
	addSaveFunc([list, win]() {
		const std::string ssid = list->getSelected();
		if (ssid.empty()) return;

		win->pushGui(new GuiArcadeVirtualKeyboard(win, "WIFI 비밀번호", "",
			[ssid](const std::string& psk) {
				GuiWifiSelect::enableNetwork(ssid, psk);
			}));
	});
}
