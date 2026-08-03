// Reading a PDF's text without any outside help: the DEFLATE decoder, the content-stream operators,
// and a subset font's /ToUnicode map. The PDFs here are built by hand so what should come out is known
// exactly - the same three shapes a real datasheet arrives in.
#include "harness/Datasheet.h"
#include "io/PdfText.h"

#include <QByteArray>
#include <QFile>
#include <QDir>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {
int g_fail = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}

// A one-page PDF whose single content stream is `content`, optionally Flate-compressed, plus any extra
// objects the caller wants (a ToUnicode CMap, say). Enough of a PDF to be read: a header, the objects,
// and a trailer. The cross-reference table is deliberately left out - real producers get it wrong often
// enough that the reader scans for streams instead, and this proves that path works.
QByteArray makePdf(const QByteArray& content, bool compress, const QByteArray& extraObjects = {},
                   const QByteArray& fontDict = "<< /F1 << /Type /Font /BaseFont /Helvetica >> >>") {
    QByteArray stream = content;
    QByteArray filter;
    if (compress) {
        // qCompress writes a zlib stream behind a four-byte length; a PDF carries the zlib stream
        // alone, so the length comes off. That is exactly what the reader has to cope with.
        const QByteArray z = qCompress(content, 9);
        stream = z.mid(4);
        filter = " /Filter /FlateDecode";
    }
    QByteArray pdf = "%PDF-1.4\n";
    pdf += "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n";
    pdf += "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n";
    pdf += "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << /Font " + fontDict +
           " >> >> endobj\n";
    pdf += "4 0 obj << /Length " + QByteArray::number(stream.size()) + filter + " >>\nstream\n";
    pdf += stream;
    pdf += "\nendstream\nendobj\n";
    pdf += extraObjects;
    pdf += "trailer << /Root 1 0 R >>\n%%EOF\n";
    return pdf;
}
} // namespace

int run_pdftext_tests() {
    g_fail = 0;
    std::printf("PDF text tests\n");

    // --- The DEFLATE decoder, on its own: every block type a producer might write.
    {
        const QByteArray original =
            "Rated power: 250 mW\nTolerance: 1 %\nTemperature coefficient: 50 ppm/K\n"
            "A line that repeats a line that repeats a line that repeats, so the back-references "
            "overlap and the copy has to be byte by byte.";
        const QByteArray z = qCompress(original, 9).mid(4);   // a bare zlib stream
        check(PdfText::inflate(z, true) == original, "inflate: a zlib stream comes back exactly");
        // Stored blocks (level 0 still wraps them in zlib).
        const QByteArray stored = qCompress(original, 0).mid(4);
        check(PdfText::inflate(stored, true) == original, "inflate: stored blocks too");
        // Rubbish in: nothing out, and no hanging about.
        check(PdfText::inflate(QByteArray("not a stream at all"), true).size() <
                  static_cast<qsizetype>(64),
              "inflate: nonsense gives nothing rather than looping");
        check(PdfText::inflate(QByteArray(), true).isEmpty(), "inflate: nothing in, nothing out");
        // Truncated: what was recovered is kept, because half a datasheet beats none.
        const QByteArray cut = z.left(z.size() / 2);
        check(PdfText::inflate(cut, true).size() < original.size(),
              "inflate: a truncated stream gives what there was");
    }

    // --- A plain content stream: Tj, TJ and the escapes.
    {
        const QByteArray content =
            "BT /F1 10 Tf 72 720 Td (Rated power: 250 mW) Tj 0 -14 Td\n"
            "[(Tolerance: ) -250 (1 %)] TJ 0 -14 Td (Range: -55 to +125 \\260C) Tj ET";
        QString why;
        const QString text = PdfText::extract(makePdf(content, false), &why);
        std::printf("    (uncompressed: %s)\n", qPrintable(why));
        check(text.contains(QStringLiteral("Rated power: 250 mW")), "pdf: a Tj string comes out");
        check(text.contains(QStringLiteral("Tolerance:")) && text.contains(QStringLiteral("1 %")),
              "pdf: a TJ array comes out");
        check(text.contains(QString::fromUtf8("-55 to +125 °C")),
              "pdf: an octal escape becomes its character");
        check(text.count(QLatin1Char('\n')) >= 2, "pdf: Td starts a new line, so lines survive");
    }

    // --- The same, compressed, which is what a real datasheet does.
    {
        const QByteArray content =
            "BT /F1 9 Tf 40 700 Td (Nominal resistance: 4.7 kohm) Tj 0 -12 Td "
            "(Working voltage: 250 V) Tj ET";
        QString why;
        const QString text = PdfText::extract(makePdf(content, true), &why);
        std::printf("    (compressed: %s)\n", qPrintable(why));
        check(text.contains(QStringLiteral("Nominal resistance: 4.7 kohm")),
              "pdf: a Flate-compressed content stream is inflated and read");
        check(text.contains(QStringLiteral("Working voltage: 250 V")),
              "pdf: and all of it, not just the first line");
    }

    // --- A subset font: the bytes are glyph numbers, and only /ToUnicode makes them letters.
    {
        // "ABC" as glyphs 1, 2, 3 - two-byte codes, as Identity-H uses.
        const QByteArray cmap =
            "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
            "/CMapName /Custom def /CMapType 2 def\n"
            "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n"
            "3 beginbfchar\n<0001> <0041>\n<0002> <0042>\n<0003> <0043>\nendbfchar\n"
            "1 beginbfrange\n<0010> <0012> <0030>\nendbfrange\n"
            "endcmap end end";
        QByteArray extra = "5 0 obj << /Length " + QByteArray::number(cmap.size()) + " >>\nstream\n" +
                           cmap + "\nendstream\nendobj\n";
        const QByteArray fontDict =
            "<< /F1 6 0 R >>";
        extra += "6 0 obj << /Type /Font /Subtype /Type0 /BaseFont /AAAAAA+Arial "
                 "/Encoding /Identity-H /ToUnicode 5 0 R >> endobj\n";
        // <0001 0002 0003> is "ABC"; <0010 0011 0012> is "012" through the bfrange.
        const QByteArray content = "BT /F1 10 Tf 10 700 Td <000100020003> Tj "
                                   "0 -12 Td <001000110012> Tj ET";
        QString why;
        const QString text = PdfText::extract(makePdf(content, false, extra, fontDict), &why);
        std::printf("    (subset font: %s -> \"%s\")\n", qPrintable(why),
                    qPrintable(QString(text).replace(QLatin1Char('\n'), QLatin1Char('|'))));
        check(text.contains(QStringLiteral("ABC")),
              "pdf: beginbfchar maps glyph codes to their letters");
        check(text.contains(QStringLiteral("012")),
              "pdf: beginbfrange maps a run of them");
    }

    // --- What it will not do, said plainly rather than pretending the datasheet is blank.
    {
        QString why;
        check(PdfText::extract(QByteArray("just some text, not a PDF"), &why).isEmpty() &&
                  why.contains(QStringLiteral("not a PDF")),
              "pdf: something that is not a PDF is called that");
        why.clear();
        QByteArray enc = makePdf("BT /F1 9 Tf (x) Tj ET", false);
        enc.replace("trailer <<", "trailer << /Encrypt 9 0 R ");
        check(PdfText::extract(enc, &why).isEmpty() && why.contains(QStringLiteral("encrypted")),
              "pdf: an encrypted one says so");
        why.clear();
        // A page with no text on it at all - a scan, in effect.
        check(PdfText::extract(makePdf("0 0 100 100 re f", false), &why).isEmpty() &&
                  why.contains(QStringLiteral("scan")),
              "pdf: no text at all reads as a scan, which needs character recognition");
        check(PdfText::looksLikePdf(QByteArray("%PDF-1.7\n...")), "pdf: the header is recognised");
        check(!PdfText::looksLikePdf(QByteArray("PK\x03\x04")), "pdf: and a zip is not a PDF");
    }

    // --- End to end: a PDF on disk, read by the datasheet reader, and its numbers pulled out.
    {
        const QByteArray content =
            "BT /F1 9 Tf 40 700 Td (Nominal resistance: 4.7 kohm) Tj 0 -12 Td "
            "(Tolerance: 1 %) Tj 0 -12 Td (Rated power: 250 mW) Tj ET";
        const QString path = QDir::tempPath() + QStringLiteral("/edraw-test-datasheet.pdf");
        QFile f(path);
        check(f.open(QIODevice::WriteOnly), "pdf: a test file to read");
        f.write(makePdf(content, true));
        f.close();

        QString how;
        const QString text = datasheetText(path, &how);
        std::printf("    (datasheetText: %s)\n", qPrintable(how));
        check(text.contains(QStringLiteral("4.7 kohm")),
              "pdf: the datasheet reader reads a PDF with no outside program");
        // And the field puller gets numbers out of it, which is the point of the whole exercise.
        const std::vector<PulledField> pulled = pullTechnicalFields(text, PartKind::Resistor);
        QStringList keys;
        for (const PulledField& p : pulled) keys << p.key;
        std::printf("    (pulled: %s)\n", qPrintable(keys.join(QStringLiteral(", "))));
        check(keys.contains(QStringLiteral("value")) && keys.contains(QStringLiteral("power")),
              "pdf: the technical fields are pulled from what was read");
        QFile::remove(path);
    }

    if (g_fail == 0) std::printf("  -> ok\n");
    return g_fail;
}
