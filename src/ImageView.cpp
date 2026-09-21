#include "src/ImageView.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QBrush>
#include <cmath>

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

void ImageView::setImage(const QImage& img)
{
    m_scene->clear();
    m_item = m_scene->addPixmap(QPixmap::fromImage(img));
    m_item->setTransformationMode(Qt::SmoothTransformation);
    applyItemTransform();
    if (m_fitMode) fitToWindow(); else emit zoomChanged(transform().m11());
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

// final-fix B2b：鼠标侧键翻页（spec §5）。Back/Forward 消费掉；
// 其余按键（含 LeftButton 的 ScrollHandDrag 平移）交回基类。
void ImageView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::BackButton) { emit prevRequested(); e->accept(); return; }
    if (e->button() == Qt::ForwardButton) { emit nextRequested(); e->accept(); return; }
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
