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

El log posterior vuelve a registrar `fifo recycle: drain stalled` (2000 ms).
La corrección visual no resuelve ese aviso. El congelamiento por inactividad
en menús queda como investigación separada y posterior a los efectos, por
prioridad del usuario.

Referencias de implementación:
[heaps de descriptores D3D12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-descriptor-heaps),
[modos de textura GCM en RPCS3](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/gcm_enums.h)
y decodificadores existentes `rsx_live_draw.c`/`rsx_vulkan_backend.c` del toolkit.
