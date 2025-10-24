#pragma once

#include <string>
#include <libintl.h>
#include <locale.h>

// Translation macros
#define _(String) LocaleES::translate(String)
#define _U(String) LocaleES::translate(String).c_str()

/*!
 * @brief Simple locale management for EmulationStation
 * Uses GNU gettext for internationalization
 */
class LocaleES
{
public:
    /*!
     * @brief Initialize locale system
     * @param language Language code (e.g., "ko_KR", "en_US")
     * @return True if locale was initialized successfully
     */
    static bool init(const std::string& language);

    /*!
     * @brief Translate a string
     * @param msgid String to translate
     * @return Translated string or original if no translation found
     */
    static std::string translate(const char* msgid);

    /*!
     * @brief Get current language code
     * @return Current language code (e.g., "ko_KR")
     */
    static std::string getLanguage();

private:
    static std::string sLanguage;
    static bool sInitialized;
};
