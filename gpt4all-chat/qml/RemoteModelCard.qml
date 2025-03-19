import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: remoteModelCard
    property var provider // required
    property alias providerName: providerNameLabel.text
    property alias providerImage: myimage.source
    property alias providerDesc: providerDescLabel.text
    property bool providerUsesApiKey: true

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
            visible: !provider.isBuiltin

            MySettingsLabel {
                text: qsTr("Name")
                font.bold: true
                font.pixelSize: theme.fontSizeLarge
                color: theme.settingsTitleTextColor
            }

            MyTextField {
                id: nameField
                property bool initialized: false
                property bool ok: true
                Layout.fillWidth: true
                font.pixelSize: theme.fontSizeLarge
                wrapMode: Text.WrapAnywhere
                Component.onCompleted: {
                    text = provider.name;
                    initialized = true;
                }
                onTextChanged: {
                    if (!initialized) return;
                    ok = provider.setNameQml(text.trim());
                }
                placeholderText: qsTr("Provider Name")
                Accessible.role: Accessible.EditableText
                Accessible.name: placeholderText
            }
        }

        ColumnLayout {
            visible: "apiKey" in provider

            MySettingsLabel {
                text: qsTr("API Key")
                font.bold: true
                font.pixelSize: theme.fontSizeLarge
                color: theme.settingsTitleTextColor
            }

            MyTextField {
                id: apiKeyField
                property bool initialized: false
                property bool ok: false
                Layout.fillWidth: true
                font.pixelSize: theme.fontSizeLarge
                wrapMode: Text.WrapAnywhere
                echoMode: TextField.Password
                Component.onCompleted: {
                    if (parent.visible) {
                        text = provider.apiKey;
                        ok = text.trim() != "";
                    } else
                        ok = true;
                    initialized = true;
                }
                onTextChanged: {
                    if (!initialized) return;
                    console.log(`${provider} has an apiKey: ${('apiKey' in provider)},${typeof provider.apiKey},${provider.apiKey}`);
                    return;
                    ok = provider.setApiKeyQml(text.trim()) && text.trim() !== "";
                }
                placeholderText: qsTr("Provider API Key")
                Accessible.role: Accessible.EditableText
                Accessible.name: placeholderText
            }
        }

        ColumnLayout {
            visible: !provider.isBuiltin
            MySettingsLabel {
                text: qsTr("Base Url")
                font.bold: true
                font.pixelSize: theme.fontSizeLarge
                color: theme.settingsTitleTextColor
            }
            MyTextField {
                id: baseUrlField
                property bool initialized: false
                property bool ok: true
                Layout.fillWidth: true
                font.pixelSize: theme.fontSizeLarge
                wrapMode: Text.WrapAnywhere
                Component.onCompleted: {
                    text = provider.baseUrl;
                    initialized = true;
                }
                onTextChanged: {
                    if (!initialized) return;
                    ok = provider.setBaseUrlQml(text.trim()) && text.trim() !== "";
                }
                placeholderText: qsTr("Provider Base URL")
                Accessible.role: Accessible.EditableText
                Accessible.name: placeholderText
            }
        }

        ColumnLayout {
            visible: myModelList.count > 0

            MySettingsLabel {
                text: qsTr("Models")
                font.bold: true
                font.pixelSize: theme.fontSizeLarge
                color: theme.settingsTitleTextColor
            }

            RowLayout {
                spacing: 10

                MyComboBox {
                    Layout.fillWidth: true
                    id: myModelList
                    currentIndex: -1
                    property bool ready: nameField.ok && baseUrlField.ok && apiKeyField.ok
                    onReadyChanged: {
                        if (!ready) return;
                        provider.listModelsQml().then(modelList => {
                            if (modelList !== null) {
                                model = modelList;
                                currentIndex = -1;
                            }
                        });
                    }
                }
            }
        }

        MySettingsButton {
            id: installButton
            Layout.alignment: Qt.AlignRight
            text: qsTr("Install")
            font.pixelSize: theme.fontSizeLarge

            property string apiKeyText: apiKeyField.text.trim()
            property string modelNameText: myModelList.currentText.trim()

            enabled: nameField.ok && baseUrlField.ok && apiKeyField.ok && modelNameText !== ""

            onClicked: {
                Download.installCompatibleModel(
                            modelNameText,
                            apiKeyText,
                            baseUrlText,
                            );
                myModelList.currentIndex = -1;
            }
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Install")
            Accessible.description: qsTr("Install remote model")
        }
    }
}
