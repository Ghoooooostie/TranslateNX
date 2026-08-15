// Switch Translate Overlay — L+R+ZL ile aç
// TESLA_INIT_IMPL sadece bu dosyada tanımlanıyor (tek implementation unit)
#define TESLA_INIT_IMPL
#include "tesla.hpp"

#include "config.hpp"
#include "screenshot.hpp"
#include "http_client.hpp"
#include "ocr.hpp"
#include "translate.hpp"

#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <switch.h>

// ─── Global durum ─────────────────────────────────────────────────────────
static Config            g_config;
static std::atomic<bool> g_translating{false};
static std::mutex        g_resultMutex;
static std::vector<OcrWord> g_ocrWords;
static std::vector<std::string> g_translatedLines;
static std::string       g_errorText;
static std::string       g_originalText;
static Screenshot        g_lastShot;   // 裁剪后的截图（含 regionX/Y/W/H，用于 OCR box 还原）

// ─── Yardımcı Fonksiyon: UI Çevirisi (tr / en / zh) ──────────────────────
static std::string L(const std::string& tr, const std::string& en, const std::string& zh) {
    if (g_config.uiLang == "EN") return en;
    if (g_config.uiLang == "ZH") return zh;
    return tr;
}

// ─── Forward declarations ─────────────────────────────────────────────────
class SetupGui;
class TranslateGui;
class SettingsGui;
class RegionEditorGui;
class OnScreenOverlayGui;

void reloadOverlay();

// ─── Arka planda çeviri yap ────────────────────────────────────────────────
static void doTranslate(std::vector<uint8_t> jpegData) {
    {
        std::lock_guard<std::mutex> lk(g_resultMutex);
        g_ocrWords.clear();
        g_translatedLines.clear();
        g_errorText.clear();
        g_originalText.clear();
    }

    if (jpegData.empty()) {
        std::lock_guard<std::mutex> lk(g_resultMutex);
        g_errorText    = L("OCR Hatası: Ekran görüntüsü alınamadı", "OCR Error: Screenshot could not be captured", "OCR 错误：无法截取屏幕");
        g_translating  = false;
        return;
    }

    OcrResult ocr;
    if (g_config.ocrApi == OcrApi::GoogleVision) {
        if (g_config.visionApiKey.empty()) {
            std::lock_guard<std::mutex> lk(g_resultMutex);
            g_errorText   = L("OCR Hatası: Google Vision API key girilmemiş", "OCR Error: Google Vision API key is missing", "OCR 错误：未填写 Google Vision API 密钥");
            g_translating = false;
            return;
        }
        ocr = Ocr::runGoogleVision(jpegData, g_config.visionApiKey, g_config.srcLang);
    } else if (g_config.ocrApi == OcrApi::OpenAiVision) {
        if (g_config.openaiVisionApiKey.empty() || g_config.openaiVisionBaseUrl.empty() || g_config.openaiModel.empty()) {
            std::lock_guard<std::mutex> lk(g_resultMutex);
            g_errorText   = L("OCR Hatası: OpenAI Vision ayarları eksik", "OCR Error: OpenAI Vision settings missing", "OCR 错误：未填写 OpenAI Vision 设置");
            g_translating = false;
            return;
        }
        ocr = Ocr::runOpenAiVision(jpegData, g_config.openaiVisionApiKey, g_config.openaiVisionBaseUrl, g_config.openaiModel, g_config.srcLang);
    } else {
        ocr = Ocr::runOcrSpace(jpegData, g_config.ocrApiKey, g_config.srcLang);
    }

    if (!ocr.success) {
        std::lock_guard<std::mutex> lk(g_resultMutex);
        g_errorText   = ocr.errorMsg;
        g_translating = false;
        return;
    }

    std::vector<std::string> linesToTranslate;
    std::vector<OcrWord> filteredWords;
    // 若截图做了局部裁剪，把 OCR 返回(相对小图)的归一化 box 还原回整图(1280x720)坐标
    const Screenshot& rs = g_lastShot;
    for (const auto& w : ocr.words) {
        OcrWord rw = w;
        if (rs.regionW > 0 && rs.regionH > 0) {
            rw.x = (rs.regionX + w.x * rs.regionW) / 1280.0f;
            rw.y = (rs.regionY + w.y * rs.regionH) / 720.0f;
            rw.w = (w.w * rs.regionW) / 1280.0f;
            rw.h = (w.h * rs.regionH) / 720.0f;
        }
        linesToTranslate.push_back(rw.text);
        filteredWords.push_back(rw);
    }

    TranslateResult tr;
    std::string langPair = g_config.srcLang + "|" + g_config.dstLang;
    if (g_config.translateApi == TranslateApi::DeepL) {
        if (g_config.deeplApiKey.empty()) {
            std::lock_guard<std::mutex> lk(g_resultMutex);
            g_errorText   = L("Çeviri Hatası: DeepL API key girilmemiş", "Translation Error: DeepL API key is missing", "翻译错误：未填写 DeepL API 密钥");
            g_translating = false;
            return;
        }
        std::string deeplSource = g_config.srcLang;
        std::transform(deeplSource.begin(), deeplSource.end(), deeplSource.begin(), ::toupper);
        std::string deeplTarget = g_config.dstLang;
        std::transform(deeplTarget.begin(), deeplTarget.end(), deeplTarget.begin(), ::toupper);
        tr = Translate::runDeepL(linesToTranslate, g_config.deeplApiKey, deeplSource, deeplTarget);
    } else if (g_config.translateApi == TranslateApi::GoogleCloud) {
        if (g_config.googleTransApiKey.empty()) {
            std::lock_guard<std::mutex> lk(g_resultMutex);
            g_errorText   = L("Çeviri Hatası: Google Cloud API key girilmemiş", "Translation Error: Google Cloud API key is missing", "翻译错误：未填写 Google Cloud API 密钥");
            g_translating = false;
            return;
        }
        tr = Translate::runGoogleCloud(linesToTranslate, g_config.googleTransApiKey, g_config.srcLang, g_config.dstLang);
    } else if (g_config.translateApi == TranslateApi::OpenAiTranslate) {
        if (g_config.openaiTransApiKey.empty() || g_config.openaiTransBaseUrl.empty() || g_config.openaiTransModel.empty()) {
            std::lock_guard<std::mutex> lk(g_resultMutex);
            g_errorText   = L("Çeviri Hatası: OpenAI Translate ayarları eksik", "Translation Error: OpenAI Translate settings missing", "翻译错误：未填写 OpenAI Translate 设置");
            g_translating = false;
            return;
        }
        tr = Translate::runOpenAiTranslate(linesToTranslate, g_config.openaiTransApiKey, g_config.openaiTransBaseUrl, g_config.openaiTransModel, g_config.srcLang, g_config.dstLang);
    } else {
        tr = Translate::runMyMemory(linesToTranslate, langPair);
    }

    std::lock_guard<std::mutex> lk(g_resultMutex);
    g_originalText = ocr.fullText;
    g_ocrWords = filteredWords;
    if (tr.success) {
        g_translatedLines = tr.translatedLines;
        g_errorText.clear();
    } else {
        g_errorText = tr.errorMsg;
    }
    g_translating = false;
}

// ═══════════════════════════════════════════════════════════════════════════
std::string getLanguageName(const std::string& code) {
    if (code == "TR") return L("Türkçe", "Turkish", "土耳其语");
    if (code == "EN") return L("İngilizce", "English", "英语");
    if (code == "JA") return L("Japonca", "Japanese", "日语");
    if (code == "KO") return L("Korece", "Korean", "韩语");
    if (code == "ZH") return L("Çince", "Chinese", "中文");
    if (code == "DE") return L("Almanca", "German", "德语");
    if (code == "FR") return L("Fransızca", "French", "法语");
    if (code == "ES") return L("İspanyolca", "Spanish", "西班牙语");
    if (code == "IT") return L("İtalyanca", "Italian", "意大利语");
    if (code == "RU") return L("Rusça", "Russian", "俄语");
    if (code == "BG") return L("Bulgarca", "Bulgarian", "保加利亚语");
    if (code == "CS") return L("Çekçe", "Czech", "捷克语");
    if (code == "DA") return L("Danca", "Danish", "丹麦语");
    if (code == "NL") return L("Felemenkçe", "Dutch", "荷兰语");
    if (code == "FI") return L("Fince", "Finnish", "芬兰语");
    if (code == "EL") return L("Yunanca", "Greek", "希腊语");
    if (code == "HU") return L("Macarca", "Hungarian", "匈牙利语");
    if (code == "PL") return L("Lehçe", "Polish", "波兰语");
    if (code == "PT") return L("Portekizce", "Portuguese", "葡萄牙语");
    if (code == "SL") return L("Slovence", "Slovenian", "斯洛文尼亚语");
    if (code == "SV") return L("İsveççe", "Swedish", "瑞典语");
    return code;
}

// ═══════════════════════════════════════════════════════════════════════════
// SAYFA 4: Dil Seçimi
// ═══════════════════════════════════════════════════════════════════════════
class OcrApiSelectGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("OCR API Secimi", "OCR API Select", "OCR API 选择"), L("[B] Geri", "[B] Back", "[B] 返回"));
        auto* list  = new tsl::elm::List();

        std::vector<std::pair<OcrApi, std::string>> apis = {
            {OcrApi::OcrSpace, "OCR.space"},
            {OcrApi::GoogleVision, "Google Vision"},
            {OcrApi::OpenAiVision, "OpenAI Vision"}
        };

        for (const auto& api : apis) {
            auto* item = new tsl::elm::ListItem(api.second);
            item->setClickListener([this, api](u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    if (g_config.ocrApi != api.first) {
                        g_config.ocrApi = api.first;
                        ConfigManager::save(g_config);
                        tsl::goBack();
                    } else {
                        tsl::goBack();
                    }
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }

        frame->setContent(list);
        return frame;
    }
};

class TranslateApiSelectGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Çeviri API Seçimi", "Translate API Select", "翻译 API 选择"), L("[B] Geri", "[B] Back", "[B] 返回"));
        auto* list  = new tsl::elm::List();

        std::vector<std::pair<TranslateApi, std::string>> apis = {
            {TranslateApi::MyMemory, "MyMemory"},
            {TranslateApi::DeepL, "DeepL"},
            {TranslateApi::GoogleCloud, "Google Cloud"},
            {TranslateApi::OpenAiTranslate, "OpenAI Translate"}
        };

        for (const auto& api : apis) {
            auto* item = new tsl::elm::ListItem(api.second);
            item->setClickListener([this, api](u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    if (g_config.translateApi != api.first) {
                        g_config.translateApi = api.first;
                        ConfigManager::save(g_config);
                        tsl::goBack();
                    } else {
                        tsl::goBack();
                    }
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }

        frame->setContent(list);
        return frame;
    }
};

class UiLanguageSelectGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Arayüz Dili", "UI Language", "界面语言"), L("[B] Geri", "[B] Back", "[B] 返回"));
        auto* list  = new tsl::elm::List();

        std::vector<std::string> langs = {"TR", "EN", "ZH"};

        for (const auto& langCode : langs) {
            auto* item = new tsl::elm::ListItem(getLanguageName(langCode));
            item->setClickListener([this, langCode](u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    if (g_config.uiLang != langCode) {
                        g_config.uiLang = langCode;
                        ConfigManager::save(g_config);
                        reloadOverlay();
                    } else {
                        tsl::goBack();
                    }
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }

        frame->setContent(list);
        return frame;
    }
};

class LanguageSelectGui : public tsl::Gui {
    bool m_isSource;
public:
    LanguageSelectGui(bool isSource) : m_isSource(isSource) {}

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Dil Seçimi", "Select Language", "选择语言"), L("[B] Geri", "[B] Back", "[B] 返回"));
        auto* list  = new tsl::elm::List();

        std::vector<std::string> langs = {
            "TR", "EN", "JA", "KO", "ZH", "DE", "FR", "ES", "IT", "RU", 
            "BG", "CS", "DA", "NL", "FI", "EL", "HU", "PL", "PT", "SL", "SV"
        };

        for (const auto& langCode : langs) {
            auto* item = new tsl::elm::ListItem(getLanguageName(langCode));
            item->setClickListener([this, langCode](u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    if (this->m_isSource) {
                        g_config.srcLang = langCode;
                    } else {
                        g_config.dstLang = langCode;
                    }
                    ConfigManager::save(g_config);
                    tsl::goBack();
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }

        frame->setContent(list);
        return frame;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// SAYFA 3: Ayarlar
// ═══════════════════════════════════════════════════════════════════════════
class HelpGui;

class SettingsGui : public tsl::Gui {
    tsl::elm::ListItem* m_ocrApiItem = nullptr;
    tsl::elm::ListItem* m_transApiItem = nullptr;
    tsl::elm::ListItem* m_srcItem = nullptr;
    tsl::elm::ListItem* m_dstItem = nullptr;
    tsl::elm::ListItem* m_uiLangItem = nullptr;

    void update() override {
        if (m_srcItem) m_srcItem->setValue(getLanguageName(g_config.srcLang));
        if (m_dstItem) m_dstItem->setValue(getLanguageName(g_config.dstLang));
        if (m_uiLangItem) m_uiLangItem->setValue(getLanguageName(g_config.uiLang));
        
        if (m_ocrApiItem) {
            if (g_config.ocrApi == OcrApi::GoogleVision) m_ocrApiItem->setValue("Google Vision");
            else m_ocrApiItem->setValue("OCR.space");
        }
        if (m_transApiItem) {
            if (g_config.translateApi == TranslateApi::DeepL) m_transApiItem->setValue("DeepL");
            else if (g_config.translateApi == TranslateApi::GoogleCloud) m_transApiItem->setValue("Google Cloud");
            else m_transApiItem->setValue("MyMemory");
        }
    }

public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Ayarlar", "Settings", "设置"), L("[B] Geri", "[B] Back", "[B] 返回"));
        auto* list  = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader(L("API AYARLARI", "API SETTINGS", "API 设置")));

        m_ocrApiItem = new tsl::elm::ListItem("OCR API");
        m_ocrApiItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<OcrApiSelectGui>();
                return true;
            }
            return false;
        });
        list->addItem(m_ocrApiItem);

        m_transApiItem = new tsl::elm::ListItem(L("Çeviri API", "Translate API", "翻译 API"));
        m_transApiItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<TranslateApiSelectGui>();
                return true;
            }
            return false;
        });
        list->addItem(m_transApiItem);

        list->addItem(new tsl::elm::CategoryHeader(L("DİL AYARLARI", "LANGUAGE SETTINGS", "语言设置")));

        m_srcItem = new tsl::elm::ListItem(L("Kaynak Dil", "Source Lang", "源语言"));
        m_srcItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<LanguageSelectGui>(true);
                return true;
            }
            return false;
        });
        list->addItem(m_srcItem);

        m_dstItem = new tsl::elm::ListItem(L("Hedef Dil", "Target Lang", "目标语言"));
        m_dstItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<LanguageSelectGui>(false);
                return true;
            }
            return false;
        });
        list->addItem(m_dstItem);

        m_uiLangItem = new tsl::elm::ListItem(L("Arayüz Dili", "UI Language", "界面语言"));
        m_uiLangItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<UiLanguageSelectGui>();
                return true;
            }
            return false;
        });
        list->addItem(m_uiLangItem);

        list->addItem(new tsl::elm::CategoryHeader(L("TANIMA BÖLGESİ", "OCR REGION", "识别区域")));

        auto* regionItem = new tsl::elm::ListItem(L("Tanıma Bölgesi (Yerel OCR)", "OCR Region (Local)", "识别区域（局部 OCR）"));
        regionItem->setValue(g_config.ocrRegion.enabled
            ? L("Açık (bölge seçildi)", "ON (region set)", "开 (已选区域)")
            : L("Kapalı (tam ekran)", "OFF (full screen)", "关 (全屏)"));
        regionItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<RegionEditorGui>();
                return true;
            }
            return false;
        });
        list->addItem(regionItem);

        list->addItem(new tsl::elm::CategoryHeader(L("OCR API ANAHTARLARI", "OCR API KEYS", "OCR API 密钥")));

        auto* editOcrItem = new tsl::elm::ListItem("OCR.space API Key");
        editOcrItem->setValue(g_config.ocrApiKey.empty() ? L("Pasif", "None", "未设置") : L("Aktif", "Set", "已设置"));
        editOcrItem->setValueColor(g_config.ocrApiKey.empty() ? tsl::Color(15, 0, 0, 15) : tsl::Color(0, 15, 0, 15));
        list->addItem(editOcrItem);

        auto* editVisItem = new tsl::elm::ListItem("Google Vision API Key");
        editVisItem->setValue(g_config.visionApiKey.empty() ? L("Pasif", "None", "未设置") : L("Aktif", "Set", "已设置"));
        editVisItem->setValueColor(g_config.visionApiKey.empty() ? tsl::Color(15, 0, 0, 15) : tsl::Color(0, 15, 0, 15));
        list->addItem(editVisItem);

        list->addItem(new tsl::elm::CategoryHeader(L("ÇEVİRİ API ANAHTARLARI", "TRANSLATE API KEYS", "翻译 API 密钥")));

        auto* editDeeplItem = new tsl::elm::ListItem("DeepL API Key");
        editDeeplItem->setValue(g_config.deeplApiKey.empty() ? L("Pasif", "None", "未设置") : L("Aktif", "Set", "已设置"));
        editDeeplItem->setValueColor(g_config.deeplApiKey.empty() ? tsl::Color(15, 0, 0, 15) : tsl::Color(0, 15, 0, 15));
        list->addItem(editDeeplItem);

        auto* editGoogleTransItem = new tsl::elm::ListItem("Google Cloud API Key");
        editGoogleTransItem->setValue(g_config.googleTransApiKey.empty() ? L("Pasif", "None", "未设置") : L("Aktif", "Set", "已设置"));
        editGoogleTransItem->setValueColor(g_config.googleTransApiKey.empty() ? tsl::Color(15, 0, 0, 15) : tsl::Color(0, 15, 0, 15));
        list->addItem(editGoogleTransItem);

        list->addItem(new tsl::elm::CategoryHeader(L("OPENAI (ÖZEL MODEL)", "OPENAI (CUSTOM MODEL)", "OPENAI（自定义模型）")));

        list->addItem(new tsl::elm::CategoryHeader(L("GÖRME (OCR)", "VISION (OCR)", "视觉模型（OCR）")));

        auto* oaiVisKey = new tsl::elm::ListItem(L("Vision API Key", "Vision API Key", "视觉 API Key"));
        bool visOk = !g_config.openaiVisionApiKey.empty() && !g_config.openaiVisionBaseUrl.empty();
        oaiVisKey->setValue(visOk ? L("Aktif", "Set", "已设置") : L("Pasif", "None", "未设置"));
        oaiVisKey->setValueColor(visOk ? tsl::Color(0, 15, 0, 15) : tsl::Color(15, 0, 0, 15));
        list->addItem(oaiVisKey);

        auto* oaiVisUrl = new tsl::elm::ListItem(L("Vision Base URL", "Vision Base URL", "视觉接口地址"));
        oaiVisUrl->setValue(g_config.openaiVisionBaseUrl.empty() ? L("Boş", "Empty", "空") : g_config.openaiVisionBaseUrl);
        list->addItem(oaiVisUrl);

        auto* oaiVis = new tsl::elm::ListItem(L("Vision Model", "Vision Model", "视觉模型名"));
        oaiVis->setValue(g_config.openaiModel.empty() ? L("Boş", "Empty", "空") : g_config.openaiModel);
        list->addItem(oaiVis);

        list->addItem(new tsl::elm::CategoryHeader(L("ÇEVİRİ", "TRANSLATE", "翻译模型")));

        auto* oaiTransKey = new tsl::elm::ListItem(L("Translate API Key", "Translate API Key", "翻译 API Key"));
        bool transOk = !g_config.openaiTransApiKey.empty() && !g_config.openaiTransBaseUrl.empty();
        oaiTransKey->setValue(transOk ? L("Aktif", "Set", "已设置") : L("Pasif", "None", "未设置"));
        oaiTransKey->setValueColor(transOk ? tsl::Color(0, 15, 0, 15) : tsl::Color(15, 0, 0, 15));
        list->addItem(oaiTransKey);

        auto* oaiTransUrl = new tsl::elm::ListItem(L("Translate Base URL", "Translate Base URL", "翻译接口地址"));
        oaiTransUrl->setValue(g_config.openaiTransBaseUrl.empty() ? L("Boş", "Empty", "空") : g_config.openaiTransBaseUrl);
        list->addItem(oaiTransUrl);

        auto* oaiTrans = new tsl::elm::ListItem(L("Translate Model", "Translate Model", "翻译模型名"));
        oaiTrans->setValue(g_config.openaiTransModel.empty() ? L("Boş", "Empty", "空") : g_config.openaiTransModel);
        list->addItem(oaiTrans);

        frame->setContent(list);
        return frame;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// DÜZ METİN VE KAYDIRMA (SCROLL) SİSTEMİ
// ═══════════════════════════════════════════════════════════════════════════
std::vector<std::string> splitTextGlobal(const std::string& text, size_t maxLen) {
    std::vector<std::string> lines;
    std::string currentLine;
    std::string word;
    for (char c : text) {
        if (c == ' ' || c == '\n') {
            if (!currentLine.empty() && currentLine.length() + word.length() + 1 > maxLen) {
                lines.push_back(currentLine);
                currentLine = word;
            } else {
                if (!currentLine.empty()) currentLine += " ";
                currentLine += word;
            }
            word.clear();
            if (c == '\n') {
                lines.push_back(currentLine);
                currentLine.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        if (!currentLine.empty() && currentLine.length() + word.length() + 1 > maxLen) {
            lines.push_back(currentLine);
            lines.push_back(word);
        } else {
            if (!currentLine.empty()) currentLine += " ";
            currentLine += word;
            lines.push_back(currentLine);
        }
    } else if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
    return lines;
}

class ScrollText : public tsl::elm::Element {
    std::vector<std::pair<std::string, tsl::Color>> m_lines;
    u16 m_fontSize;
    float m_scrollY = 0;
    float m_maxScrollY = 0;
    
public:
    ScrollText(u16 fontSize) : m_fontSize(fontSize) {}

    void addText(const std::string& text, tsl::Color color, size_t maxLen) {
        auto splitted = splitTextGlobal(text, maxLen);
        for (const auto& s : splitted) {
            m_lines.push_back({s, color});
        }
    }

    void addEmptyLine() {
        m_lines.push_back({"", tsl::Color(0,0,0,0)});
    }

    void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(parentX, parentY, parentWidth, parentHeight);
        
        // Tesla overlay içerik alanı (header 97 px, footer 73 px)
        s32 contentHeight = 550; // 720 - 97 - 73
        m_maxScrollY = (s32)(m_lines.size() * (m_fontSize + 10)) - contentHeight;
        if (m_maxScrollY < 0) m_maxScrollY = 0;
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        // Overlay sınırlarını sabitliyoruz
        s32 topBound = 97;
        s32 bottomBound = 647; // 720 - 73
        
        s32 contentHeight = 550;
        s32 y = topBound - m_scrollY;

        // Satırların yarım gözükmesi durumunda çerçevenin dışına taşmaması için donanımsal kırpma (scissoring) açıyoruz
        renderer->enableScissoring(this->getX(), topBound, this->getWidth(), contentHeight);

        for (const auto& pair : m_lines) {
            s32 textTop = y;
            s32 textBottom = y + m_fontSize + 2;

            if (textBottom > topBound && textTop < bottomBound) {
                renderer->drawString(pair.first, false, this->getX() + 15, textBottom, m_fontSize, pair.second);
            }
            y += m_fontSize + 10;
        }

        renderer->disableScissoring();

        // Draw scrollbar
        if (m_maxScrollY > 0) {
            float percent = (float)m_scrollY / (float)m_maxScrollY;
            s32 barHeight = (contentHeight * contentHeight) / (contentHeight + m_maxScrollY);
            if (barHeight < 20) barHeight = 20;
            s32 barY = topBound + percent * (contentHeight - barHeight);
            renderer->drawRect(this->getX() + this->getWidth() - 10, barY, 4, barHeight, tsl::Color(255, 255, 255, 128));
        }
    }

    tsl::elm::Element* requestFocus(tsl::elm::Element* oldFocus, tsl::FocusDirection direction) override {
        return this; // Required to receive input
    }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        bool handled = false;
        
        // Fast scroll with D-pad or Joystick
        if ((keysHeld & HidNpadButton_Up) || joyStickPosLeft.y > 10000 || joyStickPosRight.y > 10000) {
            m_scrollY -= 15; // Faster speed
            handled = true;
        } else if ((keysHeld & HidNpadButton_Down) || joyStickPosLeft.y < -10000 || joyStickPosRight.y < -10000) {
            m_scrollY += 15; // Faster speed
            handled = true;
        }

        // Consume horizontal inputs to prevent libtesla's boundary glow animation
        if ((keysHeld & HidNpadButton_Left) || (keysHeld & HidNpadButton_Right) || 
            joyStickPosLeft.x > 10000 || joyStickPosLeft.x < -10000 ||
            joyStickPosRight.x > 10000 || joyStickPosRight.x < -10000) {
            handled = true;
        }

        if (m_scrollY < 0) m_scrollY = 0;
        if (m_scrollY > m_maxScrollY) m_scrollY = m_maxScrollY;
        
        return handled;
    }
};

class TextLineItem : public tsl::elm::ListItem {
    std::string m_text;
    u16 m_fontSize;
    tsl::Color m_color;
public:
    TextLineItem(const std::string& text, u16 fontSize, tsl::Color color) 
        : tsl::elm::ListItem(""), m_text(text), m_fontSize(fontSize), m_color(color) {
    }
    
    void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(this->getX(), this->getY(), parentWidth, m_fontSize + 10);
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        // Seçim kutucuğu çizmeyip sadece metni çizeceğiz
        renderer->drawString(m_text, false, this->getX() + 15, this->getY() + m_fontSize + 2, m_fontSize, m_color);
    }
    
    // YENİ EKLENEN KISIM: Ultrahand'in varsayılan seçim kutucuğunu (cursor) tamamen kapatıyoruz!
    void drawHighlight(tsl::gfx::Renderer* renderer) override {}
    void drawFocusBackground(tsl::gfx::Renderer* renderer) override {}
    void drawClickFlash(tsl::gfx::Renderer* renderer) override {}
    void drawClickAnimation(tsl::gfx::Renderer* renderer) override {}
};

class HelpGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Yardım & Rehber", "Help & Guide", "帮助 & 指南"), L("[B] Geri", "[B] Back", "[B] 返回"));
        
        auto* scroll = new ScrollText(20);

        std::vector<std::string> linesTR = {
            "Herhangi bir sorun yaşarsanız GitHub'dan veya buralardan bana ulaşabilirsiniz:",
            "",
            "İnstagram: cilsertay",
            "Discord: sertay",
            "",
            "!!!KRİTİK UYARI!!!",
            "Çökmeyi önlemek için UltraHand menüsünden (+ tuşu > System) Overlay Memory değerini 4MB'den 8MB'a yükseltin!",
            "",
            "### 1. API ALMA VE LİMİTLER",
            "Uygulamanın çalışması için bir OCR (Tarama) ve bir Çeviri API'si girmeniz gerekir. Sitelerden ücretsiz hesap açıp anahtar (Key) alabilirsiniz:",
            "",
            "## OCR (Metin Tarama) API'leri",
            "Google Cloud Vision (Önerilen): Aylık 1.000 kullanım ücretsiz. En stabil çalışanıdır.",
            "OCR.space: Günlük 500 kullanım ücretsiz. Kart bilgisi istemeyen iyi bir alternatiftir.",
            "",
            "## Çeviri API'leri",
            "DeepL (Önerilen): Tek seferlik 1.000.000 karakter ücretsiz. En kaliteli çeviriyi sunar. Kotanız bittiğinde yeni ücretsiz hesap açabilirsiniz.",
            "Google Cloud Translate: Aylık 500.000 karakter ücretsiz. Kayıt esnasında kart bilgisi isteyebilir, kotayı aşmadıkça ücret kesmez.",
            "MyMemory: Günlük 5.000 karakter ücretsiz. Uygulamada hazır kurulu gelir, ayardan seçip direkt kullanabilirsiniz.",
            "",
            "### 2. API ANAHTARLARINI YÜKLEME",
            "Aldığınız kodları sisteme kaydetmek için iki yol vardır:",
            "",
            "Switch: Hbmenu > TranslateNX uygulamasını açarak girin.",
            "PC: SD Kart içindeki config/translate/config.ini dosyasını bilgisayarda açıp düzenleyin.",
            "",
            "### 3. KULLANIM ADIMLARI",
            "Oyundayken UltraHand arayüzünü açın. (Ekranın sol kenarından sağa kaydırın veya L + R + Aşağı Yön + Sağ Analoğa basın.)",
            "Menüden TranslateNX'i başlatın (Y tuşu ile kısayol atayabilirsiniz).",
            "Ayarlar'dan kullanacağınız API'leri, Oyun Dilini (Kaynak) ve Kendi Dilinizi (Hedef) seçin.",
            "Ekranda yazı varken \"Çeviriye Başla\"ya basın. Taranan cümleler listelenecektir; detaylı çeviri için üzerine tıklamanız yeterlidir.",
        };

        std::vector<std::string> linesEN = {
            "If you have any issues, you can contact me via GitHub or these platforms:",
            "",
            "Instagram: cilsertay",
            "Discord: sertay",
            "",
            "!!!CRITICAL WARNING!!!",
            "To prevent crashes, increase the Overlay Memory from 4MB to 8MB in the UltraHand menu (+ button > System)!",
            "",
            "### 1. GETTING APIs AND LIMITS",
            "To use the app, you need to enter an OCR (Scanning) and a Translation API. You can create a free account and get a Key:",
            "",
            "## OCR (Text Scanning) APIs",
            "Google Cloud Vision (Recommended): 1,000 free uses per month. The most stable option.",
            "OCR.space: 500 free uses per day. A good alternative that doesn't require card info.",
            "",
            "## Translation APIs",
            "DeepL (Recommended): 1,000,000 free characters at once. Offers the highest quality translation. You can create a new free account when you reach the limit.",
            "Google Cloud Translate: 500,000 free characters per month. May require card info during registration, but won't charge as long as you don't exceed the quota.",
            "MyMemory: 5,000 free characters per day. Comes pre-installed, you can just select it from the settings.",
            "",
            "### 2. INSTALLING API KEYS",
            "There are two ways to save the codes you received:",
            "",
            "Switch: Open the TranslateNX app via Hbmenu.",
            "PC: Open and edit the config/translate/config.ini file on your SD Card.",
            "",
            "### 3. USAGE STEPS",
            "Open the UltraHand interface while in-game. (Swipe right from the left edge of the screen or press L + R + D-Pad Down + Right Stick.)",
            "Start TranslateNX from the menu (You can assign a shortcut using the Y button).",
            "Select the APIs, Game Language (Source) and Your Language (Target) from the Settings.",
            "When there is text on the screen, press \"Start Translation\". The scanned sentences will be listed; simply click on them for detailed translation.",
        };

        const auto& lines = (g_config.uiLang == "TR") ? linesTR : linesEN;

        for (const auto& line : lines) {
            if (line.empty()) {
                scroll->addEmptyLine();
            } else {
                scroll->addText(line, tsl::Color(255, 255, 255, 255), 38);
            }
        }

        frame->setContent(scroll);
        return frame;
    }
};

class TranslationDetailGui : public tsl::Gui {
    std::string m_trText;
    std::string m_jpText;
public:
    TranslationDetailGui(const std::string& tr, const std::string& jp) 
        : m_trText(tr), m_jpText(jp) {}

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Detaylı Çeviri", "Translation Detail", "详细翻译"), L("[B] Geri", "[B] Back", "[B] 返回"));
        
        auto* scroll = new ScrollText(22);

        scroll->addText(L("Çeviri", "Translation", "翻译"), tsl::Color(0, 255, 0, 255), 40);
        scroll->addText(m_trText, tsl::Color(255, 255, 255, 255), 30); 
        
        scroll->addEmptyLine();
        
        scroll->addText(L("Orijinal Metin", "Original Text", "原文"), tsl::Color(255, 0, 0, 255), 40);
        scroll->addText(m_jpText, tsl::Color(150, 150, 150, 255), 30);

        frame->setContent(scroll);
        return frame;
    }
};

class TranslationItem : public tsl::elm::ListItem {
    std::string m_trText;
    std::string m_jpText;
    std::vector<std::string> m_linesTR;
    std::vector<std::string> m_linesJP;
    u16 m_customHeight;

public:
    TranslationItem(const std::string& tr, const std::string& jp, const std::vector<std::string>& trLines, const std::vector<std::string>& jpLines, u16 height)
        : tsl::elm::ListItem(""), m_trText(tr), m_jpText(jp), m_linesTR(trLines), m_linesJP(jpLines), m_customHeight(height) {
        
        this->setBoundaries(this->getX(), this->getY(), this->getWidth(), height);
        
        this->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<TranslationDetailGui>(this->m_trText, this->m_jpText);
                return true;
            }
            return false;
        });
    }

    void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        // ListItem::layout resets height to 70. We must override it!
        this->setBoundaries(this->getX() + 3, this->getY(), parentWidth - 6, m_customHeight);
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        s32 x = this->getX();
        s32 y = this->getY();
        s32 w = this->getWidth();
        s32 h = this->getHeight();
        
        if (this->m_focused) {
            renderer->drawRect(x, y, w, h, tsl::Color(255, 255, 255, 50));
        }

        s32 currY = y + 27;
        for (const auto& l : m_linesTR) {
            renderer->drawString(l, false, x + 15, currY, 22, tsl::Color(255, 255, 255, 255));
            currY += 25;
        }
        currY += 2; // Extra padding between TR and JP
        for (const auto& l : m_linesJP) {
            renderer->drawString(l, false, x + 15, currY, 18, tsl::Color(150, 150, 150, 255));
            currY += 20;
        }
        renderer->drawRect(x + 10, y + h - 2, w - 20, 1, tsl::Color(255, 255, 255, 50));
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// SAYFA 2: Ana Çeviri Ekranı (Şeffaf üst bar)
// ═══════════════════════════════════════════════════════════════════════════
class TopBarElement : public tsl::elm::Element {
    std::vector<std::string> m_lines;
public:
    TopBarElement(const std::vector<std::string>& lines) : m_lines(lines) {}

    void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(parentX, parentY, parentWidth, parentHeight);
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        constexpr u16 barHeight = 80;
        constexpr u16 fontSize  = 26;
        renderer->drawRect(0, 0, 1280, barHeight, tsl::Color(0, 0, 0, 180));

        u16 y = 12;
        for (const auto& line : m_lines) {
            if (y + fontSize > barHeight - 8) break;
            renderer->drawString(line, false, 20, y + fontSize, fontSize, tsl::Color(255, 255, 255, 255));
            y += fontSize + 6;
        }
    }

    bool handleInput(u64, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        return false;
    }
};

class TranslationResultGui : public tsl::Gui {
    size_t m_oldBackgroundAlpha = 0;
public:
    TranslationResultGui() {
        m_oldBackgroundAlpha = tsl::defaultBackgroundAlpha;
        tsl::defaultBackgroundAlpha = 0;
    }

    ~TranslationResultGui() {
        tsl::defaultBackgroundAlpha = m_oldBackgroundAlpha;
    }

    tsl::elm::Element* createUI() override {
        std::lock_guard<std::mutex> lk(g_resultMutex);
        std::vector<std::string> lines;

        if (!g_errorText.empty()) {
            lines = splitTextGlobal(L("HATA: ", "ERROR: ", "错误：") + g_errorText, 55);
        } else if (g_translatedLines.empty()) {
            lines = { L("Çevirilecek yazı bulunamadı.", "No text found to translate.", "未找到可翻译的文字。") };
        } else {
            std::string combined;
            for (const auto& l : g_translatedLines) {
                if (!combined.empty()) combined += " ";
                combined += l;
            }
            lines = splitTextGlobal(combined, 55);
            if (lines.size() > 2) {
                lines.resize(2);
                lines.back() += "...";
            }
        }

        return new TopBarElement(lines);
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        if (keysDown & HidNpadButton_B) {
            tsl::goBack();
            return true;
        }
        return false;
    }
};

// ─── Global olarak çekilen fotoğrafı saklayalım ───────────────────────────
static std::vector<uint8_t> g_screenshotData;

class LoadingGui : public tsl::Gui {
    int m_frames = 0;
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("TranslateNX", "TranslateNX", "TranslateNX"), L("Lütfen Bekleyin...", "Please Wait...", "请稍候..."));
        auto* list = new tsl::elm::List();
        list->addItem(new tsl::elm::ListItem(L("Çeviri yapılıyor...", "Translating...", "正在翻译...")));
        frame->setContent(list);
        return frame;
    }
    
    void update() override {
        m_frames++;
        // Ekrana loading arayüzünün çizilmesi için 2 kare bekle
        if (m_frames == 2) {
            // Ana thread üzerinde senkron olarak çalıştır (sysmodule'de std::thread crash'e sebep oluyor)
            doTranslate(std::move(g_screenshotData));
            g_screenshotData.clear();
            
            tsl::goBack(); // LoadingGui'yi kapat
            tsl::changeTo<TranslationResultGui>(); // Sonuclari goster
        }
    }
    
    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        return false; // İşlem bitene kadar B'ye basıp çıkmasını engelle
    }
};

class ScreenshotWaitGui : public tsl::Gui {
    int m_frames = 0;
public:
    tsl::elm::Element* createUI() override {
        // Tamamen seffaf element, menuyu gizler
        auto* dummy = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* r, s32, s32, s32, s32){
            r->clearScreen();
        });
        dummy->setBoundaries(0, 0, 1280, 720);
        return dummy;
    }
    
    void update() override {
        m_frames++;
        // Menünün tamamen ekrandan kaymasını beklemek için 40 kare (~0.6 sn) bekle
        if (m_frames == 40) {
            // Ekran tam temizken çekim yap
            auto shot = ScreenshotCapture::capture(65);
            // 局部识别开启时，只裁剪指定区域再发给 OCR（提速 + 聚焦对话框）
            if (g_config.ocrRegion.enabled) {
                shot.regionCrop(g_config.ocrRegion.x, g_config.ocrRegion.y,
                                g_config.ocrRegion.w, g_config.ocrRegion.h);
            }
            g_lastShot = shot;
            g_screenshotData = std::move(shot.jpegData);
            
            tsl::goBack(); // ScreenshotWaitGui'yi kapat
            tsl::changeTo<LoadingGui>(); // Kullanıcıya yükleniyor ekranını göster
        }
    }
    
    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        return false;
    }
};

class TranslateGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(
            "TranslateNX",
            "by SertAy - 1.0.0");

        auto* list = new tsl::elm::List();

        auto* translateBtn = new tsl::elm::ListItem(L("Çeviriye Başla", "Start Translating", "开始翻译"));
        translateBtn->setClickListener([](u64 keys) -> bool {
            return false; // handleInput'da islenecek
        });
        list->addItem(translateBtn);

        auto* helpItem = new tsl::elm::ListItem(L("Yardım & Rehber", "Help & Guide", "帮助 & 指南"));
        helpItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<HelpGui>();
                return true;
            }
            return false;
        });
        list->addItem(helpItem);
        
        auto* settingsBtn = new tsl::elm::ListItem(L("Ayarlar", "Settings", "设置"));
        settingsBtn->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<SettingsGui>();
                return true;
            }
            return false;
        });
        list->addItem(settingsBtn);

        frame->setContent(list);
        return frame;
    }

    void update() override {
        // Removed g_forceClose logic to prevent use-after-free crashes.
    }

    bool handleInput(u64 keysDown, u64,
                     const HidTouchState&,
                     HidAnalogStickState, HidAnalogStickState) override {
        if ((keysDown & HidNpadButton_A) && !g_translating) {
            {
                std::lock_guard<std::mutex> lk(g_resultMutex);
                g_translatedLines.clear();
                g_errorText.clear();
                g_originalText.clear();
                g_ocrWords.clear();
            }
            g_translating = true;
            
            // Önce menüyü gizleyen wait ekranına geç
            tsl::changeTo<ScreenshotWaitGui>(); 
            
            return true;
        }
        if (keysDown & HidNpadButton_Y) {
            tsl::changeTo<SettingsGui>();
            return true;
        }
        return false;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// SAYFA: OCR 识别区域编辑器（局部识别）
// ═══════════════════════════════════════════════════════════════════════════
class RegionEditorGui : public tsl::Gui {
    OcrRegion m_region;   // 编辑中的副本
    bool      m_enabled;  // 局部识别开关
    int       m_frames = 0;

    void clampRegion() {
        auto c = [](float& v, float lo, float hi){ if (v < lo) v = lo; if (v > hi) v = hi; };
        c(m_region.x, 0.0f, 1.0f);
        c(m_region.y, 0.0f, 1.0f);
        c(m_region.w, 0.02f, 1.0f);
        c(m_region.h, 0.02f, 1.0f);
        if (m_region.x + m_region.w > 1.0f) m_region.x = 1.0f - m_region.w;
        if (m_region.y + m_region.h > 1.0f) m_region.y = 1.0f - m_region.h;
    }

    void drawOverlay(tsl::gfx::Renderer* r) {
        r->clearScreen();
        s32 x = (s32)(m_region.x * 1280.0f);
        s32 y = (s32)(m_region.y * 720.0f);
        s32 w = (s32)(m_region.w * 1280.0f);
        s32 h = (s32)(m_region.h * 720.0f);
        r->drawRect(0, 0, 1280, y, tsl::Color(0, 0, 0, 140));
        r->drawRect(0, y + h, 1280, 720 - (y + h), tsl::Color(0, 0, 0, 140));
        r->drawRect(0, y, x, h, tsl::Color(0, 0, 0, 140));
        r->drawRect(x + w, y, 1280 - (x + w), h, tsl::Color(0, 0, 0, 140));
        r->drawRect(x, y, w, h, tsl::Color(255, 60, 60, 255));
        r->drawRect(x + 2, y + 2, w - 4, h - 4, tsl::Color(255, 60, 60, 120));

        r->drawString(L("方向键: 移动区域", "D-pad: move region", "方向键: 移动区域").c_str(), false,
                      40, 60, 22, tsl::Color(255,255,255,255));
        r->drawString(L("ZL/ZR: 宽  X/Y: 高  (-): 开关", "ZL/ZR: width  X/Y: height  (-): toggle", "ZL/ZR: 宽  X/Y: 高  (-): 开关").c_str(), false,
                      40, 90, 22, tsl::Color(255,255,255,255));
        r->drawString(L("A: 保存并退出   B: 取消", "A: save & exit   B: cancel", "A: 保存并退出   B: 取消").c_str(), false,
                      40, 120, 22, tsl::Color(255,255,255,255));
        std::string status = m_enabled
            ? L("局部识别: 开", "Region OCR: ON", "局部识别: 开")
            : L("局部识别: 关 (全屏)", "Region OCR: OFF (full)", "局部识别: 关 (全屏)");
        char info[256];
        snprintf(info, sizeof(info), "%s\nX:%.2f Y:%.2f  W:%.2f H:%.2f",
                 status.c_str(), m_region.x, m_region.y, m_region.w, m_region.h);
        r->drawString(info, false, 40, 170, 22, tsl::Color(0,255,120,255));
    }

public:
    tsl::elm::Element* createUI() override {
        m_region  = g_config.ocrRegion;
        m_enabled = g_config.ocrRegion.enabled;
        auto* drawer = new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* r, s32, s32, s32, s32){
            this->drawOverlay(r);
        });
        drawer->setBoundaries(0, 0, 1280, 720);
        return drawer;
    }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        const float STEP = 0.02f;
        bool moved = false;
        if (keysHeld & HidNpadButton_Up)    { m_region.y -= STEP * 0.5f; moved = true; }
        if (keysHeld & HidNpadButton_Down)  { m_region.y += STEP * 0.5f; moved = true; }
        if (keysHeld & HidNpadButton_Left)  { m_region.x -= STEP * 0.5f; moved = true; }
        if (keysHeld & HidNpadButton_Right) { m_region.x += STEP * 0.5f; moved = true; }
        if (moved) { clampRegion(); return true; }

        if (keysDown & HidNpadButton_ZL) { m_region.w -= STEP; clampRegion(); return true; }
        if (keysDown & HidNpadButton_ZR) { m_region.w += STEP; clampRegion(); return true; }
        if (keysDown & HidNpadButton_X)  { m_region.h -= STEP; clampRegion(); return true; }
        if (keysDown & HidNpadButton_Y)  { m_region.h += STEP; clampRegion(); return true; }
        if (keysDown & HidNpadButton_Minus) {
            m_enabled = !m_enabled;
            return true;
        }
        if (keysDown & HidNpadButton_A) {
            m_region.enabled = m_enabled;
            g_config.ocrRegion = m_region;
            ConfigManager::save(g_config);
            tsl::goBack();
            return true;
        }
        if (keysDown & HidNpadButton_B) {
            tsl::goBack();
            return true;
        }
        return false;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// OVERLAY GİRİŞ NOKTASI
// ═══════════════════════════════════════════════════════════════════════════

void reloadOverlay() {
    tsl::swapTo<TranslateGui>(SwapDepth(10));
}

class TranslateOverlay : public tsl::Overlay {
public:
    void initServices() override {
        HttpClient::init();
        g_config = ConfigManager::load();
    }

    void exitServices() override {
        HttpClient::cleanup();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        // Menü her açıldığında config.ini dosyasını otomatik olarak tekrar oku!
        g_config = ConfigManager::load();
        ult::useHapticFeedback = false; // Titreşimi tamamen kapat

        return initially<TranslateGui>();
    }
};

int main(int argc, char **argv) {
    ult::useHapticFeedback = false;
    return tsl::loop<TranslateOverlay>(argc, argv);
}
