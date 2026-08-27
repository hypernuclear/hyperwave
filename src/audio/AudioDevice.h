#pragma once

#include <QString>
#include <QVector>

namespace hyperwave {

// An output device the system can play through.
//
// This, rather than a list of applications, is what "audio source" means here.
// Applications turned out to be the wrong axis twice over: CoreAudio will only
// say whether a process holds an open output stream, not whether it is actually
// making a sound, so the list was full of things sitting idle with a device
// open; and a Mac with a virtual device, an interface and three monitors has a
// genuinely interesting choice of where sound is going, which the app list does
// not express at all.
struct AudioDevice
{
    quint32 objectId = 0;
    QString uid;      // stable across launches; what the tap is configured with
    QString name;
    bool isDefaultOutput = false;
};

using AudioDeviceList = QVector<AudioDevice>;

} // namespace hyperwave
