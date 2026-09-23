# LightSee 代码优化清单

> 审查日期：2026-09-22 · 范围：`src/`、`heif-plugin/`、`main.cpp`、构建配置
> 按优先级分组：P=性能（高）→ R=健壮性（中）→ M=可维护性（低）。每项含位置、现状、建议。
>
> **进度**：P1 ✅、P2 ✅、P3 ✅、P4 ✅、R1 ✅、M5 ✅（2026-09-22 修复，见各项"修复记录"）；其余待做。

---

## 一、性能（高优先级）

### P1. 缩略图与主图解码共享全局线程池，大目录下当前图上屏被排队阻塞 ✅ 已修复

- **修复记录**（2026-09-22）：缩略图改投 `ThumbnailLoader` 自有 2 线程专用池；预解码改投 `MainWindow::m_preloadPool` 专用 1 线程池（且每次先 `clear()` 丢弃过期的排队邻居任务）。QtConcurrent 全局池此后只服务主图交互解码，三者互不排队。`cancel()` 同时 `m_pool.clear()` 删除排队任务（旧实现只丢结果不丢任务）。

- **位置**：[MainWindow.cpp:311](../src/MainWindow.cpp#L311)（主图 `QtConcurrent::run(&decodeImage, ...)`）、[ThumbnailLoader.cpp:53](../src/ThumbnailLoader.cpp#L53)（缩略图 `QtConcurrent::run(&renderThumb, ...)`）
- **现状**：两类任务都投递到 QtConcurrent 默认全局池（线程数 = CPU 核数）。
- **问题**：打开一个几百张图的目录时，`populate` 先入队 N 个缩略图任务，随后 `startLoad` 的主图解码任务排在队尾。用户翻页（next/prev）的新解码请求也持续排在残余缩略图任务之后——**当前图的上屏延迟随目录大小线性增长**，这是看图软件最核心的体验指标。
- **建议**：
  1. 给缩略图建独立 `QThreadPool`（如 `maxThreadCount(2)`），`QtConcurrent::run(pool, ...)` 重载投递；
  2. 或保留全局池，但把主图解码改投专用单线程池，保证交互任务永远不被批量任务挤占。

### P2. 点击缩略图触发"全目录重扫 + 全量缩略图重建" ✅ 已修复

- **修复记录**（2026-09-22）：`FolderModel` 新增 `setCurrentFile()`（同目录 O(1) 移索引，未命中返回 false）；`itemClicked` 改走 `setCurrentFile + showCurrent`，仅在面板与模型失步时兜底 `setPath`。点击缩略图不再触发目录重扫与面板重建。

- **位置**：[MainWindow.cpp:196-200](../src/MainWindow.cpp#L196-L200)（`itemClicked` → `m_model.setPath(path)` + `rebuildThumbPanel()`）
- **现状**：`FolderModel` 没有 `setIndex()`，点击缩略图只能调 `setPath()`——它会对整个目录重新 `entryInfoList` 扫描；随后 `rebuildThumbPanel()` → `populate()` 把面板 `clear()` 后**为每个文件重新创建 item 并重新入队解码**。
- **问题**：
  1. 同一目录内切换图片做的是 O(目录大小) 的重复扫描；
  2. 已加载好的缩略图全部丢弃重解码；
  3. 旧解码任务无法取消（见 P3），快速连点时 CPU 任务层层叠加。
- **建议**：`FolderModel` 增加 `bool setCurrentIndex(int)`（同目录时只移索引、不重扫）；`MainWindow::showCurrent` 已负责同步 `setCurrentRow`，`itemClicked` 改为 `setIndex + showCurrent`，跳过 `rebuildThumbPanel`。

### P3. 缩略图无任何缓存，`populate` 即全量重解码 ✅ 已修复

- **修复记录**（2026-09-22）：`ThumbnailLoader` 新增 `QHash<QString,QPixmap>` 缓存（96MB 字节预算、换目录整批失效、超预算驱逐）；`populate` 缓存命中的条目同步上图不入队；新增 `removeOne()` 增量删除（`onDelete` 改用之，删除单张不再全量重建）。在途解码结果即使代数过期也入缓存，留给下次命中。

- **位置**：[ThumbnailLoader.cpp:31-55](../src/ThumbnailLoader.cpp#L31-L55)
- **现状**：`populate` 每次 `target->clear()` 后为所有路径重新入队 `renderThumb`；`cancel()` 只靠代数号丢弃**结果**，已排队/运行中的解码照常烧 CPU。
- **问题**：切换面板显隐、删除单张（`onDelete` → `rebuildThumbPanel`）、打开同目录另一张图，都会触发几百个重复解码。删除一张图本只需移除一个 item。
- **建议**：
  1. `ThumbnailLoader` 内加 `QHash<QString, QPixmap>` 内存缓存（缩略图很小，全目录缓存通常不过几 MB），`populate` 先填缓存命中的，只对 miss 的入队；
  2. 增加 `removeOne(path)` / `addOne(path)` 增量接口，`onDelete` 与目录监视（见 R3）走增量更新而非全量重建。

### P4. PreloadCache 按张数限容，大图可致内存暴涨 ✅ 已修复

- **修复记录**（2026-09-22）：`PreloadCache` 改为双上限——张数 Cap=12（原语义保留）+ 字节预算 512MB（`put` 按 `sizeInBytes()` 记账、同键覆盖先回退旧账）；超预算逐个驱逐但保底留 1 张（超大图预加载仍有意义，单张上界由 R1 守卫限制）。SelfTest 验证：12×64MB → 驱逐至 8 张恰达预算；两张 320MB → 保留 1 张不清零。

- **位置**：[PreloadCache.h:18](../src/PreloadCache.h#L18)（`Cap = 12`）
- **现状**：缓存上限 = 12 张 `QImage`，不看字节。
- **问题**：现代手机 48MP 照片 ARGB32 约 260MB/张，12 张 + 上屏 `QPixmap::fromImage` 的整份拷贝，峰值轻松超过 3GB。用户连续翻看大图目录时内存失控。
- **建议**：改为按字节预算（如 512MB），`put` 时累计 `img.sizeInBytes()`，超预算再修剪；`Cap` 仅作张数上限兜底。

---

## 二、健壮性（中优先级）

### R1. 解码无内存上限（解压炸弹 / 异常大图 OOM）✅ 已修复

- **修复记录**（2026-09-23，守卫误伤真实大图的跟进）：像素超限不再一刀切拒解——JPEG 走解码期降采样（Qt 5.14 `qjpeghandler` 会把 `setScaledSize` 换算成 libjpeg 的 M/8 缩放解码，峰值内存等于缩放后尺寸），按 1/2、1/4、1/8 取第一个落进 40MP 显示预算的档位；PNG/TIFF 等整图解码后再缩放的格式仍硬拒。单边 > 32767 任何格式都拒。缩略图路径同规则放行超限 JPEG（原先大图的缩略图也是空格子）。状态栏/信息栏区分"图片过大（原图 W×H，已降采样）"与"无法读取"，不再把守卫拦下误报成坏文件。实测 20557×23049（4.74 亿像素，18MB）成都轨道线网拼接图：原先"无法读取"，现降采样为 5139×5762 上屏，进程工作集约 385MB。SelfTest 补 5 条降采样档位断言。

- **跟进**（2026-09-23）：降采样是默认档而非唯一选择——工具栏新增可勾选的"原始尺寸"（`actFullRes`，位于 1:1 之后），仅在当前图确实被降采样上屏时可点。勾选=后台用 `decodeImageFull` 绕开像素守卫整图重解码（单边 > 32767 仍拒，内存由用户显式承担），成功后换图像素源但保持当前缩放（fit 态则重新适配，手动缩放/1:1 态缩放率不变）；取消=换回已缓存的降采样版本，不重解码。全尺寸结果只上屏，绝不进预加载缓存，翻页/换目录由 `startLoad → resetFullRes` 释放。解码期间按钮禁用防连点，失败（内存不足）自动弹回降采样并提示。实测同一张 4.74 亿像素图：降采样 347MB → 勾选 2157MB → 取消 346MB，两侧内存都能回落。顺带修掉状态栏叠字（`QStatusBar` 临时消息从最左端起画会压住 `lblFile`，消息在场时隐掉文件名）与"正在解码"提示成功后不消失、取消勾选后图标未换回深色两处收尾问题。

- **修复记录**（2026-09-22）：新增 `src/ImageLimits.h` 头部尺寸守卫（单边 > 32767 或像素 > 2^28≈268MP 拒绝，头部读不出尺寸则放行交给 reader 判定），`decodeImage` 与 `renderThumb` 在 `read()` 分配前预检，超限按解码失败处理（走"无法读取"分支）而非 OOM。阈值宽松：48MP 手机照片、1.5 亿像素中画幅均放行。SelfTest 验证 30000×30000 炸弹、40000×100 细长条被拒。

- **位置**：[MainWindow.cpp:37-42](../src/MainWindow.cpp#L37-L42)（`decodeImage`）、[ThumbnailLoader.cpp:10-25](../src/ThumbnailLoader.cpp#L10-L25)（`renderThumb`）
- **现状**：直接 `reader.read()`。Qt 5.14 没有 `QImageReader::setAllocationLimit`（Qt 6 才有）。
- **问题**：一张声称 30000×30000 的恶意/损坏 PNG 可触发 ~3.4GB 分配，程序直接假死或崩溃。
- **建议**：读之前 `reader.size()` 拿头信息，超过阈值（如 400MP 或宽/高 > 32767）拒绝并走"无法读取"分支；缩略图路径已有 `setScaledSize`，仍建议加同样的头检查。

### R2. 日志写入 exe 目录，安装到 Program Files 后静默失败

- **位置**：[Log.cpp:32-33](../src/Log.cpp#L32-L33)（`applicationDirPath() + "/logs"`）
- **问题**：标准安装位置下进程无写权限，`mkpath` 失败后 `daily_file_sink` 构造抛异常（spdlog 默认抛），或日志全部丢失——排障能力归零。
- **建议**：改用 `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)`；创建失败时回退临时目录并降级为仅控制台输出。

### R3. FolderModel 不监视目录变化，列表会过期

- **位置**：[FolderModel.cpp:26-47](../src/FolderModel.cpp#L26-L47)（一次性扫描）
- **问题**：外部程序（相机导入、另存、Explorer 删除）改变目录后，模型列表与缩略图栏仍是旧快照；翻到已不存在的文件会解码失败。
- **建议**：`MainWindow` 挂 `QFileSystemWatcher` 监视当前目录的 `directoryChanged`，触发后仅重扫 + 增量更新缩略图（配合 P3 的增量接口）。

### R4. 目录扫描在 UI 线程同步执行

- **位置**：[FolderModel.cpp:37](../src/FolderModel.cpp#L37)（`entryInfoList` 收集全部 QFileInfo）
- **问题**：万图目录或网络目录（UNC 打开）时 `openFile` 明显卡顿。
- **建议**：扫描移到 `QtConcurrent`（与 P1 的缩略图池共用即可），完成后再切回 UI 线程刷新面板；打开期间状态栏显示"正在扫描目录"。

### R5. PreloadCache 修剪是"字典序删半"，不是 LRU

- **位置**：[PreloadCache.h:24-28](../src/PreloadCache.h#L24-L28)
- **现状**：超限后按 key 字符串排序删除前一半（注释写"删最旧"，实际与新旧无关，是为了测试确定性）。
- **问题**：可能刚预加载的"下一张"被删掉、很早的旧图反而留下，翻页回看时缓存命中率打折，预加载白做。
- **建议**：改为 FIFO（插入序）或 LRU（`get` 命中时提升）；SelfTest 的容量断言不受影响（仍可断言精确剩余数）。

### R6. onInfo 在 UI 线程同步读图片头

- **位置**：[MainWindow.cpp:400-401](../src/MainWindow.cpp#L400-L401)（`QImageReader r(path); r.size()`）
- **问题**：网络盘/冷缓存大文件时弹窗前会卡一下。轻微，但与 P1/R4 同属"IO 上 UI 线程"一类。
- **建议**：尺寸可复用 `m_lblInfo` 已有数据（当前图的宽高在 `onLoadFinished` 时已知），不必重读。

---

## 三、可维护性（低优先级）

### M1. 受支持扩展名清单存在三份，已有漂移

- **位置**：[FolderModel.cpp:7-9](../src/FolderModel.cpp#L7-L9)（13 种，含 svg/tif/ico/hif）、[FileAssoc.cpp:17-20](../src/FileAssoc.cpp#L17-L20)（7 种，无 svg/tif/ico/hif）、[MainWindow.cpp:214-215](../src/MainWindow.cpp#L214-L215)（对话框过滤器又手写一遍）
- **问题**：新增格式要改三处；`onOpen` 过滤器与 `FolderModel` 的清单将来必然不同步。
- **建议**：对话框过滤器从 `FolderModel::supportedExtensions()` 动态拼接；`FileAssoc::photoExtensions` 若刻意只关联主流格式，加注释说明差异是有意的。

### M2. Settings 每次调用都构造 QSettings（走注册表）

- **位置**：[Settings.cpp:4-12](../src/Settings.cpp#L4-L12)
- **问题**：`QSettings("LightSee","LightSee")` 构造/析构均有开销；`closeEvent` 一次连写 4 键 = 4 次独立注册表写。
- **建议**：`main()` 里 `QSettings::setPath`/`setDefaultFormat` 一次，`Settings::value/setValue` 用默认构造的栈实例（Qt 自带按进程合并写入）；或干脆持有静态实例。

### M3. ImageView 每次切图销毁重建 item

- **位置**：[ImageView.cpp:160-171](../src/ImageView.cpp#L160-L171)（`m_scene->clear()` + `addPixmap`）
- **问题**：高频翻页时反复 new/delete QGraphicsPixmapItem + QPixmap 转换；量级小，属顺手优化。
- **建议**：首次创建后复用 `m_item->setPixmap(...)`，仅 `clearImage` 时真正释放。

### M4. onOpen 的 static 初始目录不随会话内打开位置更新

- **位置**：[MainWindow.cpp:210-216](../src/MainWindow.cpp#L210-L216)
- **问题**：static 只在首次求值；用户拖拽/命令行打开别的目录后，Ctrl+O 仍停在旧目录（关窗重启才纠正）。
- **建议**：static 改为成员，或在 `openFile` 成功后同步更新。

### M5. ThumbnailLoader watcher 的 context 是 target 而非 this，捕获 this 依赖巧合安全 ✅ 已修复

- **修复记录**（2026-09-22）：随 P1/P3 重构一并修复——`QFutureWatcher` 连接的 context 从 `target` 改为 `this`（loader），捕获 this 不再依赖析构顺序巧合；目标面板改用 `QPointer<QListWidget>` 持有，随窗口析构自动置空。

- **位置**：[ThumbnailLoader.cpp:46-52](../src/ThumbnailLoader.cpp#L46-L52)
- **问题**：lambda 捕 `this`（读 `m_gen`），连接 context 是 `target`。当前因 `thumbPanel` 先于 `m_thumbs` 析构而恰好安全；一旦两者父子关系变化即成 UAF。
- **建议**：context 改为 `this`，或把 `m_gen` 捕获为值（`gen` 已捕获，`this->m_gen` 换成捕获时快照即可，删除对 `this` 的依赖）。

### M6. 仓库根残留三份历史构建产物目录

- **位置**：`QtWidgetsLight/`、`LightSee/`、`x64/`（根目录）
- **问题**：均为项目改名/迁移期间的 obj 中间产物（`.gitignore` 已覆盖，不入库），磁盘残留且 `QtWidgetsLight/QtWidgetsLight.exe.recipe` 等旧名文件易误导检索。
- **建议**：确认无调试需要后整目录删除，不影响构建（当前输出在 `x64/` 与 `heif-plugin/x64/`）。

### M7. 关窗收进托盘的路径也全量写设置

- **位置**：[MainWindow.cpp:801-817](../src/MainWindow.cpp#L801-L817)
- **问题**：每次隐藏到托盘（非退出）都执行 4 次设置写入；高频隐藏/恢复场景写注册表无谓。
- **建议**：持久化移到 `m_quitting == true` 或真正退出（`QCoreApplication::aboutToQuit`）时执行一次。

---

## 建议实施顺序

1. ~~**P1 + P2 + P3**（线程池隔离 / setIndex / 缩略图缓存）~~ ✅ 已完成（2026-09-22，含 M5 顺手修复），SelfTest 新增 `setCurrentFile` 与 ThumbnailLoader（专用池交付/缓存同步命中/增量移除）用例，全部通过；
2. ~~**P4 + R1**（内存预算 + 解码上限）~~ ✅ 已完成（2026-09-22），SelfTest 新增字节预算（12×64MB→8、单张超大保留）与尺寸守卫（炸弹拒绝/48MP 放行）用例，全部通过；
3. **R2**（日志路径）——发布前必须处理；
4. 其余 R3-R6、M1-M4、M6-M7 按需排期，互相独立。

> 注：以上所有改动均有 `--selftest` 自检覆盖（FolderModel/PreloadCache/ImageView 已有断言），改完跑 `LightSee.exe --selftest` 验证；P2/P3 涉及新接口时同步补 SelfTest 用例。
