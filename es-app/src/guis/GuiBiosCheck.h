#pragma once
#ifndef ES_APP_GUIS_GUI_BIOS_CHECK_H
#define ES_APP_GUIS_GUI_BIOS_CHECK_H

#include "GuiComponent.h"
#include "components/NinePatchComponent.h"

#include <memory>
#include <string>
#include <vector>

class ComponentList;
class TextComponent;
class Window;

// RetroPangUI: 바이오스 체크 화면.
// /usr/share/retropangui/bios-check.json 정의를 share/bios/ 실물과 대조(md5 포함)해
// 시스템별 상태를 색상 행으로 보여준다. UI를 살려두기 위해 GuiGamelistRefresh처럼
// update()에서 프레임당 파일 1개씩 동기 처리한다(별도 스레드 금지가 프로젝트 관례).
class GuiBiosCheck : public GuiComponent
{
public:
	GuiBiosCheck(Window* window);

	bool input(InputConfig* config, Input input) override;
	void update(int deltaTime) override;
	void onSizeChanged() override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	enum class BiosStatus { Ok, Warning, Missing };

	struct BiosEntry
	{
		std::string system;      // JSON 키 (예: "psx")
		std::string systemName;  // JSON "name"
		std::string path;        // share/bios/ 기준 상대 경로
		std::vector<std::string> md5;
		bool mandatory;
		bool hashMandatory;
		std::string note;
		// 스캔 결과
		BiosStatus status;
		std::string statusText;  // "OK" / "MD5 MISMATCH" / "MISSING" 등
	};

	void loadDefinitions();          // JSON 파싱 → mEntries (스캔 전 상태)
	void checkEntry(BiosEntry& e);   // 파일 존재 + md5 대조 → status/statusText 채움
	void addResultRow(const BiosEntry& e);
	void writeReport();              // share/system/bios_report.txt

	NinePatchComponent mBackground;
	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<TextComponent> mSummary;
	std::shared_ptr<ComponentList> mList;
	std::vector<BiosEntry> mEntries;
	size_t mIndex;   // update()에서 다음에 처리할 항목
	bool mDone;
	int mOkCount, mWarnCount, mMissingCount;
};

#endif // ES_APP_GUIS_GUI_BIOS_CHECK_H
