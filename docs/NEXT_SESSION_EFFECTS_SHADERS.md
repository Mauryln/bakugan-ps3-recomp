> Cierre 01/11/2026: se publica en main el avance de samplers (`0036`) y
> flujo CPU de vértices (`0037`). El experimento de profundidad por superficie
> se retiró por sombras blancas/parpadeos y pantalla blanca durante captura.
> Ver el cierre en [EFFECTS_AQUOS_2026-11-01.md](EFFECTS_AQUOS_2026-11-01.md).
> Sombras/reflejos y FPS de gameplay siguen pendientes; no reactivar el
> experimento sin una prueba GPU y comparación visual.

# Próxima sesión — efectos y shaders (cierre 31/10/2026)

**Actualización 01/11/2026:** se reprodujo y corrigió un desajuste de samplers
en Aquos (`0036`); build y 20 pruebas correctas. El usuario confirmó cascadas
y lluvia; quedan reflejo distorsionado y trabas de entrada desde la carga,
con audio antes que gameplay. Ver [EFFECTS_AQUOS_2026-11-01.md](EFFECTS_AQUOS_2026-11-01.md).

**Actualización posterior de rendimiento:** leer
[PERFORMANCE_2026-10-31.md](PERFORMANCE_2026-10-31.md). Se conservan cuadros
completos en flips, strips nativos y el límite de 30 FPS. El usuario confirmó
28–30 FPS durante lanzamiento/control; quedan ~18 FPS en primer plano de selección.
Algunos efectos y personajes mejoraron visualmente, sin validar todas las arenas.

**Punto de partida:** Battle Arena jugable; campo y cartas corregidos y
confirmados por el usuario. Se verificaron turno del jugador, lanzamiento,
turno de la CPU y entrada al enfrentamiento. Rama `codex/battle-arena-rendering`.

Leer primero [BATTLE_ARENA_FIXED.md](BATTLE_ARENA_FIXED.md): contiene causas,
parches, pruebas, capturas y los experimentos descartados. Los informes FIFO y
Battle Arena del 30/10 quedan como contexto histórico.

## Trabajo pendiente, en orden

1. **Efectos y shaders restantes.** Identificar un efecto concreto y compararlo
   con una referencia PS3. Capturar antes de modificar: FP/VP, texturas, blend,
   alpha test, depth write, viewport y destinos MRT. Revisar brillos, niebla,
   transparencias, reflejos, composición y posibles estelas. No afirmar que
   todos estén rotos: falta aislar cada diferencia visual.
2. **Sombras reales.** El backend conserva un único depth buffer. El pase a
   zeta `0x0F100000` no produce una depth texture offscreen muestreable.
   Implementar soporte por superficie, transiciones DSV/SRV y comparación de
   profundidad antes de habilitar el sampling de sombras. Mantener revertido
   `0023`; no muestrear memoria vacía para simular iluminación.
3. **Rendimiento.** Medir sin capturas ni logs masivos; separar PPU/SPU,
   empaquetado de vértices, preparación de draws y envío GPU. La sesión final
   con diagnósticos rondó 9–12 FPS; no usarla como benchmark base. Conservar
   sincronización y tests al optimizar.
4. **Regresiones ampliadas.** Probar otras arenas, varias rondas, tutorial e
   historia. Los dos últimos modos no quedaron validados en esta sesión.
   Audio ATRAC3/efectos sonoros conserva su investigación previa separada.

## Invariantes que se deben conservar

- `0026`: el flip solo agrega su marcador; el callback normal controla el wrap
  con backpressure. No rebobinar `ctx->current` porque haya un margen de cabeza
  consumida, ni esperar al flip antes de registrar su finalización.
- `0027`: conservar CONTROL3.pitch en snapshots/caché/subidas, filas BC3 y mips
  lineales. Copiar bytes útiles, no padding de origen, a cada footprint GPU.
- `0028`: comparar alfa a precisión UNORM en targets RGBA8; conservar targets
  flotantes y los valores de color que recibe el blending.
- `0024`: máximo una captura cada 500 ms. `0025`: conservar diagnósticos y
  recuperación; no usar resyncs/saltos artificiales como arreglo del productor.
- No habilitar `DEPTH_OFF`, `NODEPTH` ni variantes de shader de diagnóstico como
  solución permanente. No tocar lifts generados.

## Cómo retomar

```powershell
.\.venv\Scripts\python.exe tools\pipeline.py --only patch
.\.venv\Scripts\python.exe tools\build.py
.\tools\debug_run.ps1 -Tag effects-next -Capture -FragmentPrograms 200
```

`debug_run.ps1` ahora admite `-FragmentPrograms` (por defecto 200): treinta
compilaciones se agotaban con variantes del HUD antes de llegar a los shaders
de la arena. Disparar una captura cuando esté visible el efecto:

```powershell
Set-Content diagnostics\arena\effects-next\trigger 'capture' -Encoding ascii
```

El cierre anterior y las capturas con corrupción permanecen en
`docs/NEXT_SESSION_FIFO.md` y `battle-arena-picture/`. Las capturas finales
curadas y versionadas están en `docs/img/battle-arena-*.png`; los dumps completos
son locales y están ignorados por Git.
