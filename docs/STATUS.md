# Estado del port — Bakugan: Battle Brawlers (BLES00758)

Actualización 01/11/2026: samplers por draw corrigen cascadas y lluvia de Aquos;
flujo de vértices en RAM reduce la demora de entrada y el usuario confirma
que imagen y audio comienzan juntos. Persisten reflejos distorsionados, sombras
y gameplay de 12–18 FPS. La prueba de profundidad que provocaba blancos fue
retirada. Build Release y 20/20 pruebas de render/FIFO/pipeline correctas;
36 parches aplican sobre toolkit limpio. Detalles:
[EFFECTS_AQUOS_2026-11-01.md](EFFECTS_AQUOS_2026-11-01.md).

Última actualización (31/10/2026): **Fixed gameplay Battle Arena: turno del jugador y CPU, texturas del campo y cartas corregidos.** Confirmado visualmente por el usuario.

**Rendimiento, actualización posterior del 31/10:** cinemáticas y regreso al menú
a 30 FPS; lanzamiento/control del Bakugan a 28–30 FPS confirmados por el usuario.
Se conserva el límite original de 30. Quedan ~18 FPS en el primer plano de
selección/vistas detalladas y un pequeño tirón de carga. Strips nativos conservados, con mejora
visual observada en personajes y algunos efectos. Detalles y límites:
[PERFORMANCE_2026-10-31.md](PERFORMANCE_2026-10-31.md).

Informe de cierre: [BATTLE_ARENA_FIXED.md](BATTLE_ARENA_FIXED.md). Próxima sesión:
[NEXT_SESSION_EFFECTS_SHADERS.md](NEXT_SESSION_EFFECTS_SHADERS.md), dedicada a efectos y shaders.
Build correcto, 22/22 pruebas y 34 parches reproducibles sobre un clone limpio.
Sombras offscreen y optimización del primer plano siguen pendientes. `0023` permanece revertido;
`0024` y `0025` se conservan. Las conclusiones sobre 3D y gameplay que siguen son históricas.

El cierre de la sesión del 24/10/2026 y el plan para retomarla están en
[NEXT_SESSION.md](NEXT_SESSION.md). Ese reporte actualiza las conclusiones
históricas sobre audio y geometría que aparecen más abajo.

El juego arranca, reproduce sus vídeos (logos, apertura, intro narrativa),
recorre todas las pantallas de menú y acepta teclado. **Los estados de 3D/gameplay de §9 son históricos; el estado vigente es el del
31/10 enlazado arriba.** Los efectos de sonido conservan su investigación previa.

## Resumen

**Vídeos (23/10/2026):** añadida reproducción H.264/AAC con Media Foundation y
presentación nativa D3D12. Verificados los cuatro logos iniciales, la apertura,
la reproducción de la introducción narrativa y el regreso al menú al saltarla.
Detalles, pruebas y límites en [VIDEO.md](VIDEO.md).

| Hito | Estado |
|---|---|
| Disco extraído (`game/`) | ✅ 4,3 GB |
| `EBOOT.BIN` → `EBOOT.elf` (RPCS3) | ✅ |
| Análisis (`ppu_loader`) | ✅ 12.834 funciones OPD, 195 imports, 14 librerías |
| Lift PPU (con `--code-end`) | ✅ 15.682 funciones, 17 TUs |
| Lift SPU | ✅ 5 imágenes, 1.331 funciones |
| Tabla HLE | ✅ 1.204 handlers |
| Build clang-cl + Ninja + D3D12 | ✅ ~1,5 min |
| Arranque y bucle | ✅ 30 fps estables, GCM/audio/pad/sysutil |
| Carga de assets | ✅ 607 trabajos de inflate (deflate crudo) descomprimidos por el worker SPU |
| Avance de pantallas | ✅ aviso legal → "PRESS ✕ TO CONTINUE" → carga → "Become the Ultimate Battle Brawler." → OSK |
| Input | ✅ **teclado** (X = Cross/confirmar) y mando XInput |
| Texturas | ⚠️ el fondo compuesto que muestrea el menú/OSK no se rellena (ver §6) |

## 1. Disco y ejecutable

| Dato | Valor |
|---|---|
| TITLE_ID | `BLES00758` (leído de `PARAM.SFO`) |
| Región | Europa — En, Fr, De, Es, It, Nl, Sv |
| Imagen | 4.571.856.896 B, ISO9660 **en claro** (no necesita `.dkey`) |
| Único binario cifrado | `PS3_GAME/USRDIR/EBOOT.BIN` (SELF retail) → RPCS3 `--decrypt` |
| Entry / TOC | OPD `0x692218` → código `0x10230`, TOC `0x6BF858` |
| Funciones | 19.146 descriptores OPD → 12.834 únicas |
| Imports | 195 en 14 librerías, 98 % NIDs resueltos |

`.text` acaba en `0x554464`; los stubs de import ocupan `0x554488..0x555CE8` y
`.rodata` empieza en `0x555CE8`. **El lift necesita `--code-end 0x554488`**: sin
él el lifter convierte `.rodata` en funciones y deja 16 símbolos sin definir.

## 2. Cómo se ejecuta

```powershell
.\.venv\Scripts\python.exe tools\pipeline.py   # patch → ISO → ELF → lifts → tabla HLE
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
```

## 3. Controles de teclado

`libs/input/cellPad.c` mapea el teclado al pad 0 **cuando no hay mando XInput
conectado** (si hay mando, manda el mando). El juego recibe un pad normal:

| Tecla | Botón PS3 |
|---|---|
| **X** o Espacio | **Cross (confirmar)** |
| C | Circle |
| Z | Square |
| S | Triangle |
| Enter | Start |
| Tab o Backspace | Select |
| Flechas | D-pad |
| Q / E | L1 / R1 |
| 1 / 2 | L2 / R2 |

`PAD_NO_KEYBOARD=1` lo desactiva. `tools/press_key.ps1 -Key X` pulsa una tecla de
verdad (con la ventana al frente), útil para automatizar pruebas.

## 4. Arreglos de esta fase

1. **`--code-end` en el lift** (evita levantar `.rodata`).
2. **LF queue SPURS correcta** (`CellSyncLFQueue` de la SDK):
   `pop1 +0x00` (`m_h1..m_h4`), `push1 +0x08` (`m_h5..m_h8`), `size +0x10`,
   `depth +0x14`, `buffer +0x18`, `m_bs +0x20`, `direction +0x24`, `v1 +0x28`,
   `init +0x2C`, `eaSignal +0x70`. El inicializador anterior escribía `depth`
   en `+0x0C` (= `push1.m_h7/m_h8`), con lo que el consumidor creía que la cola
   ya tenía 47 elementos. Además se implementó el caso **ANY2ANY**
   (`direction == 3`: `buffer |= 1`, `m_bs[0..1] = 0xFFFF`, `v1 = -1`, packs
   `0xFFFF`) y el push/pop con índices de 16 bits módulo `2*depth`.
3. **Señal de fin de workload**: cuando una tarea SPURS termina, el runtime pone
   el bit del *workload event flag* (`cellSpursEventFlagInitialize` con
   taskset == NULL). Sin eso, el hilo de carga del juego esperaba para siempre.
   Con esto desaparecieron los 45 `assertion failed` y el juego empezó a avanzar.
4. **`eaSignal` nombra la instancia SPURS, no el taskset**: tabla
   instancia→taskset guardada en `CreateTaskset` para que el push despierte a la
   tarea correcta.
5. **`cellGame_init_from_paramsfo()`** en `main.cpp` (title id real).
6. **Teclado en `cellPad`** (X = Cross, etc.).

Todo empaquetado en `patches/0001-spurs-lfqueue-and-input.patch`
(`git -C ps3recomp-src apply ../patches/0001-spurs-lfqueue-and-input.patch`).

## 5. Secuencia de arranque observada

1. `cellSysmoduleLoadModule` ×14, `cellSpursInitialize`, `cellGcmInit` (720p),
   audio (2 puertos), pad, sysutil.
2. Abre `d00.bin` y `d00.arc`; lecturas AIO correctas.
3. Crea 8+ colas SPURS y 34 tareas; empuja **607 descriptores** de 32 B:
   `src`, `dst`, `size`, flags y `eventflag`. Cada `src` es **deflate crudo**:
   se verificó que la salida del worker SPU coincide byte a byte con la
   descompresión en el host (64 KB, 0 diferencias).
4. Pantallas: aviso legal → `PRESS ✕ TO CONTINUE` → carga
   ("Become the Ultimate Battle Brawler.") → **teclado en pantalla (OSK)** del
   juego pidiendo el nombre del jugador.

## 6. Visuales del menú (fondos/texturas)

**Causa raíz encontrada y corregida: el backend forzaba el test de profundidad.**

Los PSO del camino VP se creaban siempre con `DepthEnable=TRUE`,
`DepthWriteMask=ALL` y `LESS_EQUAL`, ignorando `SET_DEPTH_TEST_ENABLE` del
invitado. Este título dibuja primero un quad a pantalla completa (que escribe
`z=0` en todo el buffer) y después las capas 2D del diálogo, que caen en
`z≈0.93`. Con el test forzado, **todas esas capas se descartaban**; el texto
va a `z=0` y sobrevivía (de ahí "solo se ven los textos").

Medido con `VDEFDBG=<hex tex0>` sobre la pantalla del aviso:

| draw | textura | vértices | z NDC | resultado antes |
|---|---|---|---|---|
| panel/botón (VP 22 instr) | `0x0B0DF000` | 4 (quad) | 0.934 | descartado |
| texto (VP 8 instr) | `0x0C489D80` | 282 | 0.0 | visible |

Los vértices y las constantes eran correctos (`c8=(1,0,0,0)`,
`c9=(0,-1.77778,0,0)`, `c11=(-640,640,598.71,641)`, y `a0.xy` cae exactamente
en los píxeles 285..1068 × 103..595). El problema no era geometría, ni formato,
ni color, ni blend: era el estado de profundidad.

Arreglo (`rsx_d3d12_backend.c`): se graba `depth_test_enable` en cada
`D3D12DrawRecord` (`depth_en`), forma parte de la clave de la caché de PSO y se
aplica a `DepthEnable`/`DepthWriteMask`. `NODEPTH=1` queda como diagnóstico.

Pantallas verificadas tras el arreglo (`cap9`–`cap12`): aviso de autoguardado
(panel + rejilla + botón OK), copyright, carga con los seis símbolos,
`PRESS ✕ TO CONTINUE` con logo y anillos, y **menú de inicio**
(New Game / Load Game / OK / BACK).

### Pendiente: capa a pantalla completa desde el render-to-texture

Sigue negro el *fondo* a pantalla completa (dos quads que muestrean
`0xDF60000`, resuelto cada frame desde el RT MSAA `0xEAB0000`). El RT se limpia
cada frame y **nunca recibe geometría**; medido en el despachador del FIFO
(`g_draw_surf_eab`, independiente de la clasificación del backend):

| Prueba | Resultado |
|---|---|
| `g_draw_surf_eab` durante 780 frames | **0** (23221 draws a display, 0 al RT) |
| `RTOFF` | RT = `0xEAB0000`, 1120×584, formato `0x90A3148` (AA 2x), zeta `0xE460000` |
| `cellGcmSetTileInfo` | tile 2 = `0xEAB0000` pitch 10240; `GetTiledPitchSize(0x2300)→0x2800` |
| Blits NV3089 | 3 por frame: 2240×584 (`0xCEAB0000`) → 1120×584 (`0xCDF60000`), resolve 2:1 |
| `BAKUGAN_DUMP_TEX=0xCEAB0000` | 0 bytes no-cero |
| subchannels 2D usados | 3 (NV3062), 5 (NV308A), 6 (NV3089); sin NV309E |

Conclusión: el invitado limpia y resuelve ese RT pero no emite la pasada que lo
rellena, así que el resolve copia ceros. Es la siguiente barrera, y no afecta ya
a la legibilidad del menú (el panel del diálogo es opaco y se dibuja encima).

Nota menor sin verificar: la pantalla de carga muestra una franja blanca difusa
en el borde izquierdo (`cap9/frame_01`), posiblemente un sprite de luz con wrap.

## 7. Reproducibilidad

Todo lo generado sale de `tools/pipeline.py` y no se versiona. Los cambios del
toolkit viven en `patches/`. Tras tocar `libs/`:
`tools/pipeline.py --only hle-nids --force` y recompilar.


## 8. Estabilidad de arranque y vídeos (2026-10-24)

- **Crash tras presionar X:** confirmado como escritura SPU sobre una LFQueue
  liberada. Shutdown marca el taskset, despierta tareas bloqueadas y detiene a
  sus workers en puntos seguros. Join espera a que no queden tareas habilitadas
  antes de permitir liberar la memoria; no se oculta con un timeout.
- **Vídeos:** decodificación/presentación con reloj nativo independiente del
  polling invitado. Pausa, reapertura y cierre sincronizados; COM permanece en
  el hilo que lo inicializó. EOF vuelve al juego por el handler SAIL habitual.
- **Callbacks PPU:** stacks separados por hilo, TLS del llamador y restauración
  del contexto/trampolín para llamadas anidadas.
- **Máscara del menú:** `0003-menu-mask.patch` conserva la función y escritura
  de profundidad y el alpha test por draw/PSO; corrige el cuadrado visible detrás
  del personaje. No añade renderizado de modelos 3D.
- Los cambios del toolkit se reproducen con los parches `0001` a `0004`.
  El pipeline comprueba la serie en orden inverso sobre una copia temporal,
  para reconocer parches solapados sin reaplicarlos.

Validación: build Windows correcto; pruebas de SAIL (diez ciclos), reloj de vídeo,
callbacks concurrentes/anidados, cierre/Join de SPURS y prefijos de parches pasan.
Arranque y llegada al menú comprobados con vídeos activados y desactivados.

La intro de New Game completó sus **977 frames** sin error y volvió al menú
principal a 30 FPS. Capturas `diagnostics/stability/narrative_frames/frame_25`,
`frame_30` y `frame_37` verifican escenas distintas y el menú posterior.

## 9. Estado actual (2026-10-24)

**Funciona:** arranque, logos de distribuidora, apertura, intro narrativa (977
frames), pantalla de título, todos los menús, selector de modo, teclado en
pantalla del juego, texturas 2D, teclado/XInput, escritura de datos de guardado y
el resolve del render target 3D: el quad que muestrea `0x0DF60000` recibe el RT
`0xEAB0000` en vivo (parche 0007, confirmado con `texrt=0`).

**No funciona:**

| Síntoma | Causa conocida |
|---|---|
| Batalla en negro | El combate dibuja 400+ ops fuera de pantalla y 13–20 ops a display (quad de composición con `pso=guest-fp`, `vp=1280x720`, blend `SRC_ALPHA`, sin test de profundidad, muestreando el RT vivo), pero **nada de ese pase deja píxeles**: ni la escena ni los sprites del HUD. El menú usa el mismo estado y sí se ve. Descartados: alias, canal alfa del RT, test de profundidad, PSO de fallback, cellResc y blits ocultos. Ver `docs/NEXT_SESSION.md` para el plan de pruebas. |
| Escena 3D deformada | **Dentro** del RT la escena sale deformada: estelas radiales, triángulos gigantes y texturas estiradas. Sospechosos: constantes del vertex program y el fallback de `vp_fill_cb_slot` (sustituye `c0..c3` según `c00`), no las texturas. |
| Leer el RT desde la PPU | El backend D3D12 no escribe color renderizado en memoria del invitado; el alias solo cubre el caso "el invitado muestrea el destino como textura". |
| Gameplay (tutorial, combates) | Consecuencia directa de la escena deformada. |
| Efectos de sonido | Los `SOUND/*.PAST` son ATRAC3; solo está implementado el audio de vídeo (Media Foundation). |
| Solape de capas 2D en el "gate" del selector | Descartados blend (claves `0x122118B` = alpha estándar y `0x122108B` = aditiva, ambas correctas), scissor y stencil (el juego no los usa) y geometría (los quads caen en pantalla). Pendiente de una referencia visual del juego real. |

El resolve se arregló con un alias, no con un readback: `rsx_live_draw.c` y
`rsx_draw_engine.c` sí escriben píxeles del invitado pero solo se inicializan en
Vulkan, y `main.cpp` usa `rsx_d3d12_backend_init`. El backend guarda el RT a
1120×584 (el almacenamiento 2× del invitado no se simula), así que apuntar
`0xDF60000` al RT vivo es 1:1 y no cuesta ningún stall.

Los binarios, el código levantado y los assets no se distribuyen: todo se
regenera con `tools/pipeline.py` desde el disco del usuario.
