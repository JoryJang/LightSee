#pragma once
#include <QStringList>
class FolderModel {
public:
    static QStringList supportedExtensions();
    // final-fix I1：目录 → 按目录序（QDir::Name）取第一个受支持扩展名的文件；无则返回空。
    static QString firstSupportedFile(const QString& dirPath);
    bool setPath(const QString& filePath);          // 载入所在目录并定位
    // P2：同目录内定位到指定文件（缩略图点击用）——只移索引，不重扫目录。
    // 文件不在当前列表（或从未 setPath）返回 false，由调用方决定是否走 setPath 兜底。
    bool setCurrentFile(const QString& filePath);
    QStringList files() const { return m_files; }
    int index() const { return m_index; }
    QString current() const;
    QString next();
    QString prev();
    void removeFile(const QString& filePath);
private:
    QStringList m_files;
    int m_index = -1;
};
