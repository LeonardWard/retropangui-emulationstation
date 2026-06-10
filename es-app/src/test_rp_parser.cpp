// 파서 단위 테스트 — ES 라이브러리 없이 단독 컴파일 가능
// 빌드: g++ -std=c++17 -o /tmp/test_rp_parser test_rp_parser.cpp RetropanguiFeatures.cpp
// 실행: /tmp/test_rp_parser

#include "RetropanguiFeatures.h"
#include <iostream>
#include <cassert>
#include <cstring>

static int g_fail = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; g_fail++; } } while(0)

int main()
{
    auto menus = RetropanguiFeatures::load();

    // ── 기본 개수 ────────────────────────────────────────────────
    CHECK(!menus.empty(), "menus is empty (yml 파일 없거나 파싱 실패)");
    if (menus.empty()) return 1;

    // ── 헬퍼 ─────────────────────────────────────────────────────
    auto findMenu = [&](const std::string& id) -> const FeatureMenu* {
        for (auto& m : menus) if (m.id == id) return &m;
        return nullptr;
    };
    auto findItem = [](const FeatureMenu& m, const std::string& id) -> const FeatureItem* {
        for (auto& i : m.items) if (i.id == id) return &i;
        return nullptr;
    };

    // ── system_settings ──────────────────────────────────────────
    const FeatureMenu* sys = findMenu("system_settings");
    CHECK(sys, "system_settings 메뉴 없음");
    if (sys) {
        CHECK(sys->label == "SYSTEM SETTINGS", "system_settings label");
        CHECK(sys->parent == "main",            "system_settings parent");

        const FeatureItem* lang = findItem(*sys, "rp.language");
        CHECK(lang, "rp.language 항목 없음");
        if (lang) {
            CHECK(lang->type     == "list",            "rp.language type");
            CHECK(lang->conf_key == "system.language", "rp.language conf_key");
            CHECK(lang->restart  == "es",              "rp.language restart");
            CHECK(lang->options.size() >= 3,           "rp.language options count");
            if (!lang->options.empty())
                CHECK(lang->options[0].value == "ko_KR", "rp.language 첫 번째 옵션");
        }

        const FeatureItem* tz = findItem(*sys, "rp.timezone");
        CHECK(tz, "rp.timezone 항목 없음");
        if (tz) {
            CHECK(tz->type     == "list",           "rp.timezone type");
            CHECK(tz->conf_key == "system.timezone", "rp.timezone conf_key");
            CHECK(tz->restart  == "system",          "rp.timezone restart");
        }
    }

    // ── network_settings ─────────────────────────────────────────
    const FeatureMenu* net = findMenu("network_settings");
    CHECK(net, "network_settings 메뉴 없음");
    if (net) {
        const FeatureItem* ssh = findItem(*net, "rp.ssh");
        CHECK(ssh, "rp.ssh 항목 없음");
        if (ssh) {
            CHECK(ssh->type     == "toggle",   "rp.ssh type");
            CHECK(ssh->conf_key == "system.ssh", "rp.ssh conf_key");
            CHECK(ssh->restart  == "system",   "rp.ssh restart");
        }
    }

    // ── sound_settings (slider) ───────────────────────────────────
    const FeatureMenu* snd = findMenu("sound_settings");
    CHECK(snd, "sound_settings 메뉴 없음");
    if (snd) {
        const FeatureItem* lat = findItem(*snd, "rp.audio_latency");
        CHECK(lat, "rp.audio_latency 항목 없음");
        if (lat) {
            CHECK(lat->type     == "slider",              "audio_latency type");
            CHECK(lat->conf_key == "global.audio_latency","audio_latency conf_key");
            CHECK(lat->min      == 16.0f,                 "audio_latency min");
            CHECK(lat->max      == 256.0f,                "audio_latency max");
            CHECK(lat->step     == 8.0f,                  "audio_latency step");
            CHECK(lat->unit     == "ms",                  "audio_latency unit");
            CHECK(lat->restart  == "none",                "audio_latency restart");
        }
    }

    // ── game_settings (toggle 3개) ───────────────────────────────
    const FeatureMenu* game = findMenu("game_settings");
    CHECK(game, "game_settings 메뉴 없음");
    if (game) {
        CHECK(findItem(*game, "rp.rewind"),             "rp.rewind 없음");
        CHECK(findItem(*game, "rp.savestate_auto_save"),"rp.savestate_auto_save 없음");
        CHECK(findItem(*game, "rp.savestate_auto_load"),"rp.savestate_auto_load 없음");
    }

    // ── advanced_settings (list) ─────────────────────────────────
    const FeatureMenu* adv = findMenu("advanced_settings");
    CHECK(adv, "advanced_settings 메뉴 없음");
    if (adv) {
        const FeatureItem* drv = findItem(*adv, "rp.input_joypad_driver");
        CHECK(drv, "rp.input_joypad_driver 없음");
        if (drv) {
            CHECK(drv->type     == "list",                       "joypad_driver type");
            CHECK(drv->conf_key == "global.input_joypad_driver", "joypad_driver conf_key");
            CHECK(drv->restart  == "system",                     "joypad_driver restart");
            CHECK(drv->options.size() >= 2,                      "joypad_driver options count");
        }
    }

    // ── 결과 ─────────────────────────────────────────────────────
    if (g_fail == 0) {
        std::cout << "ALL TESTS PASSED (" << menus.size() << " menus)\n";
        for (auto& m : menus) {
            std::cout << "  [" << m.id << "] " << m.label
                      << " — " << m.items.size() << " items\n";
            for (auto& item : m.items)
                std::cout << "    " << item.id << " (" << item.type << ") "
                          << item.conf_key << "\n";
        }
    } else {
        std::cerr << g_fail << " test(s) FAILED\n";
    }

    return g_fail == 0 ? 0 : 1;
}
