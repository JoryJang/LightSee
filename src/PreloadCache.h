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
// 容量（P4）：双上限——张数 Cap=12（brief 原定，超限清一半）+ 字节预算 Budget=512MB。
// 纯张数限容的问题：48MP 相机图 ARGB32 约 260MB/张，12 张可致数 GB 级内存峰值；
// 字节超预算时逐个驱逐（QHash 无序即任意序），但保底留 1 张——单张上界已由解码
// 尺寸守卫限制（见 ImageLimits.h），超大图的预加载仍有意义。
//
// 纯逻辑、无 GUI 依赖（QImage 可在 QCoreApplication 环境构造），SelfTest 直接测容量修剪。
class PreloadCache {
public:
    enum { Cap = 12, Budget = 512 * 1024 * 1024 };   // 张数上限 / 字节预算
    void put(const QString& path, const QImage& img)
    {
        if (img.isNull()) return;          // 解码失败的结果不进缓存
        QMutexLocker lk(&m_mtx);
        const auto old = m_hash.constFind(path);
        if (old != m_hash.constEnd()) m_bytes -= old.value().sizeInBytes();   // 同键覆盖先回退旧账
        m_hash.insert(path, img);          // insert：同键覆盖，条目数不增
        m_bytes += img.sizeInBytes();      // P4：字节记账（覆盖的旧账已在上面回退）
        trimLocked();
    }
    bool get(const QString& path, QImage* out) const
    {
        QMutexLocker lk(&m_mtx);
        const auto it = m_hash.constFind(path);
        if (it == m_hash.constEnd()) return false;
        *out = it.value();               // QImage 隐式共享，拷贝廉价
        return true;
    }
    bool contains(const QString& path) const
    {
        QMutexLocker lk(&m_mtx);
        return m_hash.contains(path);
    }
    int size() const
    {
        QMutexLocker lk(&m_mtx);
        return m_hash.size();
    }
private:
    // 调用方必须已持有 m_mtx。
    void trimLocked()
    {
        if (m_hash.size() > Cap) {         // 张数超限 → 按 key 字典序删到 Cap/2（确定性，便于测试）
            QStringList keys = m_hash.keys();
            keys.sort();
            const int drop = m_hash.size() - Cap / 2;
            for (int i = 0; i < drop; ++i) m_bytes -= m_hash.take(keys[i]).sizeInBytes();
        }
        // P4：字节超预算 → 逐个驱逐至预算内；保底留 1 张（单张超大不因预算被清零）。
        while (m_bytes > Budget && m_hash.size() > 1) {
            const auto it = m_hash.begin();
            m_bytes -= it.value().sizeInBytes();
            m_hash.erase(it);
        }
    }
    mutable QMutex m_mtx;                  // get/contains 是 const，锁必须 mutable
    QHash<QString, QImage> m_hash;
    qint64 m_bytes = 0;                    // 各条目 sizeInBytes 之和（逻辑字节）
};
