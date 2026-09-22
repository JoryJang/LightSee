#include "src/MainWindow.h"
#include "src/ThumbnailLoader.h"
#include "src/Settings.h"
#include "src/RecycleBin.h"
#include "src/FileAssoc.h"
#include "src/Log.h"
#include "src/version.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QtConcurrentRun>
#include <QFutureWatcher>
#include <QCloseEvent>
#include <QShowEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QLabel>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMenu>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QPushButton>
#include <QIcon>
#include <QPainter>
#include <QSystemTrayIcon>
#include <QWindow>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#endif

static QImage decodeImage(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

// Task 7：预加载 worker。按值捕获 shared_ptr 副本 + path 拷贝，只写缓存、
// 绝不触碰 MainWindow —— 窗口析构后 worker 照常安全收尾（见 PreloadCache.h）。
static void preloadInto(std::shared_ptr<PreloadCache> cache, const QString path)
{
    const QImage img = decodeImage(path);
    if (img.isNull()) L_DEBUG("预加载解码失败: {}", path.toStdString());
    cache->put(path, img);
}

static QString humanSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 2) + " MB";
    if (bytes >= 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes) + " B";
}

// Task 6：HEIC 家族后缀（解码依赖 qheif 插件 + heif.dll/libde265.dll 部署）。
static bool isHeicSuffix(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("heic") || s == QLatin1String("heif") || s == QLatin1String("hif");
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    ui.setupUi(this);

    // Task 8：界面主题。ui/theme 读档（0=深色默认），资源缺失时以默认样式运行。
    m_theme = Settings::value("ui/theme", 0).toInt() == 1 ? 1 : 0;
    applyTheme();

    // 勾选变蓝底的图标按钮换反白版（见 installToolbarIcons 注释）。
    connect(ui.actSlide, &QAction::toggled, this, [this](bool on) {
        ui.actSlide->setIcon(on ? m_slideIconOn : m_slideIconOff);
    });
    connect(ui.actPanel, &QAction::toggled, this, [this](bool on) {
        ui.actPanel->setIcon(on ? m_panelIconOn : m_panelIconOff);
    });

    // 图标组在工具栏内水平居中：两端各插一个水平扩展弹簧（QToolBar 布局器分配剩余空间）。
    const auto makeSpring = [this] {
        auto* w = new QWidget(ui.toolBar);
        w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        return w;
    };
    ui.toolBar->insertWidget(ui.toolBar->actions().first(), makeSpring());
    ui.toolBar->addWidget(makeSpring());

    // 自定义标题栏：.ui 中挂在 centralArea 首行，setMenuWidget 会把它重挂到
    // 菜单区（工具栏之上、窗口最顶），centralArea 只剩一个空的布局占位项。
    // 裸 QWidget 默认不画 QSS background，必须显式开 WA_StyledBackground。
    ui.titleBar->setAttribute(Qt::WA_StyledBackground, true);
    // 原生标题栏已被 WM_NCCALCSIZE 裁掉，任务栏/Alt-Tab 用窗口图标，
    // 窗口内的图标只能在自定义标题栏自己画一份。
    ui.lblIcon->setPixmap(QIcon(QStringLiteral(":/lightsee.ico")).pixmap(16, 16));
    setMenuWidget(ui.titleBar);
    connect(ui.btnMin,    &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(ui.btnMax,    &QPushButton::clicked, this, [this]{
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(ui.btnClose,  &QPushButton::clicked, this, &QWidget::close);

    // 系统托盘：关窗收进托盘，右键"退出"才真退（见 closeEvent 的 m_quitting 分支）。
    // 托盘不可用的平台不创建，关窗保持原退出行为。
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        auto* trayMenu = new QMenu(this);
        trayMenu->addAction(tr("打开"), this, &MainWindow::showFromTray);
        trayMenu->addAction(tr("退出"), this, [this]{
            m_quitting = true;
            close();
        });
        m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/lightsee.ico")), this);
        m_tray->setToolTip(QStringLiteral("LightSee"));
        m_tray->setContextMenu(trayMenu);
        connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r){
            if (r == QSystemTrayIcon::DoubleClick) showFromTray();
        });
        m_tray->show();
    }

    // Task 7：启动状态恢复。actPanel 的 setChecked 放在 toggled connect 之前
    // ——恢复不触发 onTogglePanel，避免 populate 风暴（brief 允许 blockSignals 或先设再连，取后者）。
    {
        const bool panelOn = Settings::value("view/thumbPanel", false).toBool();
        ui.actPanel->setChecked(panelOn);
        updatePanelVisibility();   // 启动时目录为空 → 面板隐藏（显隐还看有无图片）
        const QByteArray split = Settings::value("view/thumbSplit").toByteArray();
        if (!split.isEmpty())
            ui.centerSplit->restoreState(split);
        else
            ui.centerSplit->setSizes(QList<int>() << 600 << 132);   // 首次：底部条 132px
    }
    m_winGeometry = Settings::value("win/geometry").toByteArray();
    if (!m_winGeometry.isEmpty())
        restoreGeometry(m_winGeometry);            // 构造期先套一次；首次 showEvent 再套一次（防 main.cpp resize 覆盖）

    m_cache = std::make_shared<PreloadCache>();
    setAcceptDrops(true);

    m_thumbs = new ThumbnailLoader(this);

    // Task 7：画布背景三档（读档应用）+ 右键菜单（显式 connect customContextMenuRequested）。
    const int bgIdx = Settings::value("view/bgMode", int(ImageView::Dark)).toInt();
    m_bgMode = (bgIdx >= 0 && bgIdx <= int(ImageView::Checker))
                   ? ImageView::Background(bgIdx) : ImageView::Dark;
    ui.canvas->setBackgroundMode(m_bgMode);
    ui.canvas->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.canvas, &QGraphicsView::customContextMenuRequested,
            this, &MainWindow::onCanvasMenu);

    // 状态栏右侧常驻信息（分辨率 · 文件大小 / 缩放 / 播放状态）。属于运行时 helper 控件，非布局。
    m_lblInfo = new QLabel(this);
    m_lblZoom = new QLabel(this);
    m_lblSlide = new QLabel(tr("▶ 播放中"), this);
    m_lblSlide->setVisible(false);
    m_lblFile = new QLabel(this);
    m_lblFile->setObjectName(QStringLiteral("lblFile"));
    ui.statusBar->addWidget(m_lblFile);   // addWidget=左侧常驻；上面三件 permanent 在右侧
    ui.statusBar->addPermanentWidget(m_lblInfo);
    ui.statusBar->addPermanentWidget(m_lblZoom);
    ui.statusBar->addPermanentWidget(m_lblSlide);

    connect(ui.actOpen,     &QAction::triggered, this, &MainWindow::onOpen);
    connect(ui.actPrev,     &QAction::triggered, this, &MainWindow::onPrev);
    connect(ui.actNext,     &QAction::triggered, this, &MainWindow::onNext);
    connect(ui.actFit,      &QAction::triggered, this, &MainWindow::onFit);
    connect(ui.actActual,   &QAction::triggered, this, &MainWindow::onActual);
    connect(ui.actZoomIn,   &QAction::triggered, this, &MainWindow::onZoomIn);
    connect(ui.actZoomOut,  &QAction::triggered, this, &MainWindow::onZoomOut);
    connect(ui.actRotateL,  &QAction::triggered, this, &MainWindow::onRotateLeft);
    connect(ui.actRotateR,  &QAction::triggered, this, &MainWindow::onRotateRight);
    connect(ui.actFlipH,    &QAction::triggered, this, &MainWindow::onFlipH);
    connect(ui.actFlipV,    &QAction::triggered, this, &MainWindow::onFlipV);
    connect(ui.actPanel,    &QAction::toggled,   this, &MainWindow::onTogglePanel);
    connect(ui.actInfo,     &QAction::triggered, this, &MainWindow::onInfo);
    connect(ui.canvas,      &ImageView::zoomChanged, this, &MainWindow::onZoomChanged);
    // final-fix B2b：鼠标侧键翻页（ImageView::mousePressEvent 发信号，显式 connect）。
    connect(ui.canvas,      &ImageView::prevRequested, this, &MainWindow::onPrev);
    connect(ui.canvas,      &ImageView::nextRequested, this, &MainWindow::onNext);

    // Task 5：幻灯片与全屏。全部显式 connect，不依赖 on_<widget>_<signal> 自动连接命名。
    connect(ui.actSlide,    &QAction::toggled,   this, &MainWindow::onSlideToggled);
    connect(&m_slide,       &SlideShowController::tick, this, &MainWindow::onNext);

    // Task 6：删除进回收站。显式 connect（actDelete 在 Viewer.ui 中未声明 shortcut，
    // Delete 键只在 keyPressEvent 里绑定一次 → 不存在双触发）。
    connect(ui.actDelete,   &QAction::triggered, this, &MainWindow::onDelete);

    // 显式 connect：点击缩略图切换。context object = this，MainWindow 析构后 lambda 不再触发，
    // 捕获 this 不会悬垂。
    connect(ui.thumbPanel, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty() && m_model.setPath(path)) { rebuildThumbPanel(); showCurrent(); }
    });

    updateActionStates();   // Task 7：启动即无图 → prev/next/slide/delete 禁用
    L_INFO("主窗口初始化完成：主题={}，缩略图面板={}，背景模式={}",
           m_theme, ui.thumbPanel->isVisible() ? "开" : "关", int(m_bgMode));
}

void MainWindow::onOpen()
{
    // Task 7：初始目录优先用上次关窗时保存的 win/lastDir（无效则回落 home）。
    static QString dir = [] {
        const QString saved = Settings::value("win/lastDir").toString();
        return (!saved.isEmpty() && QDir(saved).exists()) ? saved : QDir::homePath();
    }();
    const QString path = QFileDialog::getOpenFileName(this, tr("打开图片"), dir,
        tr("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff *.ico *.svg *.heic *.heif *.hif);;所有文件 (*)"));
    if (!path.isEmpty()) { dir = QFileInfo(path).absolutePath(); L_INFO("文件对话框选择: {}", path.toStdString()); openFile(path); }
}

// final-fix I1+I2：统一入口（openFile / dropEvent / main.cpp CLI 共用此路径）。
// 目录 → 解析为首个受支持图片（spec §5 "拖拽文件/文件夹"）；
// 路径不存在 / 目录无图 / setPath 失败 → 状态栏提示并回空态（spec §7）。
void MainWindow::openFile(const QString& path)
{
    QString target = path;
    if (QFileInfo(path).isDir())
        target = FolderModel::firstSupportedFile(path);
    if (target.isEmpty() || !m_model.setPath(target)) {
        L_WARN("openFile 失败: {}", path.toStdString());
        ui.statusBar->showMessage(tr("无法打开：%1").arg(path), 5000);
        updateActionStates();
        return;
    }
    L_INFO("openFile: {} → {}", path.toStdString(), target.toStdString());
    rebuildThumbPanel();
    showCurrent();
}

void MainWindow::showCurrent()
{
    const QString path = m_model.current();
    if (path.isEmpty()) return;
    startLoad(path);
    const int idx = m_model.index();
    const int total = m_model.files().size();
    ui.thumbPanel->setCurrentRow(idx);
    if (auto* it = ui.thumbPanel->item(idx)) ui.thumbPanel->scrollToItem(it);
    setWindowTitle(tr("%1 (%2/%3) - LightSee 看图")
        .arg(QFileInfo(path).fileName()).arg(idx + 1).arg(total));
    syncTitle();
}

// windowTitle 仍是任务栏/Alt-Tab 的显示源；左下角只镜像"文件名 (序号/总数)"。
void MainWindow::syncTitle()
{
    const QString path = m_model.current();
    if (path.isEmpty() || m_model.files().isEmpty()) {
        m_lblFile->clear();
        return;
    }
    m_lblFile->setText(tr("%1 (%2/%3)").arg(QFileInfo(path).fileName())
                       .arg(m_model.index() + 1).arg(m_model.files().size()));
}

void MainWindow::updatePanelVisibility()
{
    // 无图可看时底部条没有意义：显隐 = 用户勾选 && 目录里有图。
    ui.thumbPanel->setVisible(ui.actPanel->isChecked() && !m_model.files().isEmpty());
}

void MainWindow::rebuildThumbPanel()
{
    updatePanelVisibility();
    // 懒加载：面板可见时才 populate；setPath 后可见则立即重建。
    if (ui.thumbPanel->isVisible())
        m_thumbs->populate(ui.thumbPanel, m_model.files());
}

void MainWindow::onNext()
{
    if (m_model.files().isEmpty()) return;
    if (!m_model.next().isEmpty()) showCurrent();
}

void MainWindow::onPrev()
{
    if (m_model.files().isEmpty()) return;
    if (!m_model.prev().isEmpty()) showCurrent();
}

void MainWindow::startLoad(const QString& path)
{
    m_currentPath = path;
    ++m_loadSeq;

    // Task 7：命中预加载缓存 → 立即上屏。m_loadSeq 已 bump，先前启动的在途
    // watcher 结果会因 seq 不符被丢弃，不会覆盖本次画面（brief 要求的防竞态）。
    QImage cached;
    if (m_cache->get(path, &cached)) {
        L_DEBUG("预加载缓存命中: {}", path.toStdString());
        onLoadFinished(path, cached);
        return;
    }

    const quint64 seq = m_loadSeq;
    auto* w = new QFutureWatcher<QImage>(this);
    connect(w, &QFutureWatcher<QImage>::finished, this, [this, w, seq, path]() {
        const QImage img = w->result();
        w->deleteLater();
        if (seq == m_loadSeq) onLoadFinished(path, img);
    });
    w->setFuture(QtConcurrent::run(&decodeImage, path));
    L_DEBUG("异步解码开始: {}", path.toStdString());
    ui.statusBar->showMessage(tr("正在加载 %1 …").arg(QFileInfo(path).fileName()));
}

// Task 7：成功上屏后对相邻两张 fire-and-forget 预解码。
// 注意不能调用 m_model.next()/prev() —— 它们会移动索引；改用 files()+index()
// 环形取模自行窥视（与 FolderModel 的环绕语义一致）。
void MainWindow::preloadNeighbors()
{
    const QStringList files = m_model.files();
    const int i = m_model.index();
    const int n = files.size();
    if (n == 0 || i < 0 || i >= n) return;
    const QString paths[2] = { files[(i + 1) % n], files[(i - 1 + n) % n] };
    for (const QString& p : paths) {
        if (p.isEmpty() || m_cache->contains(p)) continue;   // 已缓存不重复解码
        QtConcurrent::run(&preloadInto, m_cache, p);         // worker 只持 shared_ptr 副本，见 PreloadCache.h
    }
}

// Task 7：无图状态（启动、末图删空）统一禁用；有图（onLoadFinished 到达）恢复。
void MainWindow::updateActionStates()
{
    const bool hasImage = !m_model.current().isEmpty();
    ui.actPrev->setEnabled(hasImage);
    ui.actNext->setEnabled(hasImage);
    ui.actSlide->setEnabled(hasImage);
    ui.actDelete->setEnabled(hasImage);
}

void MainWindow::onLoadFinished(const QString& path, const QImage& img)
{
    if (img.isNull()) {
        L_WARN("图片解码失败: {}", path.toStdString());
        ui.statusBar->showMessage(tr("%1 无法读取（已跳过记录）").arg(QFileInfo(path).fileName()));
        ui.canvas->clearImage();
        if (m_lblInfo) m_lblInfo->clear();
        // Task 6：HEIC 解码失败 → 给出一次性的部署完整性提示（其他格式不受影响）。
        maybeWarnHeicUnavailable(path);
        updateActionStates();   // Task 7：坏图仍允许 prev/next 走开，不置灰
        return;
    }
    ui.canvas->setImage(img);
    const QFileInfo fi(path);
    L_DEBUG("已上屏: {} ({}x{}, {})", path.toStdString(), img.width(), img.height(), humanSize(fi.size()).toStdString());
    ui.statusBar->clearMessage();
    if (m_lblInfo)
        m_lblInfo->setText(tr("%1×%2 · %3").arg(img.width()).arg(img.height()).arg(humanSize(fi.size())));
    updateActionStates();       // Task 7：有图上屏 → 恢复动作可用
    preloadNeighbors();         // Task 7：预解码相邻两张（命中缓存的路径直接跳过）
}

void MainWindow::maybeWarnHeicUnavailable(const QString& path)
{
    if (m_heicWarnShown || !isHeicSuffix(path)) return;
    m_heicWarnShown = true;   // 本次会话只弹一次，避免连续翻看 HEIC 时反复打断
    L_WARN("HEIC 解码失败，提示检查 heif.dll/qheif 插件部署: {}", path.toStdString());
    QMessageBox::warning(this, tr("无法解码 HEIC"),
        tr("无法解码 HEIC。请确认 exe 目录下存在 heif.dll/libde265.dll "
           "且 imageformats\\qheif.dll 已部署。"));
}

void MainWindow::onFit()    { ui.canvas->fitToWindow(); }
void MainWindow::onActual() { ui.canvas->setActualSize(); }
void MainWindow::onZoomIn() { ui.canvas->zoomBy(1.25); }
void MainWindow::onZoomOut(){ ui.canvas->zoomBy(0.8); }
void MainWindow::onRotateLeft()  { ui.canvas->rotateBy(-90); }
void MainWindow::onRotateRight() { ui.canvas->rotateBy(90); }
void MainWindow::onFlipH()  { ui.canvas->flipHorizontal(); }
void MainWindow::onFlipV()  { ui.canvas->flipVertical(); }

void MainWindow::onTogglePanel()
{
    updatePanelVisibility();
    if (ui.thumbPanel->isVisible()) rebuildThumbPanel();
}

void MainWindow::onZoomChanged(double f)
{
    if (m_lblZoom) m_lblZoom->setText(tr("缩放 %1%").arg(qRound(f * 100.0)));
}

void MainWindow::onInfo()
{
    const QString path = m_currentPath.isEmpty() ? m_model.current() : m_currentPath;
    if (path.isEmpty()) { QMessageBox::information(this, tr("图片信息"), tr("当前没有打开的图片。")); return; }

    const QFileInfo fi(path);
    QImageReader r(path);
    const QSize dim = r.size();

    // 格式：QImageReader::supportedImageFormats 命中当前后缀
    const QByteArray suf = fi.suffix().toLower().toLatin1();
    QString fmt = fi.suffix().toUpper();
    const QList<QByteArray> supported = QImageReader::supportedImageFormats();
    for (const QByteArray& f : supported)
        if (f.toLower() == suf) { fmt = QString::fromLatin1(f).toUpper(); break; }

    const QString text = tr("文件名：%1\n完整路径：%2\n格式：%3\n尺寸：%4×%5\n文件大小：%6\n修改时间：%7")
        .arg(fi.fileName(), fi.absoluteFilePath(), fmt)
        .arg(dim.width()).arg(dim.height())
        .arg(humanSize(fi.size()), fi.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
    QMessageBox::about(this, tr("图片信息"), text);
}

void MainWindow::onSlideToggled(bool on)
{
    if (on)
        m_slide.start(Settings::value("slide/intervalMs", 3000).toInt());
    else
        m_slide.stop();
    if (m_lblSlide) m_lblSlide->setVisible(on);   // Task 7：播放中常驻 "▶ 播放中"
}

// ---- 工具栏自绘线条图标（图像操作去文字化）----
// 24×24 透明画布 + 2px 圆头描边，与画布悬浮箭头同风格；不引入图片资源。
// 幻灯片/缩略图栏勾选后蓝底，而 QToolButton 勾选不会自动换 QIcon 模式
//（实测 Selected 无效），故各留一份反白版给 toggled 换装。
namespace {

constexpr double kPi = 3.14159265358979;

template<typename Draw>
QIcon lineIcon(Draw draw, const QColor& fg)
{
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(fg, 2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    draw(p, fg);
    p.end();
    return QIcon(pm);
}

// 圆弧箭头：角度制（0°=3 点钟方向，逆时针为正），终点处沿运动切线画 V 形箭头。
void arcArrow(QPainter& p, const QPointF& c, double r, double startDeg, double endDeg)
{
    p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r),
              int(startDeg * 16), int((endDeg - startDeg) * 16));
    const double a = endDeg * kPi / 180.0;
    const QPointF e(c.x() + r * std::cos(a), c.y() - r * std::sin(a));
    const double s = endDeg >= startDeg ? 1.0 : -1.0;      // 行进方向：+逆时针 -顺时针
    const QPointF t(s * -std::sin(a), s * -std::cos(a));   // 屏幕坐标切线
    const QPointF n(-t.y(), t.x());
    const QPointF back = e - t * 5.5;
    p.drawPolyline(QPolygonF() << back + n * 3.2 << e << back - n * 3.2);
}

void installToolbarIcons(Ui::ViewerForm& ui, const QColor& fg,
                         QIcon& slideOff, QIcon& slideOn,
                         QIcon& panelOff, QIcon& panelOn)
{
    ui.actOpen->setIcon(lineIcon([](QPainter& p, const QColor&) {
        p.drawPolyline(QPolygonF() << QPointF(3.5, 19.5) << QPointF(3.5, 7)
                                   << QPointF(9, 7) << QPointF(11, 9.2) << QPointF(20.5, 9.2)
                                   << QPointF(20.5, 19.5) << QPointF(3.5, 19.5));
    }, fg));
    ui.actPrev->setIcon(lineIcon([](QPainter& p, const QColor&) {
        p.drawEllipse(QPointF(12, 12), 9.2, 9.2);
        p.drawPolyline(QPolygonF() << QPointF(13.8, 7.8) << QPointF(9.8, 12) << QPointF(13.8, 16.2));
    }, fg));
    ui.actNext->setIcon(lineIcon([](QPainter& p, const QColor&) {
        p.drawEllipse(QPointF(12, 12), 9.2, 9.2);
        p.drawPolyline(QPolygonF() << QPointF(10.2, 7.8) << QPointF(14.2, 12) << QPointF(10.2, 16.2));
    }, fg));
    const auto zoom = [](bool in) {
        return [in](QPainter& p, const QColor&) {
            p.drawEllipse(QPointF(10.5, 10.5), 6.0, 6.0);
            p.drawLine(QPointF(14.9, 14.9), QPointF(19.5, 19.5));
            if (in) p.drawLine(QPointF(10.5, 7.8), QPointF(10.5, 13.2));
            p.drawLine(QPointF(7.8, 10.5), QPointF(13.2, 10.5));
        };
    };
    ui.actZoomIn->setIcon(lineIcon(zoom(true), fg));
    ui.actZoomOut->setIcon(lineIcon(zoom(false), fg));
    ui.actFit->setIcon(lineIcon([](QPainter& p, const QColor&) {
        p.drawPolyline(QPolygonF() << QPointF(4, 8) << QPointF(4, 4) << QPointF(8, 4));
        p.drawPolyline(QPolygonF() << QPointF(16, 4) << QPointF(20, 4) << QPointF(20, 8));
        p.drawPolyline(QPolygonF() << QPointF(20, 16) << QPointF(20, 20) << QPointF(16, 20));
        p.drawPolyline(QPolygonF() << QPointF(8, 20) << QPointF(4, 20) << QPointF(4, 16));
        p.drawRect(QRectF(8.5, 9.5, 7, 5));
    }, fg));
    ui.actActual->setIcon(lineIcon([](QPainter& p, const QColor& c) {
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(c);
        p.drawText(QRectF(0, 0, 24, 24), Qt::AlignCenter, QStringLiteral("1:1"));
    }, fg));
    ui.actRotateL->setIcon(lineIcon([](QPainter& p, const QColor&) {
        arcArrow(p, QPointF(12, 12.5), 8.0, -190, 90);   // 箭头在顶部指向左
    }, fg));
    ui.actRotateR->setIcon(lineIcon([](QPainter& p, const QColor&) {
        arcArrow(p, QPointF(12, 12.5), 8.0, 10, -270);   // 箭头在顶部指向右
    }, fg));
    const auto flip = [](bool horizontal) {
        return [horizontal](QPainter& p, const QColor& c) {
            QPen dash(c, 2, Qt::DashLine);
            dash.setCapStyle(Qt::FlatCap);
            p.setPen(dash);
            if (horizontal) p.drawLine(QPointF(12, 3), QPointF(12, 21));
            else            p.drawLine(QPointF(3, 12), QPointF(21, 12));
            QPen solid(c, 2);
            solid.setCapStyle(Qt::RoundCap);
            solid.setJoinStyle(Qt::RoundJoin);
            p.setPen(solid);
            if (horizontal) {
                p.drawPolyline(QPolygonF() << QPointF(9.5, 6.5) << QPointF(4.5, 12)
                                           << QPointF(9.5, 17.5) << QPointF(9.5, 6.5));
                p.drawPolyline(QPolygonF() << QPointF(14.5, 6.5) << QPointF(19.5, 12)
                                           << QPointF(14.5, 17.5) << QPointF(14.5, 6.5));
            } else {
                p.drawPolyline(QPolygonF() << QPointF(6.5, 9.5) << QPointF(12, 4.5)
                                           << QPointF(17.5, 9.5) << QPointF(6.5, 9.5));
                p.drawPolyline(QPolygonF() << QPointF(6.5, 14.5) << QPointF(12, 19.5)
                                           << QPointF(17.5, 14.5) << QPointF(6.5, 14.5));
            }
        };
    };
    ui.actFlipH->setIcon(lineIcon(flip(true), fg));
    ui.actFlipV->setIcon(lineIcon(flip(false), fg));
    const auto slideDraw = [](QPainter& p, const QColor&) {
        p.drawRoundedRect(QRectF(3, 4.5, 18, 12.5), 2, 2);
        p.drawLine(QPointF(12, 17), QPointF(12, 20.5));
        p.drawLine(QPointF(8.5, 20.5), QPointF(15.5, 20.5));
        p.drawPolyline(QPolygonF() << QPointF(10.2, 8) << QPointF(10.2, 13.5)
                                   << QPointF(14.8, 10.75) << QPointF(10.2, 8));
    };
    slideOff = lineIcon(slideDraw, fg);
    slideOn  = lineIcon(slideDraw, Qt::white);
    ui.actSlide->setIcon(ui.actSlide->isChecked() ? slideOn : slideOff);
    ui.actInfo->setIcon(lineIcon([](QPainter& p, const QColor&) {
        p.drawEllipse(QPointF(12, 12), 9.2, 9.2);
        p.drawLine(QPointF(12, 7.3), QPointF(12, 7.5));      // i 点（圆头短划）
        p.drawLine(QPointF(12, 10.8), QPointF(12, 16.6));    // i 竖
    }, fg));
    const auto panelDraw = [](QPainter& p, const QColor& c) {
        p.drawRect(QRectF(3.5, 3.5, 17, 10));                          // 上：画布
        p.setPen(Qt::NoPen);
        p.setBrush(c);                                                 // 下：三枚缩略块
        p.drawRect(QRectF(3.5, 16.5, 4.8, 4));
        p.drawRect(QRectF(9.6, 16.5, 4.8, 4));
        p.drawRect(QRectF(15.7, 16.5, 4.8, 4));
    };
    panelOff = lineIcon(panelDraw, fg);
    panelOn  = lineIcon(panelDraw, Qt::white);
    ui.actPanel->setIcon(ui.actPanel->isChecked() ? panelOn : panelOff);
    ui.actDelete->setIcon(lineIcon([](QPainter& p, const QColor&) {
        p.drawLine(QPointF(4.5, 6.5), QPointF(19.5, 6.5));
        p.drawPolyline(QPolygonF() << QPointF(9.5, 6.5) << QPointF(9.5, 4)
                                   << QPointF(14.5, 4) << QPointF(14.5, 6.5));
        p.drawPolyline(QPolygonF() << QPointF(6.5, 6.5) << QPointF(7.6, 20.5)
                                   << QPointF(16.4, 20.5) << QPointF(17.5, 6.5));
        p.drawLine(QPointF(10.2, 10), QPointF(10.2, 17));
        p.drawLine(QPointF(13.8, 10), QPointF(13.8, 17));
    }, fg));
}

} // namespace

// 主题切换：整体替换样式表，Qt 会对所有子控件重新 polish，无需重启。
void MainWindow::applyTheme()
{
    QFile qss(m_theme == 1 ? ":/light.qss" : ":/dark.qss");
    if (qss.open(QIODevice::ReadOnly))
        setStyleSheet(QString::fromUtf8(qss.readAll()));
    else {
        L_WARN("主题样式表资源缺失: {}", qss.fileName().toStdString());
        setStyleSheet(QString());
    }
    installToolbarIcons(ui, m_theme == 1 ? QColor(0x33, 0x35, 0x38)
                                         : QColor(0xd8, 0xd9, 0xdb),
                        m_slideIconOff, m_slideIconOn,
                        m_panelIconOff, m_panelIconOn);
}

// Task 7：画布右键菜单（customContextMenuRequested 显式 connect 进来）。
// 菜单项是代码内 QMenu/Action，非布局重建，不违反 .ui 规则。
void MainWindow::onCanvasMenu(const QPoint& pos)
{
    QMenu menu(this);

    QMenu* bgMenu = menu.addMenu(tr("画布背景"));
    QList<QAction*> bgActs;
    bgActs << bgMenu->addAction(tr("深色")) << bgMenu->addAction(tr("浅色"))
           << bgMenu->addAction(tr("棋盘格"));
    for (int i = 0; i < bgActs.size(); ++i) {
        QAction* a = bgActs[i];
        a->setCheckable(true);
        a->setData(i);
        a->setChecked(i == int(m_bgMode));
    }

    QMenu* themeMenu = menu.addMenu(tr("界面主题"));
    QList<QAction*> themeActs;
    themeActs << themeMenu->addAction(tr("深色")) << themeMenu->addAction(tr("白色"));
    for (int i = 0; i < themeActs.size(); ++i) {
        QAction* a = themeActs[i];
        a->setCheckable(true);
        a->setData(i);
        a->setChecked(i == m_theme);
    }

    QMenu* intervalMenu = menu.addMenu(tr("幻灯片间隔"));
    // 无需 QActionGroup：QMenu::exec 是模态的，点击任一项即关窗，不存在两项同选中的窗口期。
    const int curMs = Settings::value("slide/intervalMs", 3000).toInt();
    for (int s = 1; s <= 10; ++s) {
        QAction* a = intervalMenu->addAction(tr("%1 秒").arg(s));
        a->setCheckable(true);
        a->setData(s * 1000);
        a->setChecked(s * 1000 == curMs);
    }

    menu.addSeparator();
    QAction* assocAct = menu.addAction(tr("关联图片格式（写入系统）"));
    QAction* aboutAct = menu.addAction(tr("关于 LightSee"));

    QAction* picked = menu.exec(ui.canvas->mapToGlobal(pos));
    if (!picked) return;

    if (picked == assocAct) {
        QString err;
        if (FileAssoc::registerPhotoAssociations(QCoreApplication::applicationFilePath(), &err)) {
            L_INFO("图片格式关联已写入 HKCU");
            QMessageBox::information(this, tr("关联完成"),
                tr("已登记到右键\"打开方式\"（png/jpg/jpeg/bmp/gif/webp/heic）。\n\n"
                   "系统限制无法静默改双击默认，请再操作一次：\n"
                   "右键任意图片 → 打开方式 → 其他应用 → 选 LightSee 看图 → 勾选\"始终\"。"));
        } else {
            L_ERROR("图片格式关联失败: {}", err.toStdString());
            ui.statusBar->showMessage(
                tr("<span style=\"color:#e5484d;\">关联失败：%1</span>").arg(err));
        }
        return;
    }

    if (picked == aboutAct) {
        QMessageBox::about(this, tr("关于 LightSee"),
            tr("<b>LightSee 看图 %1</b><br/>Qt %2<br/>%3")
                .arg(QStringLiteral(LIGHTSEE_VERSION_STRING), QStringLiteral(QT_VERSION_STR),
                     QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
        return;
    }

    if (bgActs.contains(picked)) {
        m_bgMode = ImageView::Background(picked->data().toInt());
        ui.canvas->setBackgroundMode(m_bgMode);
        Settings::setValue("view/bgMode", int(m_bgMode));
        L_DEBUG("画布背景切换为 {}", int(m_bgMode));
    } else if (themeActs.contains(picked)) {
        m_theme = picked->data().toInt();
        applyTheme();
        Settings::setValue("ui/theme", m_theme);
        L_DEBUG("界面主题切换为 {}", m_theme);
    } else {   // 间隔项：data = ms
        const int ms = picked->data().toInt();
        Settings::setValue("slide/intervalMs", ms);
        if (m_slide.isRunning()) m_slide.start(ms);   // 播放中改间隔 → 立即按新节律重启计时
        L_DEBUG("幻灯片间隔设置为 {}ms", ms);
    }
}

// Task 6：当前图片移入回收站（可撤销删除）。失败绝不动模型，成功才重建树/切图。
void MainWindow::onDelete()
{
    const QString oldPath = m_model.current();
    if (oldPath.isEmpty()) {
        ui.statusBar->showMessage(tr("当前没有打开的图片。"), 3000);
        return;
    }

    const QString fileName = QFileInfo(oldPath).fileName();
    if (QMessageBox::question(this, tr("移入回收站"), tr("将移入回收站：%1？").arg(fileName),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString err;
    if (!RecycleBin::moveToRecycleBin(oldPath, &err)) {
        L_ERROR("移入回收站失败: {}（原因: {}）", oldPath.toStdString(), err.toStdString());
        // T8 才有 QSS；这里用消息标签自带的富文本着色（QLabel::AutoText 会识别 HTML）。
        ui.statusBar->showMessage(
            tr("<span style=\"color:#e5484d;\">无法移入回收站：%1（原因：%2）</span>").arg(fileName, err));
        return;   // 模型与缩略图栏保持不变
    }

    m_model.removeFile(oldPath);
    L_INFO("已移入回收站: {}（目录剩余 {} 张）", oldPath.toStdString(), m_model.files().size());
    rebuildThumbPanel();   // 已删条目从面板消失（面板可见时 populate，等价于 m_thumbs->populate）

    if (m_model.files().isEmpty()) {
        L_INFO("目录内已无图片，回空态并停止幻灯片");
        m_currentPath.clear();
        ++m_loadSeq;       // 作废仍在途的异步解码结果，避免它把已删图片又画回来
        // 目录已无图片：取消幻灯片勾选。setChecked(false) 触发 toggled(false) →
        // 构造函数中显式 connect 的 onSlideToggled(false) → m_slide.stop()，
        // 复用既有停表链路，也让 toggle 的勾选态与实际状态一致。
        ui.actSlide->setChecked(false);
        ui.canvas->clearImage();
        if (m_lblInfo) m_lblInfo->clear();
        if (m_lblZoom) m_lblZoom->clear();
        setWindowTitle(tr("LightSee 看图"));
        syncTitle();
        updateActionStates();   // Task 7：末图删空 → prev/next/slide/delete 回到禁用
        ui.statusBar->showMessage(tr("已移入回收站：%1（目录内已无图片）").arg(fileName), 5000);
        return;
    }
    showCurrent();
    ui.statusBar->showMessage(tr("已移入回收站：%1").arg(fileName), 5000);
}

void MainWindow::toggleFullScreen()
{
    m_fullscreen = !m_fullscreen;
    L_DEBUG("切换全屏: {}", m_fullscreen ? "开" : "关");
    ui.titleBar->setVisible(!m_fullscreen);
    if (m_fullscreen) {
        m_savedGeometry = saveGeometry();
        ui.toolBar->setVisible(false);
        showFullScreen();
    } else {
        showNormal();
        if (!m_savedGeometry.isEmpty())
            restoreGeometry(m_savedGeometry);
        ui.toolBar->setVisible(true);
    }
}

void MainWindow::keyPressEvent(QKeyEvent* e)
{
    // final-fix I3：Ctrl/Alt/Meta 组合键一律交回基类（QAction 快捷键如 Ctrl+O
    // 走 shortcut 系统，不经过这里）。经查下方 switch 无任何带修饰键的设计绑定，
    // 故无需豁免项；Shift 保持放行（switch 不用修饰键，Shift+A 等翻页无碍）。
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        QMainWindow::keyPressEvent(e);
        return;
    }
    // 注意：Space 预留给 Task 5 幻灯片切换，本任务不绑定 Space→next（控制方裁定）。
    switch (e->key()) {
    case Qt::Key_Left:  case Qt::Key_A:       onPrev();       return;
    case Qt::Key_Right: case Qt::Key_D:       onNext();       return;
    case Qt::Key_Plus:  case Qt::Key_Equal:   onZoomIn();     return;
    case Qt::Key_Minus:                       onZoomOut();    return;
    case Qt::Key_0:                           onFit();        return;
    case Qt::Key_1:                           onActual();     return;
    case Qt::Key_L:                           onRotateLeft(); return;
    case Qt::Key_R:                           onRotateRight();return;
    case Qt::Key_H:                           onFlipH();      return;
    case Qt::Key_V:                           onFlipV();      return;
    // Task 5：幻灯片与全屏。方向键（Left/Right 已在上方）在播放中手动翻页，
    // 不触碰 m_slide，因此不会重置计时器。
    case Qt::Key_F:  case Qt::Key_F11:        toggleFullScreen(); return;
    case Qt::Key_Space:                       ui.actSlide->trigger(); return;
    // Task 6：Delete 键。actDelete 在 .ui 中未声明 shortcut，故此处唯一触发点，不会双触发。
    case Qt::Key_Delete:                      onDelete();         return;
    case Qt::Key_Escape:
        // 仅在全屏时消费（退出全屏）；否则交给基类处理。
        if (m_fullscreen) { toggleFullScreen(); return; }
        break;
    default: break;
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();   // text/uri-list
}

void MainWindow::dropEvent(QDropEvent* e)
{
    const QList<QUrl> urls = e->mimeData()->urls();
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            L_DEBUG("拖放打开: {}", url.toLocalFile().toStdString());
            openFile(url.toLocalFile());
            break;
        }
    }
    e->acceptProposedAction();
}

// Task 7：关窗持久化 —— 窗口几何 / 缩略图面板勾选 / 最后所在目录。
// 之后若托盘可用且非"退出"路径：ignore + hide 收进托盘，进程存活；
// 托盘"退出"置 m_quitting 后再 close() 走到下面的真关闭，主窗口销毁→事件循环自然结束。
void MainWindow::closeEvent(QCloseEvent* e)
{
    Settings::setValue("win/geometry", saveGeometry());
    Settings::setValue("view/thumbPanel", ui.actPanel->isChecked());
    Settings::setValue("view/thumbSplit", ui.centerSplit->saveState());   // 记忆拖出来的高度比例
    const QString dir = QFileInfo(m_model.current()).absolutePath();
    if (!dir.isEmpty())
        Settings::setValue("win/lastDir", dir);
    L_INFO("关窗：持久化几何/面板状态/最后目录 {}", dir.toStdString());
    if (m_tray && !m_quitting) {
        L_INFO("关窗：收进系统托盘，进程保活");
        e->ignore();
        hide();
        return;
    }
    QMainWindow::closeEvent(e);
}

// 从托盘恢复窗口：hide 回来若残留最小化态需显式解除。
void MainWindow::showFromTray()
{
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

// Task 7：main.cpp 在构造后还调用 resize(1100,700)，会覆盖构造期的 restoreGeometry；
// 故首次 showEvent（此时所有外部尺寸设置都已发生）再恢复一次，保证位置+大小都记住。
void MainWindow::showEvent(QShowEvent* e)
{
    QMainWindow::showEvent(e);
    if (m_firstShow) {
        m_firstShow = false;
        if (!m_winGeometry.isEmpty())
            restoreGeometry(m_winGeometry);
#ifdef Q_OS_WIN
        // 首次 show 后 winId 才有真实 HWND；强制一次 NC 重算让 WM_NCCALCSIZE 生效。
        ::SetWindowPos(reinterpret_cast<HWND>(winId()), nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
#endif
    }
}

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange)
        ui.btnMax->setText(isMaximized() ? QStringLiteral("❐") : QStringLiteral("□"));
}

#ifdef Q_OS_WIN
// 系统标题栏裁掉但 WS_THICKFRAME/WS_CAPTION 样式保留：
//   WM_NCCALCSIZE → 客户区扩到整窗（最大化时按边框内收，防内容出屏）；
//   WM_NCHITTEST  → 四边报回 HTLEFT 等补缩放，自定义条报 HTCAPTION，
//                   于是拖动/双击最大化/Aero Snap/阴影全由系统原生提供。
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
    if (eventType == QByteArrayLiteral("windows_generic_MSG"))
    {
        MSG* msg = static_cast<MSG*>(message);
        // 只用 msg->hwnd：在 nativeEvent 里调 winId() 可能触发句柄重建/重入（曾致 TextShaping 回调崩溃）。
        HWND hwnd = msg->hwnd;
        const int framePx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        const int framePy = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);

        switch (msg->message)
        {
        case WM_NCCALCSIZE:
            if (msg->wParam)
            {
                NCCALCSIZE_PARAMS* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
                const RECT orig = p->rgrc[0];
                ::DefWindowProc(hwnd, WM_NCCALCSIZE, msg->wParam, msg->lParam);
                // 左/右/下沿用默认内收：Win10 窗口矩形在右/下含 ~8px 不可见缩放边框，
                // 若也扩成客户区，内容会铺出可视边缘 → 窗口右侧/底部出现透明空白条。
                // 顶部收回窗口顶端（普通态顶缘无出屏；最大化按边框收回，防内容出屏）。
                p->rgrc[0].top = orig.top + (IsZoomed(hwnd) ? framePy : 0);
                *result = 0;
                return true;
            }
            break;

        case WM_NCHITTEST:
        {
            // 物理屏幕坐标 → MainWindow 逻辑客户区坐标（最大化时客户原点内收一个边框）。
            const LONG px = GET_X_LPARAM(msg->lParam);
            const LONG py = GET_Y_LPARAM(msg->lParam);
            RECT wr;
            ::GetWindowRect(hwnd, &wr);
            const bool zoomed = IsZoomed(hwnd) != FALSE;
            const qreal dpr = windowHandle() ? windowHandle()->devicePixelRatio() : 1.0;
            // 客户原点的物理偏移：左右恒内收一个边框（见 WM_NCCALCSIZE），顶部仅最大化时内收。
            const QPoint local(int((px - wr.left  - framePx) / dpr),
                               int((py - wr.top   - (zoomed ? framePy : 0)) / dpr));

            // 边缘/角缩放优先（顶部 m 像素带压在标题条下面，不先判就摸不到）。
            if (!zoomed)
            {
                const int m = qMax(1, int(framePx / dpr));
                const bool l = local.x() < m, r = local.x() >= width()  - m;
                const bool t = local.y() < m, b = local.y() >= height() - m;
                if (l || r || t || b)
                {
                    if      (t && l) *result = HTTOPLEFT;
                    else if (t && r) *result = HTTOPRIGHT;
                    else if (b && l) *result = HTBOTTOMLEFT;
                    else if (b && r) *result = HTBOTTOMRIGHT;
                    else if (l)      *result = HTLEFT;
                    else if (r)      *result = HTRIGHT;
                    else if (t)      *result = HTTOP;
                    else             *result = HTBOTTOM;
                    return true;
                }
            }

            // 标题栏条带（含最大化态）：按钮区留 HTCLIENT 给 Qt，其余 HTCAPTION。
            if (ui.titleBar->isVisible() && ui.titleBar->geometry().contains(local))
            {
                for (QWidget* btn : { static_cast<QWidget*>(ui.btnMin),
                                      static_cast<QWidget*>(ui.btnMax),
                                      static_cast<QWidget*>(ui.btnClose) })
                {
                    if (QRect(btn->mapTo(this, QPoint()), btn->size()).contains(local))
                    {
                        *result = HTCLIENT;
                        return true;
                    }
                }
                *result = HTCAPTION;
                return true;
            }
            break;
        }

        default:
            break;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
