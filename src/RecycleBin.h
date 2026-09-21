#pragma once
#include <QString>

// 把文件移入回收站（Windows Shell 操作，可撤销）。非 QObject，无需 moc。
class RecycleBin
{
public:
    // 成功返回 true。失败返回 false 且（errorOut 非空时）写入错误说明：
    // SHFileOperationW 的返回码字符串，或 "aborted"（用户/系统中止），或 "empty path"，
    // 或 "not an absolute path"，或 "network path has no recycle bin"（UNC/映射盘守卫，
    // 在进入 Shell 之前拒绝，避免不可逆的永久删除）。
    static bool moveToRecycleBin(const QString& path, QString* errorOut = nullptr);
};
