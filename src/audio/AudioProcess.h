#pragma once

#include <QString>
#include <QVector>

namespace hyperlight {

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
    bool playingOutput = false;

    bool isNamed() const { return !name.isEmpty(); }
};

using AudioProcessList = QVector<AudioProcess>;

} // namespace hyperlight
