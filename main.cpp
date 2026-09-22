#include <QtWidgets/QApplication>
#include <QFileInfo>
#include <QIcon>
#include "src/MainWindow.h"
#include "src/SelfTest.h"
#include "src/Log.h"
#include "src/version.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral(LIGHTSEE_VERSION_STRING));
    // exe 图标由 app.rc 提供；此处覆盖标题栏/任务栏/Alt-Tab 的窗口图标。
    app.setWindowIcon(QIcon(":/lightsee.ico"));
    Log::init();
    QStringList args = app.arguments().mid(1);
    L_INFO("启动参数: [{}]", args.join(QLatin1Char(' ')).toStdString());
    if (args.contains("--selftest")) {
        const int rc = SelfTest::run();
        Log::shutdown();
        return rc;
    }
    MainWindow w;
    w.resize(1100, 700);
    w.show();
    // final-fix I1+I2：CLI 参数（图片/文件夹，甚至不存在的路径）统一走 openFile，
    // 共享目录解析与"无法打开"提示逻辑；此处不再自建 exists() 分支。
    for (const QString& a : args) {
        if (!a.startsWith('-')) { w.openFile(a); break; }
    }
    const int rc = app.exec();
    L_INFO("事件循环退出，code={}", rc);
    Log::shutdown();
    return rc;
}
