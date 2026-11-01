# Rendimiento — 31/10/2026

Se conserva el ritmo original del juego de **30 FPS**. No se cambia el timebase,
la simulación, el VSync ni los lifts para forzar 60 FPS.

## Problemas separados

1. **Caída persistente al regresar al menú.** El backend combinaba presentación
   en clears con presentación en flips. El modo de clears conserva estado de
   escenas anteriores (`s_clear_presents`, máximo histórico de draws) y puede
   dejar de representar correctamente los límites de un cuadro al cambiar de
   arena a menú. El port ahora activa `RSX_ACCUM_FRAME=1` antes de iniciar los
   hilos: los clears permanecen dentro del cuadro y la presentación usa sus
   flips. Un `RSX_ACCUM_FRAME=0` explícito sigue disponible para diagnóstico.
2. **Coste de preparación de geometría.** El perfil localiza aproximadamente
   50–60% del tiempo de las escenas lentas en empaquetado de vértices y snapshots
   de draws. La conversión escribía defaults y atributos por separado en memoria
   de subida. Los strips además decodificaban de nuevo dos referencias ya usadas
   por el triángulo anterior.
3. **Latencia de barreras.** El consumidor dormía 4 ms aunque existía un evento
   FIFO. Se conecta la espera al evento y se señala tras publicar PUT. Antes solo
   el polling de REF podía señalarlo: las esperas de labels no lo hacían.
4. **Consultas de diagnóstico en rutas calientes.** El muestreo encuentra tiempo
   apreciable en búsquedas del entorno. Las opciones por draw seleccionadas se
   leen una vez al arrancar, como el resto de opciones del backend.

La hipótesis del máximo histórico de draws, por sí sola, **no quedó demostrada**
como causa única: resetear ese contador durante una corrida no recuperó los FPS.
No se conserva un reset de contadores ni se elimina a ciegas el present gate.
La corrección validada es elegir una sola política de cuadros para este título.

## Cambios reproducibles

- `bakugan-recomp/main.cpp`: espera FIFO y modo de cuadros completos en flips.
- `0029-d3d12-cached-vertex-staging.patch`: conversión en memoria CPU seguida de
  copia secuencial; mismos 16 atributos, constantes y valores convertidos.
- `0030-d3d12-hot-options.patch`: opciones de arranque cacheadas y lectura de c0
  desde el estado CPU, en lugar de leer de vuelta el buffer de subida.
- `0031-d3d12-strip-vertex-cache.patch`: caché local de 16 vértices por paquete;
  no conserva datos entre paquetes. Conserva restart, winding y ancla del fan.
- `0032-gcm-put-wakeup.patch`: señal después del store PUT y creación atómica del
  evento compartido por productor y consumidor. GET/REF no señalan el evento.
- `0033-d3d12-native-triangle-strips.patch`: strips nativos, cortes con triángulos
  degenerados, continuidad/paridad entre paquetes y caché local de referencias.
  `RSX_NATIVE_STRIPS=0` permite comparar con la expansión anterior. El usuario
  pidió conservar el ahorro de geometría y confirmó mejor imagen de personajes.
- `0034-ppu-inactive-write-watch.patch`: salida rápida de sondas de escritura
  inactivas. Conserva `PPU_WVAL`, `PPU_WWATCH` y las sondas armadas en ejecución
  mediante `g_barrier_sync_watch`; no omite escrituras ni coherencia SPU.
- `0035-d3d12-frame-pipeline-ownership.patch`: prepara una sola vez el PSO de
  cada draw y lo reutiliza al grabar comandos. Conserva una referencia hasta
  completar la espera GPU del cuadro siguiente, incluso si la caché global
  expulsa la entrada. `RX_OPAQUE` se aplica a la misma clave desde la preparación.

No se alteran los arreglos `0026`–`0028`, profundidad, textura, alfa ni callbacks
para fabricar progreso. La serie contiene 34 parches, sin `0023`.

## Mediciones y límites

Pruebas manuales del usuario, con logs de intervalos en `diagnostics/performance/`:

| Versión probada | Cinemáticas/carga | Battle Arena | Menú después de jugar |
|---|---|---|---|
| Inicial | El usuario reporta caídas a 11 FPS; también se midieron vídeos a 30 | 8–12 FPS en intervalos lentos | 3–6 FPS |
| Espera FIFO + staging | Cinemática inicial a 30 | Pico de 22, después 8–12 | 9–10 FPS |
| Cuadros completos + opciones cacheadas | Cinemática 2D y carga a 30 | 11–12 FPS | 30 FPS |
| Reutilización de vértices + aviso PUT | Presentaciones a 30; presentación de arena 23–25 | 15–17 FPS en primer plano de selección | Política de cuadros completos conservada |
| Strips nativos + sondas inactivas | Cinemáticas correctas; pequeño tirón restante de carga | Selección/primer plano: ~18 FPS; lanzamiento y control del Bakugan: **28–30 FPS** | Recuperación a 30 validada en las corridas anteriores |

Los resultados manuales de la última fila fueron confirmados por el usuario.
También observó que algunos
efectos de arenas mejoraron; no se consideran todos los efectos validados.
Al mover la cámara recupera 28–29 FPS; mirar al otro jugador o elementos
detallados vuelve a bajar los FPS. La caída restante depende de la vista, no
solo del primer plano de selección. En la versión `0035` los contadores ya
confirman aproximadamente la mitad de búsquedas/hashes de PSO por cuadro.
Los shaders se calentaron entre corridas: no atribuir las diferencias de
arranque a una optimización aislada ni presentar esta tabla como un benchmark
de escenas idénticas. **No todo el gameplay está a 30 FPS: queda el primer plano
de selección, alrededor de 18 FPS, y un pequeño tirón de carga.**

Prueba aislada de caché de CPU: 131072 vértices sintéticos en una asignación
Windows `PAGE_WRITECOMBINE`: directo 8,68 ms; staging 3,82 ms (2,27× en ese
tramo, no en el juego entero). Referencia del contrato de memoria de subida:
[ID3D12Resource::Map, Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map).

Un strip de 512 referencias repartido en dos paquetes pasa de **1530 a 514
vértices enviados**. La prueba compara triángulos orientados y los 16 atributos
contra la expansión real anterior y rasteriza ambos caminos en D3D WARP con
culling frontal y trasero. Se tolera un nivel UNORM de diferencia de color y se
comprueba que haya píxeles dibujados. Recorre además 256 patrones de cortes y
límites de paquetes. Referencia:
[Primitive Topologies, Microsoft](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-primitive-topologies).

La etiqueta `guest` de PERF es el tiempo restante fuera de `render_frame`;
incluye preparación de draws y esperas. No equivale a CPU PPU pura, y las
categorías anidadas de PERF no deben sumarse.

## Validación

- Build Windows Release correcta con `tools/build.py`.
- **22/22 pruebas**: FIFO, reloj, sondas PPU, todos los `tools/test_rsx_*.py`
  y `test_pipeline_patches.py`.
- Los 34 parches aplican en orden sobre un clone local limpio del toolkit.
  `cellGcmSys.c` y `ppu_loader.cpp` coinciden exactamente. El único delta de
  backend es el diagnóstico `VTX_COL`, previo a esta sesión y conservado.
- Pruebas nuevas: aviso FIFO tras PUT (sin realimentar GET/REF), señal pendiente
  y concurrente, opciones FP de arranque y 120 cuadros pequeños de menú después
  de un cuadro grande de arena, exactamente una presentación por flip.
- Pruebas ampliadas: caché de strips, colisiones, invalidación entre paquetes y
  comparación de los 16 atributos con la conversión canónica.
- Test de propiedad de PSO: 100 cuadros con expulsión de todas las referencias
  de caché antes del envío, sin liberar los pipelines hasta completar la GPU;
  cubre claves normales y `RX_OPAQUE`. Referencia del contrato:
  [Recording command lists, Microsoft](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles).
- Mutación de la paridad de continuidad: la prueba falla al invertir la
  condición y vuelve a pasar al restaurarla. Segunda revisión estática sin
  bloqueos en el alcance cambiado; no sustituye las pruebas manuales.

Los perfiles, ejecutables de comparación, shadercache, dumps y clone de prueba
son locales e ignorados. No se versionan datos del juego ni lifts.

## Siguiente medición

Mantener cuadros completos y strips nativos. Aislar las vistas hacia el otro
jugador o elementos detallados, sin compararlas con una cámara despejada: el
usuario confirmó que tienen costes distintos. El perfil anterior a `0035` marca cerca de 40% en preparación
de draws/vértices y 16–18% en `render_frame`. El audio SPU mantiene un hilo
ocupado; eso no demuestra que limite los FPS ni autoriza omitir su trabajo.
Faltan pruebas de todas las arenas y de sesiones largas con la build final.

## Cierre de la build principal

Prueba final del usuario con `0035`: vistas detalladas a **16–18 FPS**,
cinemáticas alrededor de **27 FPS** y pequeños tirones de carga que se recuperan
rápidamente. Lanzamiento/control alcanzó 28–30 FPS en pruebas anteriores.
La cifra de unos 20 FPS de media del README es una estimación del usuario entre
escenas, no una media calculada por un benchmark. No se garantiza 30 FPS
sostenidos. Build principal: `bakugan-recomp/build/BakuganRecomp.exe`,
ejecutada con `tools/run.ps1`; conservar el límite de 30 FPS.

Para continuar: medir la misma cámara lenta mirando al rival y una vista
despejada de la misma arena; separar coste de preparación/conversión de vértices,
submit y GPU con perfiles comparables. Investigar el fetch/conversión de atributos
y el trabajo por draw restante antes de nuevas optimizaciones. Medir los tirones
de carga en frío y con shadercache caliente por separado. Validar sesiones
largas, todas las arenas, cartas, personajes y efectos. No omitir trabajo SPU
ni alterar tiempos del juego para mejorar artificialmente los contadores.
