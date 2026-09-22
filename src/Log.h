#pragma once
// spdlog 统一日志封装：main() 开头调用 Log::init()，任意位置用 L_DEBUG/L_INFO/
// L_WARN/L_ERROR（线程安全，QString 参数先 .toStdString() 转 UTF-8）。
// 落盘：<exe目录>/logs/lightsee_YYYY-MM-DD.log（每天一个文件，零点轮转，UTF-8，同日重启追加）；
// Debug 构建附加控制台；
// qDebug/qWarning 经 qInstallMessageHandler 桥接进同一文件。
#include <spdlog/spdlog.h>

namespace Log {
void init();      // 必须在 QApplication 构造之后（依赖 applicationDirPath）
void shutdown();  // 退出前冲刷落盘
}

#define L_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define L_INFO(...)  SPDLOG_INFO(__VA_ARGS__)
#define L_WARN(...)  SPDLOG_WARN(__VA_ARGS__)
#define L_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
