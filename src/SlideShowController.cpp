#include "src/SlideShowController.h"
#include <QTimer>
SlideShowController::SlideShowController(QObject* parent)
    : QObject(parent), m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &SlideShowController::tick);
}
void SlideShowController::start(int ms) { m_timer->start(ms); }
void SlideShowController::stop() { m_timer->stop(); }
bool SlideShowController::isRunning() const { return m_timer->isActive(); }
