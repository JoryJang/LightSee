#pragma once
#include <QByteArray>
#include <QSize>
#include <QtGlobal>

// R1：解码前的头部尺寸守卫（Qt 5.14 无 QImageReader::setAllocationLimit，Qt 6 才有）。
// 恶意/损坏文件可声称超大尺寸（如 30000x30000 的 PNG 解码为 ARGB32 需 ~3.4GB），
// 直接 read() 会把进程 OOM/假死。主图（MainWindow::decodeImage）与缩略图
//（ThumbnailLoader::renderThumb）在读头拿到尺寸后先过本守卫。
//   - 单边 > 32767：QImage 多数路径的安全上界，任何格式都拒（含 40000x100 细长条）；
//   - 像素 > 2^28（≈268MP，ARGB32 约 1GB）：拦下解压炸弹；JPEG 例外，见下方降采样。

// 像素超限时，主图按此预算降采样解码（40MP ≈ ARGB32 160MB）：远超 2K 屏 100% 显示
// 所需，又能把 4~5 亿像素的拼接地图/全景图从"打不开"变成"看得清"。
constexpr qint64 kDisplayBudget = qint64(40) * 1024 * 1024;

inline qint64 pixelsOf(const QSize& s) { return qint64(s.width()) * s.height(); }

// 单边上限：与格式无关，超限连降采样都危险（插件内部按原图宽度算行缓冲）。
inline bool exceedsSideLimit(const QSize& s)
{
    if (!s.isValid()) return false;   // 头部读不出尺寸（部分 SVG 等）：交给 read() 自行判定
    constexpr int kMaxSide = 32767;
    return s.width() > kMaxSide || s.height() > kMaxSide;
}

// 像素上限：超过则整图解码的内存量级失控。
inline bool exceedsPixelLimit(const QSize& s)
{
    if (!s.isValid()) return false;
    constexpr qint64 kMaxPixels = qint64(1) << 28;
    return pixelsOf(s) > kMaxPixels;
}

inline bool exceedsDecodeLimits(const QSize& s)
{
    return exceedsSideLimit(s) || exceedsPixelLimit(s);
}

// 只有 JPEG 插件（Qt 5.14 qjpeghandler）会把 setScaledSize 换算成 libjpeg 的 M/8
// 缩放解码——降采样发生在解码阶段，峰值内存等于缩放后尺寸。PNG/TIFF 等是整图解码
// 后再缩放，同样的请求救不了内存，超限时仍须硬拒。
inline bool isJpegFormat(const QByteArray& fmt)
{
    return fmt.compare("jpeg", Qt::CaseInsensitive) == 0
        || fmt.compare("jpg", Qt::CaseInsensitive) == 0;
}

// 超限 JPEG 的降采样目标尺寸；无需（或不能）降采样返回无效尺寸，调用方据此走原路径/判失败。
// 取 1/2、1/4、1/8 中第一个落进预算的整数档——libjpeg 只在这些档位上零额外开销。
// 单边已被 exceedsSideLimit 限到 32767（≤1.07GP），故 1/8 档必然能落进预算，无需更细的缩放。
inline QSize jpegDownscaleTarget(const QSize& s, bool jpeg, qint64 budget = kDisplayBudget)
{
    if (!jpeg || !s.isValid() || exceedsSideLimit(s) || !exceedsPixelLimit(s)) return QSize();
    for (int d = 2; ; d *= 2) {
        const QSize t(s.width() / d, s.height() / d);
        if (pixelsOf(t) <= budget || d == 8) return t;
    }
}
