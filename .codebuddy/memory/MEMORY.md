# TranslateNX 项目长期记忆

## 编译方法
环境（与项目内不完整 devkitpro 区分）：
- **完整 devkitpro 路径**: `D:\Program_Files\mysys2\opt\devkitpro`（MSYS2 安装）
- **make**: `D:\Program_Files\mysys2\usr\bin\make.exe`
- ⚠️ 项目内 `e:\Project\TranslateNX\devkitpro/` 是不完整副本（缺编译器/规则），**绝不能作 DEVKITPRO**

PowerShell 编译命令（完整复制即可用）：
```powershell
cd e:\Project\TranslateNX\overlay
$env:PATH = "D:\Program_Files\mysys2\usr\bin;" + $env:PATH
$env:DEVKITPRO = "D:\Program_Files\mysys2\opt\devkitpro"
make clean
make
```
产出：`overlay/translatenx.ovl`（+ .elf / .nacp）。编译前需联网？否，工具链在本地。

## Switch 升级/部署方法
- **Switch IP**: 192.168.1.6，FTP 端口 21，匿名登录（user/pass = anonymous）
- **SD 卡 FTP 根**: `/1. SD卡:/`（中文目录名）
- **ovl 目标路径**: `/1. SD卡:/switch/.overlays/translate.ovl`
- 上传方式（经验证，2026-08-15）：
  - ❌ **PowerShell `FtpWebRequest` 会 551 失败**：sys-ftpd 不允许对带冒号中文路径 `1. SD卡:` 直接 STOR/CWD；且 `.NET Core/PowerShell 7` 无 `Encoding` 属性（.NET Framework 才有），无法 UTF-8 化路径。
  - ✅ **用 msys2 的 curl.exe 上传成功**：
    ```
    D:\Program_Files\mysys2\usr\bin\curl.exe -v --ftp-pasv --user "anonymous:anonymous" -T "e:/Project/TranslateNX/overlay/translatenx.ovl" "ftp://192.168.1.6/1.%20SD%E5%8D%A1%3A/switch/.overlays/translate.ovl"
    ```
    curl 会自动 CWD 进多层中文目录再 STOR，路径需 URL 编码（`1. SD卡:` → `1.%20SD%E5%8D%A1%3A`）。
  - ⚠️ 不要在 PowerShell 里用 `& $path ... | ...` 形式（会被 cmd 接管管道导致空输出），直接用完整路径调用 curl。
- sys-ftpd 不支持对中文路径 rename（返回 553），无法在服务器上 rename 备份
- 部署后：在 Switch 上退出并重新进入 overlay 加载器，再打开 TranslateNX
- ⚠️ **约定：每次修改完代码都必须部署到 Switch**（用户明确要求）。流程：改代码 → 编译 → FTP 上传 `translatenx.ovl` → 校验字节数。

## 已知坑
- **0x559 崩溃**: 无 `heap_size.bin` 时 HOS21+ 默认仅 4MB 堆，TranslateNX(1.2MB) 加载 OOM。
  修复：写入 `/1. SD卡:/config/nx-ovlloader/heap_size.bin`（u64 LE，16MB=0x1000000）。
  OverlayHeapSize 枚举见 `libultrahand/libultra/include/tsl_utils.hpp:644`。
- **OCR 报"Görüntüde metin bulunamadı"**: OCR API 返回成功但无文字（非崩溃）。
  常见于相册(album applet)场景下 `capsscCaptureJpegScreenShot` 可能抓不到 album 画面/抓到空帧。
  触发逻辑：`ScreenshotWaitGui` 等 40 帧→`ScreenshotCapture::capture`→`runOcrSpace`/`runGoogleVision`。
  捕获层：`ViLayerStack_ApplicationForDebug` 优先，失败 fallback `ViLayerStack_Default`（见 `screenshot.cpp`）。

## 性能优化记录（2026-08-15，解决 OCR 慢）
- **症状**：OCR.space 识别一张 1280×720 截图要好几分钟。
- **根因 + 修复**（`overlay/source/`）：
  1. `http_client.cpp` 删除了自实现的 Google DNS 解析（`resolveDomainToIp` + `g_dnsCache/g_dnsMutex` + `configureCurl` 内的 `CURLOPT_RESOLVE` 块）。HOS21+ 下不需要此 hack，每次冷启动白等 1–10s。
  2. `http_client.cpp` `CURLOPT_TIMEOUT`：通用 15L→45L，multipart 20L→60L（给 OCR.space 排队留耐心）。
  3. `ocr.cpp` `runOcrSpace` 参数瘦身：`scale` 改 `false`（服务端不再先放大再 OCR，巨省时）；新增 `detectOrientation=false`；`OCREngine` 对 jpn/kor/chs/rus/bul/gre/tur 强制 `1`（CJK 专用引擎，比 2 快且准）；retry 由 3 次×1s 改为单次（失败直接返回让用户重按）。
  4. **保留** `isOverlayRequired=true`：`ocrSpaceExtractLines` 依赖 bounding box 做行定位，关掉会渲染错位。
- **预期效果**：Free 计划 30s–5min → 3–8s；Pro 计划 1–3s。
- **未做（远期）**：截图降分辨率（HOS `capsscCaptureJpegScreenShot` 无 quality 参数，硬降需自己重压 JPEG）；换 Google Vision（已有 `runGoogleVision`，有 key 即更快）。
