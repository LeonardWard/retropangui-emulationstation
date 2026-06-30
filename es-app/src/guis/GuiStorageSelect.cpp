#include "guis/GuiStorageSelect.h"
#include "guis/GuiMsgBox.h"
#include "components/OptionListComponent.h"
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>
#include <cctype>

static void writeBootConf(const std::string& uuid)
{
	const std::string confPath = "/boot/retropangui-boot.conf";
	// uuid가 비어있으면 INTERNAL, 아니면 DEV UUID=<uuid>
	std::string value = uuid.empty() ? "INTERNAL" : ("DEV UUID=" + uuid);
	std::vector<std::string> lines;
	{
		std::ifstream fin(confPath);
		std::string line;
		bool found = false;
		while (std::getline(fin, line)) {
			if (line.substr(0, 12) == "sharedevice=") {
				lines.push_back("sharedevice=" + value);
				found = true;
			} else {
				lines.push_back(line);
			}
		}
		if (!found)
			lines.push_back("sharedevice=" + value);
	}
	{
		std::ofstream fout(confPath);
		for (auto& l : lines) fout << l << "\n";
	}
	::sync();
}

// 내장 eMMC share 파티션(p3) 정보 반환. 없으면 빈 항목.
static GuiStorageSelect::StoragePartInfo collectInternalPart()
{
	GuiStorageSelect::StoragePartInfo info;

	// 부트 디바이스 확인 (예: mmcblk0)
	std::string bootDev;
	{
		std::ifstream mounts("/proc/mounts");
		std::string line;
		while (std::getline(mounts, line)) {
			std::istringstream iss(line);
			std::string dev, mnt;
			iss >> dev >> mnt;
			if (mnt == "/boot" && dev.find("/dev/") == 0) {
				bootDev = dev;
				// p1 → mmcblk0
				while (!bootDev.empty() && (isdigit((unsigned char)bootDev.back()) || bootDev.back() == 'p'))
					bootDev.pop_back();
				break;
			}
		}
	}
	if (bootDev.empty()) return info;

	std::string devName = bootDev.substr(5); // "/dev/" 제거
	// p3 파티션 찾기 (p1, p2 제외)
	std::ifstream pp("/proc/partitions");
	std::string line;
	std::getline(pp, line); std::getline(pp, line);
	std::string partName;
	while (std::getline(pp, line)) {
		std::istringstream iss(line);
		unsigned int major, minor; uint64_t blocks; std::string name;
		iss >> major >> minor >> blocks >> name;
		if (name.substr(0, devName.size()) != devName) continue;
		if (name == devName) continue;
		std::string suffix = name.substr(devName.size());
		if (suffix == "p1" || suffix == "p2" || suffix == "boot0" || suffix == "boot1" || suffix == "rpmb") continue;
		// 첫 번째 data 파티션 (p3)
		info.devname   = name;
		info.uuid      = ""; // 빈 UUID = INTERNAL 센티널
		info.sizeBytes = blocks * 1024ULL;
		info.label     = "INTERNAL";
		break;
	}
	return info;
}

std::vector<GuiStorageSelect::StoragePartInfo> GuiStorageSelect::collectExternalParts()
{
	std::vector<StoragePartInfo> parts;

	// 신호 파일에서 파티션명 읽기. 없으면 모든 외부 파티션 대상.
	std::set<std::string> deviceNames;
	{
		std::ifstream fin("/tmp/retropangui-new-storage");
		if (fin.is_open()) {
			std::string part;
			while (std::getline(fin, part)) {
				if (part.empty()) continue;
				std::string dev = part;
				while (!dev.empty() && isdigit((unsigned char)dev.back())) dev.pop_back();
				if (!dev.empty()) deviceNames.insert(dev);
			}
		}
		// 신호 파일 없으면 /proc/partitions에서 외부 장치 전체 스캔
		if (deviceNames.empty()) {
			// 부트 디바이스 결정 (/boot → / 순으로 fallback)
			std::string bootDev;
			{
				std::ifstream mf("/proc/mounts");
				std::string ml;
				while (std::getline(mf, ml)) {
					std::istringstream iss(ml);
					std::string dev, mp;
					iss >> dev >> mp;
					if ((mp == "/boot" || (bootDev.empty() && mp == "/")) &&
					    dev.substr(0, 5) == "/dev/") {
						bootDev = dev.substr(5); // "/dev/mmcblk1p1" → "mmcblk1p1"
						// 파티션 번호 제거: mmcblk1p1 → mmcblk1, sda1 → sda
						while (!bootDev.empty() && isdigit((unsigned char)bootDev.back()))
							bootDev.pop_back();
						if (!bootDev.empty() && bootDev.back() == 'p')
							bootDev.pop_back();
						if (mp == "/boot") break; // /boot 찾으면 확정
					}
				}
				if (bootDev.empty()) bootDev = "mmcblk0"; // 판단 불가 fallback
			}

			std::ifstream pp("/proc/partitions");
			std::string line;
			std::getline(pp, line); std::getline(pp, line); // 헤더 2줄 건너뜀
			while (std::getline(pp, line)) {
				std::istringstream iss(line);
				unsigned int major, minor; uint64_t blocks; std::string name;
				iss >> major >> minor >> blocks >> name;
				if (name.empty()) continue;
				if (name.substr(0, bootDev.size()) == bootDev) continue;
				if (name.substr(0,4) == "loop") continue;
				if (name.substr(0,3) == "ram") continue;
				// 숫자로 끝나지 않으면 디스크(파티션 아님)
				if (!isdigit((unsigned char)name.back())) {
					deviceNames.insert(name);
				}
			}
		}
	}

	// UUID 맵: 파티션명 → UUID
	std::map<std::string, std::string> uuidMap;
	DIR* dir = opendir("/dev/disk/by-uuid");
	if (dir) {
		struct dirent* ent;
		while ((ent = readdir(dir)) != nullptr) {
			if (ent->d_name[0] == '.') continue;
			std::string lp = std::string("/dev/disk/by-uuid/") + ent->d_name;
			char tgt[256]; ssize_t len = readlink(lp.c_str(), tgt, sizeof(tgt)-1);
			if (len > 0) {
				tgt[len] = '\0';
				std::string t(tgt);
				size_t sl = t.rfind('/');
				if (sl != std::string::npos) t = t.substr(sl + 1);
				uuidMap[t] = ent->d_name;
			}
		}
		closedir(dir);
	}

	// /proc/partitions에서 해당 디바이스의 파티션 열거
	{
		std::ifstream procParts("/proc/partitions");
		std::string line;
		std::getline(procParts, line); std::getline(procParts, line);
		while (std::getline(procParts, line)) {
			std::istringstream iss(line);
			unsigned int major, minor; uint64_t blocks; std::string name;
			iss >> major >> minor >> blocks >> name;
			if (name.empty()) continue;
			std::string parent = name;
			while (!parent.empty() && isdigit((unsigned char)parent.back())) parent.pop_back();
			if (deviceNames.find(parent) == deviceNames.end() || name == parent) continue;

			StoragePartInfo info;
			info.devname   = name;
			info.uuid      = uuidMap.count(name) ? uuidMap[name] : "";
			info.sizeBytes = blocks * 1024ULL;

			std::string cmd = "blkid -o value -s LABEL /dev/" + name + " 2>/dev/null";
			FILE* fp = popen(cmd.c_str(), "r");
			if (fp) {
				char buf[64] = {};
				if (fgets(buf, sizeof(buf), fp)) {
					info.label = buf;
					if (!info.label.empty() && info.label.back() == '\n') info.label.pop_back();
				}
				pclose(fp);
			}
			if (info.label.empty()) info.label = name;
			parts.push_back(info);
		}
	}

	return parts;
}

GuiStorageSelect::GuiStorageSelect(Window* window)
	: GuiStorageSelect(window, collectExternalParts())
{}

GuiStorageSelect::GuiStorageSelect(Window* window, const std::vector<StoragePartInfo>& parts)
	: GuiSettings(window, "저장장치 선택")
{
	auto list = std::make_shared<OptionListComponent<std::string>>(window, "파티션 선택", false);

	// 내장 파티션을 맨 앞에 추가
	StoragePartInfo internalPart = collectInternalPart();
	if (!internalPart.devname.empty()) {
		uint64_t gb = internalPart.sizeBytes / (1024ULL * 1024 * 1024);
		uint64_t mb = (internalPart.sizeBytes / (1024ULL * 1024)) % 1024;
		std::string sizeStr = gb > 0
			? (std::to_string(gb) + "." + std::to_string(mb / 100) + "GB")
			: (std::to_string(internalPart.sizeBytes / (1024ULL * 1024)) + "MB");
		std::string label = "INTERNAL [" + sizeStr + "] (" + internalPart.devname + ")";
		list->add(label, internalPart.uuid, false); // uuid="" → INTERNAL
	}

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
