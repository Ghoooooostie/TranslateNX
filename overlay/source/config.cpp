#include "config.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <cctype>

static const char* CONFIG_PATH = "sdmc:/config/translate/config.ini";

// ─── Yardımcı: basit INI okuma ─────────────────────────────────────────────
static std::string iniGet(const char* path, const char* key) {
    FILE* f = fopen(path, "r");
    if (!f) return {};
    char line[512];
    std::string k(key);
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        std::string lkey(line, eq - line);
        // trim
        while (!lkey.empty() && (lkey.back() == ' ' || lkey.back() == '\t')) lkey.pop_back();
        if (lkey != k) continue;
        std::string val(eq + 1);
        while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ' ')) val.pop_back();
        fclose(f);
        return val;
    }
    fclose(f);
    return {};
}

namespace ConfigManager {

Config load() {
    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f) {
        // Auto-create
        mkdir("sdmc:/config", 0777);
        mkdir("sdmc:/config/translate", 0777);
        f = fopen(CONFIG_PATH, "w");
        if (f) {
            fprintf(f, "ocr_api=vision\n");
            fprintf(f, "translate_api=deepl\n");
            fprintf(f, "src_lang=en\n");
            fprintf(f, "dst_lang=tr\n");
            fprintf(f, "ui_lang=en\n");
            fprintf(f, "ocr_api_key=\n");
            fprintf(f, "vision_api_key=\n");
            fprintf(f, "deepl_api_key=\n");
            fprintf(f, "google_trans_api_key=\n");
            fclose(f);
        }
    } else {
        fclose(f);
    }

    Config cfg;
    std::string ocrStr = iniGet(CONFIG_PATH, "ocr_api");
    if (ocrStr == "vision") cfg.ocrApi = OcrApi::GoogleVision;
    else if (ocrStr == "openai") cfg.ocrApi = OcrApi::OpenAiVision;
    else cfg.ocrApi = OcrApi::OcrSpace;

    std::string transStr = iniGet(CONFIG_PATH, "translate_api");
    if (transStr == "deepl") cfg.translateApi = TranslateApi::DeepL;
    else if (transStr == "googlecloud") cfg.translateApi = TranslateApi::GoogleCloud;
    else if (transStr == "openai") cfg.translateApi = TranslateApi::OpenAiTranslate;
    else cfg.translateApi = TranslateApi::MyMemory;

    cfg.srcLang          = iniGet(CONFIG_PATH, "source_lang");
    if (cfg.srcLang.empty()) cfg.srcLang = iniGet(CONFIG_PATH, "src_lang"); // Fallback for old configs

    cfg.dstLang          = iniGet(CONFIG_PATH, "target_lang");
    if (cfg.dstLang.empty()) cfg.dstLang = iniGet(CONFIG_PATH, "dst_lang");

    cfg.uiLang           = iniGet(CONFIG_PATH, "ui_lang");
    
    cfg.ocrApiKey        = iniGet(CONFIG_PATH, "ocr_api_key");
    cfg.visionApiKey     = iniGet(CONFIG_PATH, "vision_api_key");
    cfg.openaiVisionApiKey  = iniGet(CONFIG_PATH, "openai_vision_api_key");
    cfg.openaiVisionBaseUrl = iniGet(CONFIG_PATH, "openai_vision_base_url");
    cfg.openaiModel         = iniGet(CONFIG_PATH, "openai_model");
    cfg.openaiTransApiKey   = iniGet(CONFIG_PATH, "openai_trans_api_key");
    cfg.openaiTransBaseUrl  = iniGet(CONFIG_PATH, "openai_trans_base_url");
    cfg.openaiTransModel    = iniGet(CONFIG_PATH, "openai_trans_model");

    // 兼容旧配置：旧的 openai_api_key / openai_base_url 作为视觉与翻译的共用 fallback
    std::string legacyKey = iniGet(CONFIG_PATH, "openai_api_key");
    std::string legacyUrl = iniGet(CONFIG_PATH, "openai_base_url");
    if (cfg.openaiVisionApiKey.empty())  cfg.openaiVisionApiKey  = legacyKey;
    if (cfg.openaiVisionBaseUrl.empty()) cfg.openaiVisionBaseUrl = legacyUrl;
    if (cfg.openaiTransApiKey.empty())   cfg.openaiTransApiKey   = legacyKey;
    if (cfg.openaiTransBaseUrl.empty())  cfg.openaiTransBaseUrl  = legacyUrl;
    
    cfg.deeplApiKey      = iniGet(CONFIG_PATH, "deepl_api_key");
    cfg.googleTransApiKey = iniGet(CONFIG_PATH, "google_trans_api_key");

    // OCR 识别区域（归一化 0..1，相对整图 1280x720）
    std::string rx = iniGet(CONFIG_PATH, "region_x");
    std::string ry = iniGet(CONFIG_PATH, "region_y");
    std::string rw = iniGet(CONFIG_PATH, "region_w");
    std::string rh = iniGet(CONFIG_PATH, "region_h");
    std::string ren = iniGet(CONFIG_PATH, "region_enabled");
    if (!rx.empty()) cfg.ocrRegion.x = (float)atof(rx.c_str());
    if (!ry.empty()) cfg.ocrRegion.y = (float)atof(ry.c_str());
    if (!rw.empty()) cfg.ocrRegion.w = (float)atof(rw.c_str());
    if (!rh.empty()) cfg.ocrRegion.h = (float)atof(rh.c_str());
    if (!ren.empty()) cfg.ocrRegion.enabled = (ren == "1" || ren == "true" || ren == "yes");

    if (cfg.srcLang.empty()) cfg.srcLang = "en";
    if (cfg.dstLang.empty()) cfg.dstLang = "tr";
    if (cfg.uiLang.empty())  cfg.uiLang = "en";
    
    for (auto & c: cfg.srcLang) c = toupper((unsigned char)c);
    for (auto & c: cfg.dstLang) c = toupper((unsigned char)c);
    for (auto & c: cfg.uiLang) c = toupper((unsigned char)c);
    
    return cfg;
}

void save(const Config& cfg) {
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/translate", 0777);

    FILE* f = fopen(CONFIG_PATH, "r");
    std::vector<std::string> lines;
    bool foundOcrApi = false, foundTransApi = false, foundSrc = false, foundDst = false, foundUi = false;
    bool foundOaiKey = false, foundOaiUrl = false, foundOaiModel = false, foundOaiTransModel = false;
    bool foundOaiVisionKey = false, foundOaiVisionUrl = false, foundOaiTransKey = false, foundOaiTransUrl = false;
    bool foundDeeplKey = false, foundGoogleTransKey = false, foundOcrKey = false, foundVisionKey = false;
    bool foundRegion = false;

    // 兼容旧配置：保留旧 key 原值
    std::string legacyKey = iniGet(CONFIG_PATH, "openai_api_key");
    std::string legacyUrl = iniGet(CONFIG_PATH, "openai_base_url");
    
    const char* ocrStr = "ocrspace";
    if (cfg.ocrApi == OcrApi::GoogleVision) ocrStr = "vision";
    else if (cfg.ocrApi == OcrApi::OpenAiVision) ocrStr = "openai";

    const char* transStr = "mymemory";
    if (cfg.translateApi == TranslateApi::DeepL) transStr = "deepl";
    else if (cfg.translateApi == TranslateApi::GoogleCloud) transStr = "googlecloud";
    else if (cfg.translateApi == TranslateApi::OpenAiTranslate) transStr = "openai";

    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            std::string s(line);
            if (!s.empty() && s.back() != '\n') s += '\n';
            char* eq = strchr(line, '=');
            if (eq) {
                std::string lkey(line, eq - line);
                while (!lkey.empty() && (lkey.back() == ' ' || lkey.back() == '\t')) lkey.pop_back();
                
                std::string sl = cfg.srcLang; for(auto& c:sl) c=tolower((unsigned char)c);
                std::string dl = cfg.dstLang; for(auto& c:dl) c=tolower((unsigned char)c);
                std::string ul = cfg.uiLang;  for(auto& c:ul) c=tolower((unsigned char)c);
                
                if (lkey == "ocr_api") { lines.push_back("ocr_api=" + std::string(ocrStr) + "\n"); foundOcrApi = true; continue; }
                if (lkey == "openai_api_key") { lines.push_back("openai_api_key=" + legacyKey + "\n"); foundOaiKey = true; continue; }
                if (lkey == "openai_base_url") { lines.push_back("openai_base_url=" + legacyUrl + "\n"); foundOaiUrl = true; continue; }
                if (lkey == "openai_vision_api_key") { lines.push_back("openai_vision_api_key=" + cfg.openaiVisionApiKey + "\n"); foundOaiVisionKey = true; continue; }
                if (lkey == "openai_vision_base_url") { lines.push_back("openai_vision_base_url=" + cfg.openaiVisionBaseUrl + "\n"); foundOaiVisionUrl = true; continue; }
                if (lkey == "openai_trans_api_key") { lines.push_back("openai_trans_api_key=" + cfg.openaiTransApiKey + "\n"); foundOaiTransKey = true; continue; }
                if (lkey == "openai_trans_base_url") { lines.push_back("openai_trans_base_url=" + cfg.openaiTransBaseUrl + "\n"); foundOaiTransUrl = true; continue; }
                if (lkey == "openai_model") { lines.push_back("openai_model=" + cfg.openaiModel + "\n"); foundOaiModel = true; continue; }
                if (lkey == "openai_trans_model") { lines.push_back("openai_trans_model=" + cfg.openaiTransModel + "\n"); foundOaiTransModel = true; continue; }
                if (lkey == "deepl_api_key") { lines.push_back("deepl_api_key=" + cfg.deeplApiKey + "\n"); foundDeeplKey = true; continue; }
                if (lkey == "google_trans_api_key") { lines.push_back("google_trans_api_key=" + cfg.googleTransApiKey + "\n"); foundGoogleTransKey = true; continue; }
                if (lkey == "ocr_api_key") { lines.push_back("ocr_api_key=" + cfg.ocrApiKey + "\n"); foundOcrKey = true; continue; }
                if (lkey == "vision_api_key") { lines.push_back("vision_api_key=" + cfg.visionApiKey + "\n"); foundVisionKey = true; continue; }
                if (lkey == "translate_api") { lines.push_back("translate_api=" + std::string(transStr) + "\n"); foundTransApi = true; continue; }
                if (lkey == "src_lang" || lkey == "source_lang") { lines.push_back("src_lang=" + sl + "\n"); foundSrc = true; continue; }
                if (lkey == "dst_lang" || lkey == "target_lang") { lines.push_back("dst_lang=" + dl + "\n"); foundDst = true; continue; }
                if (lkey == "ui_lang") { lines.push_back("ui_lang=" + ul + "\n"); foundUi = true; continue; }
                if (lkey == "region_x" || lkey == "region_y" || lkey == "region_w" ||
                    lkey == "region_h" || lkey == "region_enabled") { foundRegion = true; continue; }
            }
            lines.push_back(s);
        }
        fclose(f);
    }
    
    std::string sl = cfg.srcLang; for(auto& c:sl) c=tolower((unsigned char)c);
    std::string dl = cfg.dstLang; for(auto& c:dl) c=tolower((unsigned char)c);
    std::string ul = cfg.uiLang;  for(auto& c:ul) c=tolower((unsigned char)c);

    if (!foundOcrApi) lines.push_back("ocr_api=" + std::string(ocrStr) + "\n");
    if (!foundTransApi) lines.push_back("translate_api=" + std::string(transStr) + "\n");
    if (!foundSrc) lines.push_back("src_lang=" + sl + "\n");
    if (!foundDst) lines.push_back("dst_lang=" + dl + "\n");
    if (!foundUi) lines.push_back("ui_lang=" + ul + "\n");
    if (!foundOaiModel) lines.push_back("openai_model=" + cfg.openaiModel + "\n");
    if (!foundOaiTransModel) lines.push_back("openai_trans_model=" + cfg.openaiTransModel + "\n");
    if (!foundOaiKey) lines.push_back("openai_api_key=" + legacyKey + "\n");
    if (!foundOaiUrl) lines.push_back("openai_base_url=" + legacyUrl + "\n");
    if (!foundOaiVisionKey) lines.push_back("openai_vision_api_key=" + cfg.openaiVisionApiKey + "\n");
    if (!foundOaiVisionUrl) lines.push_back("openai_vision_base_url=" + cfg.openaiVisionBaseUrl + "\n");
    if (!foundOaiTransKey) lines.push_back("openai_trans_api_key=" + cfg.openaiTransApiKey + "\n");
    if (!foundOaiTransUrl) lines.push_back("openai_trans_base_url=" + cfg.openaiTransBaseUrl + "\n");
    if (!foundDeeplKey) lines.push_back("deepl_api_key=" + cfg.deeplApiKey + "\n");
    if (!foundGoogleTransKey) lines.push_back("google_trans_api_key=" + cfg.googleTransApiKey + "\n");
    if (!foundOcrKey) lines.push_back("ocr_api_key=" + cfg.ocrApiKey + "\n");
    if (!foundVisionKey) lines.push_back("vision_api_key=" + cfg.visionApiKey + "\n");
    if (!foundRegion) {
        char buf[128];
        snprintf(buf, sizeof(buf), "region_x=%.4f\n", cfg.ocrRegion.x);
        lines.push_back(buf);
        snprintf(buf, sizeof(buf), "region_y=%.4f\n", cfg.ocrRegion.y);
        lines.push_back(buf);
        snprintf(buf, sizeof(buf), "region_w=%.4f\n", cfg.ocrRegion.w);
        lines.push_back(buf);
        snprintf(buf, sizeof(buf), "region_h=%.4f\n", cfg.ocrRegion.h);
        lines.push_back(buf);
        lines.push_back(std::string("region_enabled=") + (cfg.ocrRegion.enabled ? "1" : "0") + "\n");
    }

    f = fopen(CONFIG_PATH, "w");
    if (f) {
        for (const auto& l : lines) {
            fprintf(f, "%s", l.c_str());
        }
        fclose(f);
    }
}

} // namespace ConfigManager
