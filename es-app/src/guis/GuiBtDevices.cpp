#include "guis/GuiBtDevices.h"
#include "guis/GuiMsgBox.h"
#include "components/OptionListComponent.h"
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>

std::vector<GuiBtDevices::Device> GuiBtDevices::pairedDevices()
{
	std::vector<Device> result;

	FILE* p = popen("rpui-bt list 2>/dev/null", "r");
	if (!p) return result;

	char buf[256];
	while (fgets(buf, sizeof(buf), p)) {
		std::string line(buf);
		size_t mpos = line.find("\"mac\": \"");
		if (mpos == std::string::npos) continue;
		mpos += 8;
		size_t mend = line.find('"', mpos);
		if (mend == std::string::npos) continue;
		std::string mac = line.substr(mpos, mend - mpos);

		std::string name = mac;
		size_t npos = line.find("\"name\": \"", mend);
		if (npos != std::string::npos) {
			npos += 9;
			size_t nend = line.find('"', npos);
			if (nend != std::string::npos)
				name = line.substr(npos, nend - npos);
		}
		result.push_back({ mac, name });
	}
	pclose(p);
	return result;
}

// fork+execlp로 직접 실행 — 쉘을 거치지 않아 안전 (GuiWifiSelect와 동일 패턴)
static void removeDevice(const std::string& mac)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("rpui-bt", "rpui-bt", "remove", mac.c_str(), (char*)nullptr);
		_exit(127);
	} else if (pid > 0) {
		waitpid(pid, nullptr, 0);
	}
}

GuiBtDevices::GuiBtDevices(Window* window)
	: GuiSettings(window, "블루투스 기기")
{
	auto devices = pairedDevices();

	// 2026-07-11: 전부 selected=false였던 버그 - GuiStorageSelect와 동일한
	// 원인(OptionListComponent::getSelected()는 정확히 1개 selected를
	// 요구, 없으면 Back 시 크래시 → ES 재시작처럼 보임). 첫 항목을 항상
	// selected로 강제.
	auto list = std::make_shared<OptionListComponent<std::string>>(window, "페어링된 기기", false);
	bool first = true;
	for (const auto& d : devices) {
		list->add(d.name + " (" + d.mac + ")", d.mac, first);
		first = false;
	}
	if (devices.empty())
		list->add("페어링된 기기 없음", "", true);
	addWithLabel("기기", list);
	// 2026-07-12: GuiStorageSelect/GuiWifiSelect와 동일한 이유 - 뭔가는
	// 항상 selected가 되므로, 기본 선택값과 달라진 경우에만 연결 해제
	// 확인창을 띄움(아무것도 안 바꾸고 나가도 뜨던 문제 방지).
	std::string effectiveOrig = list->getSelected();

	Window* win = window;
	addSaveFunc([list, win, effectiveOrig]() {
		const std::string mac = list->getSelected();
		if (mac.empty() || mac == effectiveOrig) return;

		win->pushGui(new GuiMsgBox(win,
			"선택한 기기를 연결 해제하시겠습니까?\n" + mac,
			"예", [mac]() { removeDevice(mac); },
			"아니오", nullptr));
	});
}
