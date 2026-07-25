#pragma once
#ifndef ES_APP_GUIS_GUI_WIFI_SELECT_H
#define ES_APP_GUIS_GUI_WIFI_SELECT_H

#include "guis/GuiSettings.h"
#include "components/OptionListComponent.h"
#include <memory>
#include <string>
#include <vector>

// rpui-wifi 데몬 상태(status.json)/스캔 결과를 보여주고 SSID 선택 → 비밀번호 입력 → 연결
class GuiWifiSelect : public GuiSettings
{
public:
	GuiWifiSelect(Window* window);

	// RetroPangui: "화면 들어오자마자 SSID 목록 팝업 자동 표시" - 반드시
	// mWindow->pushGui(new GuiWifiSelect(...)) 호출 "직후"에 호출자가 불러야
	// 함(생성자 안에서 부르면 스택 순서 버그 - OptionListComponent.h의
	// openPopup() 주석 참고, 2026-07-25 실기기 크래시로 확정).
	void openInitialPopup();

	// 2026-07-11: NETWORK 화면(GuiMenu::openNetworkSettings)에서 SSID 행의
	// 기본값(현재 연결된 SSID 또는 "None")을 보여주기 위해 외부에서 호출.
	static void readStatus(bool& connected, std::string& ssid, std::string& ip);

private:
	static std::vector<std::string> scanNetworks();
	static void enableNetwork(const std::string& ssid, const std::string& psk);

	std::shared_ptr<OptionListComponent<std::string>> mSsidList;
};

#endif // ES_APP_GUIS_GUI_WIFI_SELECT_H
