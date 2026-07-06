#include "MusicManager.h"

#include "Log.h"
#include "Settings.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <vlc/vlc.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>

std::shared_ptr<MusicManager> MusicManager::sInstance = nullptr;

// libvlc는 MIDI(SMF)의 표준 태그를 못 읽어 파일명을 그대로 Title로 돌려주므로,
// 트랙 안에 들어있는 메타 이벤트(FF 03=트랙명, FF 06=마커, FF 01=텍스트)를 직접
// 파싱해 실제 제목을 얻는다 - 이러면 파일명을 손으로 바꿀 필요 없이 새 MIDI를
// 넣기만 해도 제목이 자동으로 뜬다.
static uint32_t readMidiVarLen(const std::vector<uint8_t>& buf, size_t& p, size_t end)
{
	uint32_t val = 0;
	while (p < end)
	{
		uint8_t b = buf[p++];
		val = (val << 7) | (b & 0x7F);
		if (!(b & 0x80))
			break;
	}
	return val;
}

static bool isValidUtf8(const std::string& s)
{
	size_t i = 0, n = s.size();
	while (i < n)
	{
		unsigned char c = s[i];
		size_t extra;
		if ((c & 0x80) == 0) extra = 0;
		else if ((c & 0xE0) == 0xC0) extra = 1;
		else if ((c & 0xF0) == 0xE0) extra = 2;
		else if ((c & 0xF8) == 0xF0) extra = 3;
		else return false;
		if (i + extra >= n)
			return false;
		for (size_t j = 1; j <= extra; ++j)
			if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80)
				return false;
		i += extra + 1;
	}
	return true;
}

// FF 03(트랙명)은 다중 트랙 MIDI에서 흔히 악기 이름("piano", "drums" 등)이나
// "untitled"로 채워져 있어 곡 제목으로 부적합한 경우가 많음 - 그런 값은 버림.
static bool looksLikeGenericTrackLabel(const std::string& s)
{
	static const char* generic[] = { "untitled", "piano", "drums", "drum", "bass",
		"guitar", "organ", "track", "midi", "strings", "synth", "voice" };
	std::string lower = Utils::String::toLower(s);
	for (const char* g : generic)
		if (lower == g)
			return true;
	return false;
}

static std::string trimTrailing(std::string s)
{
	while (!s.empty() && (s.back() == '\0' || s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
		s.pop_back();
	return s;
}

static std::string readSmfTitle(const std::string& path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return "";

	std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (buf.size() < 14 || memcmp(buf.data(), "MThd", 4) != 0)
		return "";

	uint16_t numTracks = (static_cast<uint16_t>(buf[10]) << 8) | buf[11];
	size_t pos = 14;

	std::string trackName, marker, text;

	for (uint16_t t = 0; t < numTracks && pos + 8 <= buf.size(); ++t)
	{
		if (memcmp(&buf[pos], "MTrk", 4) != 0)
			break;
		uint32_t trackLen = (static_cast<uint32_t>(buf[pos + 4]) << 24) | (static_cast<uint32_t>(buf[pos + 5]) << 16) |
			(static_cast<uint32_t>(buf[pos + 6]) << 8) | buf[pos + 7];
		size_t trackStart = pos + 8;
		size_t trackEnd = trackStart + trackLen;
		if (trackEnd > buf.size())
			break;

		size_t p = trackStart;
		uint8_t runningStatus = 0;
		while (p < trackEnd)
		{
			readMidiVarLen(buf, p, trackEnd); // delta time, 사용 안 함
			if (p >= trackEnd)
				break;

			uint8_t status = buf[p];
			if (status == 0xFF) // 메타 이벤트
			{
				++p;
				if (p >= trackEnd)
					break;
				uint8_t metaType = buf[p++];
				uint32_t len = readMidiVarLen(buf, p, trackEnd);
				if (p + len > trackEnd)
					break;
				std::string s(reinterpret_cast<const char*>(&buf[p]), len);
				p += len;

				if (metaType == 0x03 && trackName.empty()) trackName = s;
				else if (metaType == 0x06 && marker.empty()) marker = s;
				else if (metaType == 0x01 && text.empty()) text = s;
				else if (metaType == 0x2F)
					break; // end of track
			}
			else if (status == 0xF0 || status == 0xF7) // sysex
			{
				++p;
				uint32_t len = readMidiVarLen(buf, p, trackEnd);
				p += len;
			}
			else
			{
				if (status & 0x80)
				{
					runningStatus = status;
					++p;
				}
				else
				{
					status = runningStatus; // running status - 데이터 바이트가 이미 p에 있음
				}
				uint8_t hi = status & 0xF0;
				p += (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
			}
		}
		pos = trackEnd;
	}

	marker = trimTrailing(marker);
	trackName = trimTrailing(trackName);
	text = trimTrailing(text);

	if (!marker.empty() && isValidUtf8(marker))
		return marker;
	if (!trackName.empty() && isValidUtf8(trackName) && !looksLikeGenericTrackLabel(trackName))
		return trackName;
	if (!text.empty() && isValidUtf8(text))
		return text;
	return "";
}

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

// MIDI 합성용 사운드폰트 탐색: <share>/music 의 .sf2 (사용자 교체용) 우선,
// 없으면 이미지 번들 MT-32 (bundled-bgmusic 패키지). 둘 다 없으면 빈 문자열.
static std::string findSoundfont(const std::string& musicDir)
{
	Utils::FileSystem::stringList files = Utils::FileSystem::getDirContent(musicDir);
	for (Utils::FileSystem::stringList::const_iterator it = files.cbegin(); it != files.cend(); ++it)
	{
		if (Utils::String::toLower(Utils::FileSystem::getExtension(*it)) == ".sf2")
			return *it;
	}

	static const char* BUNDLED_SF2 = "/usr/share/soundfonts/MT32.sf2";
	if (Utils::FileSystem::exists(BUNDLED_SF2))
		return BUNDLED_SF2;

	return std::string();
}

MusicManager::MusicManager() :
	mVLC(nullptr),
	mPlayer(nullptr),
	mCurrentIndex(0),
	mPlaying(false),
	mSoundfontActive(false)
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

	const std::string musicDir = getMusicDirectory();

	// MIDI는 사운드폰트가 있어야만 재생 가능 (VLC fluidsynth 플러그인)
	// 사운드폰트는 libvlc 인스턴스 생성 시점에 --soundfont 로 고정됨 —
	// 이후 sf2를 추가한 경우 ES 재시작 필요
	const std::string soundfont = findSoundfont(musicDir);

	// libvlc 인스턴스 lazy 생성
	if (mVLC == nullptr)
	{
		// --soundfont 는 fluidsynth 플러그인이 없는 VLC에서는 unknown option이라
		// libvlc_new 자체가 실패함 → 실패 시 사운드폰트 없이 재시도 (MIDI만 비활성)
		if (!soundfont.empty())
		{
			std::string sfArg = "--soundfont=" + soundfont;
			const char* sfArgs[] = { "--quiet", "--no-video", sfArg.c_str() };
			mVLC = libvlc_new(sizeof(sfArgs) / sizeof(sfArgs[0]), sfArgs);
			if (mVLC != nullptr)
			{
				mSoundfontActive = true;
				LOG(LogInfo) << "MusicManager: 사운드폰트 - " << soundfont;
			}
			else
				LOG(LogWarning) << "MusicManager: --soundfont 적용 실패 (VLC fluidsynth 플러그인 없음?) - MIDI 비활성";
		}
		if (mVLC == nullptr)
		{
			const char* args[] = { "--quiet", "--no-video" };
			mVLC = libvlc_new(sizeof(args) / sizeof(args[0]), args);
		}
		if (mVLC == nullptr)
		{
			LOG(LogError) << "MusicManager: libvlc 인스턴스 생성 실패";
			return;
		}
	}

	// 음악 폴더 스캔 (getDirContent 는 전체 경로를 반환)
	mPlaylist.clear();
	mCurrentIndex = 0;

	const bool midiOk = mSoundfontActive;
	Utils::FileSystem::stringList files = Utils::FileSystem::getDirContent(musicDir);
	for (Utils::FileSystem::stringList::const_iterator it = files.cbegin(); it != files.cend(); ++it)
	{
		std::string ext = Utils::String::toLower(Utils::FileSystem::getExtension(*it));
		if (ext == ".mp3" || ext == ".ogg" || ext == ".flac" || ext == ".wav" || ext == ".m4a")
			mPlaylist.push_back(*it);
		else if ((ext == ".mid" || ext == ".midi") && midiOk)
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

	// BackgroundMusic 설정이 꺼진 경우 즉시 정지 (어떤 경로로 재생이 시작됐더라도)
	if (!Settings::getInstance()->getBool("BackgroundMusic"))
	{
		stop();
		return;
	}

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

std::string MusicManager::getCurrentTrackTitle() const
{
	if (!mPlaying || mPlaylist.empty() || mCurrentIndex >= mPlaylist.size())
		return "";

	return mCurrentTitle;
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

	// ID3/Vorbis 태그 등에서 실제 곡 제목 파싱 시도 (동기 호출 - deprecated API지만
	// 로컬 파일은 즉시 반환됨). MIDI는 표준 메타데이터가 없어 libvlc가 파일명을
	// 그대로 Title로 돌려주므로, 그 경우엔 태그가 "없는" 것으로 보고 아래에서
	// SMF 내부 메타 텍스트(트랙명/마커)를 직접 읽어보고, 그것도 없으면 stem으로 대체.
	libvlc_media_parse(media);
	const char* tagTitle = libvlc_media_get_meta(media, libvlc_meta_Title);
	if (tagTitle != nullptr && tagTitle[0] != '\0' && Utils::FileSystem::getFileName(path) != tagTitle)
	{
		mCurrentTitle = tagTitle;
	}
	else
	{
		std::string ext = Utils::String::toLower(Utils::FileSystem::getExtension(path));
		std::string smfTitle = (ext == ".mid" || ext == ".midi") ? readSmfTitle(path) : "";
		mCurrentTitle = !smfTitle.empty() ? smfTitle : Utils::FileSystem::getStem(path);
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
