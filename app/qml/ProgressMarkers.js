.pragma library

// Полукруг выпуклой стороной вперёд по шкале.
function strokeSemicircle(ctx, x, y, radius, direction) {
    ctx.save()
    ctx.translate(x, y)
    ctx.rotate(direction)
    ctx.beginPath()
    ctx.arc(0, 0, radius, -Math.PI / 2, Math.PI / 2, false)
    ctx.stroke()
    ctx.restore()
}

function drawRing(ctx, cx, cy, r, laps, overFrac, thickness) {
    if (r <= 0 || laps < 1)
        return
    var s = thickness * 0.46
    var spacing = s * 1.6 / r
    var endAngle = -Math.PI / 2 + overFrac * Math.PI * 2
    ctx.save()
    ctx.strokeStyle = "black"
    ctx.lineWidth = thickness * 0.26
    ctx.lineCap = "round"
    for (var k = 0; k < laps; k++) {
        // Каждый маркер лежит на кольце и имеет собственную касательную.
        var a = endAngle - k * spacing
        strokeSemicircle(ctx, cx + r * Math.cos(a), cy + r * Math.sin(a),
                         s, a + Math.PI / 2)
    }
    ctx.restore()
}

function drawBar(ctx, w, h, laps, overFrac) {
    if (w <= 0 || h <= 0 || laps < 1)
        return
    var s = h * 0.46
    ctx.save()
    ctx.strokeStyle = "black"
    ctx.lineWidth = Math.max(2.2, h * 0.26)
    ctx.lineCap = "round"
    for (var k = 0; k < laps; k++) {
        var x = ((overFrac * w - k * s * 1.6) % w + w) % w
        strokeSemicircle(ctx, x, h / 2, s, 0)
        // Продолжаем маркер через край при переходе на следующий круг.
        if (x - s - ctx.lineWidth / 2 < 0)
            strokeSemicircle(ctx, x + w, h / 2, s, 0)
        if (x + s + ctx.lineWidth / 2 > w)
            strokeSemicircle(ctx, x - w, h / 2, s, 0)
    }
    ctx.restore()
}
