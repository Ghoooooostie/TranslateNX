#pragma once
#include <string>
#include <cstdint>
#include <vector>

struct Screenshot {
    std::vector<uint8_t> jpegData;  // JPEG ham verisi
    int width  = 1280;
    int height = 720;

    // regionCrop() 裁剪后，记录被裁剪区域在原整图(1280x720)中的像素偏移，
    // 供 OCR 识别结果从"小图坐标"还原回"整图坐标"。未裁剪时为整图。
    int regionX = 0;
    int regionY = 0;
    int regionW = 1280;
    int regionH = 720;

    // 按归一化区域(相对 1280x720)裁剪并重编码为 JPEG。
    // 裁剪后 jpegData/width/height 变成小图，regionX/Y/W/H 记录原图偏移。
    // 返回 true 表示确实裁剪了（false 表示区域覆盖全图、未裁剪）。
    bool regionCrop(float rx, float ry, float rw, float rh);
};

namespace ScreenshotCapture {
    // Ekranın şu anki görüntüsünü JPEG olarak alır.
    // quality: 1-100 arası JPEG kalitesi (60-70 önerilen — RAM tasarrufu)
    // Dönüş: dolu Screenshot → başarılı, boş jpegData → hata
    Screenshot capture(int quality = 65);
}
