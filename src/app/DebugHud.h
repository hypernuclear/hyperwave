#pragma once

#include <QWidget>

#include "app/VisualiserControls.h"
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
//
// The tuning knobs are not defined here. They are the same VisualiserControls
// the settings window shows, so a parameter cannot exist for development and be
// unreachable by anyone outside this repository.
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

    VisualiserControls *controls() const { return m_controls; }

private:
    void refresh();

    AudioSource *m_source = nullptr;
    QLabel *m_stats = nullptr;
    QLabel *m_bars = nullptr;
    QLabel *m_preview = nullptr;
    VisualiserControls *m_controls = nullptr;
    FrameState m_frame;
};

} // namespace hyperwave
