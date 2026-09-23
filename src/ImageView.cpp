#include "src/ImageView.h"
#include "src/Log.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QBrush>
#include <QToolButton>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <cmath>

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setDragMode(ScrollHandDrag);
    // 滚动条隐藏（像常规看图软件）；ScrollHandDrag 走 scrollbar setValue，不依赖控件可见
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor(0x1e, 0x1f, 0x22));
    createOverlayButtons();
    viewport()->installEventFilter(this);
}

// 悬浮翻页按钮：圆形半透明白底 + 自绘箭头（与参考图一致），父对象是 viewport，
// 因此位置始终相对视口、不会被滚进场景；Enter/Leave 事件在 eventFilter 里统一处理。
static QIcon chevronIcon(bool left, int size)
{
    // 注意：不要用 setDevicePixelRatio 的高分 pixmap —— 5.14 的 QStyleSheetStyle
    // 画图标时按物理尺寸裁剪 DPR pixmap，会只剩箭头一角；1:1 绘制最稳。
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(0x55, 0x57, 0x5a), 2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    const double xIn = size * 0.35, xOut = size * 0.65;
    const double y0 = size * 0.25, yM = size * 0.52, y1 = size * 0.79;
    if (left) p.drawPolyline(QVector<QPointF>{ {xOut, y0}, {xIn, yM}, {xOut, y1} });
    else      p.drawPolyline(QVector<QPointF>{ {xIn, y0}, {xOut, yM}, {xIn, y1} });
    p.end();
    return QIcon(pm);
}

void ImageView::createOverlayButtons()
{
    const auto make = [this](bool left) {
        auto* b = new QToolButton(viewport());
        b->setIcon(chevronIcon(left, 30));
        b->setIconSize(QSize(30, 30));
        b->setFixedSize(56, 56);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(left ? QStringLiteral("上一张") : QStringLiteral("下一张"));
        b->setStyleSheet(
            "QToolButton { background-color: rgba(255,255,255,200);"
            " border: 1px solid rgba(0,0,0,25); border-radius: 28px; }"
            "QToolButton:hover { background-color: rgba(255,255,255,238); }"
            "QToolButton:pressed { background-color: rgba(216,220,226,238); }");
        auto* eff = new QGraphicsOpacityEffect(b);
        eff->setOpacity(0.0);
        b->setGraphicsEffect(eff);
        b->hide();
        connect(b, &QToolButton::clicked,
                this, left ? &ImageView::prevRequested : &ImageView::nextRequested);
        return b;
    };
    m_btnPrev = make(true);
    m_btnNext = make(false);
    m_overlayAnim = new QPropertyAnimation(this, "overlayOpacity", this);
    m_overlayAnim->setDuration(180);
    m_overlayAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_overlayAnim, &QPropertyAnimation::finished, this, [this] {
        if (!m_overlayShown)
            for (QToolButton* b : { m_btnPrev, m_btnNext }) b->hide();
    });
}

void ImageView::setOverlayVisible(bool on)
{
    if (m_overlayShown == on) return;
    m_overlayShown = on;
    m_overlayAnim->stop();
    if (on) {
        repositionOverlayButtons();
        for (QToolButton* b : { m_btnPrev, m_btnNext }) b->show();
    }
    m_overlayAnim->setStartValue(m_overlayOpacity);
    m_overlayAnim->setEndValue(on ? 1.0 : 0.0);
    m_overlayAnim->start();
}

void ImageView::setOverlayOpacity(double o)
{
    m_overlayOpacity = qBound(0.0, o, 1.0);
    for (QToolButton* b : { m_btnPrev, m_btnNext })
        static_cast<QGraphicsOpacityEffect*>(b->graphicsEffect())->setOpacity(m_overlayOpacity);
}

void ImageView::repositionOverlayButtons()
{
    if (!m_btnPrev) return;
    const int margin = 16;
    const int y = (viewport()->height() - m_btnPrev->height()) / 2;
    m_btnPrev->move(margin, y);
    m_btnNext->move(viewport()->width() - m_btnNext->width() - margin, y);
}

bool ImageView::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == viewport()) {
        switch (e->type()) {
        case QEvent::Enter:
            if (m_item) setOverlayVisible(true);
            break;
        case QEvent::Leave:
            setOverlayVisible(false);
            break;
        case QEvent::Resize:
            repositionOverlayButtons();
            break;
        default: break;
        }
    }
    return QGraphicsView::eventFilter(obj, e);
}

// Task 7：16px 双灰棋盘贴砖。Qt 5.14 中 QGraphicsView::drawBackground 用
// backgroundBrush 以“场景坐标”平铺（源码：drawBackground 先 applyTransform 再 fill），
// 因此棋盘格会随缩放一起放大、随 item 旋转而转动，并非固定在视口像素网格上。
// 若要视口恒定 16px，需改为 drawBackground + viewportTransform().inverted() 方案。
static QBrush checkerBrush()
{
    QPixmap tile(16, 16);
    tile.fill(QColor(0x82, 0x82, 0x82));
    QPainter p(&tile);
    p.fillRect(0, 0, 8, 8, QColor(0x9e, 0x9e, 0x9e));
    p.fillRect(8, 8, 8, 8, QColor(0x9e, 0x9e, 0x9e));
    p.end();
    return QBrush(tile);
}

void ImageView::setBackgroundMode(Background mode)
{
    switch (mode) {
    case Dark:    setBackgroundBrush(QColor(0x1e, 0x1f, 0x22)); break;
    case Light:   setBackgroundBrush(QColor(0xf0, 0xf0, 0xf0)); break;
    case Checker: setBackgroundBrush(checkerBrush());           break;
    }
    viewport()->update();
}

void ImageView::setImage(const QImage& img, bool keepView)
{
    L_DEBUG("画布显示图像: {}x{}{}", img.width(), img.height(), keepView ? " (保持视野)" : "");
    // 手动缩放下换像素源：记住当前倍率，重建 item 后原样还原（适应窗口态无需记，
    // 下面 fitToWindow 会按新图重算）。
    const bool keep = keepView && !m_fitMode && m_item;
    const double zoom = keep ? transform().m11() : 1.0;
    m_scene->clear();
    m_item = m_scene->addPixmap(QPixmap::fromImage(img));
    m_item->setTransformationMode(Qt::SmoothTransformation);
    applyItemTransform();   // 内含 sceneRect 收缩到当前图（大图后小图不残留滚动条）
    if (keep) {
        resetTransform();
        scale(zoom, zoom);
        centerOn(m_item);
        emit zoomChanged(transform().m11());
    } else {
        // 切图统一回到"适应窗口 + 居中"，不沿用上一张的缩放/平移状态。
        fitToWindow();
    }
    // 连续翻页时鼠标未离开画布，保持按钮可见（Enter 不会再触发）
    if (m_item && viewport()->underMouse()) setOverlayVisible(true);
}

void ImageView::clearImage()
{
    L_DEBUG("画布清空");
    m_scene->clear();
    m_item = nullptr;
    setOverlayVisible(false);
}

void ImageView::fitToWindow()
{
    if (!m_item) return;
    m_fitMode = true;
    resetTransform();
    fitInView(m_item, Qt::KeepAspectRatio);
    centerOn(m_item);   // 5.14 fitInView 有取整偏移，补一次真居中
    emit zoomChanged(transform().m11());
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
    const double z = transform().m11() * factor;
    if (z < 0.05 || z > 8.0) return;
    m_fitMode = false;
    scale(factor, factor);
    emit zoomChanged(transform().m11());
}

void ImageView::rotateBy(double degrees) { m_rotation = std::fmod(m_rotation + degrees, 360.0); applyItemTransform(); if (m_fitMode) fitToWindow(); }
void ImageView::flipHorizontal() { m_flipH = !m_flipH; applyItemTransform(); }
void ImageView::flipVertical()   { m_flipV = !m_flipV; applyItemTransform(); }

void ImageView::applyItemTransform()
{
    if (!m_item) return;
    // 绕图片中心变换：显式 bake translate(c)*T*translate(-c) 进矩阵。
    // 不用 setTransformOriginPoint —— 实测 5.14 的 sceneBoundingRect 不把它计入，
    // 包围盒/命中仍按左上角旋转，位置照样跳。
    const QPointF c = m_item->boundingRect().center();
    m_item->setTransform(QTransform()
        .translate(c.x(), c.y())
        .rotate(m_rotation)
        .scale(m_flipH ? -1 : 1, m_flipV ? -1 : 1)
        .translate(-c.x(), -c.y()));
    // sceneRect 跟随变换后的包围盒（90° 旋转宽高互换），否则滚动条/适应缩放按旧矩形算。
    m_scene->setSceneRect(m_item->sceneBoundingRect());
}

void ImageView::wheelEvent(QWheelEvent* e)
{
    zoomBy(e->angleDelta().y() > 0 ? 1.25 : 0.8);
    e->accept();
}

// final-fix B2b：鼠标侧键翻页（spec §5）。左键拖拽中压到侧键不翻页（握持发力易误触），
// 吞掉事件让拖拽继续；松开后的侧键单击照常翻页。
void ImageView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::BackButton || e->button() == Qt::ForwardButton) {
        if (!(e->buttons() & Qt::LeftButton)) {
            if (e->button() == Qt::BackButton) emit prevRequested();
            else                               emit nextRequested();
        }
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
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

// viewport 滚动（拖拽平移/滚轮缩放）经 QWidget::scroll 连带平移其子控件，
// 悬浮翻页按钮会被拖走；每次滚动后摆回固定视口位置。
void ImageView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    repositionOverlayButtons();
}
