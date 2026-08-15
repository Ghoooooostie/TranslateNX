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
- **SD 卡 FTP 根**: `/1. SD卡:/`（中文目录名，MSYS 的 `ftp` 命令行无法 cd，必须用 PowerShell .NET `FtpWebRequest` 且脚本存 UTF-8）
- **ovl 目标路径**: `/1. SD卡:/switch/.overlays/translate.ovl`
- 上传步骤（PowerShell）：
  1. 写 FTP 上传脚本（UploadFile，UseBinary，UsePassive，凭据 anonymous）
  2. 本地 `e:\Project\TranslateNX\overlay\translatenx.ovl` → 远程上述路径（覆盖）
  3. 传完用 GetFileSize 校验字节数一致
- sys-ftpd 不支持对中文路径 rename（返回 553），所以无法在服务器上 rename 备份
- 部署后：在 Switch 上退出并重新进入 overlay 加载器，再打开 TranslateNX

## 已知坑
- **0x559 崩溃**: 无 `heap_size.bin` 时 HOS21+ 默认仅 4MB 堆，TranslateNX(1.2MB) 加载 OOM。
  修复：写入 `/1. SD卡:/config/nx-ovlloader/heap_size.bin`（u64 LE，16MB=0x1000000）。
  OverlayHeapSize 枚举见 `libultrahand/libultra/include/tsl_utils.hpp:644`。
- **OCR 报"Görüntüde metin bulunamadı"**: OCR API 返回成功但无文字（非崩溃）。
  常见于相册(album applet)场景下 `capsscCaptureJpegScreenShot` 可能抓不到 album 画面/抓到空帧。
  触发逻辑：`ScreenshotWaitGui` 等 40 帧→`ScreenshotCapture::capture`→`runOcrSpace`/`runGoogleVision`。
  捕获层：`ViLayerStack_ApplicationForDebug` 优先，失败 fallback `ViLayerStack_Default`（见 `screenshot.cpp`）。
