#include "guis/GuiStorageSelect.h"
#include "guis/GuiMsgBox.h"
#include "components/OptionListComponent.h"
#include <fstream>
#include <unistd.h>

static void writeBootConf(const std::string& uuid)
{
	const std::string confPath = "/boot/retropangui-boot.conf";
	std::vector<std::string> lines;
	{
		std::ifstream fin(confPath);
		std::string line;
		bool found = false;
		while (std::getline(fin, line)) {
			if (line.substr(0, 12) == "sharedevice=") {
				lines.push_back("sharedevice=DEV UUID=" + uuid);
				found = true;
			} else {
				lines.push_back(line);
			}
		}
		if (!found)
			lines.push_back("sharedevice=DEV UUID=" + uuid);
	}
	{
		std::ofstream fout(confPath);
		for (auto& l : lines) fout << l << "\n";
	}
	::sync();
}

GuiStorageSelect::GuiStorageSelect(Window* window, const std::vector<StoragePartInfo>& parts)
	: GuiSettings(window, "저장장치 선택")
{
	auto list = std::make_shared<OptionListComponent<std::string>>(window, "파티션 선택", false);
	for (const auto& p : parts) {
		uint64_t gb  = p.sizeBytes / (1024ULL * 1024 * 1024);
		uint64_t mb  = (p.sizeBytes / (1024ULL * 1024)) % 1024;
		std::string sizeStr = gb > 0
			? (std::to_string(gb) + "." + std::to_string(mb / 100) + "GB")
			: (std::to_string(p.sizeBytes / (1024ULL * 1024)) + "MB");
		std::string label = p.label + " [" + sizeStr + "] (" + p.devname + ")";
		list->add(label, p.uuid, false);
	}
	addWithLabel("파티션", list);

	addSaveFunc([list, window]() {
		std::string uuid = list->getSelected();
		if (uuid.empty()) return;

		writeBootConf(uuid);
		::remove("/tmp/retropangui-new-storage");

		window->pushGui(new GuiMsgBox(window,
			"재부팅 후 새 저장장치로 share가 전환됩니다.\n지금 재부팅하시겠습니까?",
			"예",  []() { ::system("reboot"); },
			"아니오", nullptr));
	});
}
