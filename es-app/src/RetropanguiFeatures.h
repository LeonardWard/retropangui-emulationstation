#pragma once
#ifndef ES_APP_RETROPANGUI_FEATURES_H
#define ES_APP_RETROPANGUI_FEATURES_H

#include <string>
#include <vector>

struct FeatureOption {
	std::string value;
	std::string label;
};

struct FeatureItem {
	std::string id;
	std::string label;
	std::string type;       // "toggle" | "list" | "slider" | "input" | "command"
	std::string conf_key;   // retropangui.conf 키 (예: "global.video_smooth", "system.ssh")
	std::string exec;       // type=command 일 때 실행할 셸 커맨드
	std::string feedback_msg; // type=command 실행 직후 보여줄 확인 팝업 문구 (없으면 무반응)
	std::string restart     = "none";  // "none" | "es" | "system"
	std::string default_val = "";      // conf 키 없을 때 기본값
	std::vector<FeatureOption> options;
	float min  = 0.0f;
	float max  = 100.0f;
	float step = 1.0f;
	std::string unit;
};

struct FeatureMenu {
	std::string id;
	std::string label;
	std::string parent;
	std::vector<FeatureItem> items;
};

class RetropanguiFeatures {
public:
	// YAML 파일 로드. 경로:
	//   C5 (/share 존재):  /share/system/retropangui_features.yml
	//   데스크탑:          ~/share/system/retropangui_features.yml
	// 파일 없으면 빈 벡터 반환 (graceful skip)
	static std::vector<FeatureMenu> load();
};

#endif // ES_APP_RETROPANGUI_FEATURES_H
