# LightSee 图片查看器 — 设计规格

日期：2026-09-20
状态：已与用户逐段确认
工程：QtWidgetsLight（Qt 5.14.2 msvc2017_64，VS v143 工具链，非 git 仓库）

## 1. 目标

把现有 QtWidgetsLight 空模板改造为一款类似 2345看图王的现代风图片查看器：

- 支持主流格式：PNG / JPG / JPEG / BMP / GIF / WEBP / TIFF / ICO / SVG（Qt 5.14 自带 imageformats 插件）
- 支持 HEIC / HEIF：通过自研 `qheif` Qt 插件 + libheif 预编译二进制实现
- 浏览核心功能：打开、拖拽、翻页、缩放、平移、旋转、翻转、幻灯片、缩略图侧栏、图片信息
- 不含编辑、截图、设壁纸等外围功能（后续分期）

## 2. 已确认的关键决策

| 决策点 | 结论 |
|---|---|
| HEIC 方案 | 内置 libheif，自研 Qt imageformats 插件 |
| libheif 来源 | 官方无 Windows 预编译包（2026-09 核实 v1.23.4/v1.0.15 仅源码）。改为仓内 CMake 脚本构建 libde265+libheif 源码，产物 DLL 提交入库，一次构建免重复编译 |
| 工程落点 | 原地改造 `QtWidgetsLight.vcxproj`，不新建查看器工程 |
| 功能范围 | 第一版仅浏览核心 |
| 界面风格 | 现代看图界面（悬浮工具栏 + 画布 + 可折叠缩略图栏），深色 QSS |
| Qt 版本 | 5.14.2 msvc2017_64 |

## 3. 项目结构

```
QtWidgetsLight/
├── QtWidgetsLight.sln            # 增加 HeifPlugin 子工程
├── QtWidgetsLight.vcxproj        # 改造为主查看器
├── src/                          # 查看器新代码
│   ├── MainWindow.h/.cpp         # 主窗口（.ui promote）
│   ├── ImageView.h/.cpp          # QGraphicsView 画布子类
│   ├── FolderModel.h/.cpp        # 目录图片列表/翻页
│   ├── ThumbnailLoader.h/.cpp    # 异步缩略图
│   ├── SlideShowController.h/.cpp
│   ├── Settings.h/.cpp           # QSettings 封装
│   └── Viewer.ui                 # 主窗口 Designer 文件
├── heif-plugin/
│   ├── HeifPlugin.vcxproj
│   ├── main.cpp                  # QImageIOPlugin 导出
│   └── HeifHandler.h/.cpp        # QImageIOHandler（libheif C API → QImage）
└── third_party/libheif/          # build.ps1 源码构建产物：include/ + bin/*.dll（x64，入库）
```

## 4. HEIC 插件设计

- `HeifHandler : QImageIOHandler`
  - `read()`：`heif_read_from_memory` → 取主图 handle → `heif_decode_image` → 按通道（RGB24/YA8/RGBA32）转 `QImage`；应用 `heif_image_handle_get_primary_rotation` 旋转到显示方向
  - `jitImageProperties()`：读取宽/高/格式属性
  - 支持 `heic / heif / hif` 后缀与 MIME 嗅探（`ftyp` box）
- `main.cpp`：`QImageIOPlugin::create()` 返回 handler，`keys()` 声明三后缀
- 部署：`qheif.dll` → exe 目录 `imageformats/`；`libheif.dll` 等依赖 DLL → exe 目录
- 降级：插件加载失败时其余格式不受影响，打开 HEIC 提示部署说明
- 风险：Qt 5.14.2 官方安装器不带 libheif 头，需从 release 包/源码补 `libheif/heif.h`；libheif ≥1.x 的 C API 稳定

## 5. 查看器 UI 与交互

主窗口 `Viewer.ui`（Qt Designer 手工编辑 XML 或由用户确认后代写 .ui 内容，代码中仅 `ui.setupUi` + 显式 connect）：

- 中央 `QGraphicsView`（promote 为 `ImageView`）+ `QGraphicsScene`
- 顶部/底部浮动工具条（QToolBar + QSS 半透明）：打开、上一张、下一张、缩小/放大、适应窗口、1:1、左/右旋转、水平/垂直翻转、幻灯片、删除、信息、缩略图栏开关、全屏
- 右侧可折叠缩略图栏：`QListWidget` IconMode，当前项高亮，点击跳转
- 状态栏：文件名 (索引/总数) · 分辨率 · 文件大小 · 缩放百分比

交互规则：

| 输入 | 行为 |
|---|---|
| 滚轮 | 以光标为中心缩放（5%~800%，超范围自动切 适应/1:1） |
| 左键拖拽 | 平移（大图时）；双击画布=1:1/适应切换 |
| ← / → / 鼠标侧键 | 上一张 / 下一张（循环） |
| + / - / 0 / 1 | 放大 / 缩小 / 适应窗口 / 1:1 |
| L / R | 左旋 / 右旋；H / V 水平/垂直翻转 |
| Space | 播放/暂停幻灯片；F 或 F11 全屏；Esc 退出全屏 |
| Delete | 移入回收站（QStorageInfo + Shell API SHFileOperation，需确认框） |
| Ctrl+O | 打开文件对话框；支持拖拽文件/文件夹打开 |

## 6. 核心逻辑

- `FolderModel`：按扩展名过滤当前目录（大小写不敏感），`setCurrent(path)` 定位索引，`next()/prev()` 循环；监听被删文件后自愈
- `ThumbnailLoader`：`QThreadPool`（maxThreadCount 2）+ `ThumbnailTask : QRunnable`；`QImageReader::setScaledSize(160x160)` 解码；`setAutoTransform(true)` 修正 EXIF 方向；用生成序号丢弃过期结果（不依赖 QFutureWatcher 生命周期细节）
- 主图异步加载：同一机制，大图先显示占位；加载失败状态栏提示并允许继续翻页
- 预加载：当前图加载完成后预热相邻 2 张（同缓存键 `path+mtimeMs+targetSize`，LRU 上限 ~500MB 或 24 张）
- `SlideShowController`：QTimer 驱动 `FolderModel::next()`，间隔默认 3s
- `Settings`（QSettings，`LightSee` 组织）：窗口几何/全屏、缩略图栏开关、播放间隔、最近目录、画布背景色（深/浅/透明棋盘格三档）
- HEIC 缩略图自动复用：`QImageReader(path).setScaledSize()` 经 qheif 插件走同一 handler，无特殊分支

## 7. 错误处理

- 不支持/损坏文件：状态栏红字提示 + 跳过继续，不弹窗打断翻页流
- HEIC 打开失败（缺插件/未知子编码）：弹一次可再次提醒的对话框，附部署路径说明
- 删除进回收站失败：退回提示"移入回收站失败"，不做物理删除（安全边界）
- 打开不存在的命令行路径：提示后进入空状态页

## 8. 测试与验收

- `msbuild /p:Configuration=Release /p:Platform=x64` 全解决方案编译通过（含 HeifPlugin）
- 手动冒烟清单：
  1. 九类主流格式 + HEIC 样例各一张打开正常
  2. 翻页环绕、缩略图栏点击跳转、幻灯片播放
  3. 缩放/平移/旋转/翻转/全屏无渲染错误
  4. 删除进回收站 + 列表自愈
  5. 重开 exe 记住窗口位置与侧栏状态
  6. 拔掉 qheif.dll → 其余格式正常，HEIC 给出明确提示
- 本仓库无测试框架，第一版不引入自动化测试

## 9. 范围外（明确不做）

编辑（画笔/裁剪/调色）、截图、设为壁纸、文件关联注册、EXIF 完整面板、AVIF、GIF 动帧（v1 只显示首帧）、批量转换。
