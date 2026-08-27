import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Deliberately tiny. This app does one job; a preferences window with twenty
// controls would be a larger surface than the feature it configures.
Window {
    id: root

    property alias gradientId: gradientBox.currentValue

    width: 380
    height: 240
    title: qsTr("hyperwave")

    signal permissionRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        Label {
            text: qsTr("Appearance")
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Gradient") }
            ComboBox {
                id: gradientBox
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "id"
                model: []
            }
        }

        CheckBox {
            id: compactBox
            text: qsTr("Compact strip")
            // Worth surfacing rather than deciding for people: on a notched
            // display a full-width item can get pushed into the overflow, and
            // the user knows how crowded their menu bar is better than we do.
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            Button {
                id: permissionButton
                text: qsTr("System Audio Access…")
                onClicked: root.permissionRequested()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: Qt.application.version
                opacity: 0.5
            }
        }
    }
}
