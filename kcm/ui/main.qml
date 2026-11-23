// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: root

    readonly property var settings: kcm.settings
    readonly property bool isCustom: settings.profile === "custom"
    readonly property var customKeys: ["darkNits", "dimNits", "indoorNits", "brightNits", "daylightNits"]
    readonly property var pointNames: [i18n("Dark room"), i18n("Dim room"), i18n("Indoor lighting"), i18n("Bright room"), i18n("Daylight")]
    readonly property var profiles: [
        { value: "dimmer", text: i18n("Dimmer") },
        { value: "balanced", text: i18n("Balanced") },
        { value: "brighter", text: i18n("Brighter") },
        { value: "custom", text: i18n("Custom") }
    ]
    readonly property var responses: [
        { value: "calm", text: i18n("Calm"), help: i18n("Only follows large, lasting changes in the light, and changes slowly.") },
        { value: "balanced", text: i18n("Balanced"), help: i18n("Follows the room's lighting, but not passing shadows.") },
        { value: "quick", text: i18n("Quick"), help: i18n("Follows small changes in the light within a second or two.") }
    ]

    // Sliders work on the perceptual scale the service uses: equal steps look equally large.
    function toSlider(nits) {
        return Math.log(Math.max(1, nits)) / Math.log(kcm.maxNits);
    }
    function fromSlider(p) {
        return Math.round(Math.exp(p * Math.log(kcm.maxNits)));
    }

    header: Kirigami.InlineMessage {
        position: Kirigami.InlineMessage.Position.Header
        type: Kirigami.MessageType.Warning
        visible: root.settings.enabled && !kcm.running
        text: i18n("The automatic brightness service is not running. Changes here take effect once it starts.")
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Screen brightness:")
                text: i18n("Adjust to the light in the room")
                checked: root.settings.enabled
                onToggled: root.settings.enabled = checked

                KCM.SettingStateBinding {
                    configObject: root.settings
                    settingName: "Enabled"
                }
            }

            QQC2.ComboBox {
                id: profileBox
                Kirigami.FormData.label: i18n("Brightness:")
                enabled: root.settings.enabled
                model: root.profiles
                textRole: "text"
                valueRole: "value"
                currentIndex: indexOfValue(root.settings.profile)
                onActivated: {
                    if (currentValue === "custom" && !root.isCustom) {
                        // start from what the current profile gives, adjustments included
                        kcm.copyToCustom(root.settings.profile);
                    }
                    root.settings.profile = currentValue;
                }

                KCM.SettingStateBinding {
                    configObject: root.settings
                    settingName: "Profile"
                }
            }

            Repeater {
                model: root.isCustom ? root.customKeys.length : 0

                RowLayout {
                    required property int index
                    Kirigami.FormData.label: i18nc("@label point of the custom curve: name, lux", "%1 (%2 lx):", root.pointNames[index], kcm.pointLux[index])
                    enabled: root.settings.enabled

                    QQC2.Slider {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                        from: 0
                        to: 1
                        value: root.toSlider(root.settings[root.customKeys[parent.index]])
                        onMoved: root.settings[root.customKeys[parent.index]] = root.fromSlider(value)
                    }
                    QQC2.Label {
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 4
                        text: i18nc("@label brightness in nits", "%1 nits", Math.round(root.settings[root.customKeys[parent.index]]))
                    }
                }
            }

            QQC2.ComboBox {
                id: responseBox
                Kirigami.FormData.label: i18n("Response:")
                enabled: root.settings.enabled
                model: root.responses
                textRole: "text"
                valueRole: "value"
                currentIndex: indexOfValue(root.settings.response)
                onActivated: root.settings.response = currentValue

                KCM.SettingStateBinding {
                    configObject: root.settings
                    settingName: "Response"
                }
            }

            QQC2.Label {
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                visible: responseBox.currentIndex >= 0
                text: responseBox.currentIndex >= 0 ? root.responses[responseBox.currentIndex].help : ""
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                opacity: 0.8
            }

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Adjustments:")
                enabled: root.settings.enabled
                text: i18n("Learn from the brightness I choose")
                checked: root.settings.learn
                onToggled: root.settings.learn = checked

                KCM.SettingStateBinding {
                    configObject: root.settings
                    settingName: "Learn"
                }
            }

            QQC2.Label {
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                text: i18n("When you change the brightness with the keys or the slider, the curve moves to give that brightness in similar light from then on.")
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                opacity: 0.8
            }

            QQC2.Button {
                text: i18n("Forget Learned Adjustments")
                icon.name: "edit-reset"
                enabled: kcm.hasLearned
                onClicked: kcm.forgetLearned()
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        CurveView {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 14
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            profile: root.settings.profile
            enabled: root.settings.enabled
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            visible: kcm.running && kcm.status.ambient !== undefined
            text: i18nc("@info live readings", "Room: %1 lx · Screen: %2 nits",
                        Math.round(kcm.status.ambient ?? 0), Math.round(kcm.status.nits ?? 0))
            opacity: 0.8
        }
    }
}
