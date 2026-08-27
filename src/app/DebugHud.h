#pragma once

#include <QWidget>

#include "dsp/FrameState.h"

class QLabel;

namespace hyperwave {

class AudioSource;

// The window that gets built first and thrown away last.
//
// Two genuine unknowns sit at the bottom of this app -- what the tap's stream
// format actually is, and what happens when the permission is missing -- and
// neither can be reasoned about from the outside. Being able to watch the
// OSStatus, the callback count, the ASBD, the RMS and the run of exact zeros
// scroll past in real time answers both in minutes rather than in a day of
// guessing. It also doubles as the renderer's iteration surface via --windowed.
class DebugHud : public QWidget
{
    Q_OBJECT

public:
    explicit DebugHud(QWidget *parent = nullptr);

    void setSource(AudioSource *source) { m_source = source; }

    // Draws the strip full-size in the window, so the renderer can be worked on
    // without fighting the menu bar.
    void setPreview(const QImage &image);
    void setFrame(const FrameState &state);

private:
    void refresh();

    AudioSource *m_source = nullptr;
    QLabel *m_stats = nullptr;
    QLabel *m_bars = nullptr;
    QLabel *m_preview = nullptr;
    FrameState m_frame;
};

} // namespace hyperwave
