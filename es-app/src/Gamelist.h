#pragma once
#ifndef ES_APP_GAME_LIST_H
#define ES_APP_GAME_LIST_H

#include <string>

class SystemData;
namespace pugi { class xml_document; }

// Loads gamelist.xml data into a SystemData.
void parseGamelist(SystemData* system);

// Creates a minimal gamelist.xml from the files already in the FileData tree.
void generateGamelist(SystemData* system);

// Writes currently loaded metadata for a SystemData to gamelist.xml.
void updateGamelist(SystemData* system);

// RetroPangui: gamelist.xml을 그 자리에서 직접 덮어쓰지 않고 .tmp에 먼저
// 쓴 뒤 rename()으로 교체 - 쓰는 도중 정전(이 기기의 흔한 종료 방식)이 나도
// 기존 파일은 깨지지 않는다. 기존 파일은 .old로 백업(이 프로젝트의 OTA
// squashfs/initramfs .old 관례와 동일)해서 문제 생기면 수동 복구 가능.
// 성공하면 true.
bool saveGamelistXml(const pugi::xml_document& doc, const std::string& path);

// RetroPangui: 번들 게임(squashfs, rpui-bundlegame이 관리)은 share에 물리
// 복사 없이 gamelist.xml의 <path>가 직접 스쿼시fs 경로를 가리킨다 - 시스템
// 롬 루트(share/roms/<sys>) 밖이므로 findOrCreateFile()의 기본 거부를
// 통과시켜야 하고, refreshGamelist()의 디스크 스캔 기반 삭제 감지 대상에서도
// 제외해야 한다(안 그러면 "게임 리스트 갱신" 누를 때마다 지워짐). 두 곳에서
// 같은 판정을 쓰도록 여기 하나로 모음(todo-20260704-es-multi-path-roms.html).
bool isBundledRomPath(const std::string& absPath, const std::string& systemName);

#endif // ES_APP_GAME_LIST_H
