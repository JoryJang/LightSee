#include "src/Settings.h"
#include "src/Log.h"
#include <QSettings>
QVariant Settings::value(const QString& k, const QVariant& d)
{ return QSettings("LightSee", "LightSee").value(k, d); }
void Settings::setValue(const QString& k, const QVariant& v)
{
    // ByteArray（窗口几何）只记长度，避免刷屏与噪音。
    const QString repr = v.type() == QVariant::ByteArray
        ? QStringLiteral("<%1 bytes>").arg(v.toByteArray().size()) : v.toString();
    L_DEBUG("设置写入: {} = {}", k.toStdString(), repr.toStdString());
    QSettings("LightSee", "LightSee").setValue(k, v);
}
