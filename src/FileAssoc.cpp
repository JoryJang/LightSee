// windows.h 与 Qt 头有宏冲突风险，包含顺序规约见 RecycleBin.cpp 同款注释：
// 先 WIN32_LEAN_AND_MEAN + NOMINMAX 包含 windows/shellapi，再包含 Qt 头。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>   // SHChangeNotify / SHCNE_ASSOCCHANGED / SHCNF_IDLIST（本 SDK 位于 shlobj.h）

#include "src/FileAssoc.h"
#include <QDir>
#include <QFileInfo>

// ProgID 单一来源：扩展名 OpenWithProgids 与 Applications SupportedTypes 两处共用。
static const wchar_t* kProgId = L"LightSee.Image";
static const wchar_t* kDisplayName = L"LightSee 看图";

QStringList FileAssoc::photoExtensions()
{
    return { "png", "jpg", "jpeg", "bmp", "gif", "webp", "heic" };
}

QString FileAssoc::openCommand(const QString& exePath)
{
    return QStringLiteral("\"%1\" \"%2\"")
        .arg(QDir::toNativeSeparators(exePath), QStringLiteral("%1"));
}

// 写一个键（默认值 valueName 传空串）。键不存在自动创建，已存在直接覆盖 → 幂等。
static bool writeValue(const QString& keyPath, const QString& valueName, const QString& data, QString* errorOut)
{
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER,
                              reinterpret_cast<const wchar_t*>(keyPath.utf16()), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (rc == ERROR_SUCCESS) {
        rc = RegSetValueExW(hKey,
                            valueName.isEmpty() ? nullptr
                                                : reinterpret_cast<const wchar_t*>(valueName.utf16()),
                            0, REG_SZ,
                            reinterpret_cast<const BYTE*>(data.utf16()),
                            DWORD((data.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    if (rc != ERROR_SUCCESS && errorOut)
        *errorOut = keyPath + QStringLiteral(": rc=") + QString::number(quint32(rc));
    return rc == ERROR_SUCCESS;
}

static bool writeDefault(const QString& keyPath, const QString& data, QString* err)
{
    return writeValue(keyPath, QString(), data, err);
}

bool FileAssoc::registerPhotoAssociations(const QString& exePath, QString* errorOut)
{
    const QString exe = QDir::toNativeSeparators(exePath);
    const QString cmd = openCommand(exe);
    const QString exeName = QFileInfo(exe).fileName();

    // 改名遗留清理：Applications 键以 exe 文件名为键，旧名 QtWidgetsLight.exe 的条目
    // 会留在"打开方式"里指向已不存在的程序。删失败不影响本次注册，忽略返回值。
    ::RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\QtWidgetsLight.exe");

    // 1) ProgID：显示名 + 图标 + 打开命令（图标是 app.rc 嵌在 exe 里的资源，索引 0）。
    const QString progRoot = QStringLiteral("Software\\Classes\\LightSee.Image");
    if (!writeDefault(progRoot, QString::fromWCharArray(kDisplayName), errorOut)) return false;
    if (!writeDefault(progRoot + QStringLiteral("\\DefaultIcon"), exe + QStringLiteral(",0"), errorOut)) return false;
    if (!writeDefault(progRoot + QStringLiteral("\\shell\\open\\command"), cmd, errorOut)) return false;

    // 2) 各扩展名只追加 OpenWithProgids 条目，不写 .ext 默认值 → 不抢占现有双击行为。
    for (const QString& ext : photoExtensions()) {
        const QString key = QStringLiteral("Software\\Classes\\.%1\\OpenWithProgids").arg(ext);
        if (!writeValue(key, QString::fromWCharArray(kProgId), QString(), errorOut)) return false;
    }

    // 3) Applications 登记：让"设置 > 默认应用"列表里能直接选到本软件。
    const QString appRoot = QStringLiteral("Software\\Classes\\Applications\\") + exeName;
    if (!writeDefault(appRoot, QString::fromWCharArray(kDisplayName), errorOut)) return false;
    if (!writeDefault(appRoot + QStringLiteral("\\DefaultIcon"), exe + QStringLiteral(",0"), errorOut)) return false;
    if (!writeDefault(appRoot + QStringLiteral("\\shell\\open\\command"), cmd, errorOut)) return false;
    for (const QString& ext : photoExtensions()) {
        if (!writeValue(appRoot + QStringLiteral("\\SupportedTypes"),
                        QStringLiteral(".%1").arg(ext), QString(), errorOut)) return false;
    }

    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}
