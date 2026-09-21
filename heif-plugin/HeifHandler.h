#pragma once
#include <qimageiohandler.h>

class HeifHandler : public QImageIOHandler
{
public:
    explicit HeifHandler(QIODevice* device);
    bool canRead() const override;
    bool read(QImage* image) override;
    static bool sniff(QIODevice* device);   // Task 内共享：ftyp magic 检测
};
