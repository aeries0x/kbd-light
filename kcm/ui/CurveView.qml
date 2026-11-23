// SPDX-License-Identifier: MIT
import QtQuick
import org.kde.kirigami as Kirigami

// The brightness curve: room light (lux, log scale) across, screen brightness up.
// The solid line is what the screen follows, the dashed one the profile before
// learned adjustments, the dot where the room and the screen are now.
Item {
    id: view

    property string profile
    readonly property real maxLux: 5000
    readonly property real maxNits: kcm.maxNits
    readonly property int labelWidth: Kirigami.Units.gridUnit * 3
    readonly property int labelHeight: Kirigami.Units.gridUnit * 1.5

    opacity: enabled ? 1 : 0.5

    function faded(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha);
    }
    function xOf(lux, width) {
        return labelWidth + (width - labelWidth) * Math.log10(1 + Math.max(0, lux)) / Math.log10(1 + maxLux);
    }
    function yOf(nits, height) {
        const p = Math.log(Math.max(1, Math.min(nits, maxNits))) / Math.log(maxNits);
        return (height - labelHeight) * (1 - p);
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            const ctx = getContext("2d");
            const w = width, h = height;
            ctx.reset();
            ctx.font = Kirigami.Theme.smallFont.pixelSize + "px sans-serif";

            // grid and labels
            ctx.strokeStyle = view.faded(Kirigami.Theme.textColor, 0.15);
            ctx.fillStyle = view.faded(Kirigami.Theme.textColor, 0.7);
            ctx.lineWidth = 1;
            for (const lux of [0, 10, 100, 1000, 5000]) {
                const x = view.xOf(lux, w);
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h - view.labelHeight); ctx.stroke();
                ctx.textAlign = lux === 5000 ? "right" : "center";
                ctx.fillText(i18nc("@label axis tick", "%1 lx", lux), x, h - 4);
            }
            for (const nits of [10, 100, view.maxNits]) {
                const y = view.yOf(nits, h);
                ctx.beginPath(); ctx.moveTo(view.labelWidth, y); ctx.lineTo(w, y); ctx.stroke();
                ctx.textAlign = "right";
                ctx.fillText(i18nc("@label axis tick", "%1 nits", Math.round(nits)), view.labelWidth - 4, y + 4);
            }

            const steps = 80;
            const curve = (withLearned) => {
                ctx.beginPath();
                for (let i = 0; i <= steps; i++) {
                    const lux = Math.pow(10, Math.log10(1 + view.maxLux) * i / steps) - 1;
                    const x = view.xOf(lux, w), y = view.yOf(kcm.curveNits(view.profile, lux, withLearned), h);
                    if (i === 0)
                        ctx.moveTo(x, y);
                    else
                        ctx.lineTo(x, y);
                }
                ctx.stroke();
            };
            // the profile as set, then with what has been learned
            ctx.setLineDash([4, 4]);
            ctx.strokeStyle = view.faded(Kirigami.Theme.textColor, 0.5);
            ctx.lineWidth = 1.5;
            curve(false);
            ctx.setLineDash([]);
            ctx.strokeStyle = Kirigami.Theme.highlightColor;
            ctx.lineWidth = 2.5;
            curve(true);

            // now
            const s = kcm.status;
            if (kcm.running && s.ambient !== undefined && s.nits > 0) {
                ctx.fillStyle = Kirigami.Theme.highlightColor;
                ctx.beginPath();
                ctx.arc(view.xOf(s.ambient, w), view.yOf(s.nits, h), 5, 0, 2 * Math.PI);
                ctx.fill();
            }
        }

        Connections {
            target: kcm
            function onStatusChanged() { canvas.requestPaint(); }
            function onLearnedChanged() { canvas.requestPaint(); }
        }
        Connections {
            target: kcm.settings
            function onProfileChanged() { canvas.requestPaint(); }
            function onDarkNitsChanged() { canvas.requestPaint(); }
            function onDimNitsChanged() { canvas.requestPaint(); }
            function onIndoorNitsChanged() { canvas.requestPaint(); }
            function onBrightNitsChanged() { canvas.requestPaint(); }
            function onDaylightNitsChanged() { canvas.requestPaint(); }
        }
        Connections {
            target: Kirigami.Theme
            function onTextColorChanged() { canvas.requestPaint(); }
        }
    }

    onProfileChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
}
