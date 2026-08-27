#pragma once

#include <functional>
#include <vector>

#include <QWidget>

#include "render/RenderStyle.h"

class QComboBox;
class QGroupBox;
class QSpinBox;

namespace hyperwave {

// The knobs for whichever visualiser is running, in one widget used twice.
//
// It is the settings window the user opens from the menu, and it is the tuning
// panel embedded in the debug HUD. Those are the same controls over the same
// values, and keeping them one widget is what stops the two drifting -- a
// parameter added for development that never reaches the settings window is a
// parameter nobody outside this repository can reach.
//
// The difference between the two uses is one flag. The HUD also magnifies its
// preview, which is meaningless in a settings window, and the settings window
// offers a reset, which the HUD does not need.
class VisualiserControls : public QWidget
{
    Q_OBJECT

public:
    struct Options
    {
        bool showMagnify = false;   // preview magnification: development only
        bool showReset = false;
    };

    explicit VisualiserControls(Options options, QWidget *parent = nullptr);

    // Seeds every control. Goes through the same path the user drives, so a
    // slider and the number beside it cannot disagree about what they show.
    void setVisualiser(const QString &id);
    void setPreviewScale(qreal scale);
    void setStrands(const RenderStyle::Strands &strands);
    void setRidge(const RenderStyle::Ridge &ridge);
    void setLines(const RenderStyle::Lines &lines);
    void setMirror(const RenderStyle::Mirror &mirror);
    void setRing(const RenderStyle::Ring &ring);
    void setBloom(const RenderStyle::Bloom &bloom);

Q_SIGNALS:
    void visualiserRequested(const QString &id);
    void previewScaleRequested(qreal scale);
    void strandsRequested(const RenderStyle::Strands &strands);
    void ridgeRequested(const RenderStyle::Ridge &ridge);
    void linesRequested(const RenderStyle::Lines &lines);
    void mirrorRequested(const RenderStyle::Mirror &mirror);
    void ringRequested(const RenderStyle::Ring &ring);
    void bloomRequested(const RenderStyle::Bloom &bloom);
    void resetRequested();

private:
    void showRelevantControls();

    Options m_options;

    // True while seeding, so setting a widget's value does not read back as
    // the user having asked for it.
    bool m_seeding = false;
    std::vector<std::function<void()>> m_seeders;

    QComboBox *m_look = nullptr;
    QSpinBox *m_scale = nullptr;
    QGroupBox *m_strandBox = nullptr;
    QGroupBox *m_ridgeBox = nullptr;
    QGroupBox *m_lineBox = nullptr;
    QGroupBox *m_mirrorBox = nullptr;
    QGroupBox *m_ringBox = nullptr;

    RenderStyle::Strands m_strands;
    RenderStyle::Ridge m_ridge;
    RenderStyle::Lines m_lines;
    RenderStyle::Mirror m_mirror;
    RenderStyle::Ring m_ring;
    RenderStyle::Bloom m_bloom;
};

} // namespace hyperwave
