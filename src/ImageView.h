#pragma once
#include <QGraphicsView>

class QGraphicsPixmapItem;

class ImageView : public QGraphicsView
{
    Q_OBJECT
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
private:
    void applyItemTransform();
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_item = nullptr;
    double m_rotation = 0.0;
    bool m_flipH = false, m_flipV = false;
    bool m_fitMode = true;
};
