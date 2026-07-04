#pragma once
#ifndef ES_APP_GUIS_GUI_BT_DEVICES_H
#define ES_APP_GUIS_GUI_BT_DEVICES_H

#include "guis/GuiSettings.h"
#include <string>
#include <vector>

// rpui-bt로 페어링된 블루투스 기기 목록을 보여주고 선택 시 연결 해제(remove)
class GuiBtDevices : public GuiSettings
{
public:
	GuiBtDevices(Window* window);

private:
	struct Device { std::string mac, name; };
	static std::vector<Device> pairedDevices();
};

#endif // ES_APP_GUIS_GUI_BT_DEVICES_H
