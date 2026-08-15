#include "screenshot.hpp"
#include <switch.h>
#include <cstdlib>
#include <cstring>

// stb 图像解码/编码实现（仅在本文件实例化一次）
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// stb_image_write 原库没有提供内存版 JPEG 写入，这里用 to_func 封装一个。
struct JpgWriteMemContext {
    std::vector<stbi_uc> buffer;
};

static void stbi_jpg_write_mem_func(void* context, void* data, int size) {
    auto* ctx = static_cast<JpgWriteMemContext*>(context);
    const auto* bytes = static_cast<const stbi_uc*>(data);
    ctx->buffer.insert(ctx->buffer.end(), bytes, bytes + size);
}

static stbi_uc* stbi_write_jpg_to_mem(const void* data, int w, int h, int comp, int quality, int* out_len) {
    JpgWriteMemContext ctx;
    if (!stbi_write_jpg_to_func(stbi_jpg_write_mem_func, &ctx, w, h, comp, data, quality)) {
        *out_len = 0;
        return nullptr;
    }
    *out_len = static_cast<int>(ctx.buffer.size());
    if (*out_len == 0) return nullptr;
    stbi_uc* result = static_cast<stbi_uc*>(STBIW_MALLOC(*out_len));
    if (!result) return nullptr;
    std::memcpy(result, ctx.buffer.data(), *out_len);
    return result;
}

namespace ScreenshotCapture {

Screenshot capture(int /*quality*/) {
    Screenshot result;

    // Ekran görüntüsü servisini başlat (Screen Capture Service)
    Result rc = capsscInitialize();
    if (R_FAILED(rc)) {
        return result;
    }

    // JPEG buffer için 512 KB alan ayıralım (1.5 MB sysmodule heap'i için çok büyüktü, OOM/2168-0002 crashine yol açar)
    size_t bufSize = 512 * 1024;
    result.jpegData.resize(bufSize);

    u64 jpegSize = 0;
    // 5 saniye timeout. Önce sadece oyunu çekmeyi deneriz (ApplicationForDebug)
    rc = capsscCaptureJpegScreenShot(&jpegSize, result.jpegData.data(), bufSize, ViLayerStack_ApplicationForDebug, 5000000000ULL);
    
    // Eğer sadece oyunu çekmek başarısız olursa veya boş dönerse, Default (Tüm katmanlar) ile tekrar deneriz
    if (R_FAILED(rc) || jpegSize == 0) {
        jpegSize = 0;
        rc = capsscCaptureJpegScreenShot(&jpegSize, result.jpegData.data(), bufSize, ViLayerStack_Default, 5000000000ULL);
    }
    
    if (R_FAILED(rc) || jpegSize == 0) {
        result.jpegData.clear();
        capsscExit();
        return result;
    }

    result.jpegData.resize(jpegSize);
    result.width  = 1280;
    result.height = 720;

    capsscExit();
    return result;
}

} // namespace ScreenshotCapture

// Screenshot 是全局命名空间的结构体，其成员实现也必须在全局命名空间
bool Screenshot::regionCrop(float rx, float ry, float rw, float rh) {
    if (jpegData.empty()) return false;

    // 解码整张 JPEG → RGB
    int fullW = 0, fullH = 0, channels = 0;
    stbi_uc* full = stbi_load_from_memory(jpegData.data(), (int)jpegData.size(), &fullW, &fullH, &channels, 3);
    if (!full) return false;

    // 归一化区域 → 整图像素矩形
    int x = (int)(rx * fullW);
    int y = (int)(ry * fullH);
    int w = (int)(rw * fullW);
    int h = (int)(rh * fullH);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > fullW) w = fullW - x;
    if (y + h > fullH) h = fullH - y;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    // 区域覆盖整图 → 不裁剪
    if (x == 0 && y == 0 && w == fullW && h == fullH) {
        regionX = 0; regionY = 0; regionW = fullW; regionH = fullH;
        stbi_image_free(full);
        return false;
    }

    // 拷贝区域像素
    std::vector<stbi_uc> sub((size_t)w * h * 3);
    for (int row = 0; row < h; row++) {
        const stbi_uc* src = full + ((size_t)(y + row) * fullW + x) * 3;
        stbi_uc* dst = sub.data() + (size_t)row * w * 3;
        std::memcpy(dst, src, (size_t)w * 3);
    }
    stbi_image_free(full);

    // 重新编码为 JPEG（质量 70）
    int len = 0;
    stbi_uc* jpg = stbi_write_jpg_to_mem(sub.data(), w, h, 3, 70, &len);
    if (!jpg) { return false; }

    jpegData.assign(jpg, jpg + len);
    STBIW_FREE(jpg);

    width  = w;
    height = h;
    regionX = x; regionY = y; regionW = w; regionH = h;
    return true;
}

