#include "src/Log.h"
#include "src/version.h"
#include <QCoreApplication>
#include <QDir>
#include <QtGlobal>
#include <vector>
#include <spdlog/sinks/rotating_file_sink.h>
#ifdef QT_DEBUG
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace {

// Qt 自身告警与 qDebug 桥接进 spdlog，统一落盘。
void qtMessageBridge(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    const std::string text = msg.toStdString();
    switch (type) {
    case QtDebugMsg:    L_DEBUG("[qt] {}", text); break;
    case QtInfoMsg:     L_INFO("[qt] {}", text); break;
    case QtWarningMsg:  L_WARN("[qt] {}", text); break;
    case QtCriticalMsg: L_ERROR("[qt] {}", text); break;
    case QtFatalMsg:    L_ERROR("[qt-fatal] {}", text); break;
    }
}

} // namespace

namespace Log {
void init()
{
    const QString logDir = QCoreApplication::applicationDirPath() + "/logs";
    QDir().mkpath(logDir);

    std::vector<spdlog::sink_ptr> sinks;
    // SPDLOG_WCHAR_FILENAMES（vcxproj 全局定义）：宽字符路径，兼容非 ASCII 用户目录。
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        QDir(logDir).filePath("lightsee.log").toStdWString(), 5 * 1024 * 1024, 3));
#ifdef QT_DEBUG
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#endif

    auto logger = std::make_shared<spdlog::logger>("lightsee", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
#ifdef QT_DEBUG
    logger->set_level(spdlog::level::debug);
#else
    logger->set_level(spdlog::level::info);
#endif
    logger->flush_on(spdlog::level::warn);
    if (spdlog::get("lightsee")) spdlog::drop("lightsee");
    spdlog::set_default_logger(logger);
    qInstallMessageHandler(qtMessageBridge);
#ifdef QT_DEBUG
    logger->info("==== LightSee {} 启动（Debug 构建）====", LIGHTSEE_VERSION_STRING);
#else
    logger->info("==== LightSee {} 启动（Release 构建）====", LIGHTSEE_VERSION_STRING);
#endif
}

void shutdown()
{
    if (auto lg = spdlog::get("lightsee")) lg->flush();
}

} // namespace Log
