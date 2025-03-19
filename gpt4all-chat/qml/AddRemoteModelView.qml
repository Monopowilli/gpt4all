import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.folderlistmodel
import Qt5Compat.GraphicalEffects

import llm
import chatlistmodel
import download
import modellist
import network
import gpt4all
import gpt4all.store_provider
import mysettings
import localdocs

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
        text: qsTr("Various remote model providers that use network resources for inference.")
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
            Repeater {
                model: ProviderListSort
                RemoteModelCard {
                    width: parent.childWidth
                    height: parent.childHeight
                    provider: modelData
                    providerName: provider.name
                    providerImage: "icon" in provider ? provider.icon : "qrc:/gpt4all/icons/antenna_3.svg"
                    providerDesc: {
                        if (!provider.isBuiltin) {
                            switch (provider.type) {
                            case ProviderStore.ProviderType.openai:
                                return qsTr("A custom OpenAI provider.");
                            case ProviderStore.ProviderType.ollama:
                                return qsTr("A custom Ollama provider.");
                            }
                        }
                        return ({
                            '{20f963dc-1f99-441e-ad80-f30a0a06bcac}': qsTr(
                                'Groq offers a high-performance AI inference engine designed for low-latency and ' +
                                'efficient processing. Optimized for real-time applications, Groq’s technology is ideal ' +
                                'for users who need fast responses from open large language models and other AI ' +
                                'workloads.<br><br>Get your API key: ' +
                                '<a href="https://console.groq.com/keys">https://groq.com/</a>'
                            ),
                            '{6f874c3a-f1ad-47f7-9129-755c5477146c}': qsTr(
                                'OpenAI provides access to advanced AI models, including GPT-4 supporting a wide range ' +
                                'of applications, from conversational AI to content generation and code completion.' +
                                '<br><br>Get your API key: ' +
                                '<a href="https://platform.openai.com/signup">https://openai.com/</a>'
                            ),
                            '{7ae617b3-c0b2-4d2c-9ff2-bc3f049494cc}': qsTr(
                                'Mistral AI specializes in efficient, open-weight language models optimized for various ' +
                                'natural language processing tasks. Their models are designed for flexibility and ' +
                                'performance, making them a solid option for applications requiring scalable AI ' +
                                'solutions.<br><br>Get your API key: <a href="https://mistral.ai/">https://mistral.ai/</a>'
                            ),
                        })[provider.id.toString()];
                    }
                }
            }
        }
    }
}
