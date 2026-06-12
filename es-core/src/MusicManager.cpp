#include "MusicManager.h"

#include "Log.h"
#include "Settings.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <vlc/vlc.h>
#include <algorithm>
#include <cstdlib>
#include <random>

std::shared_ptr<MusicManager> MusicManager::sInstance = nullptr;

std::shared_ptr<MusicManager>& MusicManager::getInstance()
{
	if (sInstance == nullptr)
		sInstance = std::shared_ptr<MusicManager>(new MusicManager);
	return sInstance;
}

// RETROPANGUI_SHARE 환경 변수 → /share → ~/share 순서로 탐색 (GuiMenu.cpp 규칙과 동일)
static std::string getMusicDirectory()
{
	const char* env = getenv("RETROPANGUI_SHARE");
	if (env && env[0] != '\0')
		return std::string(env) + "/music";

	if (Utils::FileSystem::isDirectory("/share"))
		return "/share/music";

	const char* home = getenv("HOME");
	return (home ? std::string(home) + "/share" : "/share") + "/music";
}

MusicManager::MusicManager() :
	mVLC(nullptr),
	mPlayer(nullptr),
	mCurrentIndex(0),
	mPlaying(false)
{
}

MusicManager::~MusicManager()
{
	stop();
	if (mVLC != nullptr)
	{
		libvlc_release(mVLC);
		mVLC = nullptr;
	}
}

void MusicManager::start()
{
	if (!Settings::getInstance()->getBool("BackgroundMusic"))
		return;

	// 이미 재생 중이면 no-op
	if (mPlaying)
		return;

	// libvlc 인스턴스 lazy 생성
	if (mVLC == nullptr)
	{
		const char* args[] = { "--quiet", "--no-video" };
		mVLC = libvlc_new(sizeof(args) / sizeof(args[0]), args);
		if (mVLC == nullptr)
		{
			LOG(LogError) << "MusicManager: libvlc 인스턴스 생성 실패";
			return;
		}
	}

	// 음악 폴더 스캔 (getDirContent 는 전체 경로를 반환)
	const std::string musicDir = getMusicDirectory();
	mPlaylist.clear();
	mCurrentIndex = 0;

	Utils::FileSystem::stringList files = Utils::FileSystem::getDirContent(musicDir);
	for (Utils::FileSystem::stringList::const_iterator it = files.cbegin(); it != files.cend(); ++it)
	{
		std::string ext = Utils::String::toLower(Utils::FileSystem::getExtension(*it));
		if (ext == ".mp3" || ext == ".ogg" || ext == ".flac" || ext == ".wav" || ext == ".m4a")
			mPlaylist.push_back(*it);
	}

	if (mPlaylist.empty())
	{
		LOG(LogInfo) << "MusicManager: " << musicDir << " 에 음악 파일 없음 - BGM 비활성";
		return;
	}

	shufflePlaylist();
	playCurrent();
}

void MusicManager::stop()
{
	if (mPlayer != nullptr)
	{
		libvlc_media_player_stop(mPlayer);
		libvlc_media_player_release(mPlayer);
		mPlayer = nullptr;
	}
	mPlaying = false;
}

void MusicManager::update()
{
	if (!mPlaying || mPlayer == nullptr)
		return;

	// 이벤트 콜백 대신 폴링으로 종료 감지 (VideoVlcComponent::handleLooping 과 동일)
	libvlc_state_t state = libvlc_media_player_get_state(mPlayer);
	if (state == libvlc_Ended || state == libvlc_Error)
	{
		++mCurrentIndex;
		if (mCurrentIndex >= mPlaylist.size())
		{
			// 끝까지 재생했으면 재셔플, 직전 곡과 연속 중복 방지
			const std::string last = mPlaylist.back();
			shufflePlaylist();
			mCurrentIndex = 0;
			if (mPlaylist.size() >= 2 && mPlaylist[0] == last)
				std::swap(mPlaylist[0], mPlaylist[1]);
		}
		playCurrent();
	}
}

bool MusicManager::isPlaying() const
{
	return mPlaying;
}

void MusicManager::shufflePlaylist()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::shuffle(mPlaylist.begin(), mPlaylist.end(), gen);
}

void MusicManager::playCurrent()
{
	// 기존 player 해제
	if (mPlayer != nullptr)
	{
		libvlc_media_player_stop(mPlayer);
		libvlc_media_player_release(mPlayer);
		mPlayer = nullptr;
	}

	if (mVLC == nullptr || mCurrentIndex >= mPlaylist.size())
	{
		mPlaying = false;
		return;
	}

	const std::string& path = mPlaylist[mCurrentIndex];
	libvlc_media_t* media = libvlc_media_new_path(mVLC, path.c_str());
	if (media == nullptr)
	{
		LOG(LogError) << "MusicManager: media 생성 실패 - " << path;
		mPlaying = false;
		return;
	}

	// media 는 player 에 연결된 뒤 release 가능
	mPlayer = libvlc_media_player_new_from_media(media);
	libvlc_media_release(media);

	if (mPlayer == nullptr)
	{
		LOG(LogError) << "MusicManager: media player 생성 실패 - " << path;
		mPlaying = false;
		return;
	}

	libvlc_media_player_play(mPlayer);
	mPlaying = true;
	LOG(LogInfo) << "MusicManager: 재생 시작 - " << path;
}
