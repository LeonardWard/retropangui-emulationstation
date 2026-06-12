# Changelog

All notable changes to RetroPangui EmulationStation will be documented in this file.

## [2026-06-12] - 기본값 개선 / 비디오 페이드 테마 제어

### Changed
- **TransitionStyle 기본값 fade → instant**
  - fade는 시스템 전환 시 블랙 플래시 발생. conf
    (`emulationstation.TransitionStyle`)로도 노출됨
- **ButtonLayout 기본값 nintendo → xbox** (A=확인, B=취소)
  - 최초 부팅 시 A/B가 반전(닌텐도 방식)되어 있던 문제. conf
    (`emulationstation.ButtonLayout`)로도 노출됨
- **SaveGamelistsMode 기본값 never → always**
  - never라 playcount/lastplayed가 gamelist.xml에 한 번도 기록되지 않았음
    ("gamelist.xml 자동 생성이 동작하지 않음" 증상의 원인)
  - on exit은 전원을 바로 끄는 기기에서 저장 시점이 보장되지 않아 always 채택
  - conf(`emulationstation.SaveGamelistsMode`)로도 노출됨

### Added
- **video 테마 요소에 `fadeTime` 속성(초, FLOAT) 추가**
  - 스냅샷↔비디오 페이드 시간이 200ms로 하드코딩되어 테마에서 제어 불가했음
  - `fadeTime=0`이면 컷 전환. 미지정 시 기존과 동일(0.2초)
- **system view에 `gameCountNumber`/`gameCountLabel` 테마 요소 추가**
  - 게임 수 숫자와 "GAMES AVAILABLE" 레이블을 분리해 테마에서 2행 레이아웃
    (숫자 크게/레이블 작게) 구성 가능. 테마에 선언된 경우에만 표시되고
    systemInfo와 동일한 페이드 애니메이션을 따름
- **text extra에 `scrollable` 속성(BOOLEAN) 추가**
  - 긴 글(longdescription 등)이 size 박스를 넘으면 다른 요소 위로 넘쳐
    그려지던 문제 — ScrollableContainer로 감싸 클리핑 + 자동 스크롤
    (5초 후 시작, 시스템 도착 시 처음부터). system view extras에 update
    전달도 추가
- **SOUND SETTINGS의 AUDIO CARD 저장 시 `global.audio_device` conf 기록**
  - RA 출력 디바이스가 ES 메뉴와 연동됨 (부팅 시 retroarch.cfg 반영)
  - `emulationstation.AudioDevice`는 기존 역기록 메커니즘으로 이미 conf에 기록되고 있음

## [2026-06-11] - 한글 표시 / 번역 수정 (실기기 검증 완료)

### Fixed
- **ko_KR 로케일에서 한국어 번역·한글 글리프가 표시되지 않던 문제** (`eba8ef7`)
  - .mo 설치 경로를 `RETROPANGUI_LOCALE_PATH`(`/opt/retropangui/share/locale`)와
    일치하도록 변경 (이전에는 설치/탐색 경로 불일치로 번역 로드 실패)
  - 폰트 폴백 목록 맨 앞에 C5 기본 탑재 한글 폰트
    (`/usr/bin/resources/NanumBarunGothic.ttf`) 추가
  - ES 내장 LANGUAGE 메뉴(en_US/ko_KR) 정리: Settings `Language` +
    `LocaleES::init`로 즉시 반영, conf `emulationstation.Language`와 동기화
  - 나머지 원인(BR2_GENERATE_LOCALE, locale purge 화이트리스트)은
    retropangui-c5 레포에서 수정

## [2026-06-11] - retropangui.conf 연동 안정화 (실기기 검증 완료)

### Added
- **emulationstation.\* 키 역기록** (`5977fbc`)
  - 부팅 시 `loadRetropanguiConf()`가 conf 값을 ES에 주입하고, `saveFile()` 시
    conf에 존재하던 키를 현재 설정값으로 conf에 갱신
  - 이전에는 conf에 있는 키(ThemeSet, ScreenSaverTime/Behavior, AudioDevice)를
    메뉴에서 바꿔도 재부팅마다 conf 값으로 되돌아갔음 → 이제 conf가 단일 마스터로 동작
  - `ScreenSaverTime`은 conf(초) ↔ Settings(ms) 단위 변환 양방향 처리
  - conf의 주석·다른 섹션은 보존, 값이 실제로 바뀐 경우에만 파일 기록

### Fixed
- **share 경로를 RETROPANGUI_SHARE 환경 변수로 결정** (`e04dfb7`, `b6b0a8e`)
  - `getSharePath()`/`getYmlPath()`/`retropanguiConfPath()` 모두
    `$RETROPANGUI_SHARE` → `/share` → `~/share` 순서로 통일
  - 이전에는 C5에서 `/root/share` 폴백으로 잘못된 경로를 읽고 씀
- **YAML 메뉴 저장이 restart 팝업 경로에서 유실되는 버그** (`50e89ba`)
  - `setOnSave`는 GuiSettings 소멸자에서 호출되는데 저장을 팝업 OK 콜백으로
    미루면 use-after-free로 저장 실패/크래시
  - 저장은 메뉴 닫을 때 항상 즉시 실행, 팝업은 재시작 여부만 질문
    (Cancel 시에도 값은 저장되며 다음 재시작 때 적용)

### Verified
- C5 실기기에서 메뉴 변경 → `/retropangui/share/system/retropangui.conf` 기록,
  재부팅 후 값 유지까지 확인 완료 (2026-06-11)

## [2025-10-26] - 목록 표시 방법 기능 추가

### Added
- **목록 표시 방법 (ShowFolders)** 설정 추가
  - 세 가지 모드: 전체(ALL) / 등록 게임만(SCRAPED) / 등록 우선(AUTO)
  - 기본값: 등록 우선(AUTO)

### Features

#### 전체 (ALL) 모드
- 파일 시스템 구조 그대로 표시
- 파일: 파일명 + 확장자 표시 (예: `game.cue`, `game.bin`)
- 폴더: 폴더명 그대로 표시
- 용도: 파일 관리 및 디버깅

#### 등록 게임만 (SCRAPED) 모드
- gamelist.xml에 등록된 게임만 표시
- gamelist.xml 순서대로 정렬
- metadata의 name 필드 사용
- 용도: 깔끔한 게임 라이브러리

#### 등록 우선 (AUTO) 모드 - 기본값
- gamelist.xml 등록 게임을 먼저 표시
- 미등록 게임은 스마트 필터링:
  - 폴더 내 .m3u 파일이 있으면 .m3u만 표시
  - .cue 파일이 1개만 있으면 .cue 표시
  - .bin/.cue 쌍이 있으면 .cue만 표시 (.bin 중복 제거)
  - 여러 파일이 있으면 폴더 표시
- metadata의 name 필드 사용
- 용도: 등록된 게임 우선 + 미등록 게임 자동 정리

### Technical Details

#### Core Implementation
- **gamelist.xml 중심 설계**: 파일 시스템이 아닌 gamelist.xml을 최상위 기준으로 사용
- **재귀 탐색**: `getFilesRecursive()` 및 `findFileByPath()`로 폴더 내부 게임 탐색
- **중복 방지**: `addedPaths` set으로 이미 표시된 게임 추적

#### Modified Files
- `es-app/src/FileData.cpp`:
  - `getChildrenListToDisplay()`: 필터링 로직
  - `getName()`: ALL 모드에서 파일명 표시
  - `findFileByPath()`: gamelist 경로로 FileData 탐색
- `es-app/src/guis/GuiMenu.cpp`: 설정 UI 및 reload 로직
- `es-core/src/Settings.cpp`: 기본값 설정

### Fixed
- 설정 변경 시 GUI 중복 표시 및 ES 재시작 버그 수정
- 변경 감지 로직 추가: 설정이 실제로 변경되었을 때만 reload

### Changed
- 설정 이름: "폴더 표시" → "목록 표시 방법" (더 정확한 의미)

### Commits
- `9df2d82` - Clean: 백업 파일 제거
- `42573b5` - Feature: ALL 모드에서 파일명+확장자 표시 및 설정 이름 변경
- `a669429` - Fix: ShowFolders 설정 변경 버그 수정 및 디버그 로그 제거
- `67c3e5c` - Fix: AUTO 모드에 등록 우선 로직 추가
- `f36986e` - Fix: <functional> 헤더 추가 (std::function 사용)
- `03bbb98` - Refactor: gamelist.xml 중심 접근 방식으로 변경
- `e028a8b` - Fix: ShowFolders SCRAPED/AUTO 모드 수정
- `9473929` - Refactor: ShowFolders 로직 완전 재작성
- `3427743` - Fix: ShowFolders 모드별 동작 개선

---

## Earlier Changes

See git history for earlier changes to RetroPangui EmulationStation.
