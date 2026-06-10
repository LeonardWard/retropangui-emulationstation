RetroPangui EmulationStation
============================

[RetroPie EmulationStation](https://github.com/RetroPie/EmulationStation)의 포크로,
**Odroid C5 기반 RetroPangui OS** 전용으로 수정된 버전입니다.
컨트롤러 네비게이션을 지원하는 그래픽 에뮬레이터 프론트엔드입니다.

이 저장소의 `main` 브랜치가 [retropangui-c5](https://github.com/LeonardWard/retropangui-c5)
Buildroot 패키지의 소스로 사용됩니다.

주요 특징 (원본 대비 변경사항)
-------------------------------

- **`retropangui.conf` 통합 설정** — RetroArch 전역 설정(`global.*`)과 OS 설정(`system.*`)을 단일 파일로 관리
- **YAML 기반 동적 메뉴** — C++ 재빌드 없이 `retropangui_features.yml` 파일 수정만으로 설정 메뉴 항목 추가/변경
- **멀티코어 지원** — `es_systems.xml`에서 시스템당 여러 에뮬레이터 코어 지정, 우선순위 기반 자동 선택
- **Odroid C5 (Mali-G310 GLES)** 네이티브 빌드 지원
- **restart 변경 감지** — 설정 변경 시 실제 값이 바뀐 경우에만 재시작 팝업 표시


retropangui.conf
================

ES 설정 메뉴에서 저장되는 값은 `~/.emulationstation/es_settings.cfg` 대신
`/share/system/retropangui.conf` (데스크탑: `~/share/system/retropangui.conf`)에 기록됩니다.

**키 네임스페이스:**

| 접두사 | 대상 | 예시 |
|--------|------|------|
| `global.*` | RetroArch 전역 설정 | `global.rewind_enable=true` |
| `system.*` | OS 수준 설정 | `system.language=ko_KR` |

`global.*` 키는 RetroArch가 읽는 `/share/system/retroarch/retroarch.cfg`에도 동시 기록됩니다.


YAML 메뉴 엔진
==============

ES 시작 시 `retropangui_features.yml` 파일을 파싱하여 설정 메뉴 항목을 자동 생성합니다.

**파일 위치 (우선순위 순):**

1. `/share/system/retropangui_features.yml` — 사용자 정의 (C5 데이터 파티션)
2. `~/share/system/retropangui_features.yml` — 데스크탑 개발 시
3. `/opt/retropangui/retropangui_features.yml` — 시스템 기본 (C5 rootfs)

**새 메뉴 항목 추가 예시:**

```yaml
menus:
  - id: my_settings
    label: "MY SETTINGS"
    parent: main          # main 메뉴에 최상위 항목으로 추가
    items:
      - id: my.toggle
        label: "SOME OPTION"
        type: toggle
        conf_key: global.some_option   # retropangui.conf에 기록될 키
        restart: none                  # none | es | system

      - id: my.list
        label: "CHOOSE ONE"
        type: list
        conf_key: system.my_choice
        restart: system
        options:
          - value: a
            label: "Option A"
          - value: b
            label: "Option B"

      - id: my.slider
        label: "NUMERIC VALUE"
        type: slider
        conf_key: global.my_value
        min: 0
        max: 100
        step: 5
        unit: "%"
        restart: none
```

**항목 타입:**

| type | 설명 | 저장값 |
|------|------|--------|
| `toggle` | ON/OFF 스위치 | `true` / `false` |
| `list` | 드롭다운 선택 | `options[n].value` |
| `slider` | 수치 슬라이더 | 정수 문자열 |

**restart 값:**

| restart | 동작 |
|---------|------|
| `none` | 재시작 없이 즉시 적용 |
| `es` | ES 재시작 팝업 표시 |
| `system` | 시스템 재부팅 팝업 표시 |

**파서 단위 테스트:**

```bash
cd es-app/src
g++ -std=c++17 -o /tmp/test_rp_parser test_rp_parser.cpp RetropanguiFeatures.cpp
/tmp/test_rp_parser
```


빌드하기
========

데스크탑에서 빌드하기 (개발/테스트용)
--------------------------------------

### 의존성 설치 (Debian/Ubuntu):

```bash
sudo apt-get install libsdl2-dev libfreeimage-dev libfreetype6-dev libcurl4-openssl-dev \
  rapidjson-dev libasound2-dev libgles2-mesa-dev build-essential cmake \
  fonts-droid-fallback libvlc-dev libvlccore-dev vlc-bin
```

### 빌드:

```bash
git clone --recursive https://github.com/LeonardWard/retropangui-emulationstation.git
cd retropangui-emulationstation
cmake -B build -DGL=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target emulationstation -- -j$(nproc)
```

`-DGL=ON` 은 데스크탑 OpenGL을 활성화합니다. Odroid C5 타겟 빌드에는 사용하지 마세요.

### 실행:

```bash
./emulationstation
```

`~/share/system/retropangui_features.yml`이 있으면 YAML 메뉴가 자동으로 로드됩니다.


Odroid C5에서 빌드하기
----------------------

Odroid C5는 Mali-G310 GPU를 탑재하고 있으며, 임베디드 OpenGL ES(GLES)를 사용합니다.

```bash
cmake -B build -DGLES=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target emulationstation -- -j$(nproc)
```

- `-DGLES=ON` — C5에서 **필수**. Mali-G310 GLES 드라이버를 선택합니다.
- CMakeLists.txt가 `/usr/lib/libMali.so` 존재 여부를 자동 확인하여 Mali 드라이버를 선택합니다.
- 크로스컴파일 시 `-DCMAKE_FIND_ROOT_PATH=<sysroot>`로 sysroot 경로를 지정하면 자동 감지가 sysroot 기준으로 동작합니다.

**사용하지 않는 옵션:**

- `-DRPI=On` — Raspberry Pi 전용
- `-DOMX=On` — Raspberry Pi omxplayer 전용
- `-DUSE_MESA_GLES=On` — Raspberry Pi Mesa VC4/V3D 전용

전체 OS 이미지 빌드는 [retropangui-c5](https://github.com/LeonardWard/retropangui-c5)를 참고하세요.


설정하기
========

**`~/.emulationstation/es_systems.xml`:**
처음 실행하면 예제 파일이 생성됩니다. `~`는 `$HOME`입니다.

**`~/.emulationstation/es_input.cfg`:**
처음 시작 시 입력 장치 구성 화면이 표시됩니다. Start 버튼 → "CONFIGURE INPUT"으로 언제든 재구성할 수 있습니다.

`--help` 또는 `-h`로 명령줄 옵션 목록을 볼 수 있습니다. F4로 종료합니다.


es_systems.xml 작성하기
========================

### 멀티코어 지원 (RetroPangui 확장 기능)

하나의 시스템에 여러 에뮬레이터 코어를 지정할 수 있습니다:

```xml
<?xml version="1.0"?>
<systemList>
  <system>
    <name>psx</name>
    <fullname>PlayStation</fullname>
    <path>/share/roms/psx</path>
    <extension>.bin .cue .img .mdf .pbp .toc .cbn .m3u .ccd .chd .zip .7z</extension>
    <command></command>
    <cores>
      <core name="pcsx_rearmed" fullname="PCSX ReARMed" module_id="lr-pcsx-rearmed"
            priority="1" extensions=".bin .cue .img .pbp"/>
      <core name="mednafen_psx_hw" fullname="Beetle PSX" module_id="lr-beetle-psx"
            priority="2" extensions=".cue .chd"/>
    </cores>
    <platform>psx</platform>
    <theme>psx</theme>
  </system>
</systemList>
```

### 태그 설명

**기본 태그:**

| 태그 | 설명 |
|------|------|
| `<name>` | 시스템 식별자 (필수) |
| `<fullname>` | 표시 이름 |
| `<path>` | ROM 경로 (필수, `~` 확장됨) |
| `<extension>` | 지원 확장자 목록 (공백 구분, 필수) |
| `<command>` | 실행 명령어 (멀티코어 시 비워둠) |
| `<platform>` | 스크래핑 플랫폼 ID |
| `<theme>` | 테마 이름 (기본값: `<name>`) |

**`<core>` 속성:**

| 속성 | 설명 |
|------|------|
| `name` | 코어 파일명 (`snes9x` → `snes9x_libretro.so`) |
| `fullname` | 표시 이름 |
| `module_id` | 모듈 식별자 |
| `priority` | 우선순위 (낮을수록 높음, 기본값: 999) |
| `extensions` | 이 코어가 처리할 확장자 |

**명령어 치환 태그:**

| 태그 | 설명 |
|------|------|
| `%ROM%` | ROM 절대 경로 (이스케이프됨) |
| `%BASENAME%` | ROM 파일 기본 이름 |
| `%ROM_RAW%` | ROM 절대 경로 (이스케이프 안 됨) |
| `%CORE%` | 선택된 코어 전체 경로 |
| `%CONFIG%` | 시스템별 RetroArch 설정 파일 경로 |

### 실행 플로우 (멀티코어)

```
ES 시작 → es_systems.xml 로드 → CoreInfo 파싱 → priority 정렬
게임 선택 → 확장자 기반 코어 매칭
명령 조립 → /opt/retropangui/bin/retroarch -L [코어] --config [설정] [ROM]
```


gamelist.xml
============

시스템의 gamelist.xml은 게임 이름, 이미지, 설명, 출시일, 평점 등의 메타데이터를 정의합니다.

스크래퍼로 자동 생성할 수 있습니다: Start → "SCRAPER" → "SCRAPE NOW"

개별 게임 스크랩: 게임 목록에서 Select → "EDIT THIS GAME'S METADATA" → "SCRAPE"

자세한 내용은 [GAMELISTS.md](GAMELISTS.md)를 참조하세요.


테마
====

자신만의 테마 제작 방법은 [THEMES.md](THEMES.md)를 참조하세요.
