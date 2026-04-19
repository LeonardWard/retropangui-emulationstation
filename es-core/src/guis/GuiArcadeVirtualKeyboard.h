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

    // 콜백 및 제목
    OkCallback     mOkCallback;
    CancelCallback mCancelCallback;
    std::string    mTitle;

    // 폰트
    std::shared_ptr<Font> mWheelFont;         // 휠 일반 문자
    std::shared_ptr<Font> mWheelFontSelected; // 선택된 문자 (크게)
    std::shared_ptr<Font> mTextFont;          // 입력 텍스트 표시
    std::shared_ptr<Font> mHelpFont;          // 하단 도움말

    // 타이밍 상수 (ms)
    static constexpr int sRotateSlowMs    = 150;
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
    unsigned int blendColor(unsigned int a, unsigned int b, double t) const;
};

#endif // ES_CORE_GUIS_GUI_ARCADE_VIRTUAL_KEYBOARD_H
