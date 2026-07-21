#pragma once
#ifndef ES_CORE_GUIS_GUI_ARCADE_VIRTUAL_KEYBOARD_H
#define ES_CORE_GUIS_GUI_ARCADE_VIRTUAL_KEYBOARD_H

#include "GuiComponent.h"
#include "resources/Font.h"
#include <functional>
#include <string>

// 컨트롤러 기반 원형 가상 키보드
// Recalbox GuiArcadeVirtualKeyboard를 표준 ES API로 포팅
class GuiArcadeVirtualKeyboard : public GuiComponent
{
public:
    using OkCallback     = std::function<void(const std::string&)>;
    using CancelCallback = std::function<void()>;

    GuiArcadeVirtualKeyboard(Window* window,
                              const std::string& title,
                              const std::string& initValue,
                              OkCallback okCallback,
                              CancelCallback cancelCallback = nullptr);

    bool input(InputConfig* config, Input input) override;
    void textInput(const char* text) override;
    void update(int deltaTime) override;
    void render(const Transform4x4f& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    // 6개 문자 휠 (세트)
    static const wchar_t* sWheelChars[6];
    static const char*    sWheelNames[6];
    static constexpr int  sWheelCount = 6;

    enum class Direction { Left, None, Right };

    // 휠 상태
    double mAngles[sWheelCount];       // 각 휠의 현재 각도
    int    mCurrentWheel;              // 현재 선택된 휠 인덱스
    int    mPreviousWheel;             // 이전 휠 (전환 애니메이션용)
    int    mWheelChangeAnim;           // 휠 전환 애니메이션 타이머 (ms)

    // 텍스트 상태
    std::wstring mText;                // 편집 중인 텍스트 (wide)
    int          mCursor;             // 커서 위치

    // 회전 애니메이션
    Direction mMoveDir;
    bool      mMoveFast;
    bool      mMoveOn;
    int       mMoveTimer;              // 키 반복 타이머

    // 2026-07-22: 회전 관성(사용자 요청) - 버튼을 떼도 즉시 멈추지 않고
    // 서서히 감속하다 가장 가까운 문자에 스냅됨.
    bool   mInertiaActive = false;
    double mAngleVelocity = 0.0;       // rad/ms, 마지막 회전 속도(감속 시작값)
    // 2026-07-22: 0.88는 너무 뻑뻑하다는 실기기 피드백 - 감쇠를 늦춰서
    // 더 오래, 더 유연하게 굴러가도록 함.
    static constexpr double sInertiaFriction = 0.95; // 16ms당 속도 유지 비율

    // 2026-07-22: 선택 문자(빨강)가 바뀔 때마다 진동(사용자 요청 - 버튼
    // 입력이 아니라 회전 중 새 문자에 걸릴 때마다). MENU RUMBLE 설정을
    // 그대로 재사용(InputManager::rumbleNav).
    int mLastRumbleIdx;
    int mLastDeviceId = -1;

    // 2026-07-22: L2/R2 트리거 세기별 회전 속도(사용자 요청) - ES 이벤트
    // 시스템이 축 값을 -1/0/1로 뭉개서 세기 정보가 없어(InputManager.cpp),
    // SDL 조이스틱 API로 매 프레임 원본 값을 직접 폴링(pollTriggers() 참고).
    // 세게 누르면 즉시 빠르고, 살짝 눌러도 계속 쥐고 있으면 시간이 지나며
    // 최고 속도까지 올라감(사용자 요청 - "지속적으로 누르면 가장 높은
    // 값이 되도록").
    bool mTrigRestCaptured = false;
    int  mLeftTrigRest  = 0;
    int  mRightTrigRest = 0;
    double mLeftTrigHoldMs  = 0.0;
    double mRightTrigHoldMs = 0.0;
    static constexpr int    sTrigAxisLeft   = 2;  // 이 패드 실측값 - 축 번호
    static constexpr int    sTrigAxisRight  = 5;
    static constexpr int    sTrigDeadzone   = 8000;
    static constexpr int    sTrigMaxMag     = 60000; // 대략적인 최대 편차
    static constexpr double sTrigSlowestMs  = 160.0; // 세기 최소일 때 1칸당 ms
    static constexpr double sTrigFastestMs  = 25.0;  // 세기/유지시간 최대일 때
    static constexpr double sTrigRampMs     = 900.0; // 이 시간만큼 쥐고 있으면 최고 속도 도달
    bool mWasTrigActive = false; // 트리거를 막 뗀 순간을 감지해 관성으로 넘기기 위함
    bool pollTriggers(int deltaTime);

    // 2026-07-22: 짧게 누르면 정확히 1칸만 이동, 2칸 이상 넘어갈 만큼
    // 길게 누르면 관성 회전으로 넘어가도록(사용자 요청 - 관성이 너무
    // 유연해서 짧은 입력으로 정확히 고르기 어렵다는 피드백).
    int mPressStartIdx = 0;

    // 콜백 및 제목
    OkCallback     mOkCallback;
    CancelCallback mCancelCallback;
    std::string    mTitle;

    // 폰트 - 선택 문자에서 멀어질수록 작아지도록 6단계
    std::shared_ptr<Font> mWheelFontFar;      // idxDist>=5 (가장 작게)
    std::shared_ptr<Font> mWheelFontMid;      // idxDist==4
    std::shared_ptr<Font> mWheelFont;         // idxDist==3 (기준 크기)
    std::shared_ptr<Font> mWheelFontNear2;    // idxDist==2
    std::shared_ptr<Font> mWheelFontNear;     // idxDist==1
    std::shared_ptr<Font> mWheelFontSelected; // idxDist==0 (가장 크게)
    std::shared_ptr<Font> mTextFont;          // 입력 텍스트 표시
    std::shared_ptr<Font> mHelpFont;          // 하단 도움말

    // 타이밍 상수 (ms)
    // 2026-07-22: 회전 중 이동이 답답하다는 피드백 - 기본 속도를 올림.
    static constexpr int sRotateSlowMs    = 90;
    static constexpr int sRotateFastMs    = 80;
    static constexpr int sFirstRepeatMs   = 500;
    static constexpr int sRepeatMs        = 150;
    static constexpr int sWheelChangeMs   = 250;

    // 내부 메서드
    int    getCharCount(int wheelIdx) const;
    int    getCurrentCharIndex(int wheelIdx) const;
    wchar_t getCurrentChar() const;
    void   addCurrentChar();
    void   backspace();
    void   deleteChar();
    void   changeWheel(int delta);
    void   startMoving(bool left, bool fast);
    void   stopMoving();

    // UTF-8 변환
    std::string wcharToUtf8(wchar_t wc) const;
    std::string wstrToUtf8(const std::wstring& ws) const;

    // 렌더링
    void renderBackground(const Transform4x4f& trans);
    void renderCurrentText(const Transform4x4f& trans);
    void renderWheel(const Transform4x4f& trans, int wheelIdx, double dimAlpha);
    void renderHelpBar(const Transform4x4f& trans);
    struct HelpEntry;
    void renderHelpRow(const HelpEntry* entries, int entryCount, float y);
    unsigned int blendColor(unsigned int a, unsigned int b, double t) const;
};

struct GuiArcadeVirtualKeyboard::HelpEntry { std::string icon; const char* label; };

#endif // ES_CORE_GUIS_GUI_ARCADE_VIRTUAL_KEYBOARD_H
