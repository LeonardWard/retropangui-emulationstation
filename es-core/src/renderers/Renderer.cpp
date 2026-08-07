#include "renderers/Renderer.h"

#include "math/Transform4x4f.h"
#include "math/Vector2i.h"
#include "resources/ResourceManager.h"
#include "ImageIO.h"
#include "Log.h"
#include "Settings.h"

#include <SDL.h>
#include <stack>

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>

extern "C" {
#include <xf86drm.h>
}

//////////////////////////////////////////////////////////////////////////

namespace Renderer
{
	static std::stack<Rect> clipStack;
	static SDL_Window*      sdlWindow          = nullptr;
	static int              windowWidth        = 0;
	static int              windowHeight       = 0;
	static int              screenWidth        = 0;
	static int              screenHeight       = 0;
	static int              screenOffsetX      = 0;
	static int              screenOffsetY      = 0;
	static int              screenRotate       = 0;
	static bool             initialCursorState = 1;

//////////////////////////////////////////////////////////////////////////

	static void setIcon()
	{
		size_t                     width   = 0;
		size_t                     height  = 0;
		const ResourceData         resData = ResourceManager::getInstance()->getFileData(":/window_icon_256.png");
		std::vector<unsigned char> rawData = ImageIO::loadFromMemoryRGBA32(resData.ptr.get(), resData.length, width, height);

		if(!rawData.empty())
		{
			ImageIO::flipPixelsVert(rawData.data(), width, height);

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			const unsigned int rmask = 0xFF000000;
			const unsigned int gmask = 0x00FF0000;
			const unsigned int bmask = 0x0000FF00;
			const unsigned int amask = 0x000000FF;
#else
			const unsigned int rmask = 0x000000FF;
			const unsigned int gmask = 0x0000FF00;
			const unsigned int bmask = 0x00FF0000;
			const unsigned int amask = 0xFF000000;
#endif
			// try creating SDL surface from logo data
			SDL_Surface* logoSurface = SDL_CreateRGBSurfaceFrom((void*)rawData.data(), (int)width, (int)height, 32, (int)(width * 4), rmask, gmask, bmask, amask);

			if(logoSurface != nullptr)
			{
				SDL_SetWindowIcon(sdlWindow, logoSurface);
				SDL_FreeSurface(logoSurface);
			}
		}

	} // setIcon

//////////////////////////////////////////////////////////////////////////

	// RetroPangui(2026-08-07): SDL2 KMSDRM 백엔드(SDL_kmsdrmvideo.c의
	// CreateDisplay(), 약 776행)는 CRTC에 지금 실제로 걸린 모드를 커넥터가
	// 광고하는 모드 목록(EDID 기반)과 SDL_memcmp로 바이트 단위 매칭을
	// 시도하는데, odroid-drm-fbset이 동적으로 등록한 커스텀 모드라인은 이
	// 목록에 없을 수 있어 매칭 실패 시 SDL이 실제 활성 모드를 완전히
	// 무시하고 EDID preferred 모드로 조용히 대체함 - 그 결과
	// SDL_GetDesktopDisplayMode()가 실제와 다른 크기를 돌려줘서 창이 실제
	// 화면보다 작게 만들어지는 문제를 실기기로 확인함
	// (todo-20260807-hdmi-hotswap-blank-screen.html). SDL을 믿는 대신
	// odroid-drm-fbset -getcurrentmode로 CRTC의 실제 활성 모드를 libdrm
	// 레벨에서 직접 물어본다(최초엔 이 sysfs 파일을 직접 파싱했으나,
	// odroid-drm-fbset이 같은 libdrm 라이브 상태를 더 근본적으로 조회할
	// 수 있고 ES/Python/셸 어디서든 재사용 가능하도록 그쪽으로 옮김 -
	// "모듈화").
	static bool getRealDisplaySize(int& outW, int& outH)
	{
		// RetroPangui(2026-08-07, 임시 진단): 실기기에서 이 경로가 왜 한
		// 번도 개입하지 않는지(SDL 값과 항상 같다고 나오는지, 아니면
		// 조용히 실패하는지) 원인을 못 찾아서 stderr까지 같이 캡처해서
		// 로그에 남김 - 원인 확인되면 되돌릴 것.
		FILE* pipe = popen("/usr/sbin/odroid-drm-fbset -getcurrentmode 2>&1", "r");
		if(!pipe)
		{
			LOG(LogWarning) << "getRealDisplaySize: popen 실패(" << strerror(errno) << ")";
			return false;
		}

		char buf[256] = {0};
		bool gotLine = (fgets(buf, sizeof(buf), pipe) != nullptr);
		int  exitStatus = pclose(pipe);

		// 개행 제거(로그 가독성용)
		size_t len = strlen(buf);
		while(len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
			buf[--len] = '\0';

		if(!gotLine || exitStatus != 0)
		{
			LOG(LogWarning) << "getRealDisplaySize: 실패(gotLine=" << gotLine << " exitStatus=" << exitStatus << " out=[" << buf << "])";
			return false;
		}

		// odroid-drm-fbset -getcurrentmode의 출력 포맷: "너비 높이 새로고침율 이름"
		int w = 0, h = 0, refresh = 0;
		if(sscanf(buf, "%d %d %d", &w, &h, &refresh) != 3)
		{
			LOG(LogWarning) << "getRealDisplaySize: 출력 파싱 실패(out=[" << buf << "])";
			return false;
		}

		if(w <= 0 || h <= 0)
			return false;

		LOG(LogInfo) << "getRealDisplaySize: 성공(" << w << "x" << h << "@" << refresh << ")";
		outW = w;
		outH = h;
		return true;

	} // getRealDisplaySize

	static bool createWindow()
	{
		LOG(LogInfo) << "Creating window...";

		if(SDL_Init(SDL_INIT_VIDEO) != 0)
		{
			LOG(LogError) << "Error initializing SDL!\n	" << SDL_GetError();
			return false;
		}

		initialCursorState = (SDL_ShowCursor(0) != 0);

		int displayIndex = Settings::getInstance()->getInt("MonitorID");

		if(displayIndex < 0 || displayIndex >= SDL_GetNumVideoDisplays()){
			displayIndex = 0;
		}

		SDL_DisplayMode dispMode;
		SDL_GetDesktopDisplayMode(displayIndex, &dispMode);

		int realW = 0, realH = 0;
		if(getRealDisplaySize(realW, realH) && (realW != dispMode.w || realH != dispMode.h))
		{
			LOG(LogWarning) << "createWindow: SDL이 돌려준 데스크톱 모드(" << dispMode.w << "x" << dispMode.h
				<< ")가 실제 활성 해상도(" << realW << "x" << realH << ")와 다름 - 실제 값으로 대체";
			dispMode.w = realW;
			dispMode.h = realH;
		}

		windowWidth   = Settings::getInstance()->getInt("WindowWidth")   ? Settings::getInstance()->getInt("WindowWidth")   : dispMode.w;
		windowHeight  = Settings::getInstance()->getInt("WindowHeight")  ? Settings::getInstance()->getInt("WindowHeight")  : dispMode.h;
		screenWidth   = Settings::getInstance()->getInt("ScreenWidth")   ? Settings::getInstance()->getInt("ScreenWidth")   : windowWidth;
		screenHeight  = Settings::getInstance()->getInt("ScreenHeight")  ? Settings::getInstance()->getInt("ScreenHeight")  : windowHeight;
		screenOffsetX = Settings::getInstance()->getInt("ScreenOffsetX") ? Settings::getInstance()->getInt("ScreenOffsetX") : 0;
		screenOffsetY = Settings::getInstance()->getInt("ScreenOffsetY") ? Settings::getInstance()->getInt("ScreenOffsetY") : 0;
		screenRotate  = Settings::getInstance()->getInt("ScreenRotate")  ? Settings::getInstance()->getInt("ScreenRotate")  : 0;

		setupWindow();

		const unsigned int windowFlags = (Settings::getInstance()->getBool("Windowed") ? 0 : (Settings::getInstance()->getBool("FullscreenBorderless") ? SDL_WINDOW_BORDERLESS : SDL_WINDOW_FULLSCREEN)) | getWindowFlags();

		if((sdlWindow = SDL_CreateWindow("EmulationStation", SDL_WINDOWPOS_UNDEFINED_DISPLAY(displayIndex), SDL_WINDOWPOS_UNDEFINED_DISPLAY(displayIndex), windowWidth, windowHeight, windowFlags)) == nullptr)
		{
			LOG(LogError) << "Error creating SDL window!\n\t" << SDL_GetError();
			return false;
		}

		LOG(LogInfo) << "Created window successfully.";

		createContext();
		setIcon();
		setSwapInterval();

		return true;

	} // createWindow

//////////////////////////////////////////////////////////////////////////

	// RetroPangui(2026-08-07): SIGUSR1 모니터 핫스왑 시 SDL_Quit() 이후에도
	// DRM master가 완전히 반납되지 않은 채로 남는 경우가 실기기에서
	// 재현됨(todo-20260807-hdmi-hotswap-blank-screen.html) - 뒤이어 실행되는
	// apply-resolution.sh의 odroid-drm-fbset -outputmode가
	// hdmitx_common_validate_mode_locked: state_sequence_id failed로
	// 반복 실패하는 원인이었음(ES 프로세스가 살아있는 채로 deinit만 했을 때만
	// 재현, killall로 완전 종료했을 때는 재현 안 됨 - 통제실험으로 확인).
	// SDL 내부 타이밍을 추측하지 않고, 실제로 이 프로세스가 다시 DRM
	// master가 될 수 있는지 직접 확인(짧게 재시도)한 뒤에야 다음 단계로
	// 넘어가도록 함.
	static void waitForDrmMasterRelease()
	{
		const char* device        = "/dev/dri/card0";
		const int   maxAttempts   = 20;
		const int   retryDelayUs  = 25000; // 25ms * 20회 = 최대 500ms 대기

		for(int attempt = 0; attempt < maxAttempts; attempt++)
		{
			int fd = open(device, O_RDWR | O_CLOEXEC);
			if(fd < 0)
			{
				LOG(LogWarning) << "waitForDrmMasterRelease: " << device << " open 실패(" << strerror(errno) << ") - 확인 건너뜀";
				return;
			}

			if(drmSetMaster(fd) == 0)
			{
				drmDropMaster(fd);
				close(fd);
				if(attempt > 0)
					LOG(LogInfo) << "waitForDrmMasterRelease: DRM master 반납 확인(" << (attempt + 1) << "번째 시도, 약 " << (attempt * retryDelayUs / 1000) << "ms 대기)";
				return;
			}

			close(fd);
			usleep(retryDelayUs);
		}

		LOG(LogWarning) << "waitForDrmMasterRelease: " << maxAttempts << "회 시도(최대 " << (maxAttempts * retryDelayUs / 1000) << "ms) 후에도 DRM master 미확인 - 계속 진행";

	} // waitForDrmMasterRelease

	static void destroyWindow()
	{
		destroyContext();

		SDL_DestroyWindow(sdlWindow);
		sdlWindow = nullptr;

		SDL_ShowCursor(initialCursorState);

		SDL_Quit();

		waitForDrmMasterRelease();

	} // destroyWindow

//////////////////////////////////////////////////////////////////////////

	bool init()
	{
		if(!createWindow())
			return false;

		Transform4x4f projection = Transform4x4f::Identity();
		Rect          viewport   = Rect(0, 0, 0, 0);

		switch(screenRotate)
		{
			case 0:
			{
				// RetroPangui UI SCALE(2026-07-11): 뷰포트(실제 픽셀을 뿌리는
				// 물리 영역)는 항상 물리 창 전체(windowWidth/Height)를 채우고,
				// 오쏘 투영의 논리 좌표 범위(screenWidth/Height)는 "이 UI를
				// 얼마나 작게 그릴지" 값으로 완전히 분리함 - 예전엔 이 둘이
				// 같은 screenWidth/Height 값에 묶여있어서, ScreenWidth/Height를
				// 줄이면 화면 전체가 아니라 화면 한구석의 작은 사각형에만
				// 그려졌음(베젤 보정용 오프셋 배치 기능과 뒤섞여 있었음).
				// screenOffsetX/Y는 회전 없는(이 프로젝트가 실제로 쓰는) 이
				// 케이스에서는 물리 창을 이미 꽉 채우므로 의미가 없어져서 뺌
				// - 여전히 0이 아닌 값을 넣으면 아무 효과도 없다는 뜻이니
				// 베젤 보정이 필요한 포크가 있다면 별도로 고려할 것.
				viewport.x = 0;
				viewport.y = 0;
				viewport.w = windowWidth;
				viewport.h = windowHeight;

				projection.orthoProjection(0, screenWidth, screenHeight, 0, -1.0, 1.0);
			}
			break;

			case 1:
			{
				viewport.x = windowWidth - screenOffsetY - screenHeight;
				viewport.y = screenOffsetX;
				viewport.w = screenHeight;
				viewport.h = screenWidth;

				projection.orthoProjection(0, screenHeight, screenWidth, 0, -1.0, 1.0);
				projection.rotate((float)ES_DEG_TO_RAD(90), {0, 0, 1});
				projection.translate({0, screenHeight * -1.0f, 0});
			}
			break;

			case 2:
			{
				viewport.x = windowWidth  - screenOffsetX - screenWidth;
				viewport.y = windowHeight - screenOffsetY - screenHeight;
				viewport.w = screenWidth;
				viewport.h = screenHeight;

				projection.orthoProjection(0, screenWidth, screenHeight, 0, -1.0, 1.0);
				projection.rotate((float)ES_DEG_TO_RAD(180), {0, 0, 1});
				projection.translate({screenWidth * -1.0f, screenHeight * -1.0f, 0});
			}
			break;

			case 3:
			{
				viewport.x = screenOffsetY;
				viewport.y = windowHeight - screenOffsetX - screenWidth;
				viewport.w = screenHeight;
				viewport.h = screenWidth;

				projection.orthoProjection(0, screenHeight, screenWidth, 0, -1.0, 1.0);
				projection.rotate((float)ES_DEG_TO_RAD(270), {0, 0, 1});
				projection.translate({screenWidth * -1.0f, 0, 0});
			}
			break;
		}

		setViewport(viewport);
		setProjection(projection);
		swapBuffers();

		return true;

	} // init

//////////////////////////////////////////////////////////////////////////

	void deinit()
	{
		destroyWindow();

	} // deinit

//////////////////////////////////////////////////////////////////////////

	void pushClipRect(const Vector2i& _pos, const Vector2i& _size)
	{
		Rect box(_pos.x(), _pos.y(), _size.x(), _size.y());

		if(box.w == 0) box.w = screenWidth  - box.x;
		if(box.h == 0) box.h = screenHeight - box.y;

		switch(screenRotate)
		{
			case 0: { box = Rect(screenOffsetX + box.x,                       screenOffsetY + box.y,                        box.w, box.h); } break;
			case 1: { box = Rect(windowWidth - screenOffsetY - box.y - box.h, screenOffsetX + box.x,                        box.h, box.w); } break;
			case 2: { box = Rect(windowWidth - screenOffsetX - box.x - box.w, windowHeight - screenOffsetY - box.y - box.h, box.w, box.h); } break;
			case 3: { box = Rect(screenOffsetY + box.y,                       windowHeight - screenOffsetX - box.x - box.w, box.h, box.w); } break;
		}

		// make sure the box fits within clipStack.top(), and clip further accordingly
		if(clipStack.size())
		{
			const Rect& top = clipStack.top();
			if( top.x          >  box.x)          box.x = top.x;
			if( top.y          >  box.y)          box.y = top.y;
			if((top.x + top.w) < (box.x + box.w)) box.w = (top.x + top.w) - box.x;
			if((top.y + top.h) < (box.y + box.h)) box.h = (top.y + top.h) - box.y;
		}

		if(box.w < 0) box.w = 0;
		if(box.h < 0) box.h = 0;

		clipStack.push(box);

		setScissor(box);

	} // pushClipRect

//////////////////////////////////////////////////////////////////////////

	void popClipRect()
	{
		if(clipStack.empty())
		{
			LOG(LogError) << "Tried to popClipRect while the stack was empty!";
			return;
		}

		clipStack.pop();

		if(clipStack.empty()) setScissor(Rect(0, 0, 0, 0));
		else                  setScissor(clipStack.top());

	} // popClipRect

//////////////////////////////////////////////////////////////////////////

	void drawRect(const float _x, const float _y, const float _w, const float _h, const unsigned int _color, const unsigned int _colorEnd, bool horizontalGradient, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor)
	{
		const unsigned int color    = convertColor(_color);
		const unsigned int colorEnd = convertColor(_colorEnd);
		Vertex             vertices[4];

		vertices[0] = { { _x     ,_y      }, { 0.0f, 0.0f }, color };
		vertices[1] = { { _x     ,_y + _h }, { 0.0f, 0.0f }, horizontalGradient ? colorEnd : color };
		vertices[2] = { { _x + _w,_y      }, { 0.0f, 0.0f }, horizontalGradient ? color : colorEnd };
		vertices[3] = { { _x + _w,_y + _h }, { 0.0f, 0.0f }, colorEnd };

		// round vertices
		for(int i = 0; i < 4; ++i)
			vertices[i].pos.round();

		bindTexture(0);
		drawTriangleStrips(vertices, 4, _srcBlendFactor, _dstBlendFactor);

	} // drawRect

//////////////////////////////////////////////////////////////////////////

	SDL_Window* getSDLWindow()     { return sdlWindow; }
	int         getWindowWidth()   { return windowWidth; }
	int         getWindowHeight()  { return windowHeight; }
	int         getScreenWidth()   { return screenWidth; }
	int         getScreenHeight()  { return screenHeight; }
	int         getScreenOffsetX() { return screenOffsetX; }
	int         getScreenOffsetY() { return screenOffsetY; }
	int         getScreenRotate()  { return screenRotate; }

} // Renderer::
