#include "HeifHandler.h"
#include <libheif/heif.h>
#include <QBuffer>
#include <QImage>
#include <QVariant>

static const char* kBrands[] = { "heic","heix","heim","heis","hev1","hvc1","mif1","msf1" };

HeifHandler::HeifHandler(QIODevice* device) : QImageIOHandler() { setDevice(device); }

bool HeifHandler::sniff(QIODevice* device)
{
    const qint64 pos = device->pos();
    char hdr[12] = {};
    const qint64 n = device->read(hdr, 12);
    device->seek(pos);
    if (n < 12 || memcmp(hdr + 4, "ftyp", 4) != 0)
        return false;
    for (const char* b : kBrands)
        if (memcmp(hdr + 8, b, 4) == 0) return true;
    return false;
}

bool HeifHandler::canRead() const
{
    return HeifHandler::sniff(device());
}

bool HeifHandler::read(QImage* image)
{
    const QByteArray data = device()->readAll();
    heif_context* ctx = heif_context_alloc();
    if (heif_context_read_from_memory(ctx, data.constData(), data.size(), nullptr).code != heif_error_Ok) {
        heif_context_free(ctx); return false;
    }
    heif_image_handle* handle = nullptr;
    if (heif_context_get_primary_image_handle(ctx, &handle).code != heif_error_Ok) {
        heif_context_free(ctx); return false;
    }
    const bool hasAlpha = heif_image_handle_has_alpha_channel(handle);
    heif_decoding_options* opt = heif_decoding_options_alloc();
    opt->convert_hdr_to_8bit = true;           // 10-bit HEIC → 8bit
    heif_image* img = nullptr;
    const heif_error e = heif_decode_image(handle, &img, heif_colorspace_RGB,
        hasAlpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB, opt);
    heif_decoding_options_free(opt);
    if (e.code != heif_error_Ok) { heif_image_handle_release(handle); heif_context_free(ctx); return false; }

    int w = heif_image_get_width(img, heif_channel_interleaved);
    int h = heif_image_get_height(img, heif_channel_interleaved);
    int stride = 0;
    const uint8_t* px = static_cast<const uint8_t*>(
        heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride));
    const QImage::Format fmt = hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    QImage out = px ? QImage(px, w, h, stride, fmt).copy() : QImage();

    heif_image_release(img);
    heif_image_handle_release(handle);
    heif_context_free(ctx);
    if (out.isNull()) return false;
    *image = out;
    setOption(Size, QVariant::fromValue(out.size()));
    return true;
}
