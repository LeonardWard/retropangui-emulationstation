EmulationStation
================

RetroPangui를 위한 RetroPie EmulationStation의 포크입니다.
EmulationStation은 컨트롤러 네비게이션을 지원하는 크로스 플랫폼 그래픽 에뮬레이터 프론트엔드입니다.

빌드하기
========

Linux에서 빌드하기
-----------------

EmulationStation은 C++11 코드를 사용하므로, Linux에서는 최소 g++-4.7, Windows에서는 VS2010 이상이 필요합니다.

EmulationStation은 몇 가지 의존성이 있습니다. 빌드를 위해서는 CMake, SDL2, FreeImage, FreeType, LibVLC (버전 3 이상), cURL, RapidJSON이 필요합니다. 또한 중국어/일본어/한국어 문자를 위한 대체 폰트가 포함된 `fonts-droid` 패키지를 설치하는 것이 좋지만, 이것 없이도 ES는 정상적으로 작동합니다 (이 패키지는 런타임에만 사용됩니다).

### Debian/Ubuntu에서:
다음 명령으로 모든 것을 쉽게 설치할 수 있습니다:
```bash
sudo apt-get install libsdl2-dev libfreeimage-dev libfreetype6-dev libcurl4-openssl-dev rapidjson-dev \
  libasound2-dev libgles2-mesa-dev build-essential cmake fonts-droid-fallback libvlc-dev \
  libvlccore-dev vlc-bin
```
### Fedora에서:
다음 명령으로 모든 것을 쉽게 설치할 수 있습니다 (rpmfusion 활성화 필요):
```bash
sudo dnf install SDL2-devel freeimage-devel freetype-devel curl-devel \
  alsa-lib-devel mesa-libGL-devel cmake \
  vlc-devel rapidjson-devel
```

선택적으로 `pugixml`을 설치하여 사용할 수 있습니다 (Debian 패키지: `libpugixml-dev`, Fedora/SuSE 패키지: `pugixml-devel`). 하지만 EmulationStation은 찾을 수 없는 경우 포함된 자체 복사본을 사용할 수 있습니다.

**참고**: 이 저장소는 git 서브모듈을 사용합니다 - 소스와 모든 서브모듈을 체크아웃하려면 다음을 사용하세요:

```bash
git clone --recursive https://github.com/RetroPie/EmulationStation.git
```

또는

```bash
git clone https://github.com/RetroPie/EmulationStation.git
cd EmulationStation
git submodule update --init
```

그런 다음 CMake로 Makefile을 생성하고 빌드합니다:
```bash
cd YourEmulationStationDirectory
cmake .
make
```

참고: Unix/Linux에서 `Debug` 빌드를 생성하려면 Makefile 생성 단계를 다음과 같이 실행하세요:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug .
```

### Raspberry Pi에서:

* GLES 구현 선택하기.

   * Pi 시스템이 레거시/Broadcom 드라이버를 사용하는 경우, 빌드를 구성하기 위해 `cmake`를 실행하기 전에 `libraspberry-dev` 패키지를 설치하세요
   * Pi 시스템이 Mesa VC4/V3D GL 드라이버를 사용하는 경우, MESA GLES 구현을 선택하기 위해 `-DUSE_MESA_GLES=On`을 사용하여 빌드하세요. 이 옵션은 Pi4 시스템에서 컴파일할 때 _필수_입니다. 이 시스템에서는 레거시 GL 드라이버가 더 이상 지원되지 않기 때문입니다.

  참고: RasPI OS 'Bullseye'부터 레거시/Broadcom 드라이버는 더 이상 지원되지 않으므로 `-DUSE_MESA_GLES=On`을 사용해야 합니다.

* `-DRPI=On`을 빌드 옵션에 추가하여 오디오/메모리 기본값을 활성화하세요
* 게임 목록에서 비디오 미리보기를 재생하기 위해 `omxplayer`를 사용하는 지원은 빌드 옵션에 `-DOMX=On`을 추가하여 활성화됩니다.
  참고: `omxplayer` 지원은 64비트 RasPI OS 또는 기본 RasPI OS 'Bullseye' 구성에서는 사용할 수 없습니다.

**GLES 빌드 참고사항**

 시스템에 작동하는 GLESv2 구현이 없는 경우, 빌드 옵션에 `-DUSE_GLES1=On`을 추가하여 GLESv1 레거시 렌더러를 컴파일할 수 있습니다.

Windows에서 빌드하기
-------------------

* [Visual Studio 2022](https://visualstudio.microsoft.com/vs/community/)를 설치하세요. 최소한 기본 선택 항목 목록과 함께 "Desktop development with C++" 워크로드를 설치하세요.

* 최신 버전의 [CMake](https://cmake.org/download/)를 설치하세요 (예: [cmake-3.27.1-windows-x86_64.msi](https://github.com/Kitware/CMake/releases/download/v3.27.1/cmake-3.27.1-windows-x86_64.msi)). CMake는 Visual Studio 프로젝트를 생성하는 데 사용됩니다.

* git을 사용하여 [vcpkg](https://vcpkg.io/en/)를 클론한 다음, 아래와 같이 부트스트랩 스크립트를 실행하여 vcpkg를 빌드하세요. 이것은 Microsoft의 C/C++ 의존성 관리자입니다.

```batchfile
C:\src>git clone https://github.com/Microsoft/vcpkg.git
C:\src>.\vcpkg\bootstrap-vcpkg.bat
```

* Windows PATH에 있는 폴더에 최신 [nuget.exe](https://dist.nuget.org/win-x86-commandline/latest/nuget.exe)를 다운로드하세요. NuGet은 .NET 패키지 관리자입니다.

* NuGet을 사용하여 최신 [libVLC](https://www.videolan.org/vlc/libvlc.html)를 다운로드하세요. 이 라이브러리는 비디오 스냅을 재생하는 데 사용됩니다.

```batchfile
C:\src\EmulationStation>mkdir nuget
C:\src\EmulationStation>cd nuget
C:\src\EmulationStation\nuget>nuget install -ExcludeVersion VideoLAN.LibVLC.Windows
```

* vcpkg를 사용하여 최신 사전 컴파일된 [cURL](http://curl.haxx.se/download.html)을 다운로드하세요. 이것은 URL로 데이터를 전송하기 위한 라이브러리입니다.

```batchfile
c:\src>.\vcpkg\vcpkg install curl:x86-windows-static-md
```

* vcpkg를 사용하여 최신 [FreeImage](https://freeimage.sourceforge.io/index.html)를 다운로드하세요. 이 라이브러리는 인기 있는 그래픽 이미지 형식을 지원합니다.

```batchfile
c:\src\>.\vcpkg\vcpkg install freeimage:x86-windows-static-md
```

* vcpkg를 사용하여 최신 사전 컴파일된 [FreeType2](https://freetype.org/)를 다운로드하세요. 이 라이브러리는 폰트를 렌더링하는 데 사용됩니다.

```batchfile
c:\src>.\vcpkg\vcpkg install freetype:x86-windows-static-md
```

* vcpkg를 사용하여 최신 [SDL2](http://www.libsdl.org/)를 다운로드하세요. Simple DirectMedia Layer는 OpenGL 및 Direct3D를 통해 오디오, 키보드, 마우스, 조이스틱 및 그래픽 하드웨어에 대한 낮은 수준의 액세스를 제공하도록 설계된 크로스 플랫폼 개발 라이브러리입니다.

```batchfile
c:\src>.\vcpkg\vcpkg install sdl2:x86-windows-static-md
```

* vcpkg를 사용하여 최신 [RapidJSON](http://rapidjson.org/)을 다운로드하세요. 이 라이브러리는 SAX/DOM 스타일 API를 모두 갖춘 C++용 빠른 JSON 파서/생성기를 제공합니다.

```batchfile
c:\src>.\vcpkg\vcpkg install rapidjson:x86-windows-static-md
```

* 아래 예제를 사용하여 위 단계에서 설치한 라이브러리를 가리키도록 환경 변수를 구성하세요. 아래 예제는 CMake와의 호환성을 위해 의도적으로 슬래시(/)를 사용합니다.

```batchfile
C:\src\EmulationStation>set VCPKG=C:/src/vcpkg/installed/x86-windows-static-md
C:\src\EmulationStation>set NUGET=C:/src/EmulationStation/nuget
C:\src\EmulationStation>set FREETYPE_DIR=%VCPKG%
C:\src\EmulationStation>set FREEIMAGE_HOME=%VCPKG%
C:\src\EmulationStation>set VLC_HOME=%NUGET%/VideoLAN.LibVLC.Windows/build/x86
C:\src\EmulationStation>set RAPIDJSON_INCLUDE_DIRS=%VCPKG%/include
C:\src\EmulationStation>set CURL_INCLUDE_DIR=%VCPKG%/include
C:\src\EmulationStation>set SDL2_INCLUDE_DIR=%VCPKG%/include/SDL2
C:\src\EmulationStation>set VLC_INCLUDE_DIR=%VLC_HOME%/include
C:\src\EmulationStation>set CURL_LIBRARY=%VCPKG%/lib/*.lib
C:\src\EmulationStation>set SDL2_LIBRARY=%VCPKG%/lib/manual-link/SDL2main.lib
C:\src\EmulationStation>set VLC_LIBRARIES=%VLC_HOME%/libvlc*.lib
C:\src\EmulationStation>set VLC_VERSION=3.0.11
```

* CMake를 사용하여 Visual Studio 프로젝트를 생성하세요.

```batchfile
C:\src\EmulationStation>mkdir build
C:\src\EmulationStation>cmake . -B build -A Win32 ^
-DRAPIDJSON_INCLUDE_DIRS=%RAPIDJSON_INCLUDE_DIRS% ^
-DCURL_INCLUDE_DIR=%CURL_INCLUDE_DIR% ^
-DSDL2_INCLUDE_DIR=%SDL2_INCLUDE_DIR% ^
-DVLC_INCLUDE_DIR=%VLC_INCLUDE_DIR% ^
-DCURL_LIBRARY=%CURL_LIBRARY% ^
-DSDL2_LIBRARY=%SDL2_LIBRARY% ^
-DVLC_LIBRARIES=%VLC_LIBRARIES% ^
-DVLC_VERSION=%VLC_VERSION% ^
-DCMAKE_EXE_LINKER_FLAGS=/SAFESEH:NO
```

* CMake를 사용하여 Visual Studio 프로젝트를 빌드하세요.

```batchfile
C:\src\EmulationStation>cmake --build build --config Release
```

* 아래 예제를 사용하여 새로 빌드된 바이너리 및 기타 필요한 파일을 대상 폴더에 복사하세요.

```batchfile
C:\src\EmulationStation>mkdir -p C:\apps\EmulationStation\.emulationstation
C:\src\EmulationStation>xcopy C:\src\EmulationStation\resources C:\apps\EmulationStation\resources /h /i /c /k /e /r /y
C:\src\EmulationStation>copy C:\src\EmulationStation\Release\*.exe C:\apps\EmulationStation /Y
C:\src\EmulationStation>copy C:\src\EmulationStation\nuget\VideoLAN.LibVLC.Windows\build\x86\*.dll C:\apps\EmulationStation /Y
C:\src\EmulationStation>xcopy C:\src\EmulationStation\nuget\VideoLAN.LibVLC.Windows\build\x86\plugins C:\apps\EmulationStation\plugins /h /i /c /k /e /r /y

```


설정하기
===========

**~/.emulationstation/es_systems.xml:**
처음 실행하면 `~/.emulationstation/es_systems.xml`에 예제 시스템 설정 파일이 생성됩니다. `~`는 Linux에서는 `$HOME`, Windows에서는 `%HOMEPATH%`입니다. 이 예제에는 설정 파일 작성 방법을 설명하는 몇 가지 주석이 있습니다. 자세한 내용은 "es_systems.xml 작성하기" 섹션을 참조하세요.

**참고:** RetroPangui EmulationStation은 기존 `es_systems.cfg` 대신 `es_systems.xml` 형식을 사용하여 멀티코어 지원 등 확장된 기능을 제공합니다.

**~/.emulationstation/es_input.cfg:**
EmulationStation을 처음 시작하면 입력 장치를 구성하라는 메시지가 표시됩니다. 프로세스는 다음과 같습니다:

1. 구성하려는 장치의 버튼을 누르고 있습니다. 키보드도 포함됩니다.

2. 목록에 나타나는 버튼을 누릅니다. 일부 입력은 몇 초 동안 아무 버튼이나 누르고 있으면 건너뛸 수 있습니다 (예: page up/page down).

3. 위/아래를 눌러 매핑을 검토하고 A를 눌러 변경할 수 있습니다.

4. "SAVE"를 선택하여 이 장치를 저장하고 입력 구성 화면을 닫습니다.

새 구성이 `~/.emulationstation/es_input.cfg` 파일에 추가됩니다.

**새 장치와 기존 장치 모두 Start 버튼을 누르고 "CONFIGURE INPUT"을 선택하여 언제든지 (재)구성할 수 있습니다.** 여기에서 필요한 경우 메뉴를 열 때 사용한 장치를 뽑고 새 장치를 연결할 수 있습니다. 새 장치는 기존 입력 구성 파일에 추가되므로 기존 장치는 구성된 상태로 유지됩니다.

**컨트롤러가 작동하지 않으면 `~/.emulationstation/es_input.cfg` 파일을 삭제하면 다음 실행 시 입력 구성 화면이 다시 나타납니다.**

`--help` 또는 `-h`를 사용하여 명령줄 옵션 목록을 볼 수 있습니다.

ES가 멈추지 않는 한 항상 F4를 눌러 애플리케이션을 종료할 수 있습니다.


es_systems.xml 작성하기
=========================

[emulationstation.org](http://emulationstation.org/gettingstarted.html#config)에서 기본 구성 지침을 확인하세요.

`es_systems.xml` 파일에는 XML로 작성된 EmulationStation의 시스템 구성 데이터가 포함되어 있습니다. 이것은 EmulationStation에 어떤 시스템이 있는지, 어떤 플랫폼에 해당하는지 (스크래핑용), 그리고 게임이 어디에 있는지를 알려줍니다.

ES는 다음 순서로 두 곳에서 es_systems.xml 파일을 확인하며, 작동하는 파일을 찾으면 멈춥니다:
* `~/.emulationstation/es_systems.xml`
* `/etc/emulationstation/es_systems.xml`

EmulationStation이 시스템을 표시하는 순서는 정의한 순서를 반영합니다.

**참고:** 시스템은 "path" 디렉토리에 최소한 하나의 게임이 있어야 하며, 그렇지 않으면 ES가 무시합니다! 유효한 시스템을 찾을 수 없으면 ES는 오류를 보고하고 종료됩니다!

### 멀티코어 지원 (RetroPangui 확장 기능)

RetroPangui EmulationStation은 하나의 시스템에 여러 에뮬레이터 코어를 지정할 수 있는 멀티코어 지원 기능을 제공합니다. 이를 통해:
- 시스템별로 여러 코어 중 선택 가능
- 우선순위(priority)에 따라 자동으로 최적 코어 선택
- 파일 확장자별로 다른 코어 사용 가능
- RetroArch 설정을 시스템/코어별로 분리 관리

### es_systems.xml 예제 (멀티코어 지원)

```xml
<?xml version="1.0"?>
<systemList>
  <system>
    <!-- 시스템 식별자 -->
    <name>psx</name>

    <!-- 표시 이름 -->
    <fullname>PlayStation</fullname>

    <!-- ROM 경로 -->
    <path>/home/pangui/share/roms/psx</path>

    <!-- 지원 확장자 목록 (공백으로 구분) -->
    <extension>.bin .cue .img .mdf .pbp .toc .cbn .m3u .ccd .chd .zip .7z</extension>

    <!-- 명령어 (멀티코어 사용 시 비워둠) -->
    <command></command>

    <!-- 멀티코어 정의 -->
    <cores>
      <!-- 각 코어 정의 -->
      <core name="pcsx_rearmed" fullname="PCSX ReARMed" module_id="lr-pcsx-rearmed"
            priority="1" extensions=".bin .cue .img .pbp"/>
      <core name="mednafen_psx_hw" fullname="Beetle PSX" module_id="lr-beetle-psx"
            priority="2" extensions=".cue .chd"/>
    </cores>

    <!-- 스크래핑 플랫폼 -->
    <platform>psx</platform>

    <!-- 테마 이름 -->
    <theme>psx</theme>
  </system>

  <system>
    <name>snes</name>
    <fullname>Super Nintendo Entertainment System</fullname>
    <path>/home/pangui/share/roms/snes</path>
    <extension>.bin .smc .sfc .fig .swc .mgd .zip</extension>
    <command></command>
    <cores>
      <core name="snes9x" fullname="Snes9x" module_id="lr-snes9x"
            priority="1" extensions=".bin .smc .sfc .fig .swc .mgd .zip"/>
    </cores>
    <platform>snes</platform>
    <theme>snes</theme>
  </system>
</systemList>
```

### 태그 설명

#### 기본 태그
- `<name>`: 시스템의 짧은 식별자 (필수)
- `<fullname>`: 메뉴에 표시될 전체 이름 (선택)
- `<path>`: ROM을 검색할 경로 (필수, `~`는 홈 디렉토리로 확장됨)
- `<extension>`: 지원하는 파일 확장자 목록 (필수, 공백으로 구분, 대소문자 구분)
- `<command>`: 게임 실행 명령어 (멀티코어 사용 시 비워둠)
- `<platform>`: 스크래핑에 사용할 플랫폼 ID (선택)
- `<theme>`: 사용할 테마 이름 (선택, 기본값은 `<name>`)

#### 멀티코어 태그 (RetroPangui 확장)
- `<cores>`: 코어 목록 컨테이너
- `<core>`: 개별 코어 정의
  - `name`: 코어 파일 이름 (예: "snes9x"는 "snes9x_libretro.so"로 변환)
  - `fullname`: 사용자에게 표시될 전체 이름
  - `module_id`: 코어 모듈 식별자 (설치 관리용)
  - `priority`: 우선순위 (낮을수록 높은 우선순위, 기본값: 999)
  - `extensions`: 이 코어가 지원하는 확장자 (선택)

### 명령어 치환 태그

다음 태그들은 실행 명령에서 자동으로 치환됩니다:

- `%ROM%` - 선택한 ROM의 절대 경로 (Bash 특수 문자 이스케이프됨)
- `%BASENAME%` - ROM 파일의 기본 이름 (예: "/foo/bar.rom" → "bar")
- `%ROM_RAW%` - 이스케이프되지 않은 ROM의 절대 경로
- `%CORE%` - 선택된 코어의 전체 경로 (멀티코어 사용 시)
- `%CONFIG%` - 시스템별 RetroArch 설정 파일 경로 (멀티코어 사용 시)

### 실행 플로우 (멀티코어)

1. **ES 시작**: `es_systems.xml` 로드 → CoreInfo 파싱 → priority 기준 정렬
2. **게임 선택**: FileData가 게임 확장자와 코어 extensions 비교
3. **명령 생성**: Settings에서 경로 읽기 → RetroArch 명령 동적 조립
   ```
   /opt/retropangui/bin/retroarch -L [코어경로] --config [설정경로] [ROM경로]
   ```
4. **실행**: 직접 RetroArch 호출 (중간 스크립트 없음)

### 레거시 방식 (단일 코어)

멀티코어를 사용하지 않는 경우, 기존 방식대로 `<command>` 태그에 직접 명령어를 지정할 수 있습니다:

```xml
<system>
  <name>snes</name>
  <fullname>Super Nintendo Entertainment System</fullname>
  <path>~/roms/snes</path>
  <extension>.smc .sfc .SMC .SFC</extension>
  <command>snesemulator %ROM%</command>
  <platform>snes</platform>
  <theme>snes</theme>
</system>
```

EmulationStation의 실제 예제는 [SYSTEMS.md](SYSTEMS.md)를 참조하세요.

gamelist.xml
============

시스템의 gamelist.xml 파일은 이름, 이미지 (스크린샷 또는 박스 아트 등), 설명, 출시 날짜 및 평점과 같은 게임 메타데이터를 정의합니다.

시스템의 게임 중 최소한 하나에 이미지가 지정되어 있으면, ES는 해당 시스템에 대해 상세 보기를 사용합니다 (게임 목록과 함께 메타데이터를 표시합니다).

*ES의 [스크래핑](http://en.wikipedia.org/wiki/Web_scraping) 도구를 사용하여 gamelist.xml을 수동으로 만들지 않아도 됩니다.* 스크래퍼를 실행하는 두 가지 방법이 있습니다:

* **여러 게임을 스크랩하려면:** start를 눌러 메뉴를 열고 "SCRAPER" 옵션을 선택하세요. 설정을 조정하고 "SCRAPE NOW"를 누르세요.
* **게임 하나만 스크랩하려면:** ES의 게임 목록에서 게임을 찾고 select를 누르세요. "EDIT THIS GAME'S METADATA"를 선택한 다음 메타데이터 편집기 하단의 "SCRAPE" 버튼을 누르세요.

메타데이터 편집기를 사용하여 ES 내에서 메타데이터를 편집할 수도 있습니다 - 게임 목록에서 편집하려는 게임을 찾고 Select를 누른 다음 "EDIT THIS GAME'S METADATA"를 선택하세요.

명령줄 버전의 스크래퍼도 제공됩니다 - `--scrape`로 emulationstation을 실행하세요 *(현재 고장남)*.

`--ignore-gamelist` 스위치를 사용하여 gamelist를 무시하고 ES가 비상세 보기를 사용하도록 할 수 있습니다.

gamelist.xml 파일을 생성하거나 구문 분석하는 도구를 작성하는 경우, 더 자세한 문서는 [GAMELISTS.md](GAMELISTS.md)를 확인하세요.


테마
======

자신만의 테마를 만들거나 (기존 테마를 편집하는) 방법에 대해 더 알고 싶다면 [THEMES.md](THEMES.md)를 읽어보세요!

EmulationStation 웹페이지에 다운로드할 수 있는 테마를 몇 가지 올려두었습니다: http://aloshi.com/emulationstation#themes
