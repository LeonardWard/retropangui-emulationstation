#pragma once
#ifndef ES_CORE_MUSIC_MANAGER_H
#define ES_CORE_MUSIC_MANAGER_H

#include <memory>
#include <string>
#include <vector>

// libvlc 전방 선언 — vlc.h 는 MusicManager.cpp 에서만 include
struct libvlc_instance_t;
struct libvlc_media_player_t;

// 배경 음악(BGM) 재생 싱글턴. <share>/music 폴더를 스캔해 셔플 재생한다.
// 모든 호출은 메인 스레드에서만 일어난다 (mutex 불필요).
class MusicManager
{
	static std::shared_ptr<MusicManager> sInstance;

	MusicManager();

public:
	static std::shared_ptr<MusicManager>& getInstance();

	void start();   // 음악 폴더 스캔→셔플→첫 곡 재생. BackgroundMusic=false거나 파일 없으면 no-op
	void stop();    // 재생 중지 + media player 해제 (libvlc 인스턴스는 유지)
	void update();  // 매 프레임 호출: 트랙 종료(libvlc_Ended) 감지 시 다음 곡 재생
	bool isPlaying() const;

	virtual ~MusicManager(); // stop + libvlc 인스턴스 해제

private:
	void shufflePlaylist();
	void playCurrent();

	libvlc_instance_t* mVLC;
	libvlc_media_player_t* mPlayer;
	std::vector<std::string> mPlaylist;
	size_t mCurrentIndex;
	bool mPlaying;
	bool mSoundfontActive; // --soundfont 적용 성공 여부 (MIDI 재생 가능)
};

#endif // ES_CORE_MUSIC_MANAGER_H
