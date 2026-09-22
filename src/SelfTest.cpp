// On this kit (Qt 5.14.2 headers + MSVC 14.44 STL), QList<QString>::operator== used by
// the FolderModel `names == want` assertion instantiates stdext::checked_array_iterator,
// which the compiler reports as a deprecated-extension ERROR (C4996 / STL4043). The
// define below is the toolchain's own documented remedy; it suppresses that non-standard
// deprecation in this TU only and changes no assertion logic.
#define _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
#include "src/SelfTest.h"
#include "src/FolderModel.h"
#include "src/ImageView.h"
#include "src/MainWindow.h"
#include "src/PreloadCache.h"
#include "src/RecycleBin.h"
#include "src/SlideShowController.h"
#include "src/FileAssoc.h"
#include "src/Log.h"
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QLabel>
#include <QPushButton>
#include <QImageReader>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QRgb>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond, name) do { \
    const bool ok__ = (cond); \
    std::printf("%s  %s\n", ok__ ? "PASS" : "FAIL", name); \
    if (ok__) { L_DEBUG("PASS  {}", name); } else { ++g_fail; L_WARN("FAIL  {}", name); } } while (0)

// Calibrated against a known-good libde265 decode of third_party/testdata/example.heic
// (the Sigmaringen Danube photo). The MSVC /O2 miscompile of libde265 1.0.15 produced a
// 1280x854 image whose green-dominant pixel fraction was ~0.44 with a checksum of
// 4954224778; the correct decode has greenFrac ~0.00 and checksum 5259168035.
int SelfTest::run()
{
    L_INFO("==== LightSee 自检开始 ====");
    const QString sample = QCoreApplication::applicationDirPath()
                           + "/../../third_party/testdata/example.heic";
    QImageReader r(sample);
    QImage img = r.read();

    CHECK(!img.isNull(), "heic: qheif plugin decodes example.heic (non-null)");
    if (img.isNull()) { L_ERROR("自检中止：example.heic 解码失败 ({})", sample.toStdString()); return g_fail; }
    CHECK(img.size() == QSize(1280, 854), "heic: decoded size is exactly 1280x854");

    const QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    const int w = rgb.width(), h = rgb.height();

    long total = 0, greenDom = 0;
    QSet<QRgb> colors;
    double sum = 0.0, sum2 = 0.0;
    const int step = 3;
    for (int y = 0; y < h; y += step) {
        const unsigned char* line = rgb.constScanLine(y);
        for (int x = 0; x < w; x += step) {
            const unsigned char* p = line + x * 3;
            const int R = p[0], G = p[1], B = p[2];
            ++total;
            if (G > R + 40 && G > B + 40) ++greenDom;   // saturated-green block signature
            colors.insert(qRgb(R, G, B));
            const double lum = 0.299 * R + 0.587 * G + 0.114 * B;
            sum += lum; sum2 += lum * lum;
        }
    }
    const double mean = sum / total;
    const double variance = sum2 / total - mean * mean;
    const double greenFrac = double(greenDom) / double(total);
    std::printf("  heic stats: sampled=%ld distinctColors=%d greenFrac=%.3f variance=%.1f\n",
                total, colors.size(), greenFrac, variance);

    CHECK(variance > 200.0, "heic: real luminance variance (not a flat/solid image)");
    CHECK(colors.size() > 1000, "heic: many distinct colors (not a single block)");
    CHECK(greenFrac < 0.10, "heic: not the green block/mosaic decode corruption");

    // --- FolderModel ---
    {
        QDir tmp = QDir::temp();
        const QString dir = tmp.filePath("lightsee_selftest_dir");
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        // QStringList built via operator<< rather than a braced std::initializer_list:
        // the macro CHECK() splits on the commas inside `{ ... }` (braces do not group
        // macro args), and the init-list path additionally trips MSVC 14.44's
        // stdext::checked_array_iterator deprecation-as-error in this TU. Same strings.
        QStringList made; made << "b.png" << "a.jpg" << "c.txt" << "d.heic" << "e.GIF";
        for (const QString& f : made) QFile(dir + "/" + f).open(QIODevice::WriteOnly);
        FolderModel m;
        bool ok = m.setPath(dir + "/a.jpg");
        QStringList names;
        for (const QString& p : m.files()) names << QFileInfo(p).fileName();
        QStringList want; want << "a.jpg" << "b.png" << "d.heic" << "e.GIF";
        CHECK(ok && names == want, "folder: filter+sort+case-insensitive");
        // The plan's next/prev & remove expectations ("d.heic" / "e.GIF") are inconsistent
        // with its own sorted list from the assertion above (a.jpg|b.png|d.heic|e.GIF):
        // next() from a.jpg is b.png, not d.heic. Kept the same list, the same three CHECK
        // names and the same FolderModel contract, but corrected the expected constants and
        // walked a full cycle so both wrap ends and remove-self re-pointing are exercised.
        // CHECK() evaluates its condition twice, so the state-mutating next()/prev() walk
        // is folded into a side-effect-free bool first; the CHECK itself reads only that bool.
        bool wrapOk = (m.next() == dir + "/b.png")
                   && (m.next() == dir + "/d.heic")
                   && (m.next() == dir + "/e.GIF")
                   && (m.next() == dir + "/a.jpg")
                   && (m.prev() == dir + "/e.GIF");
        CHECK(wrapOk, "folder: next/prev wrap");
        m.setPath(dir + "/d.heic");            // current = d.heic (index 2)
        m.removeFile(dir + "/d.heic");         // remove self -> [a.jpg,b.png,e.GIF], current e.GIF
        CHECK(m.files().size() == 3 && m.current() == dir + "/e.GIF", "folder: remove self re-points");
        CHECK(FolderModel::supportedExtensions().contains("heic"), "folder: heic ext declared");
        CHECK(m.setPath(dir + "/missing.jpg") == false, "folder: unknown file rejected");
    }

    // --- SlideShow tick ---
    {
        // 幻灯片控制器：start(50ms) 后应发出至少一次 tick。
        // 用局部 QEventLoop 驱动定时器；无论是否收到 tick，单次超时 bailout(1s) 一定退出，
        // 保证 selftest 不会因未触发的定时器而挂起。
        SlideShowController s;
        bool seen = false;
        QEventLoop loop;
        QObject::connect(&s, &SlideShowController::tick, &loop, [&loop, &seen]() {
            seen = true;
            loop.quit();
        });
        QTimer::singleShot(1000, &loop, &QEventLoop::quit);   // 硬性超时兜底
        s.start(50);
        CHECK(s.isRunning(), "slideshow: isRunning() true after start()");
        loop.exec();
        s.stop();
        CHECK(seen, "slideshow: tick emitted while running");
        CHECK(!s.isRunning(), "slideshow: isRunning() false after stop()");
    }

    // --- RecycleBin (Task 6) ---
    {
        // 只在临时 throwaway 文件上验证回收站行为；绝不触碰仓库或 third_party/testdata 文件。
        // CHECK() 会把条件表达式求值两次，而 moveToRecycleBin 有副作用，故先把结果折叠成 bool。
        const QString dir = QDir::temp().filePath("lightsee_recycle_selftest");
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);

        QString err;
        const bool emptyRejected =
            (RecycleBin::moveToRecycleBin(QString(), &err) == false) && !err.isEmpty();
        CHECK(emptyRejected, "recyclebin: empty path rejected with error text");

        err.clear();
        const QString ghost = dir + "/never_existed.heic";
        const bool ghostRejected =
            (RecycleBin::moveToRecycleBin(ghost, &err) == false) && !err.isEmpty();
        std::printf("  recyclebin: missing-path errorOut='%s'\n", err.toLocal8Bit().constData());
        CHECK(ghostRejected, "recyclebin: missing path -> false + SHFileOperation rc in errorOut");

        // 长路径回归：brief 的 `WCHAR from[MAX_PATH + 2]` 在这种路径上会越界写（栈破坏/UB）。
        // Windows 是否允许该长度取决于系统策略/manifest，所以断言的是"调用良性且结果自洽"
        // （成功则文件必须消失；失败则 errorOut 非空），而不是某个固定返回值。
        err.clear();
        const QString longBase = dir + "/" + QString(120, QLatin1Char('l')) + "/"
                                 + QString(120, QLatin1Char('o')) + "/" + QString(120, QLatin1Char('n'))
                                 + "/" + QString(120, QLatin1Char('g'));
        const QString longFile = longBase + "/p.png";
        bool longCreated = QDir().mkpath(longBase);
        if (longCreated) {
            QFile f(longFile);
            longCreated = f.open(QIODevice::WriteOnly) && f.write("x") == 1;
        }
        // 创建失败必须显式 FAIL：否则下面的"结果自洽"断言会在没跑到的情况下空转通过。
        CHECK(longCreated, "recyclebin: long-path throwaway file actually created");
        const bool longRc = longCreated && RecycleBin::moveToRecycleBin(longFile, &err);
        const bool longConsistent = longCreated && (longRc ? !QFile::exists(longFile) : !err.isEmpty());
        std::printf("  recyclebin: longPath len=%d created=%d moved=%d errorOut='%s'\n",
                    longFile.size(), longCreated ? 1 : 0, longRc ? 1 : 0, err.toLocal8Bit().constData());
        CHECK(longConsistent, "recyclebin: >MAX_PATH-length path handled without buffer overrun");

        // 网络/UNC 守卫：必须在进 SHFileOperation 之前被纯字符串前缀分支拒绝
        // （不触碰 Shell，不可能挂起或真的删除任何东西）。
        err.clear();
        const bool uncRejected =
            (RecycleBin::moveToRecycleBin(QStringLiteral("\\\\nonexistent-host\\x\\y.png"), &err) == false)
            && !err.isEmpty();
        std::printf("  recyclebin: unc-path errorOut='%s'\n", err.toLocal8Bit().constData());
        CHECK(uncRejected, "recyclebin: UNC/network path rejected pre-shell with error text");

        err.clear();
        const QString throwaway = dir + "/throwaway.png";
        { QFile f(throwaway); if (f.open(QIODevice::WriteOnly)) f.write("x"); }
        const bool moved = RecycleBin::moveToRecycleBin(throwaway, &err);
        const bool movedGone = moved && err.isEmpty() && !QFile::exists(throwaway);
        CHECK(movedGone, "recyclebin: throwaway temp file leaves the folder (moved to recycle bin)");

        QDir(dir).removeRecursively();   // 清掉本用例自建的 throwaway 目录（不递归删除仓库内容）
    }

    // --- PreloadCache (Task 7) ---
    {
        // 纯逻辑缓存（无 GUI/无线程参与，GUI 侧并发仅靠 QMutex）。验证：
        // 命中/未命中、空图解码失败不入缓存、超容量修剪到一半、同键覆盖不增长。
        QImage img(4, 4, QImage::Format_ARGB32); img.fill(QColor(0x11, 0x22, 0x33, 0xFF));
        PreloadCache c;
        c.put("/k/a.png", img);
        c.put("/k/b.png", img);
        QImage out;
        const bool hitMiss = c.get("/k/a.png", &out) && out.size() == QSize(4, 4)
                          && !c.get("/k/missing.png", &out);
        CHECK(hitMiss, "preload: get returns cached image on hit, false on miss");

        c.put("/k/null.png", QImage());   // 解码失败（空图）不得进缓存
        const bool nullRejected = !c.contains("/k/null.png") && c.size() == 2;
        CHECK(nullRejected, "preload: null decode rejected; overwrite keeps size");

        // 容量修剪：Cap=12，put 第 13 个键 → drop = 13 - 12/2 = 7 个 → 剩 6（清一半）。
        for (int i = 0; i < 11; ++i) c.put(QString("/k/f%1.png").arg(i), img);
        const bool trimmed = c.size() == PreloadCache::Cap / 2;
        std::printf("  preload: size after 13th put = %d (cap %d)\n", c.size(), int(PreloadCache::Cap));
        CHECK(trimmed, "preload: over-cap put trims to half of cap");
    }

    // --- FileAssoc (纯逻辑：命令行拼装与扩展名表，绝不写注册表) ---
    {
        const QString cmd = FileAssoc::openCommand("C:/Program Files/LightSee/QtWidgetsLight.exe");
        const bool cmdOk = cmd == QStringLiteral("\"C:\\Program Files\\LightSee\\QtWidgetsLight.exe\" \"%1\"");
        CHECK(cmdOk, "fileassoc: openCommand quotes native path and appends \"%1\"");

        const QStringList exts = FileAssoc::photoExtensions();
        const bool extOk = exts.contains("jpg") && exts.contains("png")
                           && !exts.contains("svg") && !exts.contains(".jpg");
        CHECK(extOk, "fileassoc: photoExtensions dotless, common formats only");
    }

    // --- ImageView scene rect (滚动条残留回归) ---
    {
        // Qt 的自动 sceneRect 只增不缩（clear 也不重置），而滚动条范围由 sceneRect
        // 决定：大图撑大 sceneRect 后切小图 → 视口两侧残留滚动条。断言切图后
        // sceneRect 精确收缩到当前图边界（(0,0) 起、100x80），即证明修复生效。
        ImageView view;
        QImage big(4000, 3000, QImage::Format_ARGB32); big.fill(Qt::gray);
        QImage small(100, 80, QImage::Format_ARGB32); small.fill(Qt::blue);
        view.setImage(big);
        view.setImage(small);
        const bool shrunk = view.scene()
            && view.scene()->sceneRect() == QRectF(0, 0, 100, 80);
        std::printf("  imageview: sceneRect after big->small = %f x %f\n",
                    view.scene() ? view.scene()->sceneRect().width() : -1.0,
                    view.scene() ? view.scene()->sceneRect().height() : -1.0);
        CHECK(shrunk, "imageview: scene rect shrinks to current image (no leftover scrollbars)");
    }

    // --- 绕中心旋转（回归：变换原点默认 (0,0) 左上角，90° 后图片跳位） ---
    {
        ImageView view;
        QImage img(100, 80, QImage::Format_ARGB32); img.fill(Qt::red);
        view.setImage(img);
        const QPointF c0 = view.scene()->itemsBoundingRect().center();
        view.rotateBy(90);
        const QPointF c1 = view.scene()->itemsBoundingRect().center();
        // sceneRect 必须跟随旋转后的包围盒，否则适应缩放按旧矩形算、画面被裁。
        const bool rectFollows = view.scene()->sceneRect() == view.scene()->itemsBoundingRect();
        std::printf("  rotate: center before=(%.0f,%.0f) after90=(%.0f,%.0f) rectFollows=%d\n",
                    c0.x(), c0.y(), c1.x(), c1.y(), rectFollows ? 1 : 0);
        CHECK(QLineF(c0, c1).length() < 1.0 && rectFollows,
              "rotate: 90deg keeps center, scene rect follows bounding box");
    }

    // --- dark.qss 状态栏文字可读性（回归：常驻 QLabel 曾实测拿到黑色 windowText） ---
    // 用裸 QMainWindow 且在子控件创建前设表：既避开用户可改的 ui/theme 持久设置，
    // 也避开运行时换表才涉及的 polish 缓存问题（light 用例同理）。
    {
        QMainWindow probe;
        QFile qss(QStringLiteral(":/dark.qss"));
        // CHECK 对条件求值两次：open() 有副作用（第二次会因"已打开"返回 false），先折叠成 bool。
        const bool darkOpen = qss.open(QIODevice::ReadOnly);
        CHECK(darkOpen, "dark: resource :/dark.qss exists");
        if (!qss.isOpen()) { L_ERROR("自检中止：:/dark.qss 资源缺失"); return g_fail; }
        probe.setStyleSheet(QString::fromUtf8(qss.readAll()));
        probe.resize(500, 100);
        // 复刻主窗口用法：常驻 QLabel + 临时消息，均须为浅色文字（深底 #232428 上黑色不可读）。
        auto* lbl = new QLabel(QStringLiteral("1920\u00D71080"), &probe);
        probe.statusBar()->addPermanentWidget(lbl);
        probe.ensurePolished();
        lbl->ensurePolished();
        const QColor lb = lbl->palette().windowText().color();
        const QColor sb = probe.statusBar()->palette().windowText().color();
        std::printf("  qss: label windowText=rgb(%d,%d,%d) statusBar windowText=rgb(%d,%d,%d)\n",
                    lb.red(), lb.green(), lb.blue(), sb.red(), sb.green(), sb.blue());
        CHECK(qGray(lb.rgb()) >= 100 && qGray(sb.rgb()) >= 100,
              "qss: statusbar message & permanent label text are legible (light)");
    }

    // --- light.qss 状态栏文字可读性（浅底上必须为深色文字） ---
    // 注：用裸 QMainWindow 且在子控件创建前设表 —— 与 dark 用例同构。
    // 运行时换已有样式的窗口靠 polish 事件（需真实事件循环 + 可见控件），不在本用例范围。
    {
        QMainWindow probe;
        QFile qss(QStringLiteral(":/light.qss"));
        const bool lightOpen = qss.open(QIODevice::ReadOnly);   // 同上：折叠副作用，避免二次 open()
        CHECK(lightOpen, "light: resource :/light.qss exists");
        if (!qss.isOpen()) { L_ERROR("自检中止：:/light.qss 资源缺失"); return g_fail; }   // 资源缺失时 palette 检查无意义
        probe.setStyleSheet(QString::fromUtf8(qss.readAll()));
        auto* lbl = new QLabel(QStringLiteral("1920\u00D71080"), &probe);
        probe.statusBar()->addPermanentWidget(lbl);
        probe.ensurePolished();
        lbl->ensurePolished();
        const QColor lb = lbl->palette().windowText().color();
        std::printf("  light-qss: label windowText=rgb(%d,%d,%d)\n",
                    lb.red(), lb.green(), lb.blue());
        CHECK(lb == QColor("#55585e"),
              "light qss: statusbar label text = #55585e (dark on light)");
    }

    // --- 自定义标题栏：三键+标题存在，且已被 setMenuWidget 重挂为主窗口直接子级 ---
    {
        MainWindow probe;
        QPushButton* bMin   = probe.findChild<QPushButton*>("btnMin");
        QPushButton* bMax   = probe.findChild<QPushButton*>("btnMax");
        QPushButton* bClose = probe.findChild<QPushButton*>("btnClose");
        QWidget* tb         = probe.findChild<QWidget*>("titleBar");
        CHECK(bMin && bMax && bClose && probe.findChild<QLabel*>("lblFile"),
              "titlebar: caption buttons exist and statusbar file label present");
        CHECK(tb && tb->parent() == &probe,
              "titlebar: installed as menu-area widget (direct child of MainWindow)");
    }

    L_INFO("==== 自检结束：失败 {} 项 ====", g_fail);
    return g_fail;
}
