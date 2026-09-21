#include "src/FolderModel.h"
#include "src/Log.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>

QStringList FolderModel::supportedExtensions()
{ return { "png","jpg","jpeg","bmp","gif","webp","tif","tiff","ico","svg",
           "heic","heif","hif" }; }

// final-fix I1：拖入/打开目录时解析出首个受支持图片（目录序 QDir::Name），
// 与 setPath 相同的一次性物化 QStringList 写法（避免双临时量 range 崩溃，见 setPath 注释）。
QString FolderModel::firstSupportedFile(const QString& dirPath)
{
    const QStringList extList = supportedExtensions();
    const QSet<QString> exts(extList.begin(), extList.end());
    QDir dir(dirPath, "*", QDir::Name, QDir::Files);
    const QFileInfoList list = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo& e : list)
        if (exts.contains(e.suffix().toLower()))
            return e.absoluteFilePath();
    L_DEBUG("目录内无受支持图片: {}", dirPath.toStdString());
    return QString();
}

bool FolderModel::setPath(const QString& filePath)
{
    const QFileInfo fi(filePath);
    // FIX vs brief: the brief wrote `QSet<QString> exts(supportedExtensions().begin(),
    // supportedExtensions().end())`. Those are TWO separate returned temporaries, so
    // begin()/end() come from different containers -> invalid range -> access violation
    // in Qt5Core (observed crashing --selftest). Materialize the list once first.
    const QStringList extList = supportedExtensions();
    const QSet<QString> exts(extList.begin(), extList.end());
    QDir dir(fi.absolutePath(), "*", QDir::Name, QDir::Files);
    m_files.clear();
    const QFileInfoList list = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo& e : list)
        if (exts.contains(e.suffix().toLower()))
            m_files << e.absoluteFilePath();
    m_index = m_files.indexOf(fi.absoluteFilePath());
    if (m_index < 0)
        L_DEBUG("setPath 未命中: {} 不在目录可打开列表", filePath.toStdString());
    else
        L_DEBUG("目录就绪: {} 共 {} 个受支持文件，当前第 {} 个", fi.absolutePath().toStdString(), m_files.size(), m_index + 1);
    return m_index >= 0;
}

QString FolderModel::current() const
{ return m_index >= 0 && m_index < m_files.size() ? m_files[m_index] : QString(); }

QString FolderModel::next()
{ if (m_files.isEmpty()) return QString(); m_index = (m_index + 1) % m_files.size(); return current(); }

QString FolderModel::prev()
{ if (m_files.isEmpty()) return QString(); m_index = (m_index - 1 + m_files.size()) % m_files.size(); return current(); }

void FolderModel::removeFile(const QString& filePath)
{
    const int i = m_files.indexOf(filePath);
    if (i < 0) { L_WARN("removeFile 未找到条目: {}", filePath.toStdString()); return; }
    m_files.removeAt(i);
    if (i < m_index) --m_index;
    else if (i == m_index) m_index = m_files.isEmpty() ? -1 : qMin(i, m_files.size() - 1);
    L_DEBUG("模型移除后剩余 {} 项，当前索引 {}", m_files.size(), m_index);
}
