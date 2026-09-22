#pragma once
#include <QString>
#include <QStringList>

// Windows 图片文件关联：全部写 HKCU\Software\Classes（免管理员、幂等，可重复执行）。
// 注册后本软件进入右键"打开方式"列表；Win10 的 UserChoice 哈希禁止程序静默设为
// 双击默认，接管双击需用户在"打开方式"里勾一次"始终"（提示文案已说明）。非 QObject，无需 moc。
class FileAssoc
{
public:
    // 参与关联的常见照片格式（小写、不带点）。
    static QStringList photoExtensions();
    // 纯逻辑：拼 shell\open\command 值 → "<exe原生路径>" "%1"。
    static QString openCommand(const QString& exePath);
    // 写入 ProgID、各扩展名 OpenWithProgids、Applications\<exe名> 并刷新壳层。
    // 成功返回 true；失败返回 false 且（errorOut 非空时）写入错误说明。
    static bool registerPhotoAssociations(const QString& exePath, QString* errorOut = nullptr);
};
