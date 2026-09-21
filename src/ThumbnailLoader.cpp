#include "src/ThumbnailLoader.h"
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

ThumbnailLoader::ThumbnailLoader(QObject* parent) : QObject(parent) {}

void ThumbnailLoader::cancel() { ++m_gen; }

void ThumbnailLoader::populate(QListWidget* target, const QStringList& paths)
{
    cancel();
    L_DEBUG("缩略图面板刷新: {} 项", paths.size());
    const quint64 gen = m_gen;
    target->clear();
    int row = 0;
    for (const QString& p : paths) {
        // 底部横排只显示缩略图不带标题；文件名放 tooltip，悬停可见。
        // 图标异步加载：创建时无 icon/text，delegate sizeHint 会退化成极小格子且事后不重算，
        // 必须显式定格子（icon 150x110 + 四周 padding）。
        auto* item = new QListWidgetItem(target);
        item->setSizeHint(QSize(158, 118));
        item->setToolTip(QFileInfo(p).fileName());
        item->setData(Qt::UserRole, p);
        auto* w = new QFutureWatcher<QPixmap>(target);
        QObject::connect(w, &QFutureWatcher<QPixmap>::finished, target, [target, w, gen, this, row]() {
            const QPixmap pm = w->result();
            w->deleteLater();
            if (gen != m_gen || !target->item(row)) return;
            if (!pm.isNull()) target->item(row)->setIcon(QIcon(pm));
        });
        w->setFuture(QtConcurrent::run(&renderThumb, p));
        ++row;
    }
}
