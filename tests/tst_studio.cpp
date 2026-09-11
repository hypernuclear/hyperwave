#include <QtTest>
#include <cmath>
#include <QColorSpace>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>

#include "render/StrandRenderer.h"
#include "studio/AudioLoader.h"
#include "studio/ExportJob.h"
#include "studio/SystemAudioCapture.h"

using namespace hyperwave;
using namespace hyperwave::studio;

namespace {
class ManualAudioSource : public AudioSource
{
public:
    bool start() override { active = true; return true; }
    void stop() override { active = false; }
    bool isRunning() const override { return active; }
    double sampleRate() const override { return rate; }
    std::uint32_t formatEpoch() const override { return epoch; }
    PermissionState permissionState() const override { return PermissionState::Granted; }
    MonoRing *ring() override { return &samples; }
    std::uint64_t callbackCount() const override { return 1; }
    std::uint64_t overrunCount() const override { return overruns.load(); }
    QString statusDetail() const override { return QStringLiteral("Test audio source"); }
    void setRate(int newRate) { rate = newRate; ++epoch; Q_EMIT formatChanged(rate, epoch); }
    bool active = false;
    int rate = 48000;
    unsigned epoch = 1;
    MonoRing samples;
    std::atomic<quint64> overruns{0};
};

QImage firstFrame(RenderSession &session)
{
    QImage frame;
    while (session.nextFrame() <= 0) frame = session.step();
    return frame;
}

QJsonObject manifest(const QString &directory)
{
    QFile file(QDir(directory).filePath("capture.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
} // namespace

class TstStudio : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void systemAudioRecordsOnItsOwnThreadAndExportsSyncedWav()
    {
        auto source = std::make_unique<ManualAudioSource>();
        auto *manual = source.get();
        SystemAudioCapture capture(std::move(source));
        std::shared_ptr<const AudioClip> clip;
        connect(&capture, &SystemAudioCapture::recorded, &capture, [&](auto value) { clip = std::move(value); });
        QVERIFY(capture.start());
        QTest::qWait(30);
        QVector<float> preceding(4800, 0.125f);
        QVERIFY(manual->samples.write(preceding.data(), preceding.size(), manual->epoch));
        QTRY_VERIFY(capture.frame().seq > 0);
        QVERIFY(capture.record(60));
        QVector<float> take(48000);
        for (int i = 0; i < take.size(); ++i) take[i] = float(i) / take.size() * 0.5f;
        QVERIFY(manual->samples.write(take.data(), take.size(), manual->epoch));
        // No GUI event processing while samples arrive: recording must continue.
        QTest::qSleep(250);
        QVERIFY(!capture.recording());
        QTRY_VERIFY(clip != nullptr);
        QCOMPARE(clip->samples.size(), 144000);
        QVERIFY(clip->systemAudio);
        QCOMPARE(clip->samples[95999], 0.125f);
        for (int i = 0; i < take.size(); ++i) QCOMPARE(clip->samples[96000 + i], take[i]);
        capture.stop();
        QVERIFY(!capture.running());
        QTemporaryDir temp;
        CaptureSettings settings;
        settings.frames = 60;
        settings.startSeconds = 2;
        settings.size = {182, 44};
        ExportJob exporter;
        QSignalSpy done(&exporter, &ExportJob::finished);
        QVERIFY(exporter.start(settings, clip, temp.path()));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY(done[0][2].toString().isEmpty());
        QFile wav(QDir(exporter.directory()).filePath("audio.wav"));
        QVERIFY(wav.open(QIODevice::ReadOnly));
        const QByteArray bytes = wav.readAll();
        QCOMPARE(bytes.size(), 44 + 48000 * 2);
        QCOMPARE(bytes.first(4), QByteArray("RIFF"));
        QCOMPARE(qFromLittleEndian<qint16>(bytes.constData() + 44), qint16(0));
        QCOMPARE(qFromLittleEndian<qint16>(bytes.constData() + 44 + 47999 * 2),
                 qint16(qRound(take.last() * 32767)));
        QCOMPARE(manifest(exporter.directory())["audioFile"].toString(), QStringLiteral("audio.wav"));
        QCOMPARE(manifest(exporter.directory())["audioFileStartSeconds"].toDouble(), 0.0);
    }

    void systemAudioRejectsInterruptedTakesAndCanBeCancelled()
    {
        auto source = std::make_unique<ManualAudioSource>();
        auto *manual = source.get();
        SystemAudioCapture capture(std::move(source));
        QSignalSpy failed(&capture, &SystemAudioCapture::failed);
        QSignalSpy recorded(&capture, &SystemAudioCapture::recorded);
        QVERIFY(capture.start());
        QTest::qWait(30);
        QVERIFY(capture.record(60));
        capture.cancelRecording();
        QVERIFY(!capture.recording());
        QVERIFY(capture.record(60));
        manual->setRate(44100);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!capture.recording());
        QVERIFY(failed[0][0].toString().contains("device changed"));
        QVERIFY(capture.record(60));
        manual->overruns.fetch_add(1);
        QTRY_COMPARE(failed.size(), 2);
        QVERIFY(!capture.recording());
        QVERIFY(failed[1][0].toString().contains("dropped"));
        QCOMPARE(recorded.size(), 0);
        capture.stop();
    }

    void exactSizeAndAlpha_data()
    {
        QTest::addColumn<QString>("visualiser");
        QTest::addColumn<int>("layout");
        for (const auto &id : visualisers())
            for (int layout = 0; layout < 3; ++layout)
                QTest::addRow("%s-%d", qPrintable(id), layout) << id << layout;
    }

    void exactSizeAndAlpha()
    {
        QFETCH(QString, visualiser);
        QFETCH(int, layout);
        if (visualiser == QLatin1String("strands") && !StrandRenderer::isAvailable())
            QSKIP("Strands requires a GPU");
        CaptureSettings settings;
        settings.visualiser = visualiser;
        settings.layout = static_cast<Layout>(layout);
        settings.style.bloom = defaultBloom(visualiser);
        settings.size = {389, 157};
        RenderSession session(settings);
        const QImage image = firstFrame(session);
        QCOMPARE(image.size(), settings.size);
        bool clear = false, ink = false, partial = false;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const int alpha = qAlpha(image.pixel(x, y));
                clear |= alpha == 0;
                ink |= alpha > 32;
                partial |= alpha > 0 && alpha < 255;
            }
        }
        QVERIFY(clear);
        QVERIFY(ink);
        QVERIFY(partial);
    }

    void offlineTimeIsIndependentOfWallClock()
    {
        if (!StrandRenderer::isAvailable()) QSKIP("Strands requires a GPU");
        CaptureSettings settings;
        settings.visualiser = QStringLiteral("strands");
        settings.size = {364, 88};
        RenderSession first(settings), second(settings);
        QCOMPARE(firstFrame(first), firstFrame(second));
        QTest::qWait(50);
        QCOMPARE(first.step(), second.step());
        const QImage a = first.step();
        QTest::qWait(50);
        QCOMPARE(a, second.step());
    }

    void notchBackgroundIsOptional()
    {
        CaptureSettings settings;
        settings.layout = Layout::NotchExpanded;
        settings.size = {754, 212};
        RenderSession transparent(settings);
        const QImage a = firstFrame(transparent);
        settings.includePanel = true;
        RenderSession panel(settings);
        const QImage b = firstFrame(panel);
        QCOMPARE(qAlpha(a.pixel(377, 10)), 0);
        QCOMPARE(b.pixelColor(377, 10), QColor(Qt::black));
        QCOMPARE(qAlpha(b.pixel(0, 211)), 0);
        QCOMPARE(b.size(), a.size());
    }

    void pngRoundTripKeepsColorAndAlpha()
    {
        QTemporaryDir temp;
        CaptureSettings settings;
        settings.size = {364, 88};
        RenderSession session(settings);
        const QImage original = firstFrame(session);
        QString error;
        const QString path = temp.filePath("frame.png");
        QVERIFY2(savePng(original, path, &error), qPrintable(error));
        const QImage decoded(path);
        QVERIFY(decoded.hasAlphaChannel());
        QVERIFY(decoded.colorSpace().isValid());
        QCOMPARE(decoded.convertToFormat(QImage::Format_ARGB32_Premultiplied), original);
    }

    void sequenceHasExactlySixtyFramesAndDoesNotOverwrite()
    {
        QTemporaryDir temp;
        CaptureSettings settings;
        settings.size = {182, 44};
        settings.frames = 60;
        ExportJob job;
        QSignalSpy done(&job, &ExportJob::finished);
        QVERIFY(job.start(settings, {}, temp.path()));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY(done[0][2].toString().isEmpty());
        const QString directory = job.directory();
        QCOMPARE(QDir(directory).entryList({"*.png"}, QDir::Files).size(), 60);
        QVERIFY(QFile::exists(QDir(directory).filePath("frame_000000.png")));
        QVERIFY(QFile::exists(QDir(directory).filePath("frame_000059.png")));
        const QJsonObject info = manifest(directory);
        QCOMPARE(info["fps"].toInt(), 60);
        QCOMPARE(info["writtenFrames"].toInt(), 60);
        QCOMPARE(info["durationSeconds"].toDouble(), 1.0);
        QVERIFY(info["complete"].toBool());
        QVERIFY(QImage(QDir(directory).filePath("frame_000000.png"))
                != QImage(QDir(directory).filePath("frame_000059.png")));
        QVERIFY(job.start(settings, {}, temp.path()));
        const QString second = job.directory();
        QVERIFY(second != directory);
        job.cancel();
        QCOMPARE(QDir(directory).entryList({"*.png"}, QDir::Files).size(), 60);
    }

    void cancellationKeepsOnlyCompletedFrames()
    {
        QTemporaryDir temp;
        CaptureSettings settings;
        settings.size = {182, 44};
        ExportJob job;
        QSignalSpy done(&job, &ExportJob::finished);
        connect(&job, &ExportJob::progress, &job, [&](int count) {
            if (count == 3) job.cancel();
        });
        QVERIFY(job.start(settings, {}, temp.path()));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY(done[0][1].toBool());
        QCOMPARE(QDir(job.directory()).entryList({"*.png"}, QDir::Files).size(), 3);
        QCOMPARE(manifest(job.directory())["writtenFrames"].toInt(), 3);
        QVERIFY(!manifest(job.directory())["complete"].toBool());
    }

    void invalidSettingsAndWriteFailuresAreReported()
    {
        CaptureSettings settings;
        settings.size = {8192, 8192};
        QVERIFY(!validate(settings).isEmpty());
        settings.size = {182, 44};
        auto clip = std::make_shared<AudioClip>();
        clip->samples.fill(0.1f, 48000);
        QVERIFY(!validate(settings, clip.get()).isEmpty());
        ExportJob job;
        QSignalSpy done(&job, &ExportJob::finished);
        QTemporaryDir temp;
        QVERIFY(!job.start(settings, {}, temp.filePath("missing/parent")));
        QCOMPARE(done.size(), 1);
        QVERIFY(!done[0][2].toString().isEmpty());
        QVERIFY(!job.running());
    }

    void audioFileDecodesAndDrivesTheRenderer()
    {
        QTemporaryDir temp;
        const QString path = temp.filePath("tone.wav");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream out(&file);
        out.setByteOrder(QDataStream::LittleEndian);
        constexpr int count = 44100;
        out.writeRawData("RIFF", 4); out << quint32(36 + count * 2);
        out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1);
        out << quint32(44100) << quint32(88200) << quint16(2) << quint16(16);
        out.writeRawData("data", 4); out << quint32(count * 2);
        for (int i = 0; i < count; ++i) out << qint16(12000 * std::sin(i * 6.28318530718 * 440 / 44100));
        file.close();
        AudioLoader loader;
        std::shared_ptr<const AudioClip> clip;
        QString error;
        connect(&loader, &AudioLoader::loaded, &loader, [&](auto loaded) { clip = std::move(loaded); });
        connect(&loader, &AudioLoader::failed, &loader, [&](const QString &message) { error = message; });
        loader.load(path);
        QTRY_VERIFY_WITH_TIMEOUT(clip || !error.isEmpty(), 15000);
        QVERIFY2(clip != nullptr, qPrintable(error));
        QVERIFY(std::abs(clip->duration() - 1.0) < 0.01);
        CaptureSettings settings;
        settings.frames = 30;
        settings.startSeconds = 0.25;
        settings.size = {364, 88};
        RenderSession audio(settings, clip), demo(settings);
        QVERIFY(firstFrame(audio) != firstFrame(demo));
    }
};

QTEST_MAIN(TstStudio)
#include "tst_studio.moc"
