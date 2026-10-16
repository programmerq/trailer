// Known bugs in the image document. Each test asserts the correct behaviour;
// QEXPECT_FAIL marks the check that fails today.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/ImageAdapter.h"

#include <QBuffer>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <cstdlib> // defines __GLIBC__ when the C library is glibc
#include <optional>
#include <vector>

#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
#include <malloc.h>
#define KNOWN_BUGS_HAVE_MALLINFO2 1
#endif

using namespace trailer;

namespace {

Annotation redRectangle(const QRectF &bounds) {
    Annotation a;
    a.page = 0;
    a.type = AnnotationType::Rectangle;
    a.bounds = bounds;
    a.style.stroke = QColor(255, 0, 0);
    a.style.strokeWidth = 3.0;
    return a;
}

bool isRed(const QImage &img, QPoint p) {
    if (!img.valid(p))
        return false;
    const QColor c = img.pixelColor(p);
    return c.red() > 180 && c.green() < 80 && c.blue() < 80;
}

// Whether the view shows red at image pixel `p`. The grab of the image label
// holds the raster and, as a child, the overlay with the live marks; at 100 %
// one image pixel is one logical pixel of the label.
bool shownRedAt(QWidget *label, QPoint p) {
    QCoreApplication::processEvents();
    const QImage grab = label->grab().toImage();
    const qreal dpr = grab.devicePixelRatio();
    return isRed(grab,
                 QPoint(qRound((p.x() + 0.5) * dpr - 0.5), qRound((p.y() + 0.5) * dpr - 0.5)));
}

#ifdef KNOWN_BUGS_HAVE_MALLINFO2
// Bytes in use on the C heap: what glibc has allocated in every arena, plus
// its mmapped blocks, where large images usually live.
qint64 heapBytesInUse() {
    const struct mallinfo2 info = mallinfo2();
    return static_cast<qint64>(info.uordblks + info.hblkhd);
}

// A digest of an image's size and pixels, so a test can recognise an earlier
// state without holding a copy of it.
QByteArray pixelDigest(const QImage &img) {
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArray::number(img.width()) + 'x' + QByteArray::number(img.height()));
    hash.addData(
        QByteArrayView(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes()));
    return hash.result();
}
#endif

QByteArray readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// --- JPEG fixture and inspection -------------------------------------------

quint32 byteAt(const QByteArray &b, qsizetype i) {
    return (i >= 0 && i < b.size()) ? static_cast<quint8>(b[i]) : 0u;
}

struct Segment {
    quint32 marker;
    QByteArray payload;
};

// The marker segments ahead of the first scan, in file order.
std::vector<Segment> jpegSegments(const QByteArray &jpeg) {
    std::vector<Segment> out;
    if (byteAt(jpeg, 0) != 0xFF || byteAt(jpeg, 1) != 0xD8)
        return out;
    qsizetype i = 2;
    while (i + 4 <= jpeg.size() && byteAt(jpeg, i) == 0xFF) {
        const quint32 marker = byteAt(jpeg, i + 1);
        if (marker == 0xFF) { // fill byte
            ++i;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) // start of scan, end of image
            break;
        const qsizetype length =
            static_cast<qsizetype>(byteAt(jpeg, i + 2) << 8 | byteAt(jpeg, i + 3));
        if (length < 2 || i + 2 + length > jpeg.size())
            break;
        out.push_back({marker, jpeg.mid(i + 4, length - 2)});
        i += 2 + length;
    }
    return out;
}

// Sum of the 64 steps in the quantisation table the first (luma) component
// uses; -1 when it can't be found. Lower means higher quality.
int lumaQuantisationSum(const QByteArray &jpeg) {
    const std::vector<Segment> segments = jpegSegments(jpeg);
    int lumaTable = -1;
    for (const Segment &s : segments) {
        if (s.marker >= 0xC0 && s.marker <= 0xC2 && s.payload.size() >= 9)
            lumaTable = static_cast<int>(byteAt(s.payload, 8)); // Tq of component 1
    }
    for (const Segment &s : segments) {
        if (s.marker != 0xDB)
            continue;
        qsizetype p = 0;
        while (p < s.payload.size()) {
            const quint32 precision = byteAt(s.payload, p) >> 4;
            const int table = static_cast<int>(byteAt(s.payload, p) & 0x0F);
            const qsizetype width = precision ? 2 : 1;
            if (p + 1 + 64 * width > s.payload.size())
                return -1;
            if (table == lumaTable) {
                int sum = 0;
                for (qsizetype k = 0; k < 64; ++k) {
                    const qsizetype at = p + 1 + k * width;
                    sum += static_cast<int>(precision ? byteAt(s.payload, at) << 8 |
                                                            byteAt(s.payload, at + 1)
                                                      : byteAt(s.payload, at));
                }
                return sum;
            }
            p += 1 + 64 * width;
        }
    }
    return -1;
}

struct ExifTags {
    QByteArray make;
    QByteArray model;
    QByteArray dateTimeOriginal;
    int orientation = 0; // 0 when absent
};

// Make, Model, Orientation and DateTimeOriginal from the Exif APP1 segment,
// in either TIFF byte order. Empty when the file has no Exif segment.
std::optional<ExifTags> readExif(const QByteArray &jpeg) {
    for (const Segment &s : jpegSegments(jpeg)) {
        if (s.marker != 0xE1 || !s.payload.startsWith(QByteArray("Exif\0\0", 6)))
            continue;
        const QByteArray tiff = s.payload.mid(6);
        const bool little = tiff.startsWith("II");
        if (!little && !tiff.startsWith("MM"))
            return std::nullopt;
        auto u16 = [&](qsizetype at) {
            const quint32 a = byteAt(tiff, at), b = byteAt(tiff, at + 1);
            return little ? (a | b << 8) : (a << 8 | b);
        };
        auto u32 = [&](qsizetype at) {
            return little ? (u16(at) | u16(at + 2) << 16) : (u16(at) << 16 | u16(at + 2));
        };
        ExifTags tags;
        quint32 exifIfd = 0;
        auto readIfd = [&](qsizetype ifd) {
            const quint32 entries = u16(ifd);
            for (quint32 k = 0; k < entries; ++k) {
                const qsizetype e = ifd + 2 + 12 * static_cast<qsizetype>(k);
                const quint32 tag = u16(e), type = u16(e + 2), count = u32(e + 4);
                QByteArray ascii;
                if (type == 2) { // ASCII: inline when it fits in 4 bytes
                    ascii = tiff.mid(count > 4 ? static_cast<qsizetype>(u32(e + 8)) : e + 8,
                                     static_cast<qsizetype>(count));
                    while (ascii.endsWith('\0'))
                        ascii.chop(1);
                }
                if (tag == 0x010F)
                    tags.make = ascii;
                else if (tag == 0x0110)
                    tags.model = ascii;
                else if (tag == 0x0112)
                    tags.orientation = static_cast<int>(u16(e + 8));
                else if (tag == 0x8769)
                    exifIfd = u32(e + 8);
                else if (tag == 0x9003)
                    tags.dateTimeOriginal = ascii;
            }
        };
        readIfd(static_cast<qsizetype>(u32(4)));
        if (exifIfd != 0)
            readIfd(static_cast<qsizetype>(exifIfd));
        return tags;
    }
    return std::nullopt;
}

// A minimal little-endian Exif APP1 segment. IFD0 holds Make, Model,
// Orientation and the Exif IFD pointer; the Exif IFD holds DateTimeOriginal.
QByteArray exifSegment(quint16 orientation) {
    const QByteArray make("TestCam", 8); // the 8th byte is the NUL
    const QByteArray model("Model X", 8);
    const QByteArray taken("2025:01:02 03:04:05", 20);
    const quint32 ifd0 = 8;
    const quint32 makeAt = ifd0 + 2 + 4 * 12 + 4;
    const quint32 modelAt = makeAt + 8;
    const quint32 exifIfd = modelAt + 8;
    const quint32 takenAt = exifIfd + 2 + 12 + 4;

    QByteArray tiff;
    QDataStream out(&tiff, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("II", 2);
    out << quint16(42) << ifd0;
    out << quint16(4);
    out << quint16(0x010F) << quint16(2) << quint32(8) << makeAt;
    out << quint16(0x0110) << quint16(2) << quint32(8) << modelAt;
    out << quint16(0x0112) << quint16(3) << quint32(1) << orientation << quint16(0);
    out << quint16(0x8769) << quint16(4) << quint32(1) << exifIfd;
    out << quint32(0); // no IFD1
    out.writeRawData(make.constData(), 8);
    out.writeRawData(model.constData(), 8);
    out << quint16(1);
    out << quint16(0x9003) << quint16(2) << quint32(20) << takenAt;
    out << quint32(0);
    out.writeRawData(taken.constData(), 20);

    const qsizetype length = 2 + 6 + tiff.size();
    QByteArray segment("\xFF\xE1", 2);
    segment.append(static_cast<char>(length >> 8)).append(static_cast<char>(length & 0xFF));
    segment.append(QByteArray("Exif\0\0", 6)).append(tiff);
    return segment;
}

// Stored 96x64 with a red block in the stored top-left corner. Orientation 6
// shows it 64x96 with the block at the top right.
constexpr QSize kShownSize(64, 96);
constexpr QPoint kRedBlockShown(56, 12);

// A quality-95 JPEG of a detailed test card, with Exif (Orientation 6)
// spliced in right after SOI.
QString makePhoto(const QTemporaryDir &dir) {
    QImage card(96, 64, QImage::Format_RGB32);
    for (int y = 0; y < card.height(); ++y)
        for (int x = 0; x < card.width(); ++x)
            card.setPixel(x, y, qRgb(40 + 2 * x, 60 + 3 * y, (x * 7 + y * 13) % 256));
    QPainter(&card).fillRect(0, 0, 24, 16, QColor(255, 0, 0));

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(95);
    if (!writer.write(card))
        return {};
    QByteArray bytes = buffer.data();
    bytes.insert(2, exifSegment(6));
    const QString path = dir.filePath(QStringLiteral("photo.jpg"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        return {};
    return path;
}

} // namespace

class KnownBugsImage : public QObject {
    Q_OBJECT
  private slots:
    void undoAfterSaveRemovesMark();
    void editedJpegSaveKeepsExif();
    void editedJpegSaveKeepsSourceQuality();
    void fitIsDrawnAtScreenResolution();
    void undoMemoryStaysWithinFourImages();
};

// todo/2026-10-15-image-undo-after-save: On an image, ⌘Z after Save can never remove a mark.
void KnownBugsImage::undoAfterSaveRemovesMark() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("mark.png"));
    QImage base(64, 48, QImage::Format_RGB32);
    base.fill(QColor(0, 0, 255));
    QVERIFY(base.save(path));
    const QPoint onStroke(10, 20); // left edge of the rectangle

    // The document in its view, at 100 %.
    ImageDocument doc(path);
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = qobject_cast<QScrollArea *>(doc.createView(&host));
    QVERIFY(scroll);
    layout->addWidget(scroll);
    host.resize(200, 160);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QVERIFY(doc.awaitDecodeForTest());
    doc.zoomActual();
    auto *label = qobject_cast<QLabel *>(scroll->widget());
    QVERIFY(label);
    QVERIFY(!shownRedAt(label, onStroke));

    doc.annotations()->add(redRectangle(QRectF(10, 10, 30, 20)));
    QVERIFY(shownRedAt(label, onStroke));
    QVERIFY(doc.save());
    QVERIFY(isRed(QImage(path), onStroke));
    // Still a mark, not pixels.
    QEXPECT_FAIL("", "todo/2026-10-15-image-undo-after-save: Save burns the mark into the pixels and drops it from the marks",
                 Continue);
    QCOMPARE(doc.annotations()->count(), 1);

    QVERIFY(doc.undo());
    QEXPECT_FAIL("", "todo/2026-10-15-image-undo-after-save: Save burns the mark into the pixels; undo restores only the mark list",
                 Abort);
    QVERIFY(!shownRedAt(label, onStroke));
    QVERIFY(doc.isDirty());

    QVERIFY(doc.redo());
    QVERIFY(shownRedAt(label, onStroke));

    QVERIFY(doc.undo());
    QVERIFY(doc.save());
    QVERIFY(!isRed(QImage(path), onStroke));
}

// todo/2026-10-15-save-rewrites-unedited-files: Save rewrites unedited files; JPEG Save re-encodes and drops EXIF.
void KnownBugsImage::editedJpegSaveKeepsExif() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makePhoto(dir);
    QVERIFY(!path.isEmpty());
    const std::optional<ExifTags> source = readExif(readFile(path));
    QVERIFY(source.has_value());
    QCOMPARE(source->make, QByteArray("TestCam"));
    QCOMPARE(source->model, QByteArray("Model X"));
    QCOMPARE(source->dateTimeOriginal, QByteArray("2025:01:02 03:04:05"));
    QCOMPARE(source->orientation, 6);
    QVERIFY(QImageReader(path).transformation() == QImageIOHandler::TransformationRotate90);

    ImageDocument doc(path);
    const QImage shown = doc.image();
    QCOMPARE(shown.size(), kShownSize);
    QVERIFY(isRed(shown, kRedBlockShown));
    doc.annotations()->add(redRectangle(QRectF(8, 60, 20, 20)));
    QVERIFY(doc.save());

    // Reopened, the photo still shows the way it did before Save.
    QImageReader reopen(path);
    reopen.setAutoTransform(true);
    const QImage reopened = reopen.read();
    QCOMPARE(reopened.size(), kShownSize);
    QVERIFY(isRed(reopened, kRedBlockShown));

    const std::optional<ExifTags> saved = readExif(readFile(path));
    QEXPECT_FAIL("", "todo/2026-10-15-save-rewrites-unedited-files: save() writes through QImageWriter, which writes no Exif", Abort);
    QVERIFY(saved.has_value());
    QCOMPARE(saved->make, source->make);
    QCOMPARE(saved->model, source->model);
    QCOMPARE(saved->dateTimeOriginal, source->dateTimeOriginal);
}

// todo/2026-10-15-save-rewrites-unedited-files: Save rewrites unedited files; JPEG Save re-encodes and drops EXIF.
void KnownBugsImage::editedJpegSaveKeepsSourceQuality() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makePhoto(dir);
    QVERIFY(!path.isEmpty());
    // The quantisation table is the encoder's quality setting itself. File
    // size also moves with content and encoder, so it can't isolate quality.
    const int sourceSum = lumaQuantisationSum(readFile(path));
    QVERIFY(sourceSum > 0);

    ImageDocument doc(path);
    doc.annotations()->add(redRectangle(QRectF(8, 60, 20, 20)));
    QVERIFY(doc.save());
    const int savedSum = lumaQuantisationSum(readFile(path));
    QVERIFY(savedSum > 0);
    const QString detail =
        QStringLiteral("luma steps sum %1, source %2").arg(savedSum).arg(sourceSum);
    QEXPECT_FAIL("", "todo/2026-10-15-save-rewrites-unedited-files: save() re-encodes at QImageWriter's default quality", Abort);
    QVERIFY2(savedSum <= sourceSum, qPrintable(detail));
}

// todo/2026-10-15-images-at-fit-blurry-hidpi: Images at Fit are drawn at 1x and upscaled on HiDPI screens.
void KnownBugsImage::fitIsDrawnAtScreenResolution() {
    QImage source(4000, 3000, QImage::Format_RGB32);
    source.fill(Qt::white);
    ImageDocument doc{QString()};
    doc.setImageForTest(source);

    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = qobject_cast<QScrollArea *>(doc.createView(&host));
    QVERIFY(scroll);
    layout->addWidget(scroll);
    host.resize(1000, 750);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    doc.zoomFitPage();
    QCoreApplication::processEvents();

    auto *label = qobject_cast<QLabel *>(scroll->widget());
    QVERIFY(label);
    QCOMPARE(doc.zoomMode(), ZoomMode::FitInView);
    const double zoom = doc.zoomFactor();
    QVERIFY2(zoom > 0.0 && zoom < 0.5, qPrintable(QString::number(zoom)));
    // Drawn width comes from the zoom, not from the pixmap under test; the
    // label must agree with it.
    const double drawnWidth = source.deviceIndependentSize().width() * zoom;
    QVERIFY2(
        std::abs(label->width() - drawnWidth) <= 1.0,
        qPrintable(QStringLiteral("label %1, zoom says %2").arg(label->width()).arg(drawnWidth)));
    const double dpr = label->devicePixelRatioF();
    QVERIFY(dpr >= 1.0);

    // The pixmap handed to the label. QLabel draws from it and never adds
    // pixels; QLabel::pixmap() is re-derived at the app's dpr, so not used.
    const QPixmap handed = doc.labelPixmapForTest();
    const double needed = std::min(static_cast<double>(source.width()), drawnWidth * dpr) - 1.0;
    const QString detail = QStringLiteral("pixmap %1 device px wide, needs %2 at dpr %3")
                               .arg(handed.width())
                               .arg(needed)
                               .arg(dpr);
    if (dpr > 1.0)
        QEXPECT_FAIL("", "todo/2026-10-15-images-at-fit-blurry-hidpi: the Fit pixmap is built at 1x and the screen upscales it", Abort);
    QVERIFY2(handed.width() >= needed, qPrintable(detail));
}

// todo/2026-10-15-image-undo-memory-unbounded: Image undo memory is unbounded by size.
void KnownBugsImage::undoMemoryStaysWithinFourImages() {
#ifndef KNOWN_BUGS_HAVE_MALLINFO2
    QSKIP("Counts the heap's bytes in use with glibc's mallinfo2(); known-bug tests run on Linux");
#else
    QImage source(2000, 1500, QImage::Format_RGB32);
    source.fill(QColor(0, 128, 128));
    QPainter(&source).fillRect(0, 0, 100, 50, QColor(255, 0, 0));
    ImageDocument doc{QString()};
    doc.setImageForTest(source);
    source = QImage(); // the document now holds the only copy
    const qint64 imageBytes = doc.image().sizeInBytes();

    // Eight rotates and flips. The test keeps a digest of each earlier state,
    // not the image, so what the heap gains is what the document holds.
    std::vector<QByteArray> before;
    before.reserve(8);
    const qint64 heapBefore = heapBytesInUse();
    for (int i = 0; i < 8; ++i) {
        before.push_back(pixelDigest(doc.image()));
        if (i % 2 == 0)
            doc.rotatePage(0, 90);
        else
            doc.flipHorizontal();
    }
    const qint64 held = heapBytesInUse() - heapBefore;
    QVERIFY(doc.canUndo());
    // 1 MiB on top of four images covers the history's own bookkeeping; a
    // fifth image (12 MB here) does not fit in it.
    const qint64 allowed = 4 * imageBytes + (1 << 20);
    const QString detail =
        QStringLiteral("the heap grew by %1 bytes for a %2-byte image").arg(held).arg(imageBytes);
    QEXPECT_FAIL("", "todo/2026-10-15-image-undo-memory-unbounded: every rotate and flip keeps a full copy; the cap is 32 steps", Abort);
    QVERIFY2(held <= allowed, qPrintable(detail));

    // Bounded without giving up undo: each step undoes to the state before it.
    for (int i = 7; i >= 0; --i) {
        QVERIFY(doc.undo());
        QCOMPARE(pixelDigest(doc.image()), before.at(static_cast<size_t>(i)));
    }
    QVERIFY(!doc.canUndo());
#endif
}

QTEST_MAIN(KnownBugsImage)
#include "test_known_bugs_image.moc"
