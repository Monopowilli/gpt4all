import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    spacing: 5

    Label {
        Layout.topMargin: 0
        Layout.bottomMargin: 25
        Layout.rightMargin: 150 * theme.fontScale
        Layout.alignment: Qt.AlignTop
        Layout.fillWidth: true
        verticalAlignment: Text.AlignTop
        text: qsTr("Add custom model providers here.")
        font.pixelSize: theme.fontSizeLarger
        color: theme.textColor
        wrapMode: Text.WordWrap
    }

    ScrollView {
        id: scrollView
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentWidth: availableWidth
        clip: true
        Flow {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 20
            bottomPadding: 20
            property int childWidth: 330 * theme.fontScale
            property int childHeight: 400 + 166 * theme.fontScale
            CustomProviderCard {
                width: parent.childWidth
                height: parent.childHeight
                withApiKey: true
                createProvider: QmlFunctions.newCustomOpenaiProvider
                providerName: qsTr("OpenAI")
                providerImage: "qrc:/gpt4all/icons/antenna_3.svg"
                providerDesc: qsTr("Configure a custom OpenAI provider.")
            }
            CustomProviderCard {
                width: parent.childWidth
                height: parent.childHeight
                withApiKey: false
                createProvider: QmlFunctions.newCustomOllamaProvider
                providerName: qsTr("Ollama")
                providerImage: "qrc:/gpt4all/icons/antenna_3.svg"
                providerDesc: qsTr("Configure a custom Ollama provider.")
            }
        }
    }
}
