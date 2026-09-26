#pragma once

#include <string>
#include <map>

class LanguageManager {
private:
    std::map<std::string, std::string> m_texts;
    static LanguageManager* m_instance;
    LanguageManager() {}

public:
    static LanguageManager* instance();

    // Ahora recibe std::string (ej: "lang/es.ini")
    bool loadLanguage(const std::string& filename);

    // Return the translated text for the requested key.
    std::string get(const std::string& key);
};