#pragma once
#include <QObject>
#include <QSize>
class QListWidget;
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject* parent = nullptr);
    void populate(QListWidget* target, const QStringList& paths);  // 重建并异步填图
    void cancel();                                                  // 丢弃进行中的结果
private:
    quint64 m_gen = 0;
};
