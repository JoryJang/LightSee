#include <QtWidgets/QApplication>
#include <QFileInfo>
#include "src/MainWindow.h"
#include "src/SelfTest.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    if (args.contains("--selftest")) return SelfTest::run();
    MainWindow w;
    w.resize(1100, 700);
    w.show();
    // final-fix I1+I2：CLI 参数（图片/文件夹，甚至不存在的路径）统一走 openFile，
    // 共享目录解析与"无法打开"提示逻辑；此处不再自建 exists() 分支。
    for (const QString& a : args) {
        if (!a.startsWith('-')) { w.openFile(a); break; }
    }
    return app.exec();
}
