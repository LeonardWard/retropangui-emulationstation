#include "guis/GuiStorageSelect.h"
#include "guis/GuiMsgBox.h"
#include "LocaleES.h"
#include "components/OptionListComponent.h"
#include <fstream>
#include <sstream>

// devices.json 단순 파싱 — 외부 JSON 라이브러리 없음
// 포맷은 storage-mgr가 직접 기록하므로 구조가 고정됨
static std::string jsonVal(const std::string& line, const std::string& key)
{
	std::string needle = "\"" + key + "\": \"";
	size_t pos = line.find(needle);
	if (pos == std::string::npos) return {};
	pos += needle.size();
	size_t end = line.find('"', pos);
	return end == std::string::npos ? std::string() : line.substr(pos, end - pos);
}

static int jsonIntVal(const std::string& line, const std::string& key)
{
	std::string needle = "\"" + key + "\": ";
	size_t pos = line.find(needle);
	if (pos == std::string::npos) return 0;
	return std::stoi(line.substr(pos + needle.size()));
}

std::vector<GuiStorageSelect::DeviceInfo> GuiStorageSelect::readDevicesJson(std::string& outCurrent)
{
	std::vector<DeviceInfo> list;
	std::ifstream f("/tmp/retropangui-storage-devices.json");
	if (!f.is_open()) return list;

	DeviceInfo cur;
	bool inDevice = false;

	std::string line;
	while (std::getline(f, line)) {
		if (outCurrent.empty())
			outCurrent = jsonVal(line, "current");
		// 객체 시작
		if (line.find('{') != std::string::npos && line.find("devices") == std::string::npos
		    && line.find("current") == std::string::npos)
		{
			inDevice = true;
			cur = {};
			continue;
		}
		// 객체 끝
		if (inDevice && line.find('}') != std::string::npos) {
			if (!cur.id.empty())
				list.push_back(cur);
			inDevice = false;
			continue;
		}
		if (!inDevice) continue;

		if (cur.id.empty())       cur.id      = jsonVal(line, "id");
		if (cur.label.empty())    cur.label   = jsonVal(line, "label");
		if (cur.dev.empty())      cur.dev     = jsonVal(line, "dev");
		if (cur.size_gb == 0)     cur.size_gb = jsonIntVal(line, "size_gb");
		if (cur.type.empty())     cur.type    = jsonVal(line, "type");
		if (cur.uuid.empty())     cur.uuid    = jsonVal(line, "uuid");
	}
	return list;
}

GuiStorageSelect::GuiStorageSelect(Window* window)
	: GuiSettings(window, _("SELECT STORAGE DEVICE"))
{
	std::string current;
	auto devices = readDevicesJson(current);

	auto list = std::make_shared<OptionListComponent<std::string>>(window, _("STORAGE DEVICE"), false);

	// 2026-07-11: 전부 selected=false로 추가하던 버그 - 단일 선택
	// OptionListComponent::getSelected()는 정확히 1개가 selected여야
	// 하는데(assert(selected.size()==1) 이후 .at(0)), 아무것도 선택
	// 안 되면 빈 벡터에 .at(0) 호출로 예외 발생 → ES가 죽고 부팅
	// 루프가 재시작시켜서 "메뉴 나가면 ES가 재시작되는" 것처럼 보였음
	// (목록에 체크 표시가 하나도 안 보였던 것도 동일 원인). devices.json의
	// "current"와 일치하는 항목을 selected로, 못 찾으면 첫 항목을
	// 강제로 selected 처리해서 항상 정확히 하나는 선택되게 함.
	bool currentMatches = false;
	for (const auto& d : devices)
		if (!current.empty() && d.id == current) { currentMatches = true; break; }

	for (size_t i = 0; i < devices.size(); i++) {
		const auto& d = devices[i];
		std::string label = d.label;
		if (d.size_gb > 0)
			label += " [" + std::to_string(d.size_gb) + "GB]";
		label += " (" + d.dev + ")";
		bool sel = currentMatches ? (d.id == current) : (i == 0);
		list->add(label, d.id, sel);
	}

	if (devices.empty())
		list->add(_("No devices detected"), "", true);

	addWithLabel(_("PARTITION"), list);
	// 2026-07-12: 위 크래시 수정으로 뭔가는 항상 selected가 되기 때문에,
	// GuiWifiSelect와 동일한 이유로 "선택 안 됨=id.empty()"만으로는 더 이상
	// "사용자가 실제로 다른 장치를 고름"을 판별할 수 없음 - 화면에 처음
	// 표시된 기본 선택값과 달라진 경우에만 재부팅 확인창을 띄움(아무것도
	// 안 바꾸고 그냥 나가도 재부팅 창이 뜨던 문제 방지).
	std::string effectiveOrig = list->getSelected();

	addSaveFunc([list, window, effectiveOrig]() {
		const std::string& id = list->getSelected();
		if (id.empty() || id == effectiveOrig) return;

		// storage-mgr에 선택 명령 전달
		std::ofstream cmd("/tmp/retropangui-storage-cmd");
		cmd << "SELECT " << id << "\n";
		cmd.close();

		window->pushGui(new GuiMsgBox(window,
			_("Share will switch to the new storage device after reboot.\nReboot now?"),
			_("YES"), []() { ::system("reboot"); },
			_("NO"),  nullptr));
	});
}
