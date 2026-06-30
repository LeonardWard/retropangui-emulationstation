#pragma once
#ifndef ES_APP_GUIS_GUI_STORAGE_SELECT_H
#define ES_APP_GUIS_GUI_STORAGE_SELECT_H

#include "guis/GuiSettings.h"
#include <string>
#include <vector>

class GuiStorageSelect : public GuiSettings
{
public:
	struct DeviceInfo {
		std::string id;       // "INTERNAL" | "DEV UUID=xxxx"
		std::string label;
		std::string dev;
		int         size_gb;
		std::string type;
		std::string uuid;
	};

	GuiStorageSelect(Window* window);

private:
	static std::vector<DeviceInfo> readDevicesJson();
};

#endif // ES_APP_GUIS_GUI_STORAGE_SELECT_H
