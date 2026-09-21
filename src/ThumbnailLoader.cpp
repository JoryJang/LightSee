#include "src/ThumbnailLoader.h"
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
    return img.isNull() ? QPixmap() : QPixmap::fromImage(img);
}

ThumbnailLoader::ThumbnailLoader(QObject* parent) : QObject(parent) {}

void ThumbnailLoader::cancel() { ++m_gen; }

void ThumbnailLoader::populate(QListWidget* target, const QStringList& paths)
{
    cancel();
    const quint64 gen = m_gen;
    target->clear();
    int row = 0;
    for (const QString& p : paths) {
        auto* item = new QListWidgetItem(QFileInfo(p).fileName(), target);
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
