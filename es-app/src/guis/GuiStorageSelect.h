#pragma once
#ifndef ES_APP_GUIS_GUI_STORAGE_SELECT_H
#define ES_APP_GUIS_GUI_STORAGE_SELECT_H

#include "guis/GuiSettings.h"
#include <string>
#include <vector>
#include <cstdint>

class GuiStorageSelect : public GuiSettings
{
public:
	struct StoragePartInfo {
		std::string devname;
		std::string uuid;
		std::string label;
		uint64_t    sizeBytes;
	};

	GuiStorageSelect(Window* window, const std::vector<StoragePartInfo>& parts);
};

#endif // ES_APP_GUIS_GUI_STORAGE_SELECT_H
