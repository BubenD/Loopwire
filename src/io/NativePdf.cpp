#include "io/NativePdf.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <vector>
#ifdef _WIN32
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace Loopwire {
PdfRaster renderNativePdf(const QString& path, int pageIndex, int pixelWidth) {
    PdfRaster result;
#ifdef _WIN32
    try {
        struct Apartment {
            Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
            ~Apartment() { winrt::uninit_apartment(); }
        } apartment;
        const auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
            QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString()).get();
        const auto document = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
        result.count = static_cast<int>(document.PageCount());
        if (!result.count) { result.error = QStringLiteral("The PDF has no pages."); return result; }
        result.page = std::clamp(pageIndex, 0, result.count - 1);
        const auto page = document.GetPage(static_cast<uint32_t>(result.page));
        const auto size = page.Size();
        result.pageSize = QSizeF(size.Width, size.Height);
        if (size.Width <= 0 || size.Height <= 0) { result.error = QStringLiteral("Invalid PDF page size."); return result; }
        double width = std::clamp(pixelWidth, 64, 4096);
        double height = width * size.Height / size.Width;
        if (height > 8192) { width *= 8192 / height; height = 8192; }
        winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
        options.DestinationWidth(static_cast<uint32_t>(std::max(1.0, width)));
        options.DestinationHeight(static_cast<uint32_t>(std::max(1.0, height)));
        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        page.RenderToStreamAsync(stream, options).get();
        const uint32_t length = static_cast<uint32_t>(stream.Size());
        winrt::Windows::Storage::Streams::DataReader reader(stream.GetInputStreamAt(0));
        reader.LoadAsync(length).get();
        std::vector<uint8_t> bytes(length);
        reader.ReadBytes(winrt::array_view<uint8_t>(bytes));
        result.image = QImage::fromData(bytes.data(), static_cast<int>(bytes.size()));
        if (result.image.isNull()) result.error = QStringLiteral("The PDF page could not be rendered.");
    } catch (const winrt::hresult_error& e) {
        result.error = QStringLiteral("Cannot display the PDF: %1").arg(QString::fromWCharArray(e.message().c_str()));
    } catch (const std::exception& e) {
        result.error = QString::fromUtf8(e.what());
    }
#else
    Q_UNUSED(path); Q_UNUSED(pageIndex); Q_UNUSED(pixelWidth);
    result.error = QStringLiteral("Native PDF viewing requires Windows.");
#endif
    return result;
}
}
