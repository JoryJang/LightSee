#pragma once
#include <QVariant>
class Settings {
public:
    static QVariant value(const QString& key, const QVariant& def = QVariant());
    static void setValue(const QString& key, const QVariant& v);
};
