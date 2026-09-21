# LightSee 图片查看器 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 QtWidgetsLight 空模板改造为支持主流格式 + HEIC 的 Qt5 桌面图片查看器（2345看图王风格）。

**Architecture:** 主工程原地改造（MainWindow + QGraphicsView 画布 + 缩略图栏），HEIC 通过新增 HeifPlugin 子工程（QImageIOPlugin，调用 libheif）实现；libheif/libde265 用仓内 PowerShell 脚本从官方源码构建一次，产物提交入库。

**Tech Stack:** Qt 5.14.2 msvc2017_64、VS2022 v143、Qt VS Tools、CMake、libde265 v1.0.15、libheif v1.23.4。

**Spec:** `docs/superpowers/specs/2026-09-20-image-viewer-design.md`（先读它再动手）

## Global Constraints

- Qt 版本固定 **5.14.2 msvc2017_64**（`C:\Qt\Qt5.14.2\5.14.2\msvc2017_64`），仅 x64 配置。
- **信号槽必须显式 `connect()`**；禁止 `on_<objectName>_<signal>` auto-connect 命名约定。
- **UI 布局优先 .ui 文件**；自定义控件通过 .ui promote 引入；代码中不得手工 new 布局控件树。
- 本目录不是 git 仓库：每个任务以"构建成功 + 任务内验证通过"作为完成点，无 commit 步骤；不得顺手 `git init`。
- 主工程不引入第三方 GUI 依赖；唯一的第三方库是 libheif（插件私有）。
- 构建命令统一用仓库根 `build.cmd`（Task 1 创建）。
- 所有新文件 UTF-8 with BOM 或纯 ASCII 均可；界面文案用中文。

**里程碑说明：** Task 1/2 是 HEIC 依赖链，Task 3-8 是查看器，Task 9 收尾。Task 3 结束即可运行程序看普通图片。

---

### Task 1: HEIC 依赖构建（libde265 + libheif）与构建脚本

**Files:**
- Create: `scripts/build-heif.ps1`
- Create: `build.cmd`
- Create（构建产物，入库）: `third_party/libheif/include/libheif/*.h`、`third_party/libheif/bin/heif.dll`、`third_party/libheif/bin/libde265.dll`、`third_party/libheif/lib/heif.lib`
- Create（测试样例）: `third_party/testdata/example.heic`

**Interfaces:**
- Consumes: 网络（github.com）、本机 CMake（VS2022 自带）、MSVC v143。
- Produces: `third_party/libheif/` 三件套（include/lib/bin），供 Task 2 的 HeifPlugin 链接；`third_party/testdata/example.heic` 供 Task 2/6 验证。

- [ ] **Step 1: 前置探测**

```cmd
cmake --version
```
预期：`cmake version 3.2x`。若命令不存在，停止并告知用户需在 VS Installer 勾选"用于 Windows 的 C++ CMake"。

- [ ] **Step 2: 写 `build.cmd`（全任务共用的 msbuild 包装）**

```bat
@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i
"%MSBUILD%" "%~dp0QtWidgetsLight.sln" /t:Build /p:Configuration=%1 /p:Platform=x64 /m:1 /nologo /v:m
exit /b %ERRORLEVEL%
```

- [ ] **Step 3: 写 `scripts/build-heif.ps1`**

```powershell
# 一次性构建 libde265 + libheif，产物安装到 ..\third_party\libheif
$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot          # 仓库根
$work  = Join-Path $root "third_party\libheif\_build"
New-Item -ItemType Directory -Force $work | Out-Null
function Fetch-Extract($url, $name) {
  $tgz = Join-Path $work "$name.tar.gz"
  if (-not (Test-Path $tgz)) { curl.exe -sL $url -o $tgz }
  if (-not (Test-Path (Join-Path $work $name))) { tar -xzf $tgz -C $work }
}
Fetch-Extract "https://github.com/strukturag/libde265/releases/download/v1.0.15/libde265-1.0.15.tar.gz" "libde265-1.0.15"
Fetch-Extract "https://github.com/strukturag/libheif/releases/download/v1.23.4/libheif-1.23.4.tar.gz"  "libheif-1.23.4"

$de265 = Join-Path $work "libde265-1.0.15"
cmake -S $de265 -B "$de265\b" -A x64
cmake --build "$de265\b" --config Release
cmake --install "$de265\b" --prefix (Join-Path $work "de265-install")

cmake -S "$work\libheif-1.23.4" -B "$work\heif-b" -A x64 `
  -DWITH_EXAMPLES=OFF -DBUILD_TESTING=OFF `
  -DCMAKE_PREFIX_PATH="$work\de265-install" `
  -DCMAKE_INSTALL_PREFIX="$root\third_party\libheif"
cmake --build "$work\heif-b" --config Release
cmake --install "$work\heif-b" --config Release

# 测试样例（来自 libheif 源码包 examples/example.heic）
New-Item -ItemType Directory -Force "$root\third_party\testdata" | Out-Null
Copy-Item "$work\libheif-1.23.4\examples\example.heic" "$root\third_party\testdata\example.heic" -Force
Write-Host "OK: third_party\libheif and testdata ready"
```

- [ ] **Step 4: 执行**

```cmd
powershell -ExecutionPolicy Bypass -File scripts\build-heif.ps1
```
预期末尾 `OK: third_party\libheif and testdata ready`（约 5-10 分钟）。若 libheif 只产出静态库，补 `-DWITH_SHARED_LIBS=ON` 重跑。

- [ ] **Step 5: 验证产物**

```cmd
dir /b third_party\libheif\include\libheif\heif.h third_party\libheif\bin\heif.dll third_party\libheif\bin\libde265.dll third_party\libheif\lib\heif.lib third_party\testdata\example.heic
```
预期五个路径全部存在。

---

### Task 2: HeifPlugin 子工程（qheif.dll）

**Files:**
- Create: `heif-plugin/main.cpp`、`heif-plugin/HeifHandler.h`、`heif-plugin/HeifHandler.cpp`、`heif-plugin/heif.json`、`heif-plugin/HeifPlugin.vcxproj`、`heif-plugin/HeifPlugin.vcxproj.filters`
- Modify: `QtWidgetsLight.sln`（注册新工程）

**Interfaces:**
- Consumes: Task 1 的 `third_party/libheif/{include,lib,bin}`；`third_party/testdata/example.heic`。
- Produces: `heif-plugin\$(Platform)\$(Configuration)\qheif.dll`（Task 3 的 post-build 拷贝它）；`HeifHandler : public QImageIOHandler`，`HeifPlugin : public QImageIOPlugin`。

- [ ] **Step 1: `heif-plugin/heif.json`**

```json
{ "Keys": [ { "Extensions": ["heic","heif","hif"], "MimeTypes": ["image/heif","image/heic"] } ] }
```

- [ ] **Step 2: `heif-plugin/HeifHandler.h`**

```cpp
#pragma once
#include <qimageiohandler.h>

class HeifHandler : public QImageIOHandler
{
public:
    explicit HeifHandler(QIODevice* device);
    bool canRead() const override;
    bool read(QImage* image) override;
    static bool sniff(QIODevice* device);   // Task 内共享：ftyp magic 检测
};
```

- [ ] **Step 3: `heif-plugin/HeifHandler.cpp`**

```cpp
#include "HeifHandler.h"
#include <libheif/heif.h>
#include <QBuffer>

static const char* kBrands[] = { "heic","heix","heim","heis","hev1","hvc1","mif1","msf1" };

HeifHandler::HeifHandler(QIODevice* device) : QImageIOHandler(device) {}

bool HeifHandler::sniff(QIODevice* device)
{
    const qint64 pos = device->pos();
    char hdr[12] = {};
    const qint64 n = device->read(hdr, 12);
    device->seek(pos);
    if (n < 12 || memcmp(hdr + 4, "ftyp", 4) != 0)
        return false;
    for (const char* b : kBrands)
        if (memcmp(hdr + 8, b, 4) == 0) return true;
    return false;
}

bool HeifHandler::canRead() const
{
    return HeifHandler::sniff(device());
}

bool HeifHandler::read(QImage* image)
{
    const QByteArray data = device()->readAll();
    heif_context* ctx = heif_context_alloc();
    if (heif_context_read_from_memory(ctx, data.constData(), data.size(), nullptr).code != heif_error_Ok) {
        heif_context_free(ctx); return false;
    }
    heif_image_handle* handle = nullptr;
    if (heif_context_get_primary_image_handle(ctx, &handle).code != heif_error_Ok) {
        heif_context_free(ctx); return false;
    }
    const bool hasAlpha = heif_image_handle_has_alpha_channel(handle);
    heif_decoding_options* opt = heif_decoding_options_alloc();
    opt->convert_hdr_to_8bit = true;           // 10-bit HEIC → 8bit
    heif_image* img = nullptr;
    const heif_error e = heif_decode_image(handle, &img, heif_colorspace_RGB,
        hasAlpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB, opt);
    heif_decoding_options_free(opt);
    if (e.code != heif_error_Ok) { heif_context_free(ctx); return false; }

    int w = heif_image_get_width(img, heif_channel_interleaved);
    int h = heif_image_get_height(img, heif_channel_interleaved);
    int stride = 0;
    const uint8_t* px = static_cast<const uint8_t*>(
        heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride));
    const QImage::Format fmt = hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    QImage out = px ? QImage(px, w, h, stride, fmt).copy() : QImage();

    heif_image_release(img);
    heif_image_handle_release(handle);
    heif_context_free(ctx);
    if (out.isNull()) return false;
    *image = out;
    setSize(out.size());
    return true;
}
```

注：libheif 默认应用 irot/clap 变换（`ignore_transformations` 未置位），无需手动旋转。

- [ ] **Step 4: `heif-plugin/main.cpp`**

```cpp
#include <qimageioplugin.h>
#include "HeifHandler.h"

class HeifPlugin : public QImageIOPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface" FILE "heif.json")
public:
    Capabilities capabilities(QIODevice* device, const QByteArray& format) const override
    {
        Q_UNUSED(format);
        return (device && HeifHandler::sniff(device)) ? CanRead : Capabilities();
    }
    QImageIOHandler* create(QIODevice* device) override { return new HeifHandler(device); }
};
```

- [ ] **Step 5: `heif-plugin/HeifPlugin.vcxproj`**

以 `QtWidgetsLight.vcxproj` 为模板复制一份，然后逐条应用以下差异（其余保持）：

1. `<ProjectGuid>` 改为 `{9C4F0A11-2B3C-4D5E-8F60-1A2B3C4D5E6F}`；删掉 `<RootNamespace>`/`<Keyword>QtVS_v304</Keyword>` 保留原值。
2. 两个 PropertyGroup 中 `<QtModules>core;gui;widgets</QtModules>` 改为 `<QtModules>core;gui</QtModules>`。
3. Debug|x64 与 Release|x64 两处 `<ConfigurationType>Application</ConfigurationType>` 改为 `DynamicLibrary`。
4. `<OutDir>` 两处改为 `$(SolutionDir)heif-plugin\$(Platform)\$(Configuration)\`，`<IntDir>` 同路径；Release 的 `<TargetName>` 设为 `qheif`，Debug 设为 `qheifd`。
5. 两处 ItemDefinitionGroup 加：
   `<AdditionalIncludeDirectories>$(SolutionDir)third_party\libheif\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>`
   `<AdditionalDependencies>$(SolutionDir)third_party\libheif\lib\heif.lib;%(AdditionalDependencies)</AdditionalDependencies>`
6. 删掉原工程的 ClCompile/QtMoc/QtUic/QtRcc ItemGroup，替换为：
```xml
<ItemGroup>
  <ClCompile Include="main.cpp" />
  <ClCompile Include="HeifHandler.cpp" />
  <QtMoc Include="main.cpp" />
  <QtMoc Include="HeifHandler.h" />
</ItemGroup>
```
（`main.cpp` 内含 Q_OBJECT，需同时进 ClCompile 与 QtMoc；Qt VS Tools 会处理 #include "main.moc"——因此在 `main.cpp` 末尾追加一行 `#include "main.moc"`。）
7. `<None Include="heif.json" />` 加入 ItemGroup。

- [ ] **Step 6: 注册到 .sln**

`QtWidgetsLight.sln` 的 `Project(...)` 段之后追加：

```
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "HeifPlugin", "heif-plugin\HeifPlugin.vcxproj", "{9C4F0A11-2B3C-4D5E-8F60-1A2B3C4D5E6F}"
EndProject
```
并在 `GlobalSection(ProjectConfigurationPlatforms)` 内追加四行：

```
		{9C4F0A11-2B3C-4D5E-8F60-1A2B3C4D5E6F}.Debug|x64.ActiveCfg = Debug|x64
		{9C4F0A11-2B3C-4D5E-8F60-1A2B3C4D5E6F}.Debug|x64.Build.0 = Debug|x64
		{9C4F0A11-2B3C-4D5E-8F60-1A2B3C4D5E6F}.Release|x64.ActiveCfg = Release|x64
		{9C4F0A11-2B3C-4D5E-8F60-1A2B3C4D5E6F}.Release|x64.Build.0 = Release|x64
```

（先 `sln` 中确认既有工程的平台映射里只有 x64 条目；若有 Win32 平台则一并为其补 ActiveCfg 行，指向 Win32 时改为 `HeifPlugin` 不参与：ActiveCfg = Release|x64、删 Build.0。）

- [ ] **Step 7: 构建并验证插件**

```cmd
build.cmd Release
```
预期两工程均编译成功，存在 `heif-plugin\x64\Release\qheif.dll`。

---

### Task 3: 主工程骨架——打开单张图片并显示

**Files:**
- Create: `src/Viewer.ui`、`src/MainWindow.h`、`src/MainWindow.cpp`、`src/ImageView.h`、`src/ImageView.cpp`、`src/SelfTest.h`、`src/SelfTest.cpp`
- Modify: `QtWidgetsLight.vcxproj`（ItemGroup、QtModules）、`main.cpp`（整体重写）
- Delete: `QtWidgetsLight.h`、`QtWidgetsLight.cpp`、`QtWidgetsLight.ui`（及其在 vcxproj/filters 中的条目）

**Interfaces:**
- Consumes: qheif.dll、heif.dll、libde265.dll（Task 1/2）。
- Produces: `MainWindow`（后续所有任务在此扩展）、`ImageView::setImage/fitToWindow/setActualSize/zoomBy/rotateBy/flipH/flipV/zoomChanged`、`SelfTest::run() -> int`。

- [ ] **Step 1: `src/Viewer.ui`（完整内容）**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>ViewerForm</class>
 <widget class="QMainWindow" name="ViewerForm">
  <property name="geometry"><rect><x>0</x><y>0</y><width>1100</width><height>700</height></rect></property>
  <property name="windowTitle"><string>LightSee 看图</string></property>
  <widget class="QWidget" name="centralArea">
   <layout class="QHBoxLayout" name="centerLayout">
    <property name="spacing"><number>0</number></property>
    <property name="leftMargin"><number>0</number></property>
    <property name="topMargin"><number>0</number></property>
    <property name="rightMargin"><number>0</number></property>
    <property name="bottomMargin"><number>0</number></property>
    <item><widget class="ImageView" name="canvas">
      <property name="mouseTracking"><bool>true</bool></property>
      <property name="acceptDrops"><bool>true</bool></property>
    </widget></item>
    <item><widget class="QListWidget" name="thumbPanel">
      <property name="sizePolicy">
        <sizepolicy hsizetype="Fixed" vsizetype="Expanding"><horstretch>0</horstretch><verstretch>0</verstretch></sizepolicy>
      </property>
      <property name="minimumSize"><size><width>180</width><height>0</height></size></property>
      <property name="maximumSize"><size><width>180</width><height>16777215</height></size></property>
      <property name="viewMode"><enum>QListView::IconMode</enum></property>
      <property name="iconSize"><size><width>150</width><height>110</height></size></property>
      <property name="resizeMode"><enum>QListView::Adjust</enum></property>
      <property name="movement"><enum>QListView::Static</enum></property>
      <property name="spacing"><number>6</number></property>
      <property name="uniformItemSizes"><bool>true</bool></property>
      <property name="horizontalScrollBarPolicy"><enum>Qt::ScrollBarAlwaysOff</enum></property>
      <property name="selectionBehavior"><enum>QAbstractItemView::SelectItems</enum></property>
      <property name="focusPolicy"><enum>Qt::NoFocus</enum></property>
      <property name="frameShape"><enum>QFrame::NoFrame</enum></property>
     </widget></item>
   </layout>
  </widget>
  <widget class="QStatusBar" name="statusBar"/>
  <widget class="QToolBar" name="toolBar">
   <property name="toolButtonStyle"><enum>Qt::ToolButtonIconOnly</enum></property>
   <property name="movable"><bool>false</bool></property>
   <property name="floatable"><bool>false</bool></property>
   <attribute name="toolBarArea"><enum>TopToolBarArea</enum></attribute>
   <attribute name="toolBarBreak"><bool>false</bool></attribute>
   <addaction name="actOpen"/>
   <addaction name="separator"/>
   <addaction name="actPrev"/>
   <addaction name="actNext"/>
   <addaction name="separator"/>
   <addaction name="actZoomIn"/>
   <addaction name="actZoomOut"/>
   <addaction name="actFit"/>
   <addaction name="actActual"/>
   <addaction name="separator"/>
   <addaction name="actRotateL"/>
   <addaction name="actRotateR"/>
   <addaction name="actFlipH"/>
   <addaction name="actFlipV"/>
   <addaction name="separator"/>
   <addaction name="actSlide"/>
   <addaction name="actDelete"/>
   <addaction name="actInfo"/>
   <addaction name="separator"/>
   <addaction name="actPanel"/>
  </widget>
  <action name="actOpen"><property name="text"><string>打开</string></property></action>
  <action name="actPrev"><property name="text"><string>上一张</string></property></action>
  <action name="actNext"><property name="text"><string>下一张</string></property></action>
  <action name="actZoomIn"><property name="text"><string>放大</string></property></action>
  <action name="actZoomOut"><property name="text"><string>缩小</string></property></action>
  <action name="actFit"><property name="text"><string>适应窗口</string></property></action>
  <action name="actActual"><property name="text"><string>1:1</string></property></action>
  <action name="actRotateL"><property name="text"><string>左转</string></property></action>
  <action name="actRotateR"><property name="text"><string>右转</string></property></action>
  <action name="actFlipH"><property name="text"><string>水平翻转</string></property></action>
  <action name="actFlipV"><property name="text"><string>垂直翻转</string></property></action>
  <action name="actSlide"><property name="checkable"><bool>true</bool></property><property name="text"><string>幻灯片</string></property></action>
  <action name="actDelete"><property name="text"><string>删除</string></property></action>
  <action name="actInfo"><property name="text"><string>信息</string></property></action>
  <action name="actPanel"><property name="checkable"><bool>true</bool></property><property name="checked"><bool>true</bool></property><property name="text"><string>缩略图栏</string></property></action>
 </widget>
 <customwidgets>
  <customwidget>
   <class>ImageView</class><extends>QGraphicsView</extends><header>src/ImageView.h</header>
  </customwidget>
 </customwidgets>
 <resources/>
 <connections/>
</ui>
```

- [ ] **Step 2: `src/ImageView.h`**

```cpp
#pragma once
#include <QGraphicsView>

class QGraphicsPixmapItem;

class ImageView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);
    void setImage(const QImage& img);
    void clearImage();
public slots:
    void fitToWindow();
    void setActualSize();
    void zoomBy(double factor);
    void rotateBy(double degrees);
    void flipHorizontal();
    void flipVertical();
signals:
    void zoomChanged(double factor);
protected:
    void wheelEvent(QWheelEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
private:
    void applyItemTransform();
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_item = nullptr;
    double m_rotation = 0.0;
    bool m_flipH = false, m_flipV = false;
    bool m_fitMode = true;
};
```

- [ ] **Step 3: `src/ImageView.cpp`**

```cpp
#include "src/ImageView.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QWheelEvent>
#include <QMouseEvent>

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setDragMode(ScrollHandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor(0x1e, 0x1f, 0x22));
}

void ImageView::setImage(const QImage& img)
{
    m_scene->clear();
    m_item = m_scene->addPixmap(QPixmap::fromImage(img));
    m_item->setTransformationMode(Qt::SmoothTransformation);
    applyItemTransform();
    if (m_fitMode) fitToWindow(); else emit zoomChanged(transformation().m11());
}

void ImageView::clearImage()
{
    m_scene->clear();
    m_item = nullptr;
}

void ImageView::fitToWindow()
{
    if (!m_item) return;
    m_fitMode = true;
    resetTransform();
    fitInView(m_item, Qt::KeepAspectRatio);
    emit zoomChanged(transformation().m11());
}

void ImageView::setActualSize()
{
    if (!m_item) return;
    m_fitMode = false;
    resetTransform();
    applyItemTransform();
    emit zoomChanged(1.0);
}

void ImageView::zoomBy(double factor)
{
    if (!m_item) return;
    const double z = transformation().m11() * factor;
    if (z < 0.05 || z > 8.0) return;
    m_fitMode = false;
    scale(factor, factor);
    emit zoomChanged(transformation().m11());
}

void ImageView::rotateBy(double degrees) { m_rotation = std::fmod(m_rotation + degrees, 360.0); applyItemTransform(); }
void ImageView::flipHorizontal() { m_flipH = !m_flipH; applyItemTransform(); }
void ImageView::flipVertical()   { m_flipV = !m_flipV; applyItemTransform(); }

void ImageView::applyItemTransform()
{
    if (!m_item) return;
    m_item->setTransform(QTransform()
        .rotate(m_rotation)
        .scale(m_flipH ? -1 : 1, m_flipV ? -1 : 1));
}

void ImageView::wheelEvent(QWheelEvent* e)
{
    zoomBy(e->angleDelta().y() > 0 ? 1.25 : 0.8);
    e->accept();
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        if (m_fitMode) setActualSize(); else fitToWindow();
        e->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(e);
}

void ImageView::resizeEvent(QResizeEvent* e)
{
    QGraphicsView::resizeEvent(e);
    if (m_fitMode) fitToWindow();
}
```

- [ ] **Step 4: `src/MainWindow.h/.cpp`（骨架：打开 + 显示，显式 connect）**

`MainWindow.h`：

```cpp
#pragma once
#include <QMainWindow>
#include "ui_Viewer.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openFile(const QString& path);
private slots:
    void onOpen();
    void onFit();
    void onActual();
    void onZoomIn();
    void onZoomOut();
    void onRotateLeft();
    void onRotateRight();
    void onFlipH();
    void onFlipV();
    void onTogglePanel();
    void onZoomChanged(double factor);
    void onLoadFinished(const QString& path, const QImage& img);
private:
    void startLoad(const QString& path);
    Ui::ViewerForm ui;
    QString m_currentPath;
    quint64 m_loadSeq = 0;
};
```

`MainWindow.cpp`（本任务范围内仅实现上述功能；Task 4+ 扩展）：

```cpp
#include "src/MainWindow.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QtConcurrentRun>
#include <QFutureWatcher>

static QImage decodeImage(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    ui.setupUi(this);
    ui.thumbPanel->setVisible(false);        // Task 4 启用
    ui.actPanel->setChecked(false);
    setAcceptDrops(true);

    connect(ui.actOpen,     &QAction::triggered, this, &MainWindow::onOpen);
    connect(ui.actFit,      &QAction::triggered, this, &MainWindow::onFit);
    connect(ui.actActual,   &QAction::triggered, this, &MainWindow::onActual);
    connect(ui.actZoomIn,   &QAction::triggered, this, &MainWindow::onZoomIn);
    connect(ui.actZoomOut,  &QAction::triggered, this, &MainWindow::onZoomOut);
    connect(ui.actRotateL,  &QAction::triggered, this, &MainWindow::onRotateLeft);
    connect(ui.actRotateR,  &QAction::triggered, this, &MainWindow::onRotateRight);
    connect(ui.actFlipH,    &QAction::triggered, this, &MainWindow::onFlipH);
    connect(ui.actFlipV,    &QAction::triggered, this, &MainWindow::onFlipV);
    connect(ui.actPanel,    &QAction::toggled,   this, &MainWindow::onTogglePanel);
    connect(ui.canvas,      &ImageView::zoomChanged, this, &MainWindow::onZoomChanged);
}

void MainWindow::onOpen()
{
    static QString dir = QDir::homePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("打开图片"), dir,
        tr("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff *.ico *.svg *.heic *.heif *.hif);;所有文件 (*)"));
    if (!path.isEmpty()) { dir = QFileInfo(path).absolutePath(); openFile(path); }
}

void MainWindow::openFile(const QString& path)
{
    m_currentPath = path;
    startLoad(path);
}

void MainWindow::startLoad(const QString& path)
{
    ++m_loadSeq;
    const quint64 seq = m_loadSeq;
    auto* w = new QFutureWatcher<QImage>(this);
    connect(w, &QFutureWatcher<QImage>::finished, this, [this, w, seq, path]() {
        const QImage img = w->result();
        w->deleteLater();
        if (seq == m_loadSeq) onLoadFinished(path, img);
    });
    w->setFuture(QtConcurrent::run(&decodeImage, path));
    ui.statusBar->showMessage(tr("正在加载 %1 …").arg(QFileInfo(path).fileName()));
}

void MainWindow::onLoadFinished(const QString& path, const QImage& img)
{
    if (img.isNull()) {
        ui.statusBar->showMessage(tr("无法读取图片：%1").arg(QFileInfo(path).fileName()));
        ui.canvas->clearImage();
        return;
    }
    ui.canvas->setImage(img);
    const QFileInfo fi(path);
    ui.statusBar->showMessage(QString());
    setWindowTitle(tr("%1 - LightSee 看图").arg(fi.fileName()));
}

void MainWindow::onFit()    { ui.canvas->fitToWindow(); }
void MainWindow::onActual() { ui.canvas->setActualSize(); }
void MainWindow::onZoomIn() { ui.canvas->zoomBy(1.25); }
void MainWindow::onZoomOut(){ ui.canvas->zoomBy(0.8); }
void MainWindow::onRotateLeft()  { ui.canvas->rotateBy(-90); }
void MainWindow::onRotateRight() { ui.canvas->rotateBy(90); }
void MainWindow::onFlipH()  { ui.canvas->flipHorizontal(); }
void MainWindow::onFlipV()  { ui.canvas->flipVertical(); }
void MainWindow::onTogglePanel(){ ui.thumbPanel->setVisible(ui.actPanel->isChecked()); }
void MainWindow::onZoomChanged(double f) { /* Task 4 起写入状态栏 */ Q_UNUSED(f); }
```

拖拽打开：`MainWindow` 重写 `dragEnterEvent/dropEvent`（接受 `text/uri-list` 中首个本地文件 → `openFile`），在此文件补上，5 行。

- [ ] **Step 5: `src/SelfTest.h/.cpp` + `main.cpp` 重写**

`SelfTest.h`：
```cpp
#pragma once
namespace SelfTest { int run(); }   // 0=全部通过，非 0=失败条数
```

`SelfTest.cpp`（本任务先建 harness + 第一个用例，后续任务往里加）：
```cpp
#include "src/SelfTest.h"
#include <QCoreApplication>
#include <QImageReader>
#include <QFileInfo>
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond, name) do { \
    std::printf("%s  %s\n", (cond) ? "PASS" : "FAIL", name); \
    if (!(cond)) ++g_fail; } while (0)

int SelfTest::run()
{
    const QString sample = QCoreApplication::applicationDirPath()
                           + "/../../third_party/testdata/example.heic";
    QImageReader r(sample);
    QImage img = r.read();
    CHECK(!img.isNull() && img.width() > 0, "heic: qheif plugin decodes example.heic");
    return g_fail;
}
```

`main.cpp`：
```cpp
#include <QtWidgets/QApplication>
#include "src/MainWindow.h"
#include "src/SelfTest.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    if (args.contains("--selftest")) return SelfTest::run();
    MainWindow w;
    w.resize(1100, 700);
    w.show();
    for (const QString& a : args) {
        if (QFileInfo::exists(a)) { w.openFile(a); break; }
    }
    return app.exec();
}
```

- [ ] **Step 6: 修改 `QtWidgetsLight.vcxproj`**

1. 两处 `<QtModules>core;gui;widgets</QtModules>` → `core;gui;widgets;concurrent`。
2. 删除条目：`QtWidgetsLight.cpp`（ClCompile）、`QtWidgetsLight.h`（QtMoc）、`QtWidgetsLight.ui`（QtUic）。
3. 新增 ItemGroup（Release 与 Debug 共用的那个 ItemGroup）：

```xml
<ItemGroup>
  <ClCompile Include="src\MainWindow.cpp" />
  <ClCompile Include="src\ImageView.cpp" />
  <ClCompile Include="src\SelfTest.cpp" />
  <QtMoc Include="src\MainWindow.h" />
  <QtMoc Include="src\ImageView.h" />
  <QtUic Include="src\Viewer.ui" />
</ItemGroup>
```
（uic 头文件路径：Qt VS Tools 把 `ui_Viewer.h` 生成到中间目录，`MainWindow.h` 中 `#include "ui_Viewer.h"` 直接可用。）
4. 删除物理文件 `QtWidgetsLight.h/.cpp/.ui`；同步清理 `QtWidgetsLight.vcxproj.filters` 中对应条目。
5. 两个配置 ItemDefinitionGroup 各加 post-build：

```xml
<PostBuildEvent>
  <Command>if not exist "$(OutDir)imageformats" mkdir "$(OutDir)imageformats"
xcopy /y "$(SolutionDir)heif-plugin\$(Platform)\$(Configuration)\qheif*.dll" "$(OutDir)imageformats\"
xcopy /y "$(SolutionDir)third_party\libheif\bin\*.dll" "$(OutDir)"</Command>
  <Message>部署 HEIC 插件与 DLL</Message>
</PostBuildEvent>
```

- [ ] **Step 7: 构建 + 双验证**

```cmd
build.cmd Release
x64\Release\QtWidgetsLight.exe --selftest
```
预期：编译 0 error；selftest 输出 `PASS  heic: qheif plugin decodes example.heic` 且退出码 0。再跑
```cmd
start "" x64\Release\QtWidgetsLight.exe third_party\testdata\example.heic
```
预期窗口显示该 HEIC 图；工具条放大/缩小/翻转/适应可操作。

---

### Task 4: FolderModel + Settings + 翻页 + 缩略图栏

**Files:**
- Create: `src/FolderModel.h/.cpp`、`src/Settings.h/.cpp`、`src/ThumbnailLoader.h/.cpp`
- Modify: `src/MainWindow.h/.cpp`、`src/SelfTest.cpp`

**Interfaces:**
- Consumes: Task 3 的 MainWindow 骨架。
- Produces:
  - `static QStringList FolderModel::supportedExtensions()`（`png jpg jpeg bmp gif webp tif tiff ico svg heic heif hif`，无点、小写）
  - `bool FolderModel::setPath(const QString& filePath)`；`QStringList files()`；`int index()`；`QString current()`；`QString next()`/`prev()`（循环）；`void removeFile(const QString&)`
  - `Settings::value(key, def)/setValue(key, v)`（QSettings org/app = "LightSee"，静态方法）

- [ ] **Step 1: 先写失败用例（SelfTest.cpp 追加到 `SelfTest::run()` 内）**

```cpp
// --- FolderModel ---
{
    QDir tmp = QDir::temp();
    const QString dir = tmp.filePath("lightsee_selftest_dir");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    QStringList made{ "b.png","a.jpg","c.txt","d.heic","e.GIF" };
    for (const QString& f : made) QFile(dir + "/" + f).open(QIODevice::WriteOnly);
    FolderModel m;
    bool ok = m.setPath(dir + "/a.jpg");
    QStringList names;
    for (const QString& p : m.files()) names << QFileInfo(p).fileName();
    CHECK(ok && names == QStringList{ "a.jpg","b.png","d.heic","e.GIF" }, "folder: filter+sort+case-insensitive");
    CHECK(m.next() == dir + "/d.heic" && m.prev() == dir + "/a.jpg", "folder: next/prev wrap");
    m.removeFile(dir + "/a.jpg");
    CHECK(m.files().size() == 3 && m.current() == dir + "/e.GIF", "folder: remove self re-points");
    CHECK(FolderModel::supportedExtensions().contains("heic"), "folder: heic ext declared");
    CHECK(m.setPath(dir + "/missing.jpg") == false, "folder: unknown file rejected");
}
```
先添加头文件与类，运行 `build.cmd Release && x64\Release\QtWidgetsLight.exe --selftest`，预期 FolderModel 用例 **FAIL**（此时 FolderModel.cpp 尚不存在会先编译失败——因此本步实现顺序为：先空实现骨架让链接通过，或直接把本用例与实现放在同一提交序列里：Step 3 实现后 Step 4 复跑必须 PASS）。

- [ ] **Step 2: `src/Settings.h/.cpp`**

```cpp
#pragma once
#include <QVariant>
class Settings {
public:
    static QVariant value(const QString& key, const QVariant& def = QVariant());
    static void setValue(const QString& key, const QVariant& v);
};
```
```cpp
#include "src/Settings.h"
#include <QSettings>
QVariant Settings::value(const QString& k, const QVariant& d)
{ return QSettings("LightSee", "LightSee").value(k, d); }
void Settings::setValue(const QString& k, const QVariant& v)
{ QSettings("LightSee", "LightSee").setValue(k, v); }
```

- [ ] **Step 3: `src/FolderModel.h/.cpp`**

```cpp
#pragma once
#include <QStringList>
class FolderModel {
public:
    static QStringList supportedExtensions();
    bool setPath(const QString& filePath);          // 载入所在目录并定位
    QStringList files() const { return m_files; }
    int index() const { return m_index; }
    QString current() const;
    QString next();
    QString prev();
    void removeFile(const QString& filePath);
private:
    QStringList m_files;
    int m_index = -1;
};
```
```cpp
#include "src/FolderModel.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>

QStringList FolderModel::supportedExtensions()
{ return { "png","jpg","jpeg","bmp","gif","webp","tif","tiff","ico","svg",
           "heic","heif","hif" }; }

bool FolderModel::setPath(const QString& filePath)
{
    const QFileInfo fi(filePath);
    const QSet<QString> exts(supportedExtensions().begin(), supportedExtensions().end());
    QDir dir(fi.absolutePath(), "*", QDir::Name, QDir::Files);
    m_files.clear();
    const QFileInfoList list = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo& e : list)
        if (exts.contains(e.suffix().toLower()))
            m_files << e.absoluteFilePath();
    m_index = m_files.indexOf(fi.absoluteFilePath());
    return m_index >= 0;
}

QString FolderModel::current() const
{ return m_index >= 0 && m_index < m_files.size() ? m_files[m_index] : QString(); }

QString FolderModel::next()
{ if (m_files.isEmpty()) return QString(); m_index = (m_index + 1) % m_files.size(); return current(); }

QString FolderModel::prev()
{ if (m_files.isEmpty()) return QString(); m_index = (m_index - 1 + m_files.size()) % m_files.size(); return current(); }

void FolderModel::removeFile(const QString& filePath)
{
    const int i = m_files.indexOf(filePath);
    if (i < 0) return;
    m_files.removeAt(i);
    if (i < m_index) --m_index;
    else if (i == m_index) m_index = m_files.isEmpty() ? -1 : qMin(i, m_files.size() - 1);
}
```

- [ ] **Step 4: `src/ThumbnailLoader.h/.cpp`**

```cpp
#pragma once
#include <QObject>
#include <QSize>
class QListWidget;
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject* parent = nullptr);
    void populate(QListWidget* target, const QStringList& paths);  // 重建并异步填图
    void cancel();                                                  // 丢弃进行中的结果
private:
    quint64 m_gen = 0;
};
```
```cpp
#include "src/ThumbnailLoader.h"
#include <QListWidget>
#include <QImageReader>
#include <QtConcurrentRun>
#include <QFutureWatcher>

static QPixmap renderThumb(const QString& path)
{
    QImageReader r(path);
    r.setAutoTransform(true);
    const QSize s = r.size();
    if (s.isValid()) {
        const QSize t(150, 110);
        r.setScaledSize(s.width() * t.height() < s.height() * t.width()
                        ? QSize(s.width() * t.height() / s.height(), t.height())
                        : QSize(t.width(), s.height() * t.width() / s.width()));
    }
    const QImage img = r.read();
    return img.isNull() ? QPixmap() : QPixmap::fromImage(img);
}

ThumbnailLoader::ThumbnailLoader(QObject* parent) : QObject(parent) {}

void ThumbnailLoader::cancel() { ++m_gen; }

void ThumbnailLoader::populate(QListWidget* target, const QStringList& paths)
{
    cancel();
    const quint64 gen = m_gen;
    target->clear();
    int row = 0;
    for (const QString& p : paths) {
        auto* item = new QListWidgetItem(QFileInfo(p).fileName(), target);
        item->setData(Qt::UserRole, p);
        auto* w = new QFutureWatcher<QPixmap>(target);
        QObject::connect(w, &QFutureWatcher<QPixmap>::finished, target, [target, w, gen, this, row]() {
            const QPixmap pm = w->result();
            w->deleteLater();
            if (gen != m_gen || !target->item(row)) return;
            if (!pm.isNull()) target->item(row)->setIcon(QIcon(pm));
        });
        w->setFuture(QtConcurrent::run(&renderThumb, p));
        ++row;
    }
}
```

- [ ] **Step 5: MainWindow 接入翻页/缩略图/信息**

要点（在 Task 3 文件上改）：
- 成员 `FolderModel m_model; ThumbnailLoader* m_thumbs;`，构造中 `m_thumbs = new ThumbnailLoader(this);`
- `openFile()` 改为：`if (!m_model.setPath(path)) return; rebuildThumbPanel(); showCurrent();`
- `showCurrent()`：`startLoad(m_model.current())`；更新窗口标题 `%1 (%2/%3)`；`ui.thumbPanel->setCurrentRow(m_model.index())` 并 `scrollToItem`。
- `onNext/onPrev` 槽：`m_model.next()/prev()` → `startLoad`；失败图片（isNull）在 `onLoadFinished` 里状态栏提示 `"%1 无法读取（已跳过记录）"` 但**不**自动跳（用户确认规则：仅键盘翻页时提示）。
- `actPanel` 打开时调 `m_thumbs->populate(ui.thumbPanel, m_model.files())`（懒加载：首次展开才 populate；`setPath` 后若面板可见立即 populate）。
- `connect(ui.thumbPanel->...)`：`itemClicked` → `m_model.setPath(item 的 UserRole 路径)` → `showCurrent()`（显式 connect，lambda 内不捕获 this 悬垂：用 QPointer 或保证 parent 生命周期）。
- `onZoomChanged`：`ui.statusBar` 右侧 permanent widget——在 `MainWindow` 构造中 `auto* lbl = new QLabel; ui.statusBar->addPermanentWidget(lbl);` 存成员 `m_lblZoom`，显示 `缩放 %1%`；同时 permanent 显示 `分辨率 WxH · 文件大小`（onLoadFinished 更新）。
- `actInfo`：QMessageBox::about 显示 文件名/完整路径/格式(QImageReader::supportedImageFormats 命中)/尺寸/大小/修改时间。

- [ ] **Step 6: vcxproj 条目追加 + 构建 + selftest**

`QtWidgetsLight.vcxproj` ItemGroup 追加：
```xml
<ClCompile Include="src\FolderModel.cpp" />
<ClCompile Include="src\Settings.cpp" />
<ClCompile Include="src\ThumbnailLoader.cpp" />
<QtMoc Include="src\ThumbnailLoader.h" />
```
（FolderModel.h/Settings.h 无 Q_OBJECT，不进 QtMoc。）
```cmd
build.cmd Release && x64\Release\QtWidgetsLight.exe --selftest
```
预期 selftest 全部 PASS；打开 `third_party\testdata\` 目录内图片，缩略图栏出现且点击/方向键翻页正常。

- [ ] **Step 7: 键盘翻页**

`MainWindow` 重写 `keyPressEvent`：Left/A→onPrev，Right/D/Space(非播放)→onNext，+→onZoomIn，-→onZoomOut，0→onFit，1→onActual，l/r→旋转，h/v→翻转，F→Task 6 处理，其余交基类。显式实现于 .cpp，不新增 connect。

---

### Task 5: 幻灯片 + 全屏

**Files:**
- Create: `src/SlideShowController.h/.cpp`
- Modify: `src/MainWindow.h/.cpp`

**Interfaces:**
- Produces: `SlideShowController(QObject*)`；`void start(int intervalMs)`；`void stop()`；`bool isRunning()`；`signal void tick()`。

- [ ] **Step 1: 实现 SlideShowController**

```cpp
#pragma once
#include <QObject>
class QTimer;
class SlideShowController : public QObject {
    Q_OBJECT
public:
    explicit SlideShowController(QObject* parent = nullptr);
    void start(int intervalMs);
    void stop();
    bool isRunning() const;
signals:
    void tick();
private:
    QTimer* m_timer;
};
```
```cpp
#include "src/SlideShowController.h"
#include <QTimer>
SlideShowController::SlideShowController(QObject* parent)
    : QObject(parent), m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &SlideShowController::tick);
}
void SlideShowController::start(int ms) { m_timer->start(ms); }
void SlideShowController::stop() { m_timer->stop(); }
bool SlideShowController::isRunning() const { return m_timer->isActive(); }
```

- [ ] **Step 2: MainWindow 接线**

- 成员 `SlideShowController m_slide; bool m_fullscreen = false;`
- `connect(ui.actSlide, &QAction::toggled, this, &MainWindow::onSlideToggled)`；`connect(&m_slide, &SlideShowController::tick, this, &MainWindow::onNext);`
- `onSlideToggled(bool on)`：on → `m_slide.start(Settings::value("slide/intervalMs", 3000).toInt())`；off → `stop()`。
- 全屏：成员方法 `toggleFullScreen()`：`m_fullscreen = !m_fullscreen;` 保存/恢复 geometry（QByteArray 成员），`m_fullscreen ? showFullScreen() : showNormal()+setGeometry(saved)`；`ui.toolBar->setVisible(!m_fullscreen)`。
- 键盘（在 Task 4 Step 7 的 keyPressEvent 中补）：`F` 与 `F11` → toggleFullScreen；`Escape` → 退出全屏；`Space` → `ui.actSlide->trigger()`；方向键在播放中可手动翻页（不中断计时）。
- selftest 追加：`SlideShowController s; s.start(50);` 用 QEventLoop + QTimer 单次超时验证 `tick` 触发一次（CHECK）。

- [ ] **Step 3: 构建 + 验证**

`build.cmd Release` 成功后手动：放 3 张图进一个目录 → 打开 → 幻灯片开/停、F 全屏/退、Esc 退出、Space 切换播放。

---

### Task 6: 删除进回收站 + HEIC 端到端确认

**Files:**
- Create: `src/RecycleBin.h/.cpp`
- Modify: `src/MainWindow.h/.cpp`、`src/SelfTest.cpp`

**Interfaces:**
- Produces: `bool RecycleBin::moveToRecycleBin(const QString& path, QString* errorOut)`。

- [ ] **Step 1: 实现（shellapi）**

```cpp
#include "src/RecycleBin.h"
#include <windows.h>
#include <shellapi.h>

bool RecycleBin::moveToRecycleBin(const QString& path, QString* errorOut)
{
    WCHAR from[MAX_PATH + 2];
    const int n = path.toWCharArray(from);
    from[n] = L'\0'; from[n + 1] = L'\0';          // 双 null 结尾
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int rc = SHFileOperationW(&op);
    if (rc != 0) { if (errorOut) *errorOut = tr-lite QString::number(rc); return false; }
    return true;
}
```
（`errorOut` 直接给 rc 字符串即可，头文件签名 `#include <QString>`。）

- [ ] **Step 2: MainWindow 接线**

`actDelete` → 确认框（QMessageBox::question "将移入回收站：%1？"）→ `RecycleBin::moveToRecycleBin(m_model.current(), &err)` → 失败：状态栏红字 `moveToRecycleBin` 提示；成功：`m_model.removeFile(old)` → 列表空则 `canvas->clearImage()`+标题复位，否则 `showCurrent()`（不自动推进索引到"下一张"以外语义；用 removeFile 后 current）。

- [ ] **Step 3: HEIC 端到端 + 降级提示**

- 若 Task 3 selftest heic 用例一直 PASS，此步仅补：`onLoadFinished` 中当前文件扩展名 ∈ {heic,heif,hif} 且 img.isNull() 时，弹一次 `QMessageBox::warning`："无法解码 HEIC。请确认 exe 目录下存在 heif.dll/libde265.dll 且 imageformats\qheif.dll 已部署。"（成员 bool 防重复弹）。
- 手动验证部署完整性：临时把 `x64\Release\imageformats\qheif.dll` 改名 → 打开 example.heic 应弹上述提示且 PNG 等仍正常 → 改回。

- [ ] **Step 4: 构建 + 全量 selftest + 冒烟**

```cmd
build.cmd Release && x64\Release\QtWidgetsLight.exe --selftest
```
预期全 PASS。手动：对 `example.heic` 副本按 Delete → 确认 → 进回收站、界面自动切到剩余图。

---

### Task 7: 背景模式 + 预加载 + 状态细节

**Files:**
- Modify: `src/ImageView.h/.cpp`、`src/MainWindow.h/.cpp`

- [ ] **Step 1: 画布背景三档**

`ImageView` 增加 `enum Background { Dark, Light, Checker }; void setBackgroundMode(Background);`（Checker 用 16px 双灰棋盘 QPixmap + QBrush 平铺；Light 用 #f0f0f0）。MainWindow 右键画布（`ImageView::customContextMenuRequested`，在构造函数里 setContextMenuPolicy(CustomContextMenu) 后 MainWindow 显式 connect 弹 QMenu：背景三档/幻灯片间隔 1s..10s）→ 选后 `Settings::setValue("view/bgMode", int)`，`openFile` 前构造时读取应用。
- [ ] **Step 2: 相邻预加载**

MainWindow 成员 `QHash<QString, QImage> m_cache;`（键 path，值解码结果）。`onLoadFinished` 成功后 fire-and-forget：对 next()/prev() 路径各起一个 `QtConcurrent::run(&decodeImage, p)` 写入缓存（互斥保护或用 QMutex + 序号 gen，容量上限 12，超限清一半）。`startLoad` 命中缓存直接上屏（跳过 watcher）。
- [ ] **Step 3: 状态持久化**

`MainWindow::closeEvent`（重写）保存 `saveGeometry()` 到 `Settings::setValue("win/geometry", …)`、`ui.actPanel->isChecked()` 到 `view/thumbPanel`、当前目录到 `win/lastDir`；构造函数读取并恢复（geometry 有值才 `restoreGeometry`，thumbPanel 按存档 checked 状态设置，`actPanel->setChecked` 用 blockSignals 或先设成员再刷新，避免与 toggled connect 打架）。
- [ ] **Step 4: 状态栏 permanent 信息完善**

分辨率/大小/格式已在 Task 4；本步补：幻灯片播放中状态栏追加 "▶ 播放中"；无图片时 actPrev/actNext/actSlide/actDelete 全部 setEnabled(false)，启动即如此。
- [ ] **Step 5: 构建 + 验证**：build 通过；三档背景切换记忆；连续翻页第二圈起明显更快（人眼判断）。

---

### Task 8: QSS 现代深色风格

**Files:**
- Create: `src/style/dark.qss`、`resources.qrc`（替换 `QtWidgetsLight.qrc`）
- Modify: `src/MainWindow.cpp`、`QtWidgetsLight.vcxproj`

- [ ] **Step 1: `src/style/dark.qss`**

```qss
QMainWindow { background: #17181a; }
QToolBar { background: #232428; border: none; padding: 4px; spacing: 2px; }
QToolBar QToolButton { color: #d8d9db; background: transparent; border-radius: 6px;
                       padding: 5px 9px; font-size: 13px; }
QToolBar QToolButton:hover { background: #34363c; }
QToolBar QToolButton:pressed { background: #2b2d32; }
QToolBar QToolButton:checked { background: #0a84ff; color: white; }
QStatusBar { background: #232428; color: #b9bbc0; }
QListWidget#thumbPanel { background: #1c1d20; border: none; }
QListWidget#thumbPanel::item { color: #c9cace; border-radius: 6px; padding: 4px; }
QListWidget#thumbPanel::item:hover { background: #2b2d32; }
QListWidget#thumbPanel::item:selected { background: #0a84ff; color: white; }
QGraphicsView#canvas { background: #17181a; }
QToolTip { background: #2b2d32; color: #e6e7e9; border: 1px solid #3f4147; }
```
工具条按钮本版本显示文字（ToolButtonTextOnly 视觉更像现代看图工具的折中：在 .ui 里把 `toolButtonStyle` 改为 `Qt::ToolButtonTextOnly`，避免引入图标资源；若用户后续要求图标再补资源）。

- [ ] **Step 2: 资源接入**

```xml
<!-- resources.qrc -->
<RCC><qresource prefix="/"><file alias="dark.qss">src/style/dark.qss</file></qresource></RCC>
```
vcxproj：`QtWidgetsLight.qrc` 条目替换为 `<QtRcc Include="resources.qrc" />`，删除旧 qrc 文件。
`MainWindow` 构造 `ui.setupUi` 后：
```cpp
QFile qss(":/dark.qss");
if (qss.open(QIODevice::ReadOnly)) setStyleSheet(QString::fromUtf8(qss.readAll()));
```

- [ ] **Step 3: 构建 + 目测验收**

build 通过；运行后整体为深色工具条/状态栏/侧栏，hover/选中高亮为蓝色；GIF/PNG/HEIC 目录混合翻页视觉正常。

---

### Task 9: 收尾——完整构建、README、全量冒烟

- [ ] **Step 1: 双配置构建**
```cmd
build.cmd Release && build.cmd Debug
```
预期均 0 error（Debug 配置若因 Qt 非 debug 插件缺失报部署错，允许仅保证 Release 部署链完整，记录在 README）。
- [ ] **Step 2: `README.md`**（构建顺序：Task1 脚本 → build.cmd；部署 windeployqt 提示：`windeployqt --release --dir dist x64\Release\QtWidgetsLight.exe` 后仍需手工拷 `imageformats\qheif.dll`、`heif.dll`、`libde265.dll`；已知限制：GIF 显示首帧、AVIF 不支持、Debug 未做插件部署验证）。
- [ ] **Step 3: 执行 spec §8 冒烟清单全项**，逐项打勾记录到 README 附"验收记录"。
- [ ] **Step 4: selftest 全量**：`x64\Release\QtWidgetsLight.exe --selftest` 全 PASS 作为最终完成点。
