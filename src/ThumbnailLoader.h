#pragma once
#include <QObject>
#include <QThreadPool>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QStringList>
class QListWidget;
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject* parent = nullptr);
    ~ThumbnailLoader() override;                                   // 清空队列并等在途任务收尾
    void populate(QListWidget* target, const QStringList& paths);  // 重建并异步填图（缓存命中同步上图）
    void removeOne(const QString& path);                           // P3：删除单张，增量移除单个条目
    void cancel();                                                 // 丢弃进行中的结果与排队任务
private:
    void onDecoded(quint64 gen, int row, const QString& path, const QPixmap& pm);
    void evictOverBudget();
    QThreadPool m_pool;                    // P1：缩略图专用小池，不与主图解码抢 QtConcurrent 全局池
    QPointer<QListWidget> m_target;        // 当前 populate 的目标面板（随窗口析构自动置空）
    quint64 m_gen = 0;
    QHash<QString, QPixmap> m_cache;       // P3：path → 缩略图
    QString m_cacheDir;                    // 缓存归属目录：换目录整批失效
    qint64 m_cacheBytes = 0;               // 缓存字节记账（估算 ARGB32）
    enum { kCacheBudget = 96 * 1024 * 1024 };   // 字节预算（150x110 缩略图约 66KB/张）
};
