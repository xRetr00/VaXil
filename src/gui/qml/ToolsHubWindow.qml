import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "." as JarvisUi

Window {
    id: toolsWindow

    width: 920
    height: 860
    visible: false
    title: backend.assistantName + " Tools & Stores"
    color: "#050912"

    function syncFields() {
        webProviderField.text = backend.webSearchProvider
        mcpEnabledCheck.checked = backend.mcpEnabled
        mcpCatalogField.text = backend.mcpCatalogUrl
        mcpServerField.text = backend.mcpServerUrl
    }

    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }
        syncFields()
        backend.rescanTools()
    }

    Connections {
        target: backend

        function onSettingsChanged() {
            if (toolsWindow.visible) {
                toolsWindow.syncFields()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#091224" }
            GradientStop { position: 0.55; color: "#060b14" }
            GradientStop { position: 1.0; color: "#03060c" }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 22
        clip: true

        Column {
            width: parent.width
            spacing: 18

            Rectangle {
                width: parent.width
                implicitHeight: 210
                radius: 30
                color: "#101a2d"
                border.width: 1
                border.color: "#22395c"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 22

                    JarvisUi.OrbRenderer {
                        Layout.preferredWidth: 168
                        Layout.preferredHeight: 168
                        stateName: "PROCESSING"
                        time: 0
                        audioLevel: 0.06
                        speakingLevel: 0.15
                        distortion: 0.08
                        glow: 0.72
                        orbScale: 1.0
                        orbitalRotation: 0.0
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "Tools & Stores"
                            color: "#eef7ff"
                            font.pixelSize: 34
                            font.weight: Font.Medium
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Manage external runtimes, installed skills, MCP endpoints, and tool-facing settings without touching the main setup flow."
                            color: "#8da6c7"
                            font.pixelSize: 15
                        }

                        RowLayout {
                            spacing: 10

                            Button {
                                text: "Rescan Tools"
                                onClicked: backend.rescanTools()
                            }

                            Button {
                                text: "Open Tools Root"
                                onClicked: backend.openContainingDirectory(backend.toolsRoot)
                            }

                            Button {
                                text: "Open Skills Root"
                                onClicked: backend.openContainingDirectory(backend.skillsRoot)
                            }
                        }

                        Text {
                            text: backend.toolInstallStatus
                            color: "#b7c7dd"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            visible: text.length > 0
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: toolsColumn.implicitHeight + 28
                radius: 26
                color: "#0b1424"
                border.width: 1
                border.color: "#1d324f"

                ColumnLayout {
                    id: toolsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 18
                    spacing: 12

                    Text {
                        text: "Installed Tool Runtime"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "External binaries and model packages detected by the runtime."
                        color: "#8da6c7"
                        font.pixelSize: 13
                    }

                    Repeater {
                        model: backend.toolStatuses

                        Rectangle {
                            Layout.fillWidth: true
                            width: toolsColumn.width
                            implicitHeight: toolRow.implicitHeight + 18
                            radius: 18
                            color: "#101c31"
                            border.width: 1
                            border.color: "#22395c"

                            ColumnLayout {
                                id: toolRow
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: modelData.name
                                        color: "#eef7ff"
                                        font.pixelSize: 16
                                        font.weight: Font.Medium
                                    }

                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: modelData.installed ? "#1ecb6b" : "#f09a3e"
                                    }

                                    Text {
                                        text: modelData.installed ? "Installed" : "Missing"
                                        color: modelData.installed ? "#99e4b0" : "#ffd29a"
                                        font.pixelSize: 12
                                    }

                                    Item { Layout.fillWidth: true }

                                    Button {
                                        text: "Download"
                                        visible: modelData.downloadable
                                        onClicked: {
                                            if (modelData.category === "intent")
                                                backend.downloadModel(modelData.name)
                                            else
                                                backend.downloadTool(modelData.name)
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: "Category: " + modelData.category + (modelData.version ? "   Version: " + modelData.version : "")
                                    color: "#9ab0ca"
                                    font.pixelSize: 12
                                }

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WrapAnywhere
                                    text: modelData.path && modelData.path.length > 0 ? modelData.path : "No detected path yet."
                                    color: "#6f88a8"
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: skillsColumn.implicitHeight + 28
                radius: 26
                color: "#0b1424"
                border.width: 1
                border.color: "#1d324f"

                ColumnLayout {
                    id: skillsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 18
                    spacing: 12

                    Text {
                        text: "Skills Store"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Install declarative skills by URL or scaffold local ones."
                        color: "#8da6c7"
                        font.pixelSize: 13
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        TextField {
                            id: skillUrlField
                            Layout.fillWidth: true
                            placeholderText: "GitHub repo or zip URL"
                        }

                        Button {
                            text: "Install Skill"
                            onClicked: backend.installSkill(skillUrlField.text)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        TextField {
                            id: skillIdField
                            Layout.preferredWidth: 180
                            placeholderText: "skill-id"
                        }

                        TextField {
                            id: skillNameField
                            Layout.preferredWidth: 220
                            placeholderText: "Skill name"
                        }

                        TextField {
                            id: skillDescriptionField
                            Layout.fillWidth: true
                            placeholderText: "Short description"
                        }

                        Button {
                            text: "Create"
                            onClicked: backend.createSkill(skillIdField.text, skillNameField.text, skillDescriptionField.text)
                        }
                    }

                    Repeater {
                        model: backend.installedSkills

                        Rectangle {
                            width: skillsColumn.width
                            implicitHeight: skillEntry.implicitHeight + 16
                            radius: 16
                            color: "#101c31"
                            border.width: 1
                            border.color: "#22395c"

                            ColumnLayout {
                                id: skillEntry
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6

                                Text {
                                    text: modelData.name + " (" + modelData.id + ")"
                                    color: "#eef7ff"
                                    font.pixelSize: 15
                                    font.weight: Font.Medium
                                }

                                Text {
                                    text: (modelData.version && modelData.version.length > 0 ? "Version " + modelData.version + "   " : "") + modelData.description
                                    color: "#9ab0ca"
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: mcpColumn.implicitHeight + 28
                radius: 26
                color: "#0b1424"
                border.width: 1
                border.color: "#1d324f"

                ColumnLayout {
                    id: mcpColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 18
                    spacing: 12

                    Text {
                        text: "MCP Store"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Persist MCP connection settings now. Runtime discovery and install flows can build on this page later."
                        color: "#8da6c7"
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    CheckBox {
                        id: mcpEnabledCheck
                        text: "Enable MCP endpoints"
                    }

                    TextField {
                        id: mcpCatalogField
                        Layout.fillWidth: true
                        placeholderText: "MCP catalog URL"
                    }

                    TextField {
                        id: mcpServerField
                        Layout.fillWidth: true
                        placeholderText: "Default MCP server URL"
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: configColumn.implicitHeight + 28
                radius: 26
                color: "#0b1424"
                border.width: 1
                border.color: "#1d324f"

                ColumnLayout {
                    id: configColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 18
                    spacing: 12

                    Text {
                        text: "Tool Configuration"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Provider settings used by built-in tools."
                        color: "#8da6c7"
                        font.pixelSize: 13
                    }

                    TextField {
                        id: webProviderField
                        Layout.fillWidth: true
                        placeholderText: "Web search provider"
                    }

                    RowLayout {
                        spacing: 10

                        Button {
                            text: "Save Tool Settings"
                            onClicked: backend.saveToolsStoreSettings(
                                webProviderField.text,
                                mcpEnabledCheck.checked,
                                mcpCatalogField.text,
                                mcpServerField.text)
                        }

                        Button {
                            text: "Close"
                            onClicked: hide()
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: agentToolsColumn.implicitHeight + 28
                radius: 26
                color: "#0b1424"
                border.width: 1
                border.color: "#1d324f"

                ColumnLayout {
                    id: agentToolsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 18
                    spacing: 12

                    Text {
                        text: "Built-in Agent Tools"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Repeater {
                        model: backend.availableAgentTools

                        Rectangle {
                            width: agentToolsColumn.width
                            implicitHeight: agentToolEntry.implicitHeight + 16
                            radius: 16
                            color: "#101c31"
                            border.width: 1
                            border.color: "#22395c"

                            ColumnLayout {
                                id: agentToolEntry
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6

                                Text {
                                    text: modelData.name
                                    color: "#eef7ff"
                                    font.pixelSize: 15
                                    font.weight: Font.Medium
                                }

                                Text {
                                    text: modelData.description
                                    color: "#9ab0ca"
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }

                                TextArea {
                                    readOnly: true
                                    wrapMode: Text.WrapAnywhere
                                    text: modelData.parameters
                                    color: "#d7e6f6"
                                    selectionColor: "#355d8d"
                                    selectedTextColor: "#eef7ff"
                                    background: Rectangle {
                                        radius: 10
                                        color: "#091120"
                                        border.width: 1
                                        border.color: "#1d324f"
                                    }
                                    implicitHeight: 92
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
