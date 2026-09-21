#pragma once
#include <QObject>
class QTimer;
class SlideShowController : public QObject {
    Q_OBJECT
public:
    explicit SlideShowController(QObject* parent = nullptr);
    void start(int intervalMs);
    void stop();
    bool isRunning() const;
signals:
    void tick();
private:
    QTimer* m_timer;
};
