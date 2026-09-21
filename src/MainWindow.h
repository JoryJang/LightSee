#pragma once
#include <QMainWindow>
#include <QByteArray>
#include <memory>
#include "ui_Viewer.h"
#include "src/FolderModel.h"
#include "src/ImageView.h"
#include "src/SlideShowController.h"
#include "src/PreloadCache.h"

class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QKeyEvent;
class QLabel;
class ThumbnailLoader;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openFile(const QString& path);
protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    // Task 7：关窗保存 geometry/面板态/最后目录；首次 show 时恢复 geometry
    //（main.cpp 构造后有 resize()，构造期恢复会被它覆盖，故推迟到首次 showEvent）。
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;
    // 自定义标题栏：最大化态切换按钮字形（□/❐）。
    void changeEvent(QEvent* e) override;
#ifdef Q_OS_WIN
    // WM_NCCALCSIZE 裁掉系统标题栏（保留 WS_THICKFRAME），WM_NCHITTEST 把
    // 自定义条报回 HTCAPTION → 拖动/双击最大化/Aero Snap 全走系统实现。
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
#endif
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
    void onNext();
    void onPrev();
    void onInfo();
    void onSlideToggled(bool on);
    void onDelete();
    void onCanvasMenu(const QPoint& pos);   // Task 7：画布右键（背景三档/幻灯片间隔）
private:
    void startLoad(const QString& path);
    void showCurrent();
    void rebuildThumbPanel();
    void updatePanelVisibility();   // actPanel 勾选且当前目录有图才显示
    void toggleFullScreen();
    void maybeWarnHeicUnavailable(const QString& path);
    void preloadNeighbors();     // Task 7：fire-and-forget 预解码 next/prev 进缓存
    void updateActionStates();   // Task 7：无图时统一禁用 prev/next/slide/delete
    void applyTheme();           // 按 m_theme 加载 :/dark.qss 或 :/light.qss
    void syncTitle();            // windowTitle → lblTitle（自定义标题栏）
    Ui::ViewerForm ui;
    FolderModel m_model;
    SlideShowController m_slide;
    bool m_fullscreen = false;
    bool m_heicWarnShown = false;   // Task 6：HEIC 解码失败的部署提示只弹一次
    QByteArray m_savedGeometry;
    QByteArray m_winGeometry;       // Task 7：构造期读档、首次 showEvent 恢复
    bool m_firstShow = true;
    ImageView::Background m_bgMode = ImageView::Dark;   // Task 7：当前背景档
    int m_theme = 0;                                    // 0=深色 1=浅色（设置键 ui/theme）
    std::shared_ptr<PreloadCache> m_cache;              // Task 7：worker 持 shared_ptr 副本，见 PreloadCache.h
    ThumbnailLoader* m_thumbs = nullptr;
    QString m_currentPath;
    quint64 m_loadSeq = 0;
    QLabel* m_lblInfo = nullptr;
    QLabel* m_lblZoom = nullptr;
    QLabel* m_lblSlide = nullptr;   // Task 7：播放中常驻 "▶ 播放中"
};
