# Aquos: samplers de agua y reflejos — 01/11/2026

El usuario confirmó que el rojo de `battle-arena-picture/` ya no reproduce con
la build de cierre del 31/10. El defecto actual es la cascada del fondo: parece
un reflejo deformado en lugar del agua cayendo. No confundir ambos síntomas.

## Desajuste reproducido

La captura `diagnostics/arena/aquos-samplers-20261101/frame_101/manifest.txt`
registra el agua con FP `0x0ACA7101` y `0x0ACA7F01`:

- Normal/detalle en t0: `address=40010101`, `filter=02062000`: repetición y
  filtrado trilineal. El backend imponía point/clamp a toda la unidad cero.
- Reflejo en t3 y superficies auxiliares: `address=40010303`,
  `filter=02052000`: recorte al borde, minificación puntual y mezcla de mips.
  El backend imponía linear/wrap a las unidades distintas de cero.
- Los efectos también usan BORDER en algunas unidades. El estado del juego
  ya estaba decodificado por RSX, pero no llegaba a los samplers de D3D12.

Las superficies auxiliares de 512×512 sí reciben geometría y llegan al agua.
No se habilitaron los sustitutos `SUBVP_RTT`/`SCREEN_AS_TEX` como corrección,
ni se desactivó la profundidad. No se modificó el FIFO.

## Corrección `0036-d3d12-per-draw-samplers.patch`

Cada draw conserva ADDRESS, FILTER, CONTROL0 y BORDER_COLOR. La firma de raíz
del camino VP usa una tabla de samplers; cada unidad traduce sus modos U/V/W,
filtros min/mag/mip, límites LOD, bias y color de borde. El camino legacy
mantiene su propia firma de raíz.

Las tablas son inmutables durante el envío GPU. Se deduplican dentro de cada
página; cuando se llena el límite D3D12 de 2048 samplers, se usa otra página.
Se reutilizan después del fence existente. Al cambiar de heap se vuelven a
enlazar las dos tablas. No se añade una espera GPU por draw.

Límites: CLAMP antiguo y modos MIRROR_ONCE extendidos usan la aproximación de
borde de D3D; convolución usa filtrado lineal. Esta corrección no implementa
anisotropía ni comparación de sombras. Las sombras por superficie siguen
pendientes según `NEXT_SESSION_EFFECTS_SHADERS.md`.

## Validación

- Build Release correcta con `tools/build.py`.
- **20/20**: todos los `tools/test_rsx_*.py`, `test_gcm_fifo_wrap.py` y
  `test_pipeline_patches.py`.
- Nueva prueba WARP: el sampler fijo original falla al muestrear UV 1.25 en
  una textura que pide WRAP. Con la traducción real pasa; también comprueba
  clamp/mirror y filtros point/trilinear con límites de mip.
- Prueba D3D12: 2048 draws con estados únicos, cambios de heap, unidad 15,
  conservación de tablas anteriores y deduplicación de estados idénticos.
- Los **35 parches** aplican sobre los archivos de HEAD limpio del toolkit.
  La única diferencia restante del backend es el diagnóstico `VTX_COL`, previo
  a esta sesión. El pipeline reconoce toda la serie aplicada.
- La capa de depuración D3D12 no está instalada en este equipo; no se afirma
  validación con ella. Las pruebas GPU usan WARP/D3D12 directamente.

La captura posterior `aquos-samplers-fixed-20261101/frame_103` muestra franjas
de agua en las cascadas y nubes en el cielo, frente al fondo estirado de
`aquos-samplers-20261101/frame_101`. Es una comparación de vistas similares
con personajes distintos. El usuario confirmó que ahora se renderiza el agua
del fondo y aparece la lluvia; el reflejo todavía está distorsionado.
No se dan por validadas todas las salpicaduras. Los dumps se guardan
en `diagnostics/arena/aquos-{effects,samplers,samplers-fixed}-20261101/`.
No se consideran validados todavía Subterra, Ventus, Haos ni Pyrus.

Siguiente investigación solicitada: caída fuerte de FPS desde la pantalla de
carga al entrar a Aquos, trabas y audio que empieza antes que la imagen de
gameplay. Separar carga invitada, preparación de shaders/draws y esperas FIFO
antes de cambiar el reloj de audio o la política de cuadros completos.

## Entrada a Aquos: flujo de vértices (`0037`)

El perfil `aquos-loading-profile-20261101` muestra tres ampliaciones de buffer
(384, 768 y 1536 MB), seguidas de un intervalo de 1,79 FPS: 10,324 s en submit,
0,15 s en `render_frame` y 0,05 s en preparación de PSO. El crecimiento leía
ambas mitades del antiguo recurso UPLOAD desde CPU. Es memoria write-combine,
inadecuada para esa lectura masiva.

`0037-d3d12-cpu-vertex-stream.patch` mantiene el flujo en RAM cacheada; el
crecimiento conserva desde ella los datos y cada presentación copia solo los
bytes usados de la mitad actual a UPLOAD. Mantiene fence, paridad y geometría.
Las capturas posteriores a la GPU leen el upload; los dumps al registrar un
draw leen el flujo CPU. La nueva RAM adicional equivale al buffer de vértices
de doble paridad; en el pico observado puede llegar a 1536 MB.

Con el cambio, el intervalo que incluye esas ampliaciones registra 3,52 FPS
y 4,869 s en submit. Los intervalos contienen distinto número de draws, por
lo que no equivalen a un benchmark exacto de una misma escena. El usuario
confirmó que aún hay tirón, pero se recupera antes y el gameplay ya comienza
a la par del audio. No se modificó el reloj de sonido.

La vista de gameplay sigue lenta: unos 12–18 FPS según cámara/personajes.
La subida completa agrega coste dentro de `render_frame`; no se presenta
esta corrección de entrada como una mejora de FPS sostenidos. La prueba de
capacidad conserva ambas paridades y ahora verifica subida exclusiva del
prefijo usado, sin alterar el resto ni la mitad anterior. Build y las 20
pruebas pasan (la prueba de samplers se repitió tras ajustar el lugar del
helper de subida, que su extractor incluía accidentalmente).

Comparaciones nuevas del usuario: `battle-arena-picture/emulador/` y
`battle-arena-picture/recomp/`. La primera contiene también una captura con
título de recomp (`083436`); usar las imágenes con barra RPCS3 como referencia
de emulador. El usuario pide continuar con sombras/reflejos y su flujo.

El log posterior vuelve a registrar `fifo recycle: drain stalled` (2000 ms).
La corrección visual no resuelve ese aviso. El congelamiento por inactividad
en menús queda como investigación separada y posterior a los efectos, por
prioridad del usuario.

Referencias de implementación:
[heaps de descriptores D3D12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-descriptor-heaps),
[modos de textura GCM en RPCS3](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/gcm_enums.h)
y decodificadores existentes `rsx_live_draw.c`/`rsx_vulkan_backend.c` del toolkit.

## Cierre: sombras experimentales retiradas

La prueba posterior de profundidad por superficie produjo sombras blancas y
parpadeos en Aquos. La siguiente corrida terminó con pantalla blanca; el log
registra `0x887A0005` (dispositivo eliminado) inmediatamente después de iniciar
la captura. El readback experimental de profundidad construía el footprint
manualmente, mientras que la prueba D3D12 devuelve un footprint distinto con
`GetCopyableFootprints`. Es una causa sospechada del fallo de captura, todavía
sin confirmación; no explica por sí sola las sombras blancas anteriores.

Se retiró íntegramente ese experimento del backend final, restaurando el estado
validado de `0036` + `0037`. No se publica como solución de sombras. Las fuentes
experimentales y pruebas quedan fuera de Git en
`diagnostics/arena/shadow-experiment-withdrawn-20261101/`; las corridas están en
`aquos-native-depth-20261101` y `aquos-depth-clear-20261101`.

Para retomar: validar primero readback mediante footprints reales y patrones
de profundidad no uniformes; después comprobar comparación, orden de clears
y transiciones de cada superficie contra RPCS3. Los reflejos distorsionados,
las sombras correctas y los FPS sostenidos siguen pendientes. Esta entrega
conserva el agua/cascadas/lluvia y la mejora de entrada confirmadas por el
usuario, evitando publicar la regresión blanca.

Validación final del estado restaurado: build Release correcta, 20/20 pruebas
de render/FIFO/pipeline, 36 parches aplicados a HEAD limpio y arranque hasta
menú con ventana respondiendo (~29,5 FPS). No se repitió una navegación
completa a Aquos tras retirar el experimento; corresponde al mismo backend
que el usuario había validado antes de las pruebas de profundidad.
