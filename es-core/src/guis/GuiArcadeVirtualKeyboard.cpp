#include "guis/GuiArcadeVirtualKeyboard.h"

#include "InputConfig.h"
#include "Log.h"
#include "renderers/Renderer.h"
#include <SDL_events.h>
#include <SDL_keyboard.h>
#include <cmath>
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// 6개 문자 휠 정의
// ---------------------------------------------------------------------------
const wchar_t* GuiArcadeVirtualKeyboard::sWheelChars[6] = {
    L"abcdefghijklmnopqrstuvwxyz ",                    // 0: 영문 소문자 (기본)
    L"ABCDEFGHIJKLMNOPQRSTUVWXYZ ",                    // 1: 영문 대문자
    L"0123456789+-*/=.,;:!?\\",                        // 2: 숫자/연산자
    L"$&#@%_|<>{}()[]'`\"",                            // 3: 특수문자
    L"áàåâäãéèêëíìîïóòôöõøúùûüýñç",                   // 4: 발음기호 소문자
    L"ÁÀÅÂÄÃÉÈÊËÍÌÎÏÓÒÔÖÕØÚÙÛÜÝÑÇ",                   // 5: 발음기호 대문자
};

const char* GuiArcadeVirtualKeyboard::sWheelNames[6] = {
    "abc", "ABC", "123", "#@!", "àáâ", "ÀÁÂ"
};

// ---------------------------------------------------------------------------
// 생성자
// ---------------------------------------------------------------------------
GuiArcadeVirtualKeyboard::GuiArcadeVirtualKeyboard(
    Window* window,
    const std::string& title,
    const std::string& initValue,
    OkCallback okCallback,
    CancelCallback cancelCallback)
    : GuiComponent(window),
      mCurrentWheel(0),
      mPreviousWheel(0),
      mWheelChangeAnim(0),
      mCursor(0),
      mMoveDir(Direction::None),
      mMoveFast(false),
      mMoveOn(false),
      mMoveTimer(0),
      mOkCallback(okCallback),
      mCancelCallback(cancelCallback),
      mTitle(title)
{
    // 각도 초기화
    for (int i = 0; i < sWheelCount; i++)
        mAngles[i] = 0.0;

    // initValue를 wstring으로 변환 (간단한 ASCII 처리)
    for (unsigned char c : initValue)
        mText += (wchar_t)c;
    mCursor = (int)mText.size();

    // 폰트 크기 계산
    unsigned int screenH = Renderer::getScreenHeight();
    unsigned int screenW = Renderer::getScreenWidth();
    unsigned int baseSize = (unsigned int)(0.055f * std::min(screenH, screenW));

    mWheelFont         = Font::get(baseSize);
    mWheelFontSelected = Font::get((unsigned int)(baseSize * 1.6f));
    mTextFont          = Font::get((unsigned int)(baseSize * 0.7f));
    mHelpFont          = Font::get((unsigned int)(baseSize * 0.5f));

    // 전체 화면 크기로 설정
    setSize((float)screenW, (float)screenH);
    setPosition(0, 0);
}

// ---------------------------------------------------------------------------
// 유틸리티
// ---------------------------------------------------------------------------
int GuiArcadeVirtualKeyboard::getCharCount(int wheelIdx) const
{
    return (int)wcslen(sWheelChars[wheelIdx]);
}

int GuiArcadeVirtualKeyboard::getCurrentCharIndex(int wheelIdx) const
{
    int count = getCharCount(wheelIdx);
    double section = (2.0 * M_PI) / (double)count;
    double pos = ((2.0 * M_PI) - mAngles[wheelIdx]) / section;
    int idx = (int)pos;
    if ((pos - (double)idx) >= 0.5) idx++;
    return ((idx % count) + count) % count;
}

wchar_t GuiArcadeVirtualKeyboard::getCurrentChar() const
{
    int idx = getCurrentCharIndex(mCurrentWheel);
    return sWheelChars[mCurrentWheel][idx];
}

std::string GuiArcadeVirtualKeyboard::wcharToUtf8(wchar_t wc) const
{
    std::string r;
    unsigned int cp = (unsigned int)wc;
    if      (cp < 0x80)    { r += (char)cp; }
    else if (cp < 0x800)   { r += (char)(0xC0|(cp>>6)); r += (char)(0x80|(cp&0x3F)); }
    else if (cp < 0x10000) { r += (char)(0xE0|(cp>>12)); r += (char)(0x80|((cp>>6)&0x3F)); r += (char)(0x80|(cp&0x3F)); }
    return r;
}

std::string GuiArcadeVirtualKeyboard::wstrToUtf8(const std::wstring& ws) const
{
    std::string r;
    for (wchar_t wc : ws) r += wcharToUtf8(wc);
    return r;
}

unsigned int GuiArcadeVirtualKeyboard::blendColor(unsigned int a, unsigned int b, double t) const
{
    auto blend = [&](int ca, int cb) -> int {
        return (int)((double)ca * (1.0 - t) + (double)cb * t);
    };
    int ra = (a>>24)&0xFF, ga = (a>>16)&0xFF, ba_= (a>>8)&0xFF, aa = a&0xFF;
    int rb = (b>>24)&0xFF, gb = (b>>16)&0xFF, bb_= (b>>8)&0xFF, ab = b&0xFF;
    return ((unsigned int)blend(ra,rb)<<24)|((unsigned int)blend(ga,gb)<<16)|
           ((unsigned int)blend(ba_,bb_)<<8)|(unsigned int)blend(aa,ab);
}

// ---------------------------------------------------------------------------
// 액션 메서드
// ---------------------------------------------------------------------------
void GuiArcadeVirtualKeyboard::addCurrentChar()
{
    wchar_t c = getCurrentChar();
    if (mCursor <= (int)mText.size())
        mText.insert(mText.begin() + mCursor, c);
    else
        mText += c;
    mCursor++;
}

void GuiArcadeVirtualKeyboard::backspace()
{
    if (mCursor > 0 && !mText.empty())
    {
        mText.erase(mText.begin() + mCursor - 1);
        mCursor--;
    }
}

void GuiArcadeVirtualKeyboard::deleteChar()
{
    if (mCursor < (int)mText.size())
        mText.erase(mText.begin() + mCursor);
}

void GuiArcadeVirtualKeyboard::changeWheel(int delta)
{
    mPreviousWheel  = mCurrentWheel;
    mCurrentWheel   = ((mCurrentWheel + delta) % sWheelCount + sWheelCount) % sWheelCount;
    mWheelChangeAnim = sWheelChangeMs;
}

void GuiArcadeVirtualKeyboard::startMoving(bool left, bool fast)
{
    mMoveDir   = left ? Direction::Left : Direction::Right;
    mMoveFast  = fast;
    mMoveOn    = true;
    mMoveTimer = sFirstRepeatMs;
}

void GuiArcadeVirtualKeyboard::stopMoving()
{
    mMoveOn  = false;
    mMoveDir = Direction::None;
}

// ---------------------------------------------------------------------------
// Keyboard text input (SDL_TEXTINPUT 이벤트 → 직접 문자 입력)
// ---------------------------------------------------------------------------
void GuiArcadeVirtualKeyboard::textInput(const char* text)
{
    // UTF-8 문자열을 한 코드포인트씩 wchar_t로 변환해 삽입
    const unsigned char* p = (const unsigned char*)text;
    while (*p)
    {
        wchar_t wc = 0;
        if ((*p & 0x80) == 0)
        {
            wc = (wchar_t)*p++;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            wc = (wchar_t)((*p++ & 0x1F) << 6);
            wc |= (wchar_t)(*p++ & 0x3F);
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            wc = (wchar_t)((*p++ & 0x0F) << 12);
            wc |= (wchar_t)((*p++ & 0x3F) << 6);
            wc |= (wchar_t)(*p++ & 0x3F);
        }
        else
        {
            p++; // unsupported, skip
            continue;
        }

        if (wc == L'\b') // backspace via textInput
        {
            backspace();
        }
        else if (wc >= 0x20) // 출력 가능한 문자
        {
            if (mCursor <= (int)mText.size())
                mText.insert(mText.begin() + mCursor, wc);
            else
                mText += wc;
            mCursor++;
        }
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool GuiArcadeVirtualKeyboard::input(InputConfig* config, Input input)
{
    bool pressed  = (input.value != 0);
    bool released = (input.value == 0);

    // ── 키보드 (TYPE_KEY) ────────────────────────────────────────────────────
    // 문자 삽입은 SDL_TEXTINPUT → textInput() 에서 처리.
    // 여기서는 내비게이션 키만 처리하여 이중 입력을 방지한다.
    if (input.type == TYPE_KEY)
    {
        if (!pressed) return true; // key-up 소비
        switch (input.id)
        {
        case SDLK_RETURN:
            if (mOkCallback) mOkCallback(wstrToUtf8(mText));
            delete this; return true;
        case SDLK_ESCAPE:
            if (mCancelCallback) mCancelCallback();
            delete this; return true;
        // SDLK_BACKSPACE: InputManager가 textInput("\b")로 이미 전달
        case SDLK_DELETE: deleteChar(); return true;
        case SDLK_LEFT:   mCursor = std::max(0, mCursor - 1); return true;
        case SDLK_RIGHT:  mCursor = std::min((int)mText.size(), mCursor + 1); return true;
        case SDLK_HOME:   mCursor = 0; return true;
        case SDLK_END:    mCursor = (int)mText.size(); return true;
        default:          return true; // 나머지는 SDL_TEXTINPUT 이 처리
        }
    }

    // ── 게임패드 ─────────────────────────────────────────────────────────────
    // Start → 확인
    if (config->isMappedTo("start", input) && pressed)
    { if (mOkCallback) mOkCallback(wstrToUtf8(mText)); delete this; return true; }

    // B/Back → 취소
    if ((config->isMappedToAction("back", input) || config->isMappedTo("select", input)) && pressed)
    { if (mCancelCallback) mCancelCallback(); delete this; return true; }

    // A/Accept → 문자 추가
    if (config->isMappedToAction("accept", input) && pressed)
    { addCurrentChar(); return true; }

    // Y → 백스페이스
    if (config->isMappedTo("y", input) && pressed) { backspace(); return true; }

    // X → Delete
    if (config->isMappedTo("x", input) && pressed) { deleteChar(); return true; }

    // Up/Down → 휠 세트 전환
    if (config->isMappedLike("up", input) && pressed)   { changeWheel(-1); return true; }
    if (config->isMappedLike("down", input) && pressed) { changeWheel(1);  return true; }

    // L1/LB/L2 → 커서 처음, R1/RB/R2 → 커서 끝
    // es_input.cfg 에 따라 컨트롤러마다 L1이 leftshoulder 또는 lefttrigger 로
    // 등록될 수 있으므로 양쪽 모두 처리한다.
    if ((config->isMappedLike("leftshoulder", input) || config->isMappedTo("lefttrigger", input)) && pressed)
    { mCursor = 0; return true; }
    if ((config->isMappedLike("rightshoulder", input) || config->isMappedTo("righttrigger", input)) && pressed)
    { mCursor = (int)mText.size(); return true; }

    // Left/Right D-pad / 아날로그 → 휠 회전
    if (config->isMappedLike("left", input))
    { if (pressed) startMoving(true, false); else if (released) stopMoving(); return true; }
    if (config->isMappedLike("right", input))
    { if (pressed) startMoving(false, false); else if (released) stopMoving(); return true; }

    return true;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void GuiArcadeVirtualKeyboard::update(int deltaTime)
{
    // 휠 전환 애니메이션
    if (mWheelChangeAnim > 0)
        mWheelChangeAnim = std::max(0, mWheelChangeAnim - deltaTime);

    // 회전 처리
    if (mMoveOn && mMoveDir != Direction::None)
    {
        int count = getCharCount(mCurrentWheel);
        double section = (2.0 * M_PI) / (double)count;
        double speed = mMoveFast ? sRotateFastMs : sRotateSlowMs;
        double direction = (mMoveDir == Direction::Left) ? 1.0 : -1.0;

        mAngles[mCurrentWheel] += direction * section * (double)deltaTime / speed;

        // 각도 범위 유지
        while (mAngles[mCurrentWheel] >  2.0 * M_PI) mAngles[mCurrentWheel] -= 2.0 * M_PI;
        while (mAngles[mCurrentWheel] <  0.0)         mAngles[mCurrentWheel] += 2.0 * M_PI;
    }
    else if (mMoveDir != Direction::None)
    {
        // 버튼 뗐을 때 가장 가까운 문자로 스냅
        int count = getCharCount(mCurrentWheel);
        double section = (2.0 * M_PI) / (double)count;
        double pos = mAngles[mCurrentWheel] / section;
        double nearest = std::round(pos) * section;
        double diff = nearest - mAngles[mCurrentWheel];
        if (std::abs(diff) < 0.001)
        {
            mAngles[mCurrentWheel] = nearest;
            while (mAngles[mCurrentWheel] >= 2.0 * M_PI) mAngles[mCurrentWheel] -= 2.0 * M_PI;
            while (mAngles[mCurrentWheel] <  0.0)         mAngles[mCurrentWheel] += 2.0 * M_PI;
            mMoveDir = Direction::None;
        }
        else
        {
            mAngles[mCurrentWheel] += diff * 0.25;
        }
    }
}

// ---------------------------------------------------------------------------
// Render 헬퍼
// ---------------------------------------------------------------------------
void GuiArcadeVirtualKeyboard::renderBackground(const Transform4x4f& trans)
{
    Renderer::setMatrix(trans);
    float w = (float)Renderer::getScreenWidth();
    float h = (float)Renderer::getScreenHeight();
    Renderer::drawRect(0.f, 0.f, w, h, 0x000000CC, 0x000000CC);
}

void GuiArcadeVirtualKeyboard::renderCurrentText(const Transform4x4f& trans)
{
    Renderer::setMatrix(trans);
    float screenW = (float)Renderer::getScreenWidth();
    float screenH = (float)Renderer::getScreenHeight();

    // 제목
    {
        Vector2f titleSize = mTextFont->sizeText(mTitle);
        float tx = (screenW - titleSize.x()) / 2.f;
        float ty = screenH * 0.05f;
        TextCache* tc = mTextFont->buildTextCache(mTitle, tx, ty, 0xAAAAAAFF);
        mTextFont->renderTextCache(tc);
        delete tc;
    }

    // 현재 입력 텍스트
    {
        std::string displayText = wstrToUtf8(mText);
        if (displayText.empty()) displayText = " ";
        Vector2f textSize = mTextFont->sizeText(displayText);
        float tx = (screenW - textSize.x()) / 2.f;
        float ty = screenH * 0.12f;
        TextCache* tc = mTextFont->buildTextCache(displayText, tx, ty, 0xFFFFFFFF);
        mTextFont->renderTextCache(tc);
        delete tc;

        // 커서 (수직선)
        std::string beforeCursor = wstrToUtf8(mText.substr(0, mCursor));
        float cursorX = tx + mTextFont->sizeText(beforeCursor).x();
        float cursorH = mTextFont->getHeight();
        Renderer::drawRect(cursorX, ty, 2.f, cursorH, 0xFFFFFFFF, 0xFFFFFFFF);
    }

    // 휠 세트 이름 표시
    float nameY = screenH * 0.22f;
    float nameSpacing = screenW / (float)(sWheelCount + 1);
    for (int i = 0; i < sWheelCount; i++)
    {
        float nx = nameSpacing * (i + 1);
        unsigned int col = (i == mCurrentWheel) ? 0xFFFFFFFF : 0x555555FF;
        std::string name = sWheelNames[i];
        Vector2f ns = mTextFont->sizeText(name);
        TextCache* tc = mTextFont->buildTextCache(name, nx - ns.x()/2, nameY, col);
        mTextFont->renderTextCache(tc);
        delete tc;
    }
}

void GuiArcadeVirtualKeyboard::renderWheel(const Transform4x4f& trans, int wheelIdx, double dimAlpha)
{
    Renderer::setMatrix(trans);

    float screenW = (float)Renderer::getScreenWidth();
    float screenH = (float)Renderer::getScreenHeight();
    float centerX = screenW / 2.f;
    float centerY = screenH * 0.62f;  // 화면 아래쪽에 배치
    float xRadius = screenW  * 0.38f;
    float yRadius = screenH  * 0.28f;

    int count = getCharCount(wheelIdx);
    int selectedIdx = getCurrentCharIndex(wheelIdx);
    double section = (2.0 * M_PI) / (double)count;
    double morphRange = 2.0 * section;

    for (int i = 0; i < count; i++)
    {
        double angle = (2.0 * M_PI * (double)i / (double)count) + mAngles[wheelIdx];
        // 각도 정규화
        while (angle >  2.0 * M_PI) angle -= 2.0 * M_PI;
        while (angle <  0.0)        angle += 2.0 * M_PI;

        float x = (float)(cos(angle - M_PI / 2.0) * xRadius + centerX);
        float y = (float)(sin(angle - M_PI / 2.0) * yRadius + centerY);

        // 화면 밖 문자 스킵
        if (y < screenH * 0.27f || y > screenH * 0.97f) continue;

        // 선택 문자와의 거리 계산 (색상/크기 모핑용)
        double dist = std::abs(angle - M_PI / 2.0);
        if (dist > M_PI) dist = 2.0 * M_PI - dist;
        double ratio = std::max(0.0, 1.0 - dist / morphRange);

        // 색상: 회색 → 흰색 → 빨강(선택)
        unsigned int baseColor = 0x444444FF;
        unsigned int midColor  = 0xCCCCCCFF;
        unsigned int selColor  = 0xFF4444FF;
        unsigned int color;
        if (i == selectedIdx)
            color = selColor;
        else
            color = blendColor(baseColor, midColor, ratio);

        // dimAlpha 적용
        unsigned int alpha = (unsigned int)(255.0 * dimAlpha);
        color = (color & 0xFFFFFF00) | (((color & 0xFF) * alpha) / 255);

        // 선택 문자는 큰 폰트, 나머지는 일반 폰트
        auto& font = (i == selectedIdx) ? mWheelFontSelected : mWheelFont;
        std::string charStr = wcharToUtf8(sWheelChars[wheelIdx][i]);
        Vector2f charSize = font->sizeText(charStr);
        TextCache* tc = font->buildTextCache(charStr,
            x - charSize.x() / 2.f,
            y - charSize.y() / 2.f,
            color);
        font->renderTextCache(tc);
        delete tc;
    }
}

// ---------------------------------------------------------------------------
// Render 메인
// ---------------------------------------------------------------------------
void GuiArcadeVirtualKeyboard::render(const Transform4x4f& parentTrans)
{
    Transform4x4f trans = parentTrans * getTransform();

    renderBackground(trans);
    renderCurrentText(trans);

    // 휠 전환 애니메이션 중이면 이전 휠도 페이드 아웃
    if (mWheelChangeAnim > 0)
    {
        double t = (double)mWheelChangeAnim / (double)sWheelChangeMs;
        renderWheel(trans, mPreviousWheel, t);
        renderWheel(trans, mCurrentWheel,  1.0 - t);
    }
    else
    {
        renderWheel(trans, mCurrentWheel, 1.0);
    }

    renderHelpBar(trans);
}

// ---------------------------------------------------------------------------
// Help bar – 하단에 직접 렌더링 (ES help bar 위에 겹쳐서 덮음)
// ---------------------------------------------------------------------------
void GuiArcadeVirtualKeyboard::renderHelpBar(const Transform4x4f& trans)
{
    Renderer::setMatrix(trans);
    float screenW = (float)Renderer::getScreenWidth();
    float screenH = (float)Renderer::getScreenHeight();

    float barH = mHelpFont->getHeight() * 1.6f;
    float barY = screenH - barH;

    // 기존 ES help bar를 덮는 반투명 어두운 배경
    Renderer::drawRect(0.f, barY, screenW, barH, 0x00000000, 0x000000E0);

    // 도움말 항목: [아이콘문자] 텍스트 형태로 나열
    struct HelpEntry { const char* icon; const char* label; };
    static const HelpEntry entries[] = {
        { "◀▶", "회전" },
        { "▲▼", "세트" },
        { "A",  "입력" },
        { "Y",  "삭제" },
        { "LB/RB", "처음/끝" },
        { "Start", "확인" },
        { "B",  "취소" },
    };
    static const int entryCount = (int)(sizeof(entries) / sizeof(entries[0]));

    // 전체 너비 계산 후 중앙 정렬
    float totalW = 0.f;
    float spacing = screenW * 0.018f;
    float sepW    = screenW * 0.012f;
    struct Segment { float iconW; float labelW; };
    std::vector<Segment> segs;
    for (int i = 0; i < entryCount; i++)
    {
        float iw = mHelpFont->sizeText(entries[i].icon).x();
        float lw = mHelpFont->sizeText(entries[i].label).x();
        segs.push_back({ iw, lw });
        totalW += iw + spacing + lw;
        if (i < entryCount - 1) totalW += sepW * 3.f;
    }

    float x = (screenW - totalW) / 2.f;
    float textY = barY + (barH - mHelpFont->getHeight()) / 2.f;

    for (int i = 0; i < entryCount; i++)
    {
        // 아이콘 (노란색)
        TextCache* ic = mHelpFont->buildTextCache(entries[i].icon, x, textY, 0xFFDD44FF);
        mHelpFont->renderTextCache(ic);
        delete ic;
        x += segs[i].iconW + spacing;

        // 레이블 (흰색)
        TextCache* lc = mHelpFont->buildTextCache(entries[i].label, x, textY, 0xFFFFFFFF);
        mHelpFont->renderTextCache(lc);
        delete lc;
        x += segs[i].labelW;

        // 구분자
        if (i < entryCount - 1)
        {
            TextCache* sc = mHelpFont->buildTextCache(" | ", x, textY, 0x444444FF);
            mHelpFont->renderTextCache(sc);
            delete sc;
            x += sepW * 3.f;
        }
    }
}

// ---------------------------------------------------------------------------
// Help Prompts – ES 기본 help bar 비활성화 (우리가 직접 그림)
// ---------------------------------------------------------------------------
std::vector<HelpPrompt> GuiArcadeVirtualKeyboard::getHelpPrompts()
{
    return {}; // 빈 벡터 → ES help bar 내용 없음
}
