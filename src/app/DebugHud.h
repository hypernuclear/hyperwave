#pragma once

#include <functional>
#include <vector>

#include <QWidget>

#include "dsp/FrameState.h"
#include "render/RenderStyle.h"

class QComboBox;
class QGroupBox;
class QLabel;
class QSpinBox;

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
// It is where a visualiser gets tuned, too. Every parameter that used to be a
// recompiled constant or a relaunched command-line flag is a control here that
// takes effect on the next frame. Choosing the look, the magnification and the
// strand count before launch meant one process per combination, which is the
// wrong loop for anything that is decided by looking at it.
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

    // Seeds the controls from whatever the command line and the settings
    // decided, so the window opens agreeing with what is already on screen
    // rather than snapping it to the widget defaults the moment it appears.
    void setVisualiser(const QString &id);
    void setPreviewScale(qreal scale);
    void setStrands(const RenderStyle::Strands &strands);
    void setRidge(const RenderStyle::Ridge &ridge);

Q_SIGNALS:
    void visualiserRequested(const QString &id);
    void previewScaleRequested(qreal scale);
    void strandsRequested(const RenderStyle::Strands &strands);
    void ridgeRequested(const RenderStyle::Ridge &ridge);

private:
    void refresh();
    void showRelevantControls();

    AudioSource *m_source = nullptr;
    QLabel *m_stats = nullptr;
    QLabel *m_bars = nullptr;
    QLabel *m_preview = nullptr;
    FrameState m_frame;

    // True while the controls are being seeded, so setting a widget's value
    // does not read back as the user having asked for it.
    bool m_seeding = false;

    // Each control registers how to push a value back into itself. Seeding
    // through the same path the user drives means a slider and the number
    // beside it cannot drift apart.
    std::vector<std::function<void()>> m_seeders;

    QComboBox *m_look = nullptr;
    QSpinBox *m_scale = nullptr;
    QGroupBox *m_strandBox = nullptr;
    QGroupBox *m_ridgeBox = nullptr;

    RenderStyle::Strands m_strands;
    RenderStyle::Ridge m_ridge;
};

} // namespace hyperwave
