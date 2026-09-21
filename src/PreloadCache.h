#pragma once
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

// Task 7：相邻图片解码缓存（path -> QImage），供 MainWindow 预加载/命中上屏。
//
// 生命周期设计（关键）：MainWindow 以 std::shared_ptr<PreloadCache> 持有本缓存，
// fire-and-forget 的预加载 worker 按【值】捕获该 shared_ptr 副本、只写缓存绝不回调
// MainWindow —— 即使 worker 完成时窗口已析构，缓存仍被 worker 持有的副本保活，
// 不存在悬垂 this / use-after-free，析构侧也无需追踪或等待任何 future。
//
// 纯逻辑、无 GUI 依赖（QImage 可在 QCoreApplication 环境构造），SelfTest 直接测容量修剪。
class PreloadCache {
public:
    enum { Cap = 12 };   // 容量上限：brief 定死 12，超限清一半（trim 到 Cap/2）
    void put(const QString& path, const QImage& img)
    {
        if (img.isNull()) return;          // 解码失败的结果不进缓存
        QMutexLocker lk(&m_mtx);
        m_hash.insert(path, img);          // insert：同键覆盖，size 不增
        if (m_hash.size() > Cap) {         // 超限 → 按 key 字典序删最旧的一半（确定性，便于测试）
            QStringList keys = m_hash.keys();
            keys.sort();
            const int drop = m_hash.size() - Cap / 2;
            for (int i = 0; i < drop; ++i) m_hash.remove(keys[i]);
        }
    }
    bool get(const QString& path, QImage* out) const
    {
        QMutexLocker lk(&m_mtx);
        QHash<QString, QImage>::const_iterator it = m_hash.constFind(path);
        if (it == m_hash.constEnd()) return false;
        if (out) *out = it.value();        // QImage 隐式共享，拷贝廉价
        return true;
    }
    bool contains(const QString& path) const
    { QMutexLocker lk(&m_mtx); return m_hash.contains(path); }
    int size() const
    { QMutexLocker lk(&m_mtx); return m_hash.size(); }
private:
    mutable QMutex m_mtx;                  // get/contains 是 const，锁必须 mutable
    QHash<QString, QImage> m_hash;
};
