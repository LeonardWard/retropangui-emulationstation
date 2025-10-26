# Changelog

All notable changes to RetroPangui EmulationStation will be documented in this file.

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
