[← Все примеры](../examples.md)

# Гравитационная задача N тел

Пример показывает бесконечную 2D-анимацию системы из 64 взаимно
притягивающихся тел. Тело с индексом `0` намного массивнее остальных, но оно не
закреплено: получает ускорение от других тел и движется вместе со всей системой.

## Модель

Для каждого тела хранятся масса, координаты, скорость и ускорение. На каждом
шаге сначала обнуляются ускорения, затем один раз обрабатывается каждая пара
`i, j`, где `i < j`:

```kumir
dx := x[j] - x[i]
dy := y[j] - y[i]
r2 := dx * dx + dy * dy + SOFTENING2
invR3 := 1.0 / (sqrt(r2) * r2)

ax[i] := ax[i] + G * mass[j] * dx * invR3
ay[i] := ay[i] + G * mass[j] * dy * invR3
ax[j] := ax[j] - G * mass[i] * dx * invR3
ay[j] := ay[j] - G * mass[i] * dy * invR3
```

Оба тела пары обновляются одновременно равными и противоположными силами. Это
полноценное взаимное N-body взаимодействие, а не набор независимых орбит в
фиксированном центральном поле.

`SOFTENING2` ограничивает ускорение при близком сближении тел. Без softening
дискретный интегратор потребовал бы очень малого шага времени.

## Интегрирование velocity Verlet

Начальные ускорения вычисляются до входа в цикл. На каждом шаге velocity Verlet
сначала обновляет координаты с ускорением в начале шага:

```kumir
x[i] := x[i] + vx[i] * DT + 0.5 * ax[i] * DT * DT
y[i] := y[i] + vy[i] * DT + 0.5 * ay[i] * DT * DT
```

После этого ускорения пересчитываются по новым координатам, а скорость
корректируется средним ускорением в начале и конце шага:

```kumir
vx[i] := vx[i] + 0.5 * (ax[i] + nextAx[i]) * DT
vy[i] := vy[i] + 0.5 * (ay[i] + nextAy[i]) * DT
```

Velocity Verlet существенно лучше сохраняет энергию орбитальной системы, чем
явный или semi-implicit Euler при том же шаге времени.

Начальная скорость массивного тела компенсирует суммарный импульс остальных,
поэтому центр масс системы не получает искусственный начальный дрейф.

## Визуализация

У каждого тела сохраняются последние `TAIL` положений. Тело `0` рисуется
крупнее, остальные получают разные оттенки. Вызов `новый лист` завершает кадр и
подготавливает следующий чёрный лист.

## Полная программа

<qumir-example id="painter-orbits" io="false">

```kumir
использовать Рисователь

цел W = 900
цел H = 700
цел N = 64
цел TAIL = 20

вещ CX = 450.0
вещ CY = 350.0
вещ G = 1.0
вещ CENTRAL_MASS = 2400.0
вещ DT = 0.18
вещ SOFTENING2 = 36.0
вещ PI = 3.141592653589793

алг
нач
    вещ таб mass[0:N-1]
    вещ таб x[0:N-1], y[0:N-1], vx[0:N-1], vy[0:N-1]
    вещ таб ax[0:N-1], ay[0:N-1], nextAx[0:N-1], nextAy[0:N-1]
    вещ таб tx[0:N-1, 0:TAIL-1], ty[0:N-1, 0:TAIL-1]

    цел frame
    вещ t

    инициализировать(mass, x, y, vx, vy, tx, ty)
    вычислить_ускорения(mass, x, y, ax, ay)
    новый лист(W, H, чёрный)
    frame := 0
    t := 0.0

    нц пока да
        шаг_физики(mass, x, y, vx, vy, ax, ay, nextAx, nextAy, tx, ty)
        нарисовать(frame, t, x, y, tx, ty)
        frame := frame + 1
        t := t + DT
    кц
кон

алг инициализировать(арг рез вещ таб mass[0:N-1], арг рез вещ таб x[0:N-1], арг рез вещ таб y[0:N-1], арг рез вещ таб vx[0:N-1], арг рез вещ таб vy[0:N-1], арг рез вещ таб tx[0:N-1, 0:TAIL-1], арг рез вещ таб ty[0:N-1, 0:TAIL-1])
нач
    цел i, k
    вещ radius, angle, speed, speedScale
    вещ totalPx, totalPy

    mass[0] := CENTRAL_MASS
    x[0] := CX
    y[0] := CY
    vx[0] := 0.0
    vy[0] := 0.0
    totalPx := 0.0
    totalPy := 0.0

    нц для i от 1 до N - 1
        mass[i] := 0.7 + 1.8 * rnd(1.0)
        radius := 65.0 + 215.0 * rnd(1.0)
        angle := 2.0 * PI * rnd(1.0)
        speedScale := 0.96 + 0.08 * rnd(1.0)
        speed := sqrt(G * CENTRAL_MASS / radius) * speedScale

        x[i] := CX + radius * cos(angle)
        y[i] := CY + radius * sin(angle)
        vx[i] := -speed * sin(angle)
        vy[i] := speed * cos(angle)
        totalPx := totalPx + mass[i] * vx[i]
        totalPy := totalPy + mass[i] * vy[i]
    кц

    vx[0] := -totalPx / mass[0]
    vy[0] := -totalPy / mass[0]

    нц для i от 0 до N - 1
        нц для k от 0 до TAIL - 1
            tx[i, k] := x[i]
            ty[i, k] := y[i]
        кц
    кц
кон

алг вычислить_ускорения(вещ таб mass[0:N-1], вещ таб x[0:N-1], вещ таб y[0:N-1], арг рез вещ таб ax[0:N-1], арг рез вещ таб ay[0:N-1])
нач
    цел i, j
    вещ dx, dy, r2, invR, invR3, factor

    нц для i от 0 до N - 1
        ax[i] := 0.0
        ay[i] := 0.0
    кц

    нц для i от 0 до N - 2
        нц для j от i + 1 до N - 1
            dx := x[j] - x[i]
            dy := y[j] - y[i]
            r2 := dx * dx + dy * dy + SOFTENING2
            invR := 1.0 / sqrt(r2)
            invR3 := invR * invR * invR
            factor := G * invR3

            ax[i] := ax[i] + factor * mass[j] * dx
            ay[i] := ay[i] + factor * mass[j] * dy
            ax[j] := ax[j] - factor * mass[i] * dx
            ay[j] := ay[j] - factor * mass[i] * dy
        кц
    кц
кон

алг шаг_физики(вещ таб mass[0:N-1], арг рез вещ таб x[0:N-1], арг рез вещ таб y[0:N-1], арг рез вещ таб vx[0:N-1], арг рез вещ таб vy[0:N-1], арг рез вещ таб ax[0:N-1], арг рез вещ таб ay[0:N-1], арг рез вещ таб nextAx[0:N-1], арг рез вещ таб nextAy[0:N-1], арг рез вещ таб tx[0:N-1, 0:TAIL-1], арг рез вещ таб ty[0:N-1, 0:TAIL-1])
нач
    цел i, k

    нц для i от 0 до N - 1
        x[i] := x[i] + vx[i] * DT + 0.5 * ax[i] * DT * DT
        y[i] := y[i] + vy[i] * DT + 0.5 * ay[i] * DT * DT
    кц

    вычислить_ускорения(mass, x, y, nextAx, nextAy)

    нц для i от 0 до N - 1
        vx[i] := vx[i] + 0.5 * (ax[i] + nextAx[i]) * DT
        vy[i] := vy[i] + 0.5 * (ay[i] + nextAy[i]) * DT
        ax[i] := nextAx[i]
        ay[i] := nextAy[i]

        нц для k от TAIL - 1 до 1 шаг -1
            tx[i, k] := tx[i, k - 1]
            ty[i, k] := ty[i, k - 1]
        кц
        tx[i, 0] := x[i]
        ty[i, 0] := y[i]
    кц
кон

алг нарисовать(цел frame, вещ t, вещ таб x[0:N-1], вещ таб y[0:N-1], вещ таб tx[0:N-1, 0:TAIL-1], вещ таб ty[0:N-1, 0:TAIL-1])
нач
    цел i, k
    цел hue, alpha, radius

    нц для i от 0 до N - 1
        если i = 0 то
            hue := 45
            radius := 8
        иначе
            hue := mod(div(i * 360, N) + div(frame, 4), 360)
            radius := 2
        все

        нц для k от TAIL - 1 до 1 шаг -1
            alpha := 16 + (TAIL - k) * 8
            перо(1, HSLA(hue, 85, 60, alpha))
            кисть(HSLA(hue, 85, 60, alpha))
            окружность(int(tx[i, k]), int(ty[i, k]), 1)
        кц

        перо(1, HSL(hue, 90, 65))
        кисть(HSL(hue, 90, 65))
        окружность(int(x[i]), int(y[i]), radius)
    кц

    шрифт("Arial", 13, нет, нет)
    перо(1, RGB(160, 160, 160))
    надпись(8, 18, "N-body, t = " + вещ_в_лит(t))

    новый лист(W, H, чёрный)
кон
```

</qumir-example>

Полный исходный код находится в
[`examples/painter/orbits.kum`](../../../examples/painter/orbits.kum).

[▶ Запустить пример](/?example=painter/orbits.kum)
