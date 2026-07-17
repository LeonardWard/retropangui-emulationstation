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
// 시스템별 그룹 + 색상 상태점 행으로 보여준다. 커서를 움직이면 하단 상세줄에
// 그 파일의 설명(note)이 뜬다 - "텍스트 나열이라 투박하다"는 사용자 지적(2026-07-17)로
// 요약 칩/그룹 헤더/한국어 상태 라벨/상세줄 구조로 재설계.
// 스캔은 GuiGamelistRefresh처럼 update()에서 프레임당 1개 파일씩 동기 처리.
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
		std::string statusText;  // 영문 키(리포트용) - 화면엔 _()로 번역해 표시
		std::string detail;      // 하단 상세줄용 (note + 실측 해시 등)
	};

	void loadDefinitions();          // JSON 파싱 → mEntries (스캔 전 상태)
	void checkEntry(BiosEntry& e);   // 파일 존재 + md5 대조 → status/statusText 채움
	void addSystemHeaderRow(const std::string& name);
	void addResultRow(const BiosEntry& e);
	void updateSummaryChips();       // 정상/주의/누락 칩 텍스트+배치 갱신
	void updateDetail();             // 커서 행의 note를 하단 상세줄에 반영
	void writeReport();              // share/system/bios_report.txt

	NinePatchComponent mBackground;
	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<TextComponent> mChipOk;
	std::shared_ptr<TextComponent> mChipWarn;
	std::shared_ptr<TextComponent> mChipMiss;
	std::shared_ptr<TextComponent> mDetail;
	std::shared_ptr<ComponentList> mList;
	std::vector<BiosEntry> mEntries;
	std::vector<int> mRowEntry;      // 리스트 행 → mEntries 인덱스 (-1 = 시스템 헤더 행)
	std::string mLastSystem;         // 그룹 헤더 삽입 판단용
	size_t mIndex;   // update()에서 다음에 처리할 항목
	bool mDone;
	int mOkCount, mWarnCount, mMissingCount;
};

#endif // ES_APP_GUIS_GUI_BIOS_CHECK_H
