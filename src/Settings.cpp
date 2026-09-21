#include "src/Settings.h"
#include <QSettings>
QVariant Settings::value(const QString& k, const QVariant& d)
{ return QSettings("LightSee", "LightSee").value(k, d); }
void Settings::setValue(const QString& k, const QVariant& v)
{ QSettings("LightSee", "LightSee").setValue(k, v); }
