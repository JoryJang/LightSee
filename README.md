# LightSee 看图（QtWidgetsLight）

基于 Qt 5.14.2（msvc2017_64 / VS v143 工具链）的 Windows 图片查看器，界面风格对标现代看图软件：深色悬浮式工具栏 + 画布 + 可折叠缩略图侧栏。

## 功能一览

- 格式：PNG / JPG / JPEG / BMP / GIF / WEBP / TIFF / ICO / SVG（Qt 自带 imageformats 插件）+ HEIC / HEIF / HIF（仓内自研 `qheif` 插件 + 自构建 libheif/libde265）
- 浏览：打开 / 拖拽文件或文件夹（文件夹解析为目录序第一张图片，CLI 参数同）/ 命令行 `exe <图片或文件夹>`、翻页（循环环绕，键盘 `←→ A D` + 鼠标侧键）、缩略图侧栏点击跳转、幻灯片播放（Space）、全屏（F/F11）
- 视图：滚轮以光标为中心缩放（5%~800%）、左键拖拽平移、双击 1:1/适应切换、旋转 / 翻转、画布背景三档（深色 / 浅色 / 棋盘格）
- 其他：Delete 移入回收站（带确认框，UNC/网络路径直接拒绝以防静默永久删除）、图片信息、预加载相邻 2 张、窗口几何 / 侧栏 / 背景 / 播放间隔持久化（QSettings）

## 构建

### 1. HEIC 依赖（仅首次或依赖变更时；产物已入库，日常无需执行）

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-heif.ps1
```

产物落 `third_party\libheif\{include,bin,lib}`（x64）。

> ⚠️ **libde265 Release 必须保持 `/O1`（勿改回 `/O2`）**：MSVC 14.51（VS18）在默认 `/O2` 下会错误编译 libde265 1.0.15 —— `heif_decode_image` 返回成功但输出绿块/马赛克噪声。`/O1` 是误编译规避，不是降画质；`--selftest` 的像素门（greenFrac/方差/色彩数）会拦住回归。

### 2. 主工程 + 插件

```cmd
build.cmd Release
build.cmd Debug
```

整解决方案（QtWidgetsLight.exe + HeifPlugin/qheif(.d).dll）一次构建；post-build 自动把 `qheif(.d).dll` 拷入 exe 目录 `imageformats\`、`heif.dll`/`libde265.dll` 拷入 exe 目录。

## 运行

```cmd
x64\Release\QtWidgetsLight.exe [图片路径...]
x64\Release\QtWidgetsLight.exe --selftest   :: 22 项自动化检查（解码像素门/翻页/幻灯片/回收站守卫/预加载缓存）
```

开发机 Qt bin 在 PATH 时可直接跑；干净机器请按下面部署。

## 打包部署（干净机器）

```cmd
windeployqt --release --dir dist x64\Release\QtWidgetsLight.exe
```

然后**手工补拷 3 个 HEIC 文件**（windeployqt 不认识自研插件）：

1. `x64\Release\imageformats\qheif.dll` → `dist\imageformats\qheif.dll`
2. `x64\Release\heif.dll` → `dist\heif.dll`
3. `x64\Release\libde265.dll` → `dist\libde265.dll`

已实测：按上述配方得到的 dist 目录在无 Qt PATH 的环境下 `--selftest` 22/22 PASS（含 HEIC 解码像素门）。
Debug 配置的 `qheifd.dll` 由 build.cmd post-build 部署到 `x64\Debug\imageformats\`（selftest 双配置验证 HEIC 链路），但未做 `windeployqt --debug` 验证。

## 使用说明（键盘 / 鼠标）

按键与 `src/MainWindow.cpp::keyPressEvent` 逐项核对：

| 按键 | 功能 |
|---|---|
| `A` / `D` | 上一张 / 下一张（循环） |
| `←` / `→` | 上一张 / 下一张（循环；画布 NoFocus 后直达主窗口，见验收记录） |
| `+` / `-` | 放大 / 缩小 |
| `0` / `1` | 适应窗口 / 1:1 |
| `L` / `R` | 左旋 / 右旋 |
| `H` / `V` | 水平 / 垂直翻转 |
| `Space` | 幻灯片播放 / 暂停（状态栏出现 "▶ 播放中"） |
| `F` / `F11` | 进入 / 退出全屏；`Esc` 仅在全屏时生效 |
| `Delete` | 当前图片移入回收站（弹确认框，默认 No） |
| `Ctrl+O` | 打开文件对话框（QAction 快捷键，焦点在任意控件都生效） |
| 滚轮 | 以光标为中心缩放 |
| 左键拖拽 / 双击 | 平移 / 1:1 与适应切换 |
| 鼠标侧键 Back / Forward | 上一张 / 下一张 |
| 右键画布 | 菜单：画布背景三档、幻灯片间隔 1~10 秒 |
| 拖入文件/文件夹 | 文件直接打开；文件夹打开其中目录序第一张受支持图片（无图则状态栏提示"无法打开"）。CLI 参数 `exe <路径>` 同此语义 |

## 已知限制

- GIF 仅显示首帧；AVIF 不支持（spec §9 范围外）。
- 棋盘格背景按场景坐标平铺（Qt 5.14 `drawBackground` 固有行为）：格子随缩放放大、随旋转转动。
- 放大状态下旋转后视口可能停在空白区（rotateBy 未重跑 fit），按 `0` 恢复。
- `\\?\` 扩展长度路径会被回收站守卫当 UNC 拒绝（优雅失败）。
- 编辑、截图、设壁纸、文件关联、EXIF 完整面板、批量转换均不在第一版范围（spec §9）。

> final-fix pass 已修复并从本清单移除：←/→ 方向键翻页（画布设 NoFocus）、鼠标侧键翻页、`Ctrl+O`、拖拽/CLI 打开文件夹、不存在路径的提示、keyPressEvent 修饰键过滤、删除失败文案"错误码"→"原因"。

## 验收记录（spec §8 冒烟清单，2026-09-20，Task 9）

构建门：`build.cmd Release`、`build.cmd Debug` 均 0 error；`--selftest` Release 22 PASS / EXIT=0，Debug 22 PASS / EXIT=0，两配置输出逐行一致。

| # | 项 | 状态 | 证据 / 方法 |
|---|---|---|---|
| 1 | 九类主流格式 + HEIC 各一张打开正常 | ✅ 机验 | GUI 启动 + EnumWindows 标题 + PrintWindow 截图，10/10 渲染正确：`evidence/task9-formats-01.png` … `task9-formats-10.heic.png` |
| 2a | 翻页环绕 | ✅ 机验 | 第 10 张按 `D` 回到 `01.png (1/10)`；selftest "folder: next/prev wrap"。⚠️ 但方向键 `←/→` 被画布吞（见已知限制与 `task9-arrowkey-blocked.png`），机验用 `A/D` |
| 2b | 缩略图栏点击跳转 | ✅ 机验 | 合成 WM_LBUTTONDOWN 点击第 4 张缩略图 → 标题 `04.bmp (4/10)`：`evidence/task9-thumb-click.png` |
| 2c | 幻灯片播放 | ✅ 机验 | Space 开启自动翻页（标题 2/10→3/10）、"▶ 播放中" + 按钮蓝色按下态：`evidence/task9-slide-playing.png`；Space 停止后标题 3s 内不再变化 |
| 3a | 缩放/旋转/翻转无渲染错误 | ✅ 机验 | `1`/`+`×3 → 195%（`task9-zoom-195.png`）；`L`+`H` 变换正确（`task9-rotate-flip.png`）；`0` 恢复适应 |
| 3b | 全屏 | ✅ 机验 | `F` → 1920×1080 无边框（`task9-fullscreen.png`）；`Esc` → 恢复 1116×739 |
| 3c | 滚轮以光标为中心缩放 | ⚠️ 人工 | 合成 WM_MOUSEWHEEL 被 Qt 按真实光标位置路由、注入无效。人工方法：放大一张大图 → 指针停在图像一侧滚动滚轮，缩放中心应跟随指针 |
| 3d | 左键拖拽平移 | ⚠️ 人工 | 需真实鼠标拖拽。方法：1:1 打开大图，按住左键拖动画布应平移 |
| 3e | 工具栏 hover 高亮 | ⚠️ 人工 | 合成 WM_MOUSEMOVE 不触发 QSS :hover。方法：鼠标悬停工具栏按钮，应出现 #34363c 底色（checked 蓝态已由 2c 截图机验） |
| 4 | 删除进回收站 + 列表自愈 | ✅ 机验 | Delete → 确认框（`task9-delete-dialog.png`）→ 按 Y → 文件真实离开目录、标题 `del2.png (1/1)`、状态栏"已移入回收站"（`task9-after-delete.png`）；另有 selftest recyclebin 6 项（UNC 守卫/长路径/rc 透传） |
| 5 | 重开 exe 记住窗口位置与侧栏状态 | ✅ 机验 | Task 7：closeEvent 写注册表 `win/geometry`、`view/thumbPanel`，重启同位同尺寸 + 侧栏自动展开（`task7-checker-restored.png`）；本次复验：`view/bgMode=1` 跨重启生效 + 侧栏预置 `thumbPanel=true` 自动展开（`task9-thumb-click.png`） |
| 6 | 拔掉 qheif.dll → 其余格式正常、HEIC 明确提示 | ✅ 机验 | Task 6：改名 qheif.dll → selftest FAIL；GUI 弹"无法解码 HEIC"对话框，PNG 正常打开（task-6-report 部署完整性实测一节） |
| 7 | 右键菜单背景三档 | ✅ 机验 | 合成右键弹菜单（`task9-context-menu.png`）+ 键盘导航选"浅色"→ 注册表 `view/bgMode=1` + 截图浅色生效（`task9-bg-light.png`） |
| 8 | 右键菜单幻灯片间隔 | ⚠️ 人工 | 间隔子菜单合成按键两次未命中（同一 `onCanvasMenu` 处理器，背景分支已机验）。方法：右键画布 → 幻灯片间隔 → 选 N 秒，播放节奏应改变 |
| — | 全量 selftest（最终完成点） | ✅ | Release/Debug 各 22 PASS、EXIT=0、输出逐行一致（heic 像素门 greenFrac=0.000 variance=5130.2） |

汇总：机验 9 项 + 构建/selftest 2 项；⚠️ 人工 4 项（滚轮缩放中心、拖拽平移、hover 态、间隔子菜单），均为输入注入手段受限而非功能缺陷；1 个真实缺陷（方向键翻页被画布吞）已列入已知限制。

### final-fix pass 验收补充（2026-09-20）

构建门（修复后重跑）：`build.cmd Release`、`build.cmd Debug` 均 0 error；两配置 `--selftest` 各 22 PASS / EXIT=0，输出逐行一致。

| 项 | 状态 | 验证方法 |
|---|---|---|
| `←`/`→` 方向键翻页 | ✅ 机验 | Viewer.ui 给 canvas 设 `Qt::NoFocus` 后，向**本进程自启窗口** hwnd PostMessage `WM_KEYDOWN/UP`(VK_RIGHT/VK_LEFT)：标题 `img1.png (1/2)` → `(2/2)` → `(1/2)`；`A` 键对照组同步通过 |
| 鼠标侧键 = 上一/下一张 | ✅ 机验 | PostMessage `WM_XBUTTONDOWN/UP`（wParam=MK_XBUTTON2/MK_XBUTTON1，即 Qt 的 Forward/Back）到画布区域：`(1/2)` →`(2/2)`→ `(1/2)` |
| 文件夹打开（CLI/拖拽） | ✅ 机验(CLI) | `%TEMP%\lightsee_t9fix_dir`（img1.png、img2.png + 1 个 .txt 噪声）以目录为 CLI 参数启动 → 标题 `img1.png (1/2)`，txt 被正确过滤。拖拽走 `dropEvent`→`openFile` 同一分支（含 `firstSupportedFile` 目录解析），代码走查确认，真实 shell 拖放为人工场景 |
| 不存在/无图路径提示 + 空态 | ✅ 机验(状态) + 文案走查 | 不存在的目录参数启动：进程不崩、标题保持默认"LightSee 看图"（空态、动作禁用）。提示文本 "无法打开：%1" 在 `MainWindow::openFile` 单一分支内（状态栏文本无 API 可取，属代码走查项） |
| `Ctrl+O` | ✅ 机验 | SetForegroundWindow(自启 pid 主窗) 校验通过后 keybd_event 注入：出现新前台窗口，类名 `#32770`（Win32 打开对话框）、标题"打开图片"；Esc 关闭后应用正常 |
| keyPressEvent 修饰键过滤 | ✅ 机验 | 真实 Ctrl(down)+→ 注入：标题不变（不再误触发翻页）；松开 Ctrl 后单独 → 恢复翻页。非目标窗口零注入（每步先验证 GetForegroundWindow==自启 hwnd） |
