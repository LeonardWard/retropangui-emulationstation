#pragma once
#ifndef ES_APP_GUIS_GUI_BT_PAIRING_H
#define ES_APP_GUIS_GUI_BT_PAIRING_H

#include "components/ComponentGrid.h"
#include "components/NinePatchComponent.h"
#include "GuiComponent.h"
#include <string>
#include <vector>

class ComponentList;
class TextComponent;
class AnimatedImageComponent;

// 실시간 블루투스 검색/선택 화면 — 좌측 발견된 기기 목록, 우측 선택된 기기의
// 상세 정보(MAC/신호세기/제조사/배터리/상태)를 보여준다.
// rpui-bt 데몬이 기록하는 /tmp/retropangui-bt-discovery.json,
// /tmp/retropangui-bt-pairing-status 파일을 폴링해서 갱신한다.
class GuiBtPairing : public GuiComponent
{
public:
	// iconFilter: "input-gaming" 또는 "audio-" — 필터 정보 보관용(daemon이 이미 필터링)
	// scanStartVerb: "scan-start-pad" 또는 "scan-start-audio" — 생성 시 실행할 rpui-bt 서브커맨드
	GuiBtPairing(Window* window, const std::string& iconFilter, const std::string& scanStartVerb);
	virtual ~GuiBtPairing();

	bool input(InputConfig* config, Input input) override;
	void update(int deltaTime) override;
	void render(const Transform4x4f& parentTrans) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	void onSizeChanged() override;

private:
	struct DiscoveredDevice {
		std::string mac, name, icon, vendor;
		bool looksLikePad = false, hasRssi = false, paired = false, connected = false;
		int rssi = 0;
	};

	static void runRpuiBt(const std::string& verb, const std::string& arg = "");

	void pollDiscoveryList();   // /tmp/retropangui-bt-discovery.json 읽어서 mDevices 갱신
	void pollPairingStatus();   // /tmp/retropangui-bt-pairing-status 읽어서 상단 상태 텍스트 갱신
	void rebuildList();         // mDevices -> mList 행 재구성(선택된 mac 유지 시도)
	void updateDetailPane();    // 현재 mList 커서의 기기 정보를 우측 패널에 반영
	void selectDevice(const std::string& mac); // 사용자가 Enter로 수동 선택
	void autoConnectIfPadFound(); // mDevices 중 looksLikePad && !paired && !connected 인 게 나타나면 한 번만 자동 호출

	std::string mIconFilter;
	std::string mScanStartVerb;
	std::vector<DiscoveredDevice> mDevices;
	bool mAutoConnectFired = false;
	float mPollAccum = 0.f;
	std::string mKeepSelectedMac; // rebuildList() 호출 전에 pollDiscoveryList()가 채워서 커서 유지에 사용

	bool mScanning = true; // 스캔 세션 진행 중일 때만 스피너 회전

	// 푸터 상태 텍스트 마퀴(가로 스크롤) — TextListComponent와 동일한 방식
	float mMarqueeOffset = 0.f;
	float mMarqueeTime = 0.f;
	float mFooterTextBaseX = 0.f, mFooterTextBaseY = 0.f, mFooterTextWidth = 0.f;

	NinePatchComponent mBackground;
	ComponentGrid mGrid;
	std::shared_ptr<ComponentList> mList;
	std::shared_ptr<TextComponent> mTitleText;    // 상단 헤더 — 창 제목(고정)
	std::shared_ptr<TextComponent> mFooterStatus; // 하단 푸터 — "탐색 중..." 등 실시간 상태(마퀴)
	std::shared_ptr<TextComponent> mDetailMac, mDetailRssi, mDetailVendor, mDetailBattery, mDetailStatus;
	std::shared_ptr<AnimatedImageComponent> mSpinner; // 하단 푸터 좌측 회전 스캔 표시
};

#endif // ES_APP_GUIS_GUI_BT_PAIRING_H
