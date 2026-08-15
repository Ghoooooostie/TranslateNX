#pragma once
#include <string>

enum class OcrApi {
    OcrSpace,
    GoogleVision,
    OpenAiVision
};

enum class TranslateApi {
    MyMemory,
    DeepL,
    GoogleCloud,
    OpenAiTranslate
};

struct Config {
    OcrApi        ocrApi       = OcrApi::OcrSpace;
    TranslateApi  translateApi = TranslateApi::MyMemory;
    std::string   srcLang      = "ja";      // kaynak dil (ja, en, ...)
    std::string   dstLang      = "tr";      // hedef dil
    std::string   uiLang       = "tr";      // arayüz dili (tr, en)
    
    std::string   ocrApiKey;                // OCR.space
    std::string   visionApiKey;             // Google Vision

    // OpenAI 兼容模型 (自定义端点)
    std::string   openaiApiKey;             // Bearer token (sk-...)
    std::string   openaiBaseUrl;            // 自定义基址, 如 https://api.openai.com/v1
    std::string   openaiModel;              // 视觉模型名 (OCR 用), 如 gpt-4o / 自建模型
    std::string   openaiTransModel;         // 翻译模型名, 如 gpt-4o-mini / 自建模型
    
    std::string   deeplApiKey;              // DeepL
    std::string   googleTransApiKey;        // Google Cloud Translate
};

// SD:/config/translate/config.ini
namespace ConfigManager {
    Config load();
    void   save(const Config& cfg);

    // Switch'in kendi sanal klavyesini açar, kullanıcıdan string alır
    // title: Klavye üstünde gösterilecek başlık
    // out:   Kullanıcının girdiği string (çıktı)
    // Dönüş: true → kullanıcı OK'ladı, false → iptal
    bool promptKeyboard(const char* title, std::string& out);
}
