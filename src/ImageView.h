#pragma once
#include <QGraphicsView>

class QGraphicsPixmapItem;
class QToolButton;
class QPropertyAnimation;

class ImageView : public QGraphicsView
{
    Q_OBJECT
    // 画布悬浮翻页按钮的淡入淡出进度（0=隐藏 1=完全显示），供 QPropertyAnimation 驱动
    Q_PROPERTY(double overlayOpacity READ overlayOpacity WRITE setOverlayOpacity)
public:
    // Task 7：画布背景三档（checker = 16px 双灰平铺）。
    enum Background { Dark, Light, Checker };
    explicit ImageView(QWidget* parent = nullptr);
    void setImage(const QImage& img);
    void clearImage();
    void setBackgroundMode(Background mode);
public slots:
    void fitToWindow();
    void setActualSize();
    void zoomBy(double factor);
    void rotateBy(double degrees);
    void flipHorizontal();
    void flipVertical();
signals:
    void zoomChanged(double factor);
    void prevRequested();   // 鼠标侧键 Back（final-fix B2b）
    void nextRequested();   // 鼠标侧键 Forward
protected:
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
private:
    void createOverlayButtons();
    void setOverlayVisible(bool on);
    void setOverlayOpacity(double o);
    void repositionOverlayButtons();
    double overlayOpacity() const { return m_overlayOpacity; }
    void applyItemTransform();
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_item = nullptr;
    double m_rotation = 0.0;
    bool m_flipH = false, m_flipV = false;
    bool m_fitMode = true;
    QToolButton* m_btnPrev = nullptr;
    QToolButton* m_btnNext = nullptr;
    QPropertyAnimation* m_overlayAnim = nullptr;
    double m_overlayOpacity = 0.0;
    bool m_overlayShown = false;
};
