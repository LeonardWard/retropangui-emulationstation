#include "guis/GuiBtPairing.h"

#include "components/ComponentList.h"
#include "components/TextComponent.h"
#include "resources/Font.h"
#include "renderers/Renderer.h"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>

static const char* DISCOVERY_JSON_PATH = "/tmp/retropangui-bt-discovery.json";
static const char* PAIRING_STATUS_PATH = "/tmp/retropangui-bt-pairing-status";

// 손으로 짠 최소 JSON 값 추출 — 외부 JSON 라이브러리 없음 (GuiWifiSelect.cpp의
// jsonVal() 스타일을 "한 줄" 대신 "한 기기 레코드 조각"에 적용).
namespace {

std::string jsonStrVal(const std::string& chunk, const std::string& key)
{
	std::string needle = "\"" + key + "\": \"";
	size_t pos = chunk.find(needle);
	if (pos == std::string::npos) return {};
	pos += needle.size();
	size_t end = chunk.find('"', pos);
	return end == std::string::npos ? std::string() : chunk.substr(pos, end - pos);
}

bool jsonBoolVal(const std::string& chunk, const std::string& key)
{
	return chunk.find("\"" + key + "\": true") != std::string::npos;
}

// "rssi" 같은 정수 키 — 없으면 false 반환(= 정보 없음)
bool jsonIntVal(const std::string& chunk, const std::string& key, int& out)
{
	std::string needle = "\"" + key + "\": ";
	size_t pos = chunk.find(needle);
	if (pos == std::string::npos) return false;
	pos += needle.size();

	size_t end = pos;
	if (end < chunk.size() && chunk[end] == '-') end++;
	while (end < chunk.size() && std::isdigit(static_cast<unsigned char>(chunk[end]))) end++;
	if (end == pos || (chunk[pos] == '-' && end == pos + 1)) return false;

	out = std::atoi(chunk.substr(pos, end - pos).c_str());
	return true;
}

// 접두어 다음 문자열(범위 벗어나면 빈 문자열) — BT_PAIR_STATUS 한 줄 파싱용
std::string afterPrefix(const std::string& line, size_t prefixLen)
{
	return prefixLen <= line.size() ? line.substr(prefixLen) : std::string();
}

} // namespace

// fork+execlp로 직접 실행 — 쉘을 거치지 않아 안전 (GuiWifiSelect/GuiBtDevices와 동일 패턴).
// rpui-bt의 scan-*/pair 서브커맨드는 전부 명령만 CMD 파일에 기록하고 즉시 리턴하므로
// waitpid로 잠깐 기다려도 화면이 멈추지 않는다.
void GuiBtPairing::runRpuiBt(const std::string& verb, const std::string& arg)
{
	pid_t pid = fork();
	if (pid == 0) {
		if (arg.empty())
			execlp("rpui-bt", "rpui-bt", verb.c_str(), (char*)nullptr);
		else
			execlp("rpui-bt", "rpui-bt", verb.c_str(), arg.c_str(), (char*)nullptr);
		_exit(127);
	} else if (pid > 0) {
		waitpid(pid, nullptr, 0);
	}
}

GuiBtPairing::GuiBtPairing(Window* window, const std::string& iconFilter, const std::string& scanStartVerb)
	: GuiComponent(window), mIconFilter(iconFilter), mScanStartVerb(scanStartVerb),
	  mBackground(window, ":/frame.png"), mGrid(window, Vector2i(2, 2))
{
	auto font = Font::get(FONT_SIZE_SMALL);
	const unsigned int textColor = 0x777777FF;

	// 상단 상태 텍스트 (탐색 중.../연결됨.../실패 등) — 헤더 행, 두 컬럼에 걸침
	mHeaderStatus = std::make_shared<TextComponent>(mWindow, "탐색 중...", Font::get(FONT_SIZE_MEDIUM), textColor, ALIGN_CENTER);
	mGrid.setEntry(mHeaderStatus, Vector2i(0, 0), false, false, Vector2i(2, 1), GridFlags::BORDER_BOTTOM);

	// 좌측: 실시간 발견 목록
	mList = std::make_shared<ComponentList>(mWindow);
	mList->setCursorChangedCallback([this](CursorState state) {
		if (state == CURSOR_STOPPED) updateDetailPane();
	});
	mGrid.setEntry(mList, Vector2i(0, 1), true, true, Vector2i(1, 1), GridFlags::BORDER_RIGHT);

	// 우측: 선택된 기기의 상세 정보
	auto detailGrid = std::make_shared<ComponentGrid>(mWindow, Vector2i(1, 5));
	mDetailMac     = std::make_shared<TextComponent>(mWindow, "MAC: -", font, textColor);
	mDetailRssi    = std::make_shared<TextComponent>(mWindow, "신호 세기: -", font, textColor);
	mDetailVendor  = std::make_shared<TextComponent>(mWindow, "제조사: -", font, textColor);
	mDetailBattery = std::make_shared<TextComponent>(mWindow, "배터리: 정보 없음", font, textColor);
	mDetailStatus  = std::make_shared<TextComponent>(mWindow, "상태: -", font, textColor);
	detailGrid->setEntry(mDetailMac,     Vector2i(0, 0), false, true);
	detailGrid->setEntry(mDetailRssi,    Vector2i(0, 1), false, true);
	detailGrid->setEntry(mDetailVendor,  Vector2i(0, 2), false, true);
	detailGrid->setEntry(mDetailBattery, Vector2i(0, 3), false, true);
	detailGrid->setEntry(mDetailStatus,  Vector2i(0, 4), false, true);
	mGrid.setEntry(detailGrid, Vector2i(1, 1), false, false, Vector2i(1, 1));

	addChild(&mBackground);
	addChild(&mGrid);

	setSize(Renderer::getScreenWidth() * 0.75f, Renderer::getScreenHeight() * 0.75f);
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2.0f, (Renderer::getScreenHeight() - mSize.y()) / 2.0f);

	// 화면이 뜨자마자 스캔 시작 + 초기 상태 한 번 표시
	runRpuiBt(mScanStartVerb);
	pollDiscoveryList();
	pollPairingStatus();
}

GuiBtPairing::~GuiBtPairing()
{
	// 화면이 사라지면 데몬의 스캔 세션도 반드시 종료
	runRpuiBt("scan-stop");
}

void GuiBtPairing::onSizeChanged()
{
	mGrid.setSize(mSize);
	if (mSize.x() == 0 || mSize.y() == 0) return;

	mGrid.setColWidthPerc(0, 0.55f);
	mGrid.setRowHeightPerc(0, 0.1f);
	mGrid.onSizeChanged();

	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));
}

bool GuiBtPairing::input(InputConfig* config, Input input)
{
	if (config->isMappedToAction("back", input) && input.value != 0) {
		delete this;
		return true;
	}
	return GuiComponent::input(config, input);
}

void GuiBtPairing::update(int deltaTime)
{
	mPollAccum += (float)deltaTime;
	if (mPollAccum >= 700.f) {
		mPollAccum = 0.f;
		pollDiscoveryList();
		pollPairingStatus();
	}
	GuiComponent::update(deltaTime);
}

std::vector<HelpPrompt> GuiBtPairing::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt("up/down", "choose"));
	prompts.push_back(HelpPrompt("a", "select"));
	prompts.push_back(HelpPrompt("b", "back"));
	return prompts;
}

void GuiBtPairing::pollDiscoveryList()
{
	// 재구성 전에 현재 선택된 mac을 기억해서 커서 위치를 최대한 유지
	mKeepSelectedMac.clear();
	if (mList && mList->size() > 0) {
		int idx = mList->getCursorId();
		if (idx >= 0 && idx < (int)mDevices.size())
			mKeepSelectedMac = mDevices[idx].mac;
	}

	std::vector<DiscoveredDevice> devices;

	std::ifstream f(DISCOVERY_JSON_PATH);
	if (f.is_open()) {
		std::stringstream ss;
		ss << f.rdbuf();
		std::string buf = ss.str();

		// "mac": " 문자열이 나오는 지점마다 새 기기 레코드로 간주하고 그 사이를 잘라
		// 각 조각에서 키를 검색(파일이 한 줄에 다 있거나 여러 줄에 걸쳐 있어도 동작).
		std::vector<size_t> starts;
		size_t pos = 0;
		while ((pos = buf.find("\"mac\": \"", pos)) != std::string::npos) {
			starts.push_back(pos);
			pos += 1;
		}

		for (size_t i = 0; i < starts.size(); i++) {
			size_t begin = starts[i];
			size_t end = (i + 1 < starts.size()) ? starts[i + 1] : buf.size();
			std::string chunk = buf.substr(begin, end - begin);

			DiscoveredDevice d;
			d.mac = jsonStrVal(chunk, "mac");
			if (d.mac.empty()) continue;

			d.name = jsonStrVal(chunk, "name");
			d.icon = jsonStrVal(chunk, "icon");
			d.vendor = jsonStrVal(chunk, "vendor");
			d.looksLikePad = jsonBoolVal(chunk, "looks_like_pad");
			d.paired = jsonBoolVal(chunk, "paired");
			d.connected = jsonBoolVal(chunk, "connected");

			int rssi = 0;
			d.hasRssi = jsonIntVal(chunk, "rssi", rssi);
			d.rssi = rssi;

			devices.push_back(d);
		}
	}

	mDevices = devices;
	rebuildList();
	autoConnectIfPadFound();
}

void GuiBtPairing::pollPairingStatus()
{
	std::string line;
	std::ifstream f(PAIRING_STATUS_PATH);
	if (f.is_open())
		std::getline(f, line);

	std::string text;
	if (line.empty() || line == "SCANNING")
		text = "탐색 중...";
	else if (line == "TIMEOUT")
		text = "기기를 찾지 못했습니다";
	else if (line == "STOPPED")
		text = "탐색 종료됨";
	else if (line.rfind("CONNECTED", 0) == 0)
		text = "연결됨: " + afterPrefix(line, 10);
	else if (line.rfind("TRUSTING", 0) == 0)
		text = "신뢰 설정 중: " + afterPrefix(line, 9);
	else if (line.rfind("PAIRING", 0) == 0)
		text = "페어링 중: " + afterPrefix(line, 8);
	else if (line.rfind("CONNECTING", 0) == 0)
		text = "연결 중: " + afterPrefix(line, 11);
	else
		text = line; // "실패: ..." 등은 이미 한글이므로 그대로 표시

	if (mHeaderStatus)
		mHeaderStatus->setText(text);
}

void GuiBtPairing::rebuildList()
{
	if (!mList) return;

	mList->clear();

	if (mDevices.empty()) {
		auto placeholder = std::make_shared<TextComponent>(mWindow, "검색된 기기 없음", Font::get(FONT_SIZE_SMALL), 0x999999FF);
		ComponentListRow row;
		row.addElement(placeholder, true);
		mList->addRow(row, true);
		updateDetailPane();
		return;
	}

	for (size_t i = 0; i < mDevices.size(); i++) {
		const DiscoveredDevice& d = mDevices[i];

		std::string label;
		if (d.connected)     label = "[연결됨] ";
		else if (d.paired)   label = "[등록됨] ";
		label += d.name.empty() ? d.mac : d.name;

		auto text = std::make_shared<TextComponent>(mWindow, label, Font::get(FONT_SIZE_SMALL), 0x777777FF);

		ComponentListRow row;
		row.addElement(text, true);
		std::string mac = d.mac; // 람다 캡처용 복사
		row.makeAcceptInputHandler([this, mac] { selectDevice(mac); });

		bool setCursorHere = (!mKeepSelectedMac.empty() && mac == mKeepSelectedMac);
		mList->addRow(row, setCursorHere);
	}

	updateDetailPane();
}

void GuiBtPairing::updateDetailPane()
{
	if (!mDetailMac || !mDetailRssi || !mDetailVendor || !mDetailBattery || !mDetailStatus)
		return;

	if (mDevices.empty() || !mList || mList->size() == 0) {
		mDetailMac->setText("MAC: -");
		mDetailRssi->setText("신호 세기: -");
		mDetailVendor->setText("제조사: -");
		mDetailBattery->setText("배터리: 정보 없음");
		mDetailStatus->setText("상태: -");
		return;
	}

	int idx = mList->getCursorId();
	if (idx < 0 || idx >= (int)mDevices.size()) return;
	const DiscoveredDevice& d = mDevices[idx];

	mDetailMac->setText("MAC: " + d.mac);
	mDetailRssi->setText(d.hasRssi ? ("신호 세기: " + std::to_string(d.rssi) + " dBm") : "신호 세기: 정보 없음");
	mDetailVendor->setText(d.vendor.empty() ? "제조사: 제조사 불명" : ("제조사: " + d.vendor));
	// Battery1 D-Bus 인터페이스 지원 기기가 나오면 추후 rpui-bt 데몬에 필드 추가 필요 (현재 범위 밖) — 항상 "정보 없음"
	mDetailBattery->setText("배터리: 정보 없음");
	mDetailStatus->setText(d.connected ? "상태: 연결됨" : (d.paired ? "상태: 등록됨(연결 안 됨)" : "상태: 미등록"));
}

void GuiBtPairing::selectDevice(const std::string& mac)
{
	runRpuiBt("pair", mac);
}

void GuiBtPairing::autoConnectIfPadFound()
{
	if (mAutoConnectFired) return;

	for (const DiscoveredDevice& d : mDevices) {
		if (d.looksLikePad && !d.paired && !d.connected) {
			mAutoConnectFired = true;
			selectDevice(d.mac);
			return;
		}
	}
}
