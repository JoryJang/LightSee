#pragma once
#include <QSize>
#include <QtGlobal>

// R1：解码前的头部尺寸守卫（Qt 5.14 无 QImageReader::setAllocationLimit，Qt 6 才有）。
// 恶意/损坏文件可声称超大尺寸（如 30000x30000 的 PNG 解码为 ARGB32 需 ~3.4GB），
// 直接 read() 会把进程 OOM/假死。主图（MainWindow::decodeImage）与缩略图
//（ThumbnailLoader::renderThumb）在读头拿到尺寸后先过本守卫，超限按解码失败处理。
// 阈值取宽松档位，只拦病态尺寸、不误伤真实照片：
//   - 单边 > 32767：QImage 多数路径的安全上界（含 40000x100 这类细长拼接条）；
//   - 像素 > 2^28（≈268MP，ARGB32 约 1GB）：覆盖在售 1.5 亿像素级中画幅与常规拼接
//     全景，拦下解压炸弹。需调整时改这里两个常量即可。
inline bool exceedsDecodeLimits(const QSize& s)
{
    if (!s.isValid()) return false;   // 头部读不出尺寸（部分 SVG 等）：交给 read() 自行判定
    constexpr int kMaxSide = 32767;
    constexpr qint64 kMaxPixels = qint64(1) << 28;
    return s.width() > kMaxSide || s.height() > kMaxSide
        || qint64(s.width()) * s.height() > kMaxPixels;
}
