#include "src/SlideShowController.h"
#include "src/Log.h"
#include <QTimer>
SlideShowController::SlideShowController(QObject* parent)
    : QObject(parent), m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &SlideShowController::tick);
}
void SlideShowController::start(int ms) { L_DEBUG("幻灯片计时启动/重启: {}ms", ms); m_timer->start(ms); }
void SlideShowController::stop() { if (m_timer->isActive()) L_DEBUG("幻灯片计时停止"); m_timer->stop(); }
bool SlideShowController::isRunning() const { return m_timer->isActive(); }
