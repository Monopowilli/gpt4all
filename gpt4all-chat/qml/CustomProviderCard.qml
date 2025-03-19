import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import gpt4all.ProviderRegistry

Rectangle {
    id: root
    required property bool withApiKey
    required property var createProvider
    property alias providerName: providerNameLabel.text
    property alias providerImage: myimage.source
    property alias providerDesc: providerDescLabel.text

    color: theme.conversationBackground
    radius: 10
    border.width: 1
    border.color: theme.controlBorder
    implicitHeight: topColumn.height + bottomColumn.height + 33 * theme.fontScale

    ColumnLayout {
        id: topColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        spacing: 15 * theme.fontScale
        RowLayout {
            Layout.alignment: Qt.AlignTop
            spacing: 10
            Item {
                Layout.preferredWidth: 27 * theme.fontScale
                Layout.preferredHeight: 27 * theme.fontScale
                Layout.alignment: Qt.AlignLeft

                Image {
                    id: myimage
                    anchors.centerIn: parent
                    sourceSize.width: parent.width
                    sourceSize.height: parent.height
                    mipmap: true
                    fillMode: Image.PreserveAspectFit
                }
            }

            Label {
                id: providerNameLabel
                color: theme.textColor
                font.pixelSize: theme.fontSizeBanner
            }
        }

        Label {
            id: providerDescLabel
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.settingsTitleTextColor
            font.pixelSize: theme.fontSizeLarge
            onLinkActivated: function(link) { Qt.openUrlExternally(link); }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton // pass clicks to parent
                cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
            }
        }
    }

    ColumnLayout {
        id: bottomColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        spacing: 30

        ColumnLayout {
            MySettingsLabel {
                text: qsTr("Name")
                font.bold: true
                font.pixelSize: theme.fontSizeLarge
                color: theme.settingsTitleTextColor
            }
            MyTextField {
                id: nameField
                Layout.fillWidth: true
                font.pixelSize: theme.fontSizeLarge
                wrapMode: Text.WrapAnywhere
                placeholderText: qsTr("Provider Name")
                Accessible.role: Accessible.EditableText
                Accessible.name: placeholderText
            }
        }

        ColumnLayout {
            MySettingsLabel {
                text: qsTr("Base URL")
                font.bold: true
                font.pixelSize: theme.fontSizeLarge
                color: theme.settingsTitleTextColor
            }
            MyTextField {
                id: baseUrlField
                property bool ok: text.trim() !== ""
                Layout.fillWidth: true
                font.pixelSize: theme.fontSizeLarge
                wrapMode: Text.WrapAnywhere
                placeholderText: qsTr("Provider Base URL")
                Accessible.role: Accessible.EditableText
                Accessible.name: placeholderText
            }
        }

        ColumnLayout {
            visible: withApiKey

            MySettingsLabel {
                text: qsTr("API Key")
                font.bold: true
                font.pixelSize: theme.fontSizeLarge
                color: theme.settingsTitleTextColor
            }

            MyTextField {
                id: apiKeyField
                Layout.fillWidth: true
                font.pixelSize: theme.fontSizeLarge
                wrapMode: Text.WrapAnywhere
                echoMode: TextField.Password
                placeholderText: qsTr("Provider API Key")
                Accessible.role: Accessible.EditableText
                Accessible.name: placeholderText
            }
        }

        ColumnLayout {
            MySettingsLabel {
                text: qsTr("Status")
                font.bold: true
                font.pixelSize: theme.fontSizeLarge
                color: theme.settingsTitleTextColor
            }

            RowLayout {
                spacing: 10

                MyTextField {
                    id: statusText
                    property var provider: null // owns the new provider
                    enabled: false
                    Layout.fillWidth: true
                    font.pixelSize: theme.fontSizeLarge
                    property var inputs: ({
                        name    : nameField   .text.trim(),
                        baseUrl : baseUrlField.text.trim(),
                        apiKey  : apiKeyField .text.trim(),
                    })
                    function update() {
                        provider = null;
                        text = qsTr("...");
                        if (inputs.name === "" || inputs.baseUrl === "")
                            return;
                        const args = [inputs.name, inputs.baseUrl];
                        if (withApiKey)
                            args.push(inputs.apiKey);
                        let p = createProvider(...args);
                        if (p !== null)
                            p.get().statusQml().then(status => {
                                if (status !== null) {
                                    if (status.ok) { provider = p; }
                                    text = status.detail;
                                }
                            });
                    }
                    Component.onCompleted: update()
                    onInputsChanged: update()
                }
            }
        }

        MySettingsButton {
            id: installButton
            Layout.alignment: Qt.AlignRight
            text: qsTr("Install")
            font.pixelSize: theme.fontSizeLarge
            enabled: statusText.provider !== null
            onClicked: ProviderRegistry.addQml(statusText.provider)
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Install")
            Accessible.description: qsTr("Install custom provider")
        }
    }
}
