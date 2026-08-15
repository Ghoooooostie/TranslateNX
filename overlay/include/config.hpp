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

// OCR 识别区域（归一化坐标 0..1，相对 1280x720 整图）
// 默认识别最底部的对话框区域
struct OcrRegion {
    float x = 0.0f;   // 左  0..1
    float y = 0.55f;  // 上  0..1（默认底部对话框起始处）
    float w = 1.0f;   // 宽  0..1
    float h = 0.45f;  // 高  0..1（覆盖下方文字块 + はい/いいえ 按钮区）

    bool enabled = false;  // 默认全屏识别；开启后只识别指定局部区域

    // 把归一化区域转成整图像素矩形（srcW=1280, srcH=720）
    void toPixels(int srcW, int srcH, int& outX, int& outY, int& outW, int& outH) const {
        outX = (int)(x * srcW);
        outY = (int)(y * srcH);
        outW = (int)(w * srcW);
        outH = (int)(h * srcH);
        if (outX < 0) outX = 0;
        if (outY < 0) outY = 0;
        if (outX + outW > srcW) outW = srcW - outX;
        if (outY + outH > srcH) outH = srcH - outY;
        if (outW < 1) outW = 1;
        if (outH < 1) outH = 1;
    }
};

struct Config {
    OcrApi        ocrApi       = OcrApi::OcrSpace;
    TranslateApi  translateApi = TranslateApi::MyMemory;
    std::string   srcLang      = "ja";      // kaynak dil (ja, en, ...)
    std::string   dstLang      = "tr";      // hedef dil
    std::string   uiLang       = "tr";      // arayüz dili (tr, en)
    
    std::string   ocrApiKey;                // OCR.space
    std::string   visionApiKey;             // Google Vision

    // OpenAI 兼容视觉模型 (OCR 用，独立 key/base)
    std::string   openaiVisionApiKey;       // 视觉模型 Bearer token (sk-...)
    std::string   openaiVisionBaseUrl;      // 视觉模型基址, 如 https://api.openai.com/v1
    std::string   openaiModel;              // 视觉模型名 (OCR 用), 如 gpt-4o / 自建模型

    // OpenAI 兼容翻译模型 (独立 key/base)
    std::string   openaiTransApiKey;        // 翻译模型 Bearer token (sk-...)
    std::string   openaiTransBaseUrl;       // 翻译模型基址, 如 https://api.openai.com/v1
    std::string   openaiTransModel;         // 翻译模型名, 如 gpt-4o-mini / 自建模型
    
    std::string   deeplApiKey;              // DeepL
    std::string   googleTransApiKey;        // Google Cloud Translate

    // OCR 识别区域（默认识别最底部对话框）
    OcrRegion     ocrRegion;
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
