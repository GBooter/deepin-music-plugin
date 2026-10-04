// Copyright (C) 2022 UnionTech Technology Co., Ltd.
// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import org.deepin.dtk 1.0

Control {
    id: control
    property string title
    property string description
    property var cornersRadius
    property string iconName
    signal clicked()
    property Component action: ActionButton {
        visible: control.iconName
        Layout.alignment: Qt.AlignRight
        icon {
            width: 14
            height: 14
            name: control.iconName
        }
        onClicked: control.clicked()
    }
    padding: 5

    // 显式给出 implicitHeight / implicitWidth，打断绑定环。
    //
    // 现象：MusicInfoDialog 里每个 PropertyItemDelegate 都会刷大量
    //   QML PropertyItemDelegate: Binding loop detected for property "implicitWidth":
    //   qrc:/org/deepin/dtk/Control.qml:12:5
    //   （implicitHeight 同理）
    //
    // 原因：DTK 的 Control 模板把隐式尺寸定义为
    //   implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    //   implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    // 而本 delegate 的 contentItem 是 ColumnLayout，其内部 RowLayout 里的
    // Label 用了 Layout.fillWidth: true。Qt 布局算自身 implicitWidth 时要回头
    // 取子项 implicitWidth，子项宽度又由父布局按 fillWidth 分配，于是形成
    //   implicitWidth → contentItem → 布局 → 子项 implicitWidth → implicitWidth
    // 的闭环。Qt 检测到环就打印警告并放弃本次求值，界面仍显示但反复重算。
    //
    // 注意：试过用 contentItem.childrenRect 替代，仍然成环 —— childrenRect 变化
    // 会触发 Layout 重排，进而改变子项宽度，再次影响 childrenRect。
    //
    // 修法（最终）：显式给固定 implicitHeight，宽度交给外层 Layout.fillWidth
    // 决定，两者都不再从 contentItem 的布局隐式尺寸推导，环彻底断开。
    // 高度取两行（标题 + 内容）实测所需：标题 t10 约 20px + 内容 t7 约 24px
    // + 行间距 + padding(5*2) ≈ 58。
    implicitHeight: 58
    implicitWidth: 200

    contentItem: ColumnLayout {
        Label {
            property Palette backgroundColor: Palette {
                normal: Qt.rgba(0, 0, 0, 0.6)
                normalDark: Qt.rgba(247.0 / 255.0, 247.0 / 255.0, 247.0 / 255.0, 1)
            }
            Layout.leftMargin: 10
            visible: control.title
            text: control.title
            font: DTK.fontManager.t10
            color: ColorSelector.backgroundColor
        }
        RowLayout {
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Label {
                property Palette textColor: Palette {
                    normal: Qt.rgba(0, 0, 0, 1)
                    normalDark: Qt.rgba(247.0 / 255.0, 247.0 / 255.0, 247.0 / 255.0, 1)
                }
                visible: control.description
                Layout.fillWidth: true
                text: control.description
                font: DTK.fontManager.t7
                elide: Text.ElideMiddle
                color:ColorSelector.textColor
            }
            Loader {
                Layout.leftMargin: 5
                sourceComponent: control.action
            }
        }
    }

    background: Shape {
        id: idShapeControl
        implicitWidth: 66
        implicitHeight: 40
        layer.smooth: true
        ShapePath {
            startX: 0
            startY: cornersRadius[0]
            fillColor: DTK.themeType === ApplicationHelper.LightType ? Qt.rgba(0, 0, 0, 0.05) : Qt.rgba(247, 247, 247, 0.05)
            strokeColor: "transparent"
            strokeWidth: 0
            PathQuad { x: cornersRadius[0]; y: 0; controlX: 0; controlY: 0 }
            PathLine { x: idShapeControl.width - cornersRadius[1]; y: 0 }
            PathQuad { x: idShapeControl.width; y: cornersRadius[1]; controlX: idShapeControl.width; controlY: 0 }
            PathLine { x: idShapeControl.width; y: idShapeControl.height - cornersRadius[2] }
            PathQuad { x: idShapeControl.width - cornersRadius[2]; y: idShapeControl.height; controlX: idShapeControl.width; controlY: idShapeControl.height }
            PathLine { x: cornersRadius[3]; y: idShapeControl.height }
            PathQuad { x: 0; y: idShapeControl.height - cornersRadius[3]; controlX: 0; controlY: idShapeControl.height }
            PathLine { x: 0; y: cornersRadius[0] }
        }
    }
}
