#include "http_client.hpp"
#include <curl/curl.h>
#include <cstring>
#include <switch.h>

#include <cJSON.h>

// ─── Yardımcı: libcurl veri callback ──────────────────────────────────────
static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* str = static_cast<std::string*>(userdata);
    str->append(ptr, size * nmemb);
    return size * nmemb;
}

// ─── Ortak curl handle yapılandırması ─────────────────────────────────────
static void configureCurl(CURL* curl, const std::string& url,
                           const std::vector<std::string>& headers,
                           std::string& responseBody,
                           curl_slist*& headerList) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);          // 45 sn timeout (OCR.space yoğunken sabırlı ol)
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // SSL — Sertifika doğrulamasını kapatıyoruz
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    for (const auto& h : headers) {
        headerList = curl_slist_append(headerList, h.c_str());
    }
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }
}

namespace HttpClient {

void init() {
    socketInitializeDefault();
    curl_global_init(CURL_GLOBAL_ALL);
}

void cleanup() {
    curl_global_cleanup();
    socketExit();
}

HttpResponse post(const std::string& url,
                  const std::string& body,
                  const std::vector<std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    curl_slist* headerList = nullptr;
    configureCurl(curl, url, headers, resp.body, headerList);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());

    CURLcode res;
    int retries = 2;
    while (retries >= 0) {
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) break;
        if (retries > 0) svcSleepThread(500000000ull);
        retries--;
    }

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.statusCode);
    } else {
        resp.errorStr = curl_easy_strerror(res);
    }

    if (headerList) curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse get(const std::string& url,
                 const std::vector<std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    curl_slist* headerList = nullptr;
    configureCurl(curl, url, headers, resp.body, headerList);

    CURLcode res;
    int retries = 2;
    while (retries >= 0) {
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) break;
        if (retries > 0) svcSleepThread(500000000ull);
        retries--;
    }

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.statusCode);
    } else {
        resp.errorStr = curl_easy_strerror(res);
    }

    if (headerList) curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse postMultipart(const std::string& url,
                            const std::vector<uint8_t>& fileData,
                            const std::string& fieldName,
                            const std::string& filename,
                            const std::vector<std::pair<std::string,std::string>>& extraFields,
                            const std::vector<std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    curl_mime* form = curl_mime_init(curl);

    // Dosya alanı
    curl_mimepart* part = curl_mime_addpart(form);
    curl_mime_name(part, fieldName.c_str());
    curl_mime_filename(part, filename.c_str());
    curl_mime_data(part, reinterpret_cast<const char*>(fileData.data()), fileData.size());
    curl_mime_type(part, "image/jpeg");

    // Ekstra text alanları
    for (const auto& [k, v] : extraFields) {
        curl_mimepart* ep = curl_mime_addpart(form);
        curl_mime_name(ep, k.c_str());
        curl_mime_data(ep, v.c_str(), CURL_ZERO_TERMINATED);
    }

    curl_slist* headerList = nullptr;
    configureCurl(curl, url, headers, resp.body, headerList);

    // configureCurl 45L timeout ayarlıyor, multipart için 60L yapıyoruz (OCR.space yoğun)
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

    CURLcode res;
    int retries = 2;
    while (retries >= 0) {
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) break;
        if (retries > 0) svcSleepThread(500000000ull);
        retries--;
    }

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.statusCode);
    } else {
        resp.errorStr = curl_easy_strerror(res);
    }

    if (headerList) curl_slist_free_all(headerList);
    curl_mime_free(form);
    curl_easy_cleanup(curl);
    return resp;
}

} // namespace HttpClient
