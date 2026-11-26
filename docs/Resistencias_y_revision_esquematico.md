# Resistencias en el I2C y en los sensores + revisión del esquemático

Proyecto DR-PS-02 · PCB ESP32-C3 · Revisión A · Noviembre de 2026

---

## Parte 1. ¿Por qué se usan resistencias en el I2C?

### La idea clave: en el I2C nadie "pone" un 1, solo se "suelta" la línea

SDA y SCL son líneas de **drenador abierto (open-drain)**. Cada chip del bus (el ESP32, el AHT10,
el BH1750, la OLED) tiene por dentro un transistor que puede **conectar la línea a GND** (escribe un 0)
o **soltarla** (queda flotando). Ninguno puede conectarla a 3,3 V.

```
            +3V3
             │
            ┌┴┐  R7 = 4,7 kΩ  (pull-up)
            └┬┘
SDA ─────────┼──────────┬──────────┬──────────
             │          │          │
           ┌─┴─┐      ┌─┴─┐      ┌─┴─┐
   ESP32 ──┤ T │ AHT10┤ T │ BH1750 T │      T = transistor interno
           └─┬─┘      └─┬─┘      └─┬─┘
            GND        GND        GND
```

- Si **nadie** tira la línea a GND, la resistencia la **sube a 3,3 V** → se lee un **1**.
- Si **cualquiera** la tira a GND → se lee un **0**.

Sin la resistencia, al soltar la línea nadie la llevaría a 1: quedaría flotando y la comunicación
falla. Por eso se llama **pull-up** ("tirar hacia arriba").

### ¿Por qué se diseñó así y no con salidas normales?

1. **Varios dispositivos en dos cables sin cortocircuitos.** Si dos chips con salidas normales
   escribieran a la vez, uno a 3,3 V y otro a 0 V, harían un corto. Con open-drain lo peor que
   pasa es que la línea queda en 0: no se daña nada.
2. **El esclavo puede "frenar" al maestro** (*clock stretching*): mantiene SCL en 0 hasta estar listo.
3. **Arbitraje y ACK**: el esclavo responde "recibí" tirando SDA a 0 en el noveno pulso.
4. **Distintas tensiones**: la línea sube a la tensión a la que está conectada la pull-up (aquí 3,3 V),
   así que protege los pines de 3,3 V del ESP32.

### ¿Por qué 4,7 kΩ y no otro valor?

Es un **compromiso** entre velocidad y corriente:

| Si la resistencia es… | Ventaja | Problema |
|---|---|---|
| **Muy grande** (p. ej. 47 kΩ) | Casi no consume | La línea sube **muy lento** (el cable y los pines tienen capacitancia). El 1 llega tarde y se leen bits malos. |
| **Muy pequeña** (p. ej. 330 Ω) | Sube muy rápido | Cuando un chip la baja a 0 debe absorber mucha corriente (3,3 V/330 Ω = 10 mA). El estándar permite 3 mA: el 0 no queda bien en 0. |

Los dos cálculos de tu documento de justificación son correctos:

- **Tiempo de subida** (modo estándar, 100 kHz, límite 1 µs):
  t<sub>r</sub> ≈ 0,8473 · R · C<sub>bus</sub> = 0,8473 · 4,7 kΩ · 200 pF ≈ **0,8 µs** ✔
- **Resistencia mínima** (corriente máxima 3 mA con V<sub>OL</sub> = 0,4 V):
  R<sub>min</sub> = (3,3 − 0,4) V / 3 mA ≈ **0,97 kΩ** → 4,7 kΩ está muy por encima ✔

> **Ojo con los módulos.** El AHT10, el GY-302 y la OLED casi siempre traen **sus propias pull-ups**
> (normalmente 4,7 kΩ o 10 kΩ). Todas quedan **en paralelo** con R7/R8. Con cuatro de 4,7 kΩ:
> 4,7 kΩ / 4 ≈ **1,2 kΩ**, todavía por encima de 0,97 kΩ pero ya justo.
> Con la placa **apagada**, mide con el multímetro la resistencia entre SDA y +3V3 con todos los
> módulos puestos. Si da menos de ≈ 1,5 kΩ, no montes R7/R8 (o quita las de algún módulo).

El firmware desactiva las pull-ups internas del ESP32-C3 (~45 kΩ): son demasiado débiles para I2C.

---

## Parte 2. Las demás resistencias de los sensores y del circuito

| Ref. | Dónde | Para qué sirve, en palabras simples |
|---|---|---|
| **R9 (1 kΩ) + C10 (470 nF)** | Salida del sensor de suelo → IO0 | **Filtro paso-bajo RC**: deja pasar la señal lenta (la humedad cambia en minutos) y elimina el ruido rápido del Wi-Fi y de la parte digital. f<sub>c</sub> = 1/(2π·1 kΩ·470 nF) ≈ 339 Hz. Además C10 actúa como "depósito" de carga para que el ADC tome la muestra sin hacer caer la tensión, y R9 limita la corriente si llega un pico por el cable. |
| **R10 (1 kΩ)** | Salida del PIR → IO3 | **Resistencia serie de protección**. El HC-SR501 da 3,3 V, pero algunos clones dan casi 5 V. Si eso pasa, R10 limita la corriente que entra al diodo de protección del pin a ≈ 1,4 mA y no se daña el ESP32. |
| **R15 (10 kΩ) + C11 (470 nF)** | Pulsador ACK → IO1 | **Pull-up + antirrebote**. R15 deja el pin en 1 cuando no pulsas; al pulsar va a 0. El contacto mecánico "rebota" varios ms; C11 suaviza esos rebotes (τ = 4,7 ms) y el firmware termina de filtrarlos (50 ms). |
| **R4 (10 kΩ)** | BOOT → IO9 | Pull-up: en reposo IO9 = 1 → arranca normal. Si arrancas con BOOT pulsado (IO9 = 0) entra en modo de programación. |
| **R5, R6 (10 kΩ)** | IO2, IO8 | **Pines de strapping**: el C3 lee su nivel al arrancar para decidir cómo hacerlo. Deben estar en 1; las pull-ups lo aseguran. |
| **R3 (10 kΩ) + C7 (1 µF)** | EN (reset) | Retardo de arranque: τ = 10 ms, para que el chip salga de reset cuando la tensión ya está estable. Es lo que recomienda Espressif. |
| **R13 (1 kΩ)** | GPIO → compuerta de Q1 | Limita el pico de corriente al cargar la compuerta del MOSFET y amortigua oscilaciones. |
| **R14, R17, R18 (10 kΩ)** | Compuertas de Q1, Q2, Q3 a GND | **Pull-down**: mientras el ESP32 arranca sus pines están "flotando"; estas resistencias mantienen los MOSFET apagados, así el zumbador no suena ni el LED se enciende solo al conectar. |
| **R11, R12 (330 Ω), R16 (150 Ω)** | Cátodos del LED RGB | **Limitan la corriente del LED**. I = (5 V − V<sub>f</sub>)/R: rojo ≈ 9 mA, verde ≈ 6 mA, azul ≈ 13 mA. Sin ellas el LED se quemaría. |
| **R1, R2 (5,1 kΩ)** | CC1, CC2 del USB-C | Le dicen al cargador "soy un dispositivo, dame 5 V". Sin ellas, un cargador USB-C a USB-C no entrega energía. |

**Resumen**: las resistencias en sensores cumplen cuatro funciones básicas:
1. **Fijar un nivel lógico** cuando nadie lo impone (pull-up / pull-down): I2C, pulsadores, strapping, compuertas.
2. **Limitar corriente** (protección): R10, R13, LED.
3. **Filtrar** junto con un capacitor (RC): R9-C10, R15-C11, R3-C7.
4. **Señalizar** algo a otro equipo: R1/R2 del USB-C.

---

## Parte 3. Revisión del esquemático (Revisión A)

### ✔ Lo que está bien

- Pull-ups I2C de 4,7 kΩ a 3,3 V, 100 kHz, cálculos correctos.
- USB nativo del C3 bien asignado (IO18 = D−, IO19 = D+), con ESD (U3) y CC de 5,1 kΩ.
- Red RC de EN (10 kΩ / 1 µF), pull-ups de strapping en IO2, IO8, IO9.
- Suelo en **ADC1** (IO0): correcto, el ADC2 del C3 no es fiable.
- MOSFET con pull-down de compuerta en zumbador y LED; diodo de rueda libre bien orientado.
- PIR alimentado desde VBUS (necesita ≥ 4,5 V) con R10 en serie.
- Desacople del módulo (10 µF + 0,1 µF) y tantalio de 22 µF a la salida del AMS1117.
- Notas útiles: alejar el AHT10 del calor, antena en el borde, puntos de prueba.

### ⚠ Para corregir o verificar antes de fabricar

**1. Rango del ADC del ESP32-C3 (importante).** La tabla de pines dice "≈ 0–3,1 V". Ese valor es del
ESP32 clásico/S3. En el **ESP32-C3, con 12 dB, el rango útil es 0–2500 mV**
(documentación de ESP-IDF). Un sensor capacitivo alimentado a 3,3 V puede dar 2,5–2,9 V en seco, y
entonces el punto "seco" queda **recortado**.
*Qué hacer:* mide la salida del sensor en el aire con el multímetro.
Si es > 2,4 V, convierte R9 en divisor con las resistencias de la librería:
**R9 = 33,2 kΩ (Res_33k2)** y agrega **R19 = 73,2 kΩ (Res_73k2) de SOIL_ADC a GND**
(factor 73,2/106,4 = 0,69 → 2,9 V pasa a ≈ 2,0 V). Deja la huella de R19 en la PCB aunque no la montes (DNP);
si no la montas, R9 vuelve a ser 1 kΩ. No hace falta cambiar el firmware: la calibración seco/húmedo absorbe el factor.
El firmware ya avisa "cerca del tope del ADC" si pasa de ≈ 2,44 V.
*Extra:* revisa el chip del sensor: si dice **NE555** necesita ≥ 4,5 V (aliméntalo de VBUS y el divisor
es obligatorio); si dice **TLC555** funciona a 3,3 V.

**2. El PRD y el esquemático ya no coinciden.** El DR-PS-02 v0.2 (SR-10 y Anexo B) habla de un ESP32
clásico con GPIO21/22, GPIO34, LED verde y rojo separados y dos pulsadores. La PCB usa ESP32-C3, LED
RGB y BOOT como segundo pulsador. Además, para "alerta reconocida" el PRD dice **LED rojo fijo** y la
hoja 4 dice **amarillo**. El firmware sigue la hoja 4 (amarillo).
*Qué hacer:* actualizar en la v0.3 del PRD: SR-10, SR-16, sección 3.5 y Anexo B.

**3. Protección de alimentación (SR-31).** El PRD pide protección contra sobrecorriente con fusible
reseteable; la hoja 1 solo tiene el TVS D1. *Qué hacer:* agregar un **PTC (polyfuse) 1206 de
≈ 500 mA de mantenimiento** en serie con VBUS, entre el conector y D1/C1. (La inversión de polaridad
no aplica con USB-C.)

**4. Pull-ups en paralelo.** Ver la nota de la Parte 1: medir SDA–3V3 con los módulos puestos.
Recomendable marcar R7/R8 como "montar si hace falta".

**5. ADDR del BH1750 sin conectar.** En el GY-302 suele tener una resistencia a GND en el módulo,
pero es más seguro **llevar ADDR a GND** en J4 para fijar la dirección 0x23.
(El firmware igual prueba 0x23 y 0x5C.)

**6. AHT10 compartiendo el bus.** La hoja de datos del AHT10 trae una frase que dice que no se pueden
conectar otros dispositivos I2C al bus. En foros técnicos se considera un error de traducción y
varios usuarios reportan que funciona compartido, pero conviene **probarlo pronto** con los tres
módulos juntos durante varias horas. Si da problemas, el AHT20 es compatible pin a pin y no tiene
esa advertencia.

**7. Caída del AMS1117 (menor).** Necesita ≈ 1–1,1 V de margen. Con VBUS de 5 V va bien, pero con un
cable largo/cargador débil (VBUS ≈ 4,4 V) en un pico de Wi-Fi el 3V3 puede quedar al límite.
Si ves reinicios por *brownout* en la consola, prueba otro cable o considera un LDO de baja caída
(p. ej. de la familia AP2112K-3.3).

**8. Opcional: ESD en J6/J7.** Los cables Dupont al sensor de suelo y al PIR salen de la caja; R9 y R10
ya dan algo de protección. Si sobra espacio, un TVS doble en esas líneas.

### Prioridad

| # | Tema | Prioridad |
|---|---|---|
| 1 | Rango ADC del C3 / divisor del sensor de suelo | **Alta** — afecta la medida |
| 2 | Actualizar el PRD a ESP32-C3 + LED RGB | **Alta** — lo revisa el docente |
| 3 | Fusible PTC en VBUS | Media — lo exige SR-31 |
| 4 | Medir pull-ups en paralelo | Media |
| 5 | ADDR del BH1750 a GND | Baja |
| 6 | Probar AHT10 compartido | Media (prueba, no cambio) |
| 7–8 | LDO / ESD | Baja |
