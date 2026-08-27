#pragma once

#include <QString>
#include <QVector>

namespace hyperwave {

// One process the audio system knows about.
//
// Platform-free on purpose: the macOS backend fills this from CoreAudio's
// process objects, and the eventual Windows backend can fill it from the
// session enumerator, without anything above this layer caring which.
struct AudioProcess
{
    // CoreAudio's AudioObjectID for the process. Meaningful only for the
    // current run -- it is not stable across launches, which is why the tap is
    // configured by bundle id and these are resolved fresh each time.
    quint32 objectId = 0;
    qint64 pid = 0;
    QString bundleId;
    QString name;      // localised application name, empty for faceless helpers
    // CAUTION: this is kAudioProcessPropertyIsRunningOutput, which means the
    // process holds an open output stream -- NOT that it is making a sound. An
    // app that opened an audio context at launch and has been silent ever since
    // still reports true. Do not present it to anyone as "playing".
    bool playingOutput = false;
    // Measured peak output level in dBFS, or -120 when silent or unmetered.
    // This, not playingOutput, is what "is it making a sound" means.
    float levelDb = -120.0f;
    bool audible = false;

    bool isNamed() const { return !name.isEmpty(); }

    // The application this process belongs to, as a bundle id.
    //
    // A browser is not one process and the interesting half is usually the one
    // you cannot see. Chrome plays a YouTube tab out of com.google.Chrome.helper
    // -- measured at -17.0 dB -- while com.google.Chrome and the named "Google
    // Chrome Helper" both sit at digital silence. The level lives on a process
    // with no application name; the name and the icon live on a process with no
    // audio. Grouping by this key is what lets one answer come out of both.
    QString appKey() const
    {
        const qsizetype helper =
            bundleId.indexOf(QLatin1String(".helper"), 0, Qt::CaseInsensitive);
        return helper > 0 ? bundleId.left(helper) : bundleId;
    }
};

using AudioProcessList = QVector<AudioProcess>;

} // namespace hyperwave
