import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

// The brand moment: shown at launch, dismissed by clicking it or by waiting.
//
// Frameless and borderless on purpose -- a title bar and a close button around a
// full-bleed illustration reads as a document window, which is the opposite of
// what this is for. That does mean the artwork carries the whole thing, so
// everything below is about keeping the marks legible on top of it without
// covering it up.
Window {
    id: root

    /// Milliseconds on screen before it leaves by itself. A brand moment that
    /// outstays its welcome stops being one.
    ///
    /// Zero means it stays until clicked. That is the right behaviour when the
    /// user asked to see it: something opened on request should not take itself
    /// away again while they are still looking at it.
    property int dwellMs: 4000

    /// The brand blue, and the ink that sits on it. White rather than a dark
    /// ink: #267ac8 is a mid-tone, and dark text on it fails the contrast that
    /// a bright ground would have carried.
    readonly property color brandBlue: "#267ac8"
    readonly property color brandInk: "#ffffff"

    // Bundled, not requested by name: the system UI face is what a missing font
    // quietly becomes, and it looks close enough to Inter that nobody would
    // notice the panel had gone generic.
    FontLoader { id: inter; source: "qrc:/icons/Inter-SemiBold.ttf" }

    // Sized from the artwork's own aspect so it is never letterboxed or
    // cropped, at roughly half its pixel size -- the source is 1448x1086, so
    // this lands near 1:1 on a Retina display and stays sharp.
    width: 640
    height: 480
    flags: Qt.SplashScreen | Qt.FramelessWindowHint
    color: "transparent"
    visible: false

    // The rounded corner, as a MASK rather than a clip.
    //
    // Rectangle.radius plus clip: true does not do it: clipping in Qt Quick is
    // rectangular whatever shape the clipper is drawn as, so the artwork runs
    // straight into all four square corners. Masking the whole composed surface
    // is the only thing that actually cuts it.
    Rectangle {
        id: cornerMask
        anchors.fill: parent
        radius: 14
        color: "black"
        visible: false
        layer.enabled: true
    }

    Item {
        id: body
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: cornerMask
            // Without a threshold the mask's antialiased edge is treated as
            // partial coverage twice over and the corner comes out soft and
            // grey.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        Rectangle {
            anchors.fill: parent
            color: "#0b0b0d"   // shows for the one frame before decode
        }

        Image {
            anchors.fill: parent
            source: "qrc:/icons/splash.jpg"
            fillMode: Image.PreserveAspectCrop
            asynchronous: false   // a splash that fades in blank is worse
            smooth: true
        }

        // --- the product panel, TOP right ------------------------------
        //
        // hyperbin puts this at the bottom; here it sits at the top, which is
        // the one structural difference between the two. The trapezoid is
        // mirrored vertically to match: its short edge stays against the screen
        // edge it is attached to, so the diagonal still leans away from the
        // corner rather than into it.
        Item {
            id: panel
            width: parent.width * 0.40
            height: parent.height * 0.155
            anchors.right: parent.right
            anchors.rightMargin: parent.width * 0.00484
            anchors.top: parent.top
            anchors.topMargin: parent.height * 0.057

            // A plain rectangle. The slanted edge was carried over from
            // hyperbin, where it runs along the diagonal of that product's own
            // mark; there is no diagonal here for it to agree with, so it read
            // as decoration for its own sake.
            Rectangle {
                anchors.fill: parent
                color: root.brandBlue
            }

            // The mark and the name on one line, then the version under it.
            //
            // Set from the RIGHT edge, not centred: the slant eats into the
            // left, so a centred lockup would drift right as the panel scaled.
            // The gap that has to hold is the one to the straight edge.
            Row {
                id: lockup
                spacing: panel.height * 0.12
                anchors.right: parent.right
                anchors.rightMargin: panel.width * 0.085
                anchors.top: parent.top
                anchors.topMargin: panel.height * 0.155

                // Both marks are Shapes whose colour is a property, so they
                // are simply asked for the ink. The Image version needed
                // MultiEffect to fight the artwork's baked-in black, and
                // MultiEffect cannot lighten: colorization multiplies, and
                // black multiplied by white is still black.
                HyperwaveMark {
                    color: root.brandInk
                    height: panel.height * 0.58
                    width: height
                    anchors.verticalCenter: parent.verticalCenter
                }

                HyperwaveLogotype {
                    color: root.brandInk
                    // Sized by HEIGHT, with the width following the artwork's
                    // own aspect. Driving it from the width instead decides a
                    // height nobody checked: this logotype is nearly four times
                    // wider than it is tall.
                    height: panel.height * 0.52
                    width: height * (viewWidth / viewHeight)
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Right-aligned under the name, and close under it. Reads the app's
            // real version rather than carrying a copy: two places to change a
            // version number is one place to forget.
            Text {
                text: "v" + Qt.application.version
                // Plain white at full opacity. Dimming it was meant to rank it
                // below the name; at this size it just looked like it had not
                // finished loading.
                color: root.brandInk
                font.family: inter.name
                font.weight: Font.DemiBold
                font.pixelSize: panel.height * 0.155
                // Right-aligned to the logotype's own edge -- it is the
                // last item in the row, so lockup.right is its right -- and
                // tucked close under it rather than floating in the space
                // below.
                anchors.right: lockup.right
                anchors.top: lockup.bottom
                anchors.topMargin: panel.height * 0.01
            }
        }

        // --- the house mark, lower left --------------------------------
        //
        // On a dark glow rather than a scrim. A black shadow with no offset and
        // a wide blur darkens only what is immediately behind the letters,
        // which is what a multiply would do, and it leaves the rest of the
        // illustration alone. A rectangular scrim does not: it flattens a whole
        // band of the artwork to make room for two words.
        HypernuclearWordmark {
            color: root.brandBlue
            width: parent.width * 0.158
            height: width * (viewHeight / viewWidth)
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: parent.width * 0.060
            anchors.bottomMargin: parent.height * 0.085
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#000000"
                shadowBlur: 1.0
                shadowOpacity: 0.85
                shadowScale: 1.06
                shadowHorizontalOffset: 0
                shadowVerticalOffset: 0
            }
        }

        // The brand edge, drawn last so it sits over the artwork.
        //
        // A border on this Rectangle rather than on the window: the window is
        // transparent and frameless, so there is no frame to colour. Inside the
        // masked item, so the rounded corner cuts the border along with
        // everything else -- a border drawn outside the mask would square off
        // the corners it is meant to trace.
        //
        // radius one pixel under the mask's own, because a stroke is centred on
        // the path: at equal radii its outer half falls where the mask has
        // already faded out, and the corner reads thinner than the straight
        // edges.
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: cornerMask.radius - 1
            border.width: 5
            border.color: root.brandBlue
            antialiasing: true
        }
    }

    // Anywhere, not a button. There is no close control on a frameless window,
    // so the whole surface has to be the way out.
    //
    // Outside the masked surface: a mask makes its input a texture, and input
    // handling inside one still works but there is no reason to pay for it.
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.dismiss()
    }

    // Fading rather than vanishing, both ways. A splash that pops in and pops
    // out reads as a glitch on something this brief.
    opacity: 0
    Behavior on opacity { NumberAnimation { duration: 220 } }

    function appear() {
        visible = true;
        opacity = 1;
        if (dwellMs > 0)
            dwell.restart();
        else
            dwell.stop();
    }
    function dismiss() {
        dwell.stop();
        opacity = 0;
        gone.restart();
    }

    Timer { id: dwell; interval: root.dwellMs; onTriggered: root.dismiss() }
    // Outlasts the fade, so the window is not torn down mid-animation.
    Timer { id: gone; interval: 260; onTriggered: root.close() }
}
