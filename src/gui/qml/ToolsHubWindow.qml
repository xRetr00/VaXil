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
    title: settingsVm.assistantName + " Tools & Stores"
    color: "#04070d"

    function syncFields() {
        webProviderField.text = settingsVm.webSearchProvider
        mcpEnabledCheck.checked = settingsVm.mcpEnabled
        mcpCatalogField.text = settingsVm.mcpCatalogUrl
        mcpServerField.text = settingsVm.mcpServerUrl
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
        settingsVm.rescanTools()
    }

    Connections {
        target: settingsVm

        function onSettingsChanged() {
            if (toolsWindow.visible) {
                toolsWindow.syncFields()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#18222e" }
            GradientStop { position: 0.35; color: "#0f151d" }
            GradientStop { position: 0.72; color: "#070b11" }
            GradientStop { position: 1.0; color: "#04070c" }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: -150
        width: 520
        height: 520
        radius: 260
        color: "#4bddf3ff"
        opacity: 0.10
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 22
        clip: true

        Column {
            width: parent.width
            spacing: 18

            JarvisUi.VisionGlassPanel {
                width: parent.width
                implicitHeight: 210
                radius: 30
                panelColor: "#16192022"
                innerColor: "#1d212a30"
                outlineColor: "#24ffffff"
                highlightColor: "#18ffffff"
                shadowOpacity: 0.26

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
                            color: "#f3f7ff"
                            font.pixelSize: 34
                            font.weight: Font.Medium
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Manage external runtimes, installed skills, MCP endpoints, and tool-facing settings without touching the main setup flow."
                            color: "#ced9e8"
                            font.pixelSize: 15
                        }

                        RowLayout {
                            spacing: 10

                            Button {
                                text: "Rescan Tools"
                                onClicked: settingsVm.rescanTools()
                            }

                            Button {
                                text: "Open Tools Root"
                                onClicked: settingsVm.openContainingDirectory(settingsVm.toolsRoot)
                            }

                            Button {
                                text: "Open Skills Root"
                                onClicked: settingsVm.openContainingDirectory(settingsVm.skillsRoot)
                            }
                        }

                        Text {
                            text: settingsVm.toolInstallStatus
                            color: "#dde7f5"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            visible: text.length > 0
                        }
                    }
                }
            }

            JarvisUi.VisionGlassPanel {
                width: parent.width
                implicitHeight: toolsColumn.implicitHeight + 28
                radius: 26
                panelColor: "#16192022"
                innerColor: "#1d212b2f"
                outlineColor: "#20ffffff"
                highlightColor: "#16ffffff"
                shadowOpacity: 0.22

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
                        text: settingsVm.supportsAutoToolInstall
                              ? "External binaries and model packages detected by the runtime."
                              : "External binaries and model packages detected by the runtime. Linux uses manual tool/model setup in this release."
                        color: "#8da6c7"
                        font.pixelSize: 13
                    }

                    Repeater {
                        model: settingsVm.toolStatuses

                        JarvisUi.VisionGlassPanel {
                            Layout.fillWidth: true
                            width: toolsColumn.width
                            implicitHeight: toolRow.implicitHeight + 18
                            radius: 18
                            panelColor: "#171c2326"
                            innerColor: "#1d232c32"
                            outlineColor: "#18ffffff"
                            highlightColor: "#10ffffff"
                            shadowOpacity: 0.14

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
                                                settingsVm.downloadModel(modelData.name)
                                            else
                                                settingsVm.downloadTool(modelData.name)
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

            JarvisUi.VisionGlassPanel {
                width: parent.width
                implicitHeight: skillsColumn.implicitHeight + 28
                radius: 26
                panelColor: "#16192022"
                innerColor: "#1d212b2f"
                outlineColor: "#20ffffff"
                highlightColor: "#16ffffff"
                shadowOpacity: 0.22

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
                            onClicked: settingsVm.installSkill(skillUrlField.text)
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
                            onClicked: settingsVm.createSkill(skillIdField.text, skillNameField.text, skillDescriptionField.text)
                        }
                    }

                    Repeater {
                        model: settingsVm.installedSkills

                        JarvisUi.VisionGlassPanel {
                            width: skillsColumn.width
                            implicitHeight: skillEntry.implicitHeight + 16
                            radius: 16
                            panelColor: "#171c2326"
                            innerColor: "#1d232c32"
                            outlineColor: "#18ffffff"
                            highlightColor: "#10ffffff"
                            shadowOpacity: 0.14

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

            JarvisUi.VisionGlassPanel {
                width: parent.width
                implicitHeight: mcpColumn.implicitHeight + 28
                radius: 26
                panelColor: "#16192022"
                innerColor: "#1d212b2f"
                outlineColor: "#20ffffff"
                highlightColor: "#16ffffff"
                shadowOpacity: 0.22

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
                        text: "Enable MCP settings and install curated MCP servers in one click."
                        color: "#8da6c7"
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        text: "Quick MCP Downloads"
                        color: "#d9e9fa"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }

                    Repeater {
                        model: settingsVm.mcpQuickServers

                        JarvisUi.VisionGlassPanel {
                            Layout.fillWidth: true
                            width: mcpColumn.width
                            implicitHeight: mcpPresetLayout.implicitHeight + 16
                            radius: 14
                            panelColor: "#171c2326"
                            innerColor: "#1d232c32"
                            outlineColor: "#18ffffff"
                            highlightColor: "#10ffffff"
                            shadowOpacity: 0.14

                            RowLayout {
                                id: mcpPresetLayout
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: modelData.name
                                        color: "#eef7ff"
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                    }

                                    Text {
                                        text: modelData.description
                                        color: "#9ab0ca"
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: modelData.package + "@" + modelData.version
                                        color: "#6f88a8"
                                        font.pixelSize: 11
                                    }

                                    Text {
                                        text: "Status: " + modelData.statusLabel
                                        color: modelData.status === "working"
                                            ? "#77e1b5"
                                            : (modelData.status === "installed" ? "#a7d0ff" : "#ffb09a")
                                        font.pixelSize: 11
                                        font.weight: Font.Medium
                                    }

                                    Text {
                                        visible: modelData.installedVersion && modelData.installedVersion.length > 0
                                        text: "Installed version: " + modelData.installedVersion
                                        color: "#9ab0ca"
                                        font.pixelSize: 11
                                    }
                                }

                                Button {
                                    text: modelData.installed ? "Reinstall" : "Install"
                                    enabled: modelData.canInstall
                                    onClicked: settingsVm.installMcpQuickServer(modelData.id)
                                }
                            }
                        }
                    }

                    Text {
                        visible: settingsVm.mcpQuickServers.length > 0 && !settingsVm.mcpQuickServers[0].npmAvailable
                        text: "npm not detected. Install Node.js LTS to enable MCP downloads."
                        color: "#ffb09a"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        font.pixelSize: 12
                    }

                    Text {
                        text: "Custom MCP Package"
                        color: "#d9e9fa"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        TextField {
                            id: mcpPackageField
                            Layout.fillWidth: true
                            placeholderText: "npm package spec (e.g. @scope/server@latest)"
                        }

                        TextField {
                            id: mcpServerIdField
                            Layout.preferredWidth: 210
                            placeholderText: "Server id hint (optional)"
                        }

                        Button {
                            text: "Install"
                            onClicked: settingsVm.installMcpPackage(mcpPackageField.text, mcpServerIdField.text)
                        }
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

            JarvisUi.VisionGlassPanel {
                width: parent.width
                implicitHeight: configColumn.implicitHeight + 28
                radius: 26
                panelColor: "#16192022"
                innerColor: "#1d212b2f"
                outlineColor: "#20ffffff"
                highlightColor: "#16ffffff"
                shadowOpacity: 0.22

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
                            onClicked: settingsVm.saveToolsStoreSettings(
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

            JarvisUi.VisionGlassPanel {
                width: parent.width
                implicitHeight: agentToolsColumn.implicitHeight + 28
                radius: 26
                panelColor: "#16192022"
                innerColor: "#1d212b2f"
                outlineColor: "#20ffffff"
                highlightColor: "#16ffffff"
                shadowOpacity: 0.22

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
                        model: settingsVm.availableAgentTools

                        JarvisUi.VisionGlassPanel {
                            width: agentToolsColumn.width
                            implicitHeight: agentToolEntry.implicitHeight + 16
                            radius: 16
                            panelColor: "#171c2326"
                            innerColor: "#1d232c32"
                            outlineColor: "#18ffffff"
                            highlightColor: "#10ffffff"
                            shadowOpacity: 0.14

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
                                        color: "#171b2326"
                                        border.width: 1
                                        border.color: "#20ffffff"
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
