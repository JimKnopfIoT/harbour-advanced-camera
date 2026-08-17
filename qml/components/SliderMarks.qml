import QtQuick 2.0
import Sailfish.Silica 1.0

// Tick marks under a Silica Slider's groove: grey = default, green =
// recommended. Place as a child of the Slider.
Item {
    id: marks

    property real defaultValue: NaN
    property real recommendedValue: NaN

    readonly property Item slider: parent
    readonly property color defaultColor: Theme.rgba(Theme.secondaryColor, 0.7)
    readonly property color recommendedColor: "#3fc46a"

    anchors.fill: parent

    function xFor(v) {
        return slider.leftMargin + (v - slider.minimumValue)
                / (slider.maximumValue - slider.minimumValue) * slider._grooveWidth
    }

    Repeater {
        model: [{ "v": defaultValue, "c": defaultColor }, { "v": recommendedValue, "c": recommendedColor }]
        delegate: Rectangle {
            visible: !isNaN(modelData.v)
            width: Theme.dp(2)
            height: Theme.paddingSmall
            radius: width / 2
            color: modelData.c
            x: marks.xFor(modelData.v) - width / 2
            y: slider._backgroundItem.y + slider._backgroundItem.height + Theme.paddingSmall / 2
        }
    }
}
