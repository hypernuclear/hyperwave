// The systems an app runs on, as the website's download buttons show them:
// Apple's mark and Windows' four panes, in one colour, Apple first.
//
// Shapes rather than images so the colour is a property: they sit on each
// product's own band colour. The Apple path is the website's (the same
// glyph hypershot shows); Windows is four squares. Both marks are their
// owners' trademarks, shown only to say which systems an app is for, and
// are not covered by this repository's licence (THIRD-PARTY-NOTICES.md).
import QtQuick
import QtQuick.Shapes

Row {
    id: root

    property color color: "#ffffff"
    /// The Apple mark's height; the Windows panes sit a little shorter, as
    /// they do in the comp.
    property real size: 11
    property bool mac: true
    property bool windows: true

    spacing: Math.round(size * 0.5)

    Item {
        id: apple
        visible: root.mac
        // The glyph's own box in the source path: 10.27 × 12.63.
        readonly property real k: root.size / 12.63
        width: 10.27 * k
        height: root.size
        anchors.verticalCenter: parent.verticalCenter
        Shape {
            x: -1133.35 * apple.k
            y: -70.90 * apple.k
            preferredRendererType: Shape.CurveRenderer
            transform: Scale { xScale: apple.k; yScale: apple.k }
            ShapePath {
                strokeColor: "transparent"
                fillColor: root.color
                PathSvg {
                    path: "M1140.35 72.91C1140.77 72.41 1141.00 71.79 1141.02 71.15C1141.02 71.08 1141.02 70.98 1141.00 70.90"
                        + "C1140.28 70.98 1139.61 71.32 1139.14 71.87C1138.72 72.34 1138.47 72.93 1138.45 73.58"
                        + "C1138.45 73.65 1138.45 73.72 1138.47 73.80C1138.52 73.80 1138.59 73.82 1138.64 73.82"
                        + "C1139.31 73.77 1139.93 73.43 1140.35 72.91"
                        + "M1143.30 75.21C1142.46 75.73 1141.94 76.62 1141.94 77.61C1141.94 78.73 1142.61 79.74 1143.62 80.16"
                        + "C1143.42 80.80 1143.13 81.40 1142.75 81.94C1142.21 82.71 1141.64 83.50 1140.80 83.50"
                        + "C1139.96 83.50 1139.71 83.01 1138.72 83.01C1137.75 83.01 1137.41 83.53 1136.61 83.53"
                        + "C1135.82 83.53 1135.28 82.81 1134.66 81.92C1133.84 80.68 1133.37 79.25 1133.35 77.74"
                        + "C1133.35 75.28 1134.93 73.97 1136.52 73.97C1137.36 73.97 1138.05 74.52 1138.57 74.52"
                        + "C1139.07 74.52 1139.86 73.95 1140.80 73.95C1141.79 73.92 1142.73 74.39 1143.30 75.21"
                }
            }
        }
    }

    // Four panes with a hairline between them.
    Grid {
        visible: root.windows
        readonly property real side: root.size * 0.78
        readonly property real gap: Math.max(1, side * 0.06)
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: root.size * 0.06
        columns: 2
        spacing: gap
        Repeater {
            model: 4
            Rectangle {
                width: (parent.side - parent.gap) / 2
                height: width
                color: root.color
                antialiasing: true
            }
        }
    }
}
