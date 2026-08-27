#include <atomic>
#include <thread>
#include <vector>

#include <QTest>

#include "audio/AudioRing.h"
#include "audio/Downmix.h"

using namespace hyperwave;

class TstRing : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void writeThenRead()
    {
        AudioRing<16> ring;
        const float in[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        QVERIFY(ring.write(in, 4, 0));
        QCOMPARE(ring.available(), 4u);

        float out[4] = {};
        QCOMPARE(ring.read(out, 4), 4u);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(out[i], in[i]);
        QCOMPARE(ring.available(), 0u);
    }

    // The bug that is actually going to be there. Everything works until the
    // write cursor straddles the mask boundary and the memcpy has to split.
    void wrapsAcrossTheMaskBoundary()
    {
        AudioRing<8> ring;
        float scratch[8] = {};

        // Push the cursors to 6, so the next write of 4 has to split 2 and 2.
        const float pad[6] = {0, 0, 0, 0, 0, 0};
        QVERIFY(ring.write(pad, 6, 0));
        QCOMPARE(ring.read(scratch, 6), 6u);

        const float in[4] = {10.0f, 20.0f, 30.0f, 40.0f};
        QVERIFY(ring.write(in, 4, 0));

        float out[4] = {};
        QCOMPARE(ring.read(out, 4), 4u);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(out[i], in[i]);
    }

    // All or nothing. A partial write would split one callback's audio across
    // two analysis frames, which is worse than dropping it outright.
    void refusesToPartiallyWrite()
    {
        AudioRing<8> ring;
        const float five[5] = {1, 2, 3, 4, 5};
        QVERIFY(ring.write(five, 5, 0));
        QVERIFY(!ring.write(five, 5, 0));
        QCOMPARE(ring.available(), 5u);
    }

    void discardKeepsTheNewest()
    {
        AudioRing<16> ring;
        std::vector<float> in(12);
        for (int i = 0; i < 12; ++i)
            in[static_cast<std::size_t>(i)] = static_cast<float>(i);
        QVERIFY(ring.write(in.data(), 12, 0));

        ring.discardAllButNewest(4);
        QCOMPARE(ring.available(), 4u);

        float out[4] = {};
        QCOMPARE(ring.read(out, 4), 4u);
        QCOMPARE(out[0], 8.0f);
        QCOMPARE(out[3], 11.0f);
    }

    // Two real threads, because the whole point of the memory ordering is that
    // it holds on arm64, and a single-threaded test cannot tell.
    void survivesConcurrentProducerAndConsumer()
    {
        AudioRing<1024> ring;
        constexpr int kTotal = 200000;
        constexpr int kChunk = 64;

        std::atomic<bool> mismatch{false};
        std::atomic<int> produced{0};

        std::thread producer([&] {
            float buf[kChunk];
            int next = 0;
            while (next < kTotal) {
                for (int i = 0; i < kChunk; ++i)
                    buf[i] = static_cast<float>((next + i) % 1000);
                if (ring.write(buf, kChunk, 0)) {
                    next += kChunk;
                    produced.store(next, std::memory_order_release);
                } else {
                    std::this_thread::yield();
                }
            }
        });

        std::thread consumer([&] {
            float buf[kChunk];
            int seen = 0;
            while (seen < kTotal) {
                const std::uint32_t got = ring.read(buf, kChunk);
                for (std::uint32_t i = 0; i < got; ++i) {
                    if (buf[i] != static_cast<float>((seen + static_cast<int>(i)) % 1000)) {
                        mismatch.store(true, std::memory_order_release);
                        return;
                    }
                }
                seen += static_cast<int>(got);
                if (got == 0)
                    std::this_thread::yield();
            }
        });

        producer.join();
        consumer.join();
        QVERIFY(!mismatch.load(std::memory_order_acquire));
    }

    void downmixAveragesChannels()
    {
        const float stereo[6] = {1.0f, 0.0f, 0.5f, 0.5f, -1.0f, 1.0f};
        float mono[3] = {};
        downmixInterleaved(stereo, mono, 3, 2);
        QCOMPARE(mono[0], 0.5f);
        QCOMPARE(mono[1], 0.5f);
        QCOMPARE(mono[2], 0.0f);
    }

    void downmixPlanarMatchesInterleaved()
    {
        const float left[3] = {1.0f, 0.5f, -1.0f};
        const float right[3] = {0.0f, 0.5f, 1.0f};
        const float *planes[2] = {left, right};
        float mono[3] = {};
        downmixPlanar(planes, mono, 3, 2);
        QCOMPARE(mono[0], 0.5f);
        QCOMPARE(mono[1], 0.5f);
        QCOMPARE(mono[2], 0.0f);
    }

    // A null channel counts as silence rather than being skipped, so a
    // half-populated buffer list does not quietly change the mix gain.
    void downmixPlanarTreatsNullChannelAsSilence()
    {
        const float left[2] = {1.0f, 1.0f};
        const float *planes[2] = {left, nullptr};
        float mono[2] = {};
        downmixPlanar(planes, mono, 2, 2);
        QCOMPARE(mono[0], 0.5f);
        QCOMPARE(mono[1], 0.5f);
    }

    // One NaN reaching the FFT poisons every band permanently, and the
    // ballistics have no path back from it.
    void sanitizeKillsNonFinite()
    {
        float data[4] = {1.0f, std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(), -1.0f};
        sanitize(data, 4);
        QCOMPARE(data[0], 1.0f);
        QCOMPARE(data[1], 0.0f);
        QCOMPARE(data[2], 0.0f);
        QCOMPARE(data[3], -1.0f);
    }

    // The permission probe leans on this being exact, not thresholded.
    void exactZeroIsExact()
    {
        const float silent[3] = {0.0f, 0.0f, 0.0f};
        const float nearlySilent[3] = {0.0f, 1e-30f, 0.0f};
        QVERIFY(isExactlyZero(silent, 3));
        QVERIFY(!isExactlyZero(nearlySilent, 3));
    }

    void rmsOfSilenceIsFinite()
    {
        const float silent[8] = {};
        QCOMPARE(rmsDb(silent, 8), -120.0f);
    }
};

QTEST_APPLESS_MAIN(TstRing)
#include "tst_ring.moc"
