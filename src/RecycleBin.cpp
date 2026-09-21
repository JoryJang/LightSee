// windows.h 与 Qt 头有宏冲突风险（min/max、interface、以及大量 GDI 名字），
// 因此：先定义 WIN32_LEAN_AND_MEAN + NOMINMAX 再包含 windows.h，Qt 头放在其后。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "src/RecycleBin.h"   // QString
#include <QDir>               // isAbsolutePath：守卫只接受绝对路径
#include <QVector>            // 堆上缓冲，长度按实际字符串分配

bool RecycleBin::moveToRecycleBin(const QString& path, QString* errorOut)
{
    if (path.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty path");
        return false;
    }

    // 网络路径没有回收站：UNC/映射盘上 FOF_ALLOWUNDO 会被 Shell 忽略，且
    // FOF_NOCONFIRMATION 使 Shell 静默接受"永久删除"→ 数据不可逆丢失，而 UI
    // 承诺的是回收站。故在进入 SHFileOperation 之前直接拒绝。
    if (!QDir::isAbsolutePath(path)) {
        if (errorOut) *errorOut = QStringLiteral("not an absolute path");
        return false;
    }
    const bool uncStyle = path.startsWith(QStringLiteral("\\\\"))    // \\server\share
                       || path.startsWith(QStringLiteral("//"));     // Qt 正斜杠形式 //server/share
    if (uncStyle) {
        if (errorOut) *errorOut = QStringLiteral("network path has no recycle bin");
        return false;
    }
    if (path.size() >= 2 && path.at(1) == QLatin1Char(':')) {        // 盘符形式 X:\ 或 X:/
        // 由盘符构造规范根目录 "X:\"（避免 "C:" 这种被解释成该盘相对路径），
        // GetDriveTypeW 只做本地/映射判断，对坏盘符立即返回。
        const WCHAR root[4] = { static_cast<WCHAR>(path.at(0).toLatin1()), L':', L'\\', L'\0' };
        const UINT driveType = GetDriveTypeW(root);
        if (driveType == DRIVE_REMOTE || driveType == DRIVE_NO_ROOT_DIR) {
            if (errorOut) *errorOut = QStringLiteral("network path has no recycle bin");
            return false;
        }
    }

    // brief 用的是 `WCHAR from[MAX_PATH + 2]` + toWCharArray：长路径（>MAX_PATH，
    // 本仓库 third_party 构建目录里很容易超过）会直接写越界，且不零初始化。
    // 这里改为按实际长度分配的堆缓冲，并显式补两个终止符（pFrom 必须双 null 结尾）。
    QVector<WCHAR> buf(path.size() + 2, 0);
    const int n = path.toWCharArray(buf.data());
    buf[n] = L'\0';
    buf[n + 1] = L'\0';

    SHFILEOPSTRUCTW op = {};
    op.hwnd = nullptr;                      // 无父窗口：配合 NOERRORUI/SILENT 不弹自定义 UI
    op.wFunc = FO_DELETE;
    op.pFrom = buf.constData();
    op.pTo = nullptr;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    const int rc = SHFileOperationW(&op);
    if (rc != 0) {
        if (errorOut) *errorOut = QString::number(rc);
        return false;
    }
    // rc==0 但操作被中止的情况（例如回收站策略拒绝）：视为失败，交由调用方提示。
    if (op.fAnyOperationsAborted != FALSE) {
        if (errorOut) *errorOut = QStringLiteral("aborted");
        return false;
    }
    return true;
}
