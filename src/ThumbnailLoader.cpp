#include "src/ThumbnailLoader.h"
#include "src/ImageLimits.h"
#include "src/Log.h"
#include <QListWidget>
#include <QImageReader>
#include <QFileInfo>
#include <QIcon>
#include <QtConcurrentRun>
#include <QFutureWatcher>

static QPixmap renderThumb(const QString& path)
{
    QImageReader r(path);
    r.setAutoTransform(true);
    const QSize s = r.size();
    if (exceedsDecodeLimits(s)) {              // R1：解压炸弹/病态大图不进缩略图解码
        L_DEBUG("缩略图尺寸超限，跳过: {}", path.toStdString());
        return QPixmap();
    }
    if (s.isValid()) {
        const QSize t(150, 110);
        r.setScaledSize(s.width() * t.height() < s.height() * t.width()
                        ? QSize(s.width() * t.height() / s.height(), t.height())
                        : QSize(t.width(), s.height() * t.width() / s.width()));
    }
    const QImage img = r.read();
    if (img.isNull())
        L_DEBUG("缩略图解码失败: {}（{}）", path.toStdString(), r.errorString().toStdString());
    return img.isNull() ? QPixmap() : QPixmap::fromImage(img);
}

ThumbnailLoader::ThumbnailLoader(QObject* parent) : QObject(parent)
{
    // P1：缩略图专用小池。此前与主图解码共用 QtConcurrent 全局池：大目录一次入队
    // 上百个任务，当前图的交互解码被排到队尾，上屏延迟随目录大小线性增长。
    m_pool.setMaxThreadCount(2);
}

ThumbnailLoader::~ThumbnailLoader()
{
    cancel();               // 代数作废 + 清空排队任务
    m_pool.waitForDone();   // 至多等 2 张在途解码收尾，避免析构长时间阻塞
}

void ThumbnailLoader::cancel()
{
    ++m_gen;          // 在途结果按代数丢弃
    m_pool.clear();   // 排队未启动的任务直接删除——旧实现只丢结果不丢任务，白烧 CPU
}

void ThumbnailLoader::populate(QListWidget* target, const QStringList& paths)
{
    cancel();
    m_target = target;
    L_DEBUG("缩略图面板刷新: {} 项（缓存 {} 张/{}KB）",
            paths.size(), m_cache.size(), m_cacheBytes / 1024);
    // P3：换目录 → 旧缓存整批失效清空。
    const QString newDir = paths.isEmpty() ? QString() : QFileInfo(paths.first()).absolutePath();
    if (newDir != m_cacheDir) {
        m_cache.clear();
        m_cacheBytes = 0;
        m_cacheDir = newDir;
    }
    target->clear();
    const quint64 gen = m_gen;
    int row = 0;
    for (const QString& p : paths) {
        // 底部横排只显示缩略图不带标题；文件名放 tooltip，悬停可见。
        // 图标异步加载：创建时无 icon/text，delegate sizeHint 会退化成极小格子且事后不重算，
        // 必须显式定格子（icon 150x110 + 少量 padding，水平留白更小以贴近相邻）。
        auto* item = new QListWidgetItem(target);
        item->setSizeHint(QSize(152, 118));
        item->setToolTip(QFileInfo(p).fileName());
        item->setData(Qt::UserRole, p);
        const auto hit = m_cache.constFind(p);
        if (hit != m_cache.constEnd()) {
            item->setIcon(QIcon(hit.value()));   // P3：缓存命中同步上图，不入队解码
        } else {
            auto* w = new QFutureWatcher<QPixmap>(target);
            // context = this（loader）：loader 析构后 lambda 不再触发，捕获 this 不悬垂
            //（旧实现的 context 是 target，安全性依赖两者恰好同窗析构的巧合）。
            QObject::connect(w, &QFutureWatcher<QPixmap>::finished, this, [this, w, gen, row, p]() {
                const QPixmap pm = w->result();
                w->deleteLater();
                onDecoded(gen, row, p, pm);
            });
            w->setFuture(QtConcurrent::run(&m_pool, &renderThumb, p));   // P1：投递专用池
        }
        ++row;
    }
}

// P3：删除单张（onDelete 用）——只移除对应条目并驱逐缓存，不触发整面板重建。
void ThumbnailLoader::removeOne(const QString& path)
{
    m_cache.remove(path);
    if (!m_target) return;
    for (int i = 0; i < m_target->count(); ++i) {
        if (m_target->item(i)->data(Qt::UserRole).toString() == path) {
            delete m_target->item(i);   // QListWidgetItem 析构时把自己从面板摘除
            return;
        }
    }
}

void ThumbnailLoader::onDecoded(quint64 gen, int row, const QString& path, const QPixmap& pm)
{
    if (pm.isNull()) return;
    // 解码成果无条件入缓存——即使代数已过期（面板重建过），下次 populate 仍可命中。
    if (!m_cache.contains(path))
        m_cacheBytes += qint64(pm.width()) * pm.height() * 4;   // ARGB32 估算
    m_cache.insert(path, pm);
    evictOverBudget();
    if (gen != m_gen || !m_target) return;
    QListWidgetItem* it = m_target->item(row);
    // removeOne 之后行号会漂移：行号与路径对不上时按 path 兜底线性查找（罕见路径）。
    if (!it || it->data(Qt::UserRole).toString() != path) {
        it = nullptr;
        for (int i = 0; i < m_target->count(); ++i)
            if (m_target->item(i)->data(Qt::UserRole).toString() == path) { it = m_target->item(i); break; }
    }
    if (it) it->setIcon(QIcon(pm));
}

// QHash 无序，按任意序驱逐即可；被驱逐的缩略图下次 populate 会重新入队解码。
void ThumbnailLoader::evictOverBudget()
{
    while (m_cacheBytes > kCacheBudget && !m_cache.isEmpty()) {
        const auto it = m_cache.begin();
        m_cacheBytes -= qint64(it.value().width()) * it.value().height() * 4;
        m_cache.erase(it);
    }
}
