#include "guis/GuiChangelog.h"
#include "InputConfig.h"
#include "renderers/Renderer.h"
#include "resources/Font.h"
#include <cstdio>
#include <fstream>
#include <sstream>

#define FLAG_FILE "/etc/.ota-changelog-pending"
#define CHANGELOG_FILE "/usr/share/retropangui/changelog.txt"

static std::string readFileFull(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GuiChangelog::GuiChangelog(Window* window)
    : GuiComponent(window),
      mBackground(window, ":/frame.png"),
      mTitle(window, "업데이트 완료!", Font::get(FONT_SIZE_LARGE), 0x555555FF, ALIGN_CENTER),
      mVersion(window, "", Font::get(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_CENTER),
      mScrollContainer(window),
      mBody(window, "", Font::get(FONT_SIZE_SMALL), 0x444444FF, ALIGN_LEFT)
{
    std::string ver = readFileFull(FLAG_FILE);
    while (!ver.empty() && (ver.back() == '\n' || ver.back() == '\r'))
        ver.pop_back();
    if (!ver.empty())
        mVersion.setText("버전: " + ver);

    std::string body = readFileFull(CHANGELOG_FILE);
    if (body.empty())
        body = "(변경 내역 없음)";
    mBody.setText(body);

    addChild(&mBackground);
    addChild(&mTitle);
    addChild(&mVersion);
    addChild(&mScrollContainer);
    mScrollContainer.addChild(&mBody);

    mScrollContainer.setAutoScroll(true);

    float sw = Renderer::getScreenWidth();
    float sh = Renderer::getScreenHeight();
    setSize(sw * 0.72f, sh * 0.82f);
    setPosition((sw - mSize.x()) / 2.0f, (sh - mSize.y()) / 2.0f);
}

void GuiChangelog::onSizeChanged()
{
    const float pad    = 20.0f;
    const float innerW = mSize.x() - pad * 2.0f;
    const float titleH = Font::get(FONT_SIZE_LARGE)->getHeight();
    const float verH   = Font::get(FONT_SIZE_MEDIUM)->getHeight();
    const float scrollH = mSize.y() - pad * 3.0f - titleH - verH;

    mBackground.setSize(mSize);

    mTitle.setSize(innerW, 0);
    mTitle.setPosition(pad, pad);

    mVersion.setSize(innerW, 0);
    mVersion.setPosition(pad, pad + titleH + 2.0f);

    mScrollContainer.setPosition(pad, pad + titleH + verH + pad);
    mScrollContainer.setSize(innerW, scrollH);

    mBody.setSize(innerW, 0);
}

bool GuiChangelog::input(InputConfig* config, Input input)
{
    if (input.value != 0 &&
        (config->isMappedTo("a", input) || config->isMappedTo("start", input)))
    {
        close();
        return true;
    }
    return GuiComponent::input(config, input);
}

std::vector<HelpPrompt> GuiChangelog::getHelpPrompts()
{
    return { { "a", "닫기" } };
}

void GuiChangelog::close()
{
    std::remove(FLAG_FILE);
    mWindow->removeGui(this);
    delete this;
}
