#include <QImageIOPlugin>
#include "HeifHandler.h"

class HeifPlugin : public QImageIOPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface" FILE "heif.json")
public:
    Capabilities capabilities(QIODevice* device, const QByteArray& format) const override
    {
        Q_UNUSED(format);
        return (device && HeifHandler::sniff(device)) ? CanRead : Capabilities();
    }
    QImageIOHandler* create(QIODevice* device, const QByteArray& format) const override
    {
        Q_UNUSED(format);
        return new HeifHandler(device);
    }
};

#include "main.moc"
