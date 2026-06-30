#include "guis/GuiStorageSelect.h"
#include "guis/GuiMsgBox.h"
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

std::vector<GuiStorageSelect::DeviceInfo> GuiStorageSelect::readDevicesJson()
{
	std::vector<DeviceInfo> list;
	std::ifstream f("/tmp/retropangui-storage-devices.json");
	if (!f.is_open()) return list;

	DeviceInfo cur;
	bool inDevice = false;

	std::string line;
	while (std::getline(f, line)) {
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
	: GuiSettings(window, "저장장치 선택")
{
	auto devices = readDevicesJson();

	auto list = std::make_shared<OptionListComponent<std::string>>(window, "저장장치", false);

	for (const auto& d : devices) {
		std::string label = d.label;
		if (d.size_gb > 0)
			label += " [" + std::to_string(d.size_gb) + "GB]";
		label += " (" + d.dev + ")";
		list->add(label, d.id, false);
	}

	if (devices.empty())
		list->add("감지된 장치 없음", "", false);

	addWithLabel("파티션", list);

	addSaveFunc([list, window]() {
		const std::string& id = list->getSelected();
		if (id.empty()) return;

		// storage-mgr에 선택 명령 전달
		std::ofstream cmd("/tmp/retropangui-storage-cmd");
		cmd << "SELECT " << id << "\n";
		cmd.close();

		window->pushGui(new GuiMsgBox(window,
			"재부팅 후 새 저장장치로 share가 전환됩니다.\n지금 재부팅하시겠습니까?",
			"예",     []() { ::system("reboot"); },
			"아니오", nullptr));
	});
}
