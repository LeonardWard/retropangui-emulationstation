#include "LocaleES.h"
#include "Log.h"
#include "platform.h"
#include <cstdlib>

std::string LocaleES::sLanguage = "en_US";
bool LocaleES::sInitialized = false;

bool LocaleES::init(const std::string& language)
{
    if (language.empty() || language == "en" || language == "en_US")
    {
        // English is the default, no translation needed
        sLanguage = "en_US";
        sInitialized = true;
        LOG(LogInfo) << "Locale: Using default language (English)";
        return true;
    }

    // Set locale for the process
    std::string locale = language + ".UTF-8";
    const char* result = setlocale(LC_ALL, locale.c_str());

    if (!result)
    {
        LOG(LogWarning) << "Locale: Failed to set locale to " << locale << ", trying without .UTF-8";
        result = setlocale(LC_ALL, language.c_str());
    }

    if (!result)
    {
        LOG(LogError) << "Locale: Failed to set locale to " << language;
        // Fallback to C locale
        setlocale(LC_ALL, "C");
        sLanguage = "en_US";
        sInitialized = false;
        return false;
    }

    LOG(LogInfo) << "Locale: Set to " << result;

    // Set up gettext
    // RetroPangui: Use system locale directory
    std::string localeDir = "/usr/local/share/locale";
    bindtextdomain("emulationstation", localeDir.c_str());
    bind_textdomain_codeset("emulationstation", "UTF-8");
    textdomain("emulationstation");

    LOG(LogInfo) << "Locale: Using locale directory " << localeDir;

    sLanguage = language;
    sInitialized = true;

    LOG(LogInfo) << "Locale: Initialized with language " << language;
    return true;
}

std::string LocaleES::translate(const char* msgid)
{
    if (!sInitialized || sLanguage == "en_US")
    {
        // No translation needed for English
        return std::string(msgid);
    }

    const char* translated = gettext(msgid);
    return std::string(translated);
}

std::string LocaleES::getLanguage()
{
    return sLanguage;
}
