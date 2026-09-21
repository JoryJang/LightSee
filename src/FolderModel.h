#pragma once
#include <QStringList>
class FolderModel {
public:
    static QStringList supportedExtensions();
    // final-fix I1：目录 → 按目录序（QDir::Name）取第一个受支持扩展名的文件；无则返回空。
    static QString firstSupportedFile(const QString& dirPath);
    bool setPath(const QString& filePath);          // 载入所在目录并定位
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
