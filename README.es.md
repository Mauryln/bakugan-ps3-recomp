<div align="center">

<img src="docs/img/title.jpg" alt="Bakugan: Battle Brawlers como ejecutable nativo de Windows" width="100%">

<h1>Bakugan: Battle Brawlers — recompilación nativa de la versión PS3 para Windows</h1>

<p><strong>La versión de PlayStation 3 (<code>BLES00758</code>), recompilada estáticamente a un ejecutable nativo de Windows x64.</strong><br>
</p>

<p>
<img src="https://img.shields.io/badge/plataforma-Windows%20x64-0078D6?style=flat-square" alt="Plataforma: Windows x64">
<img src="https://img.shields.io/badge/gr%C3%A1ficos-Direct3D%2012-107C10?style=flat-square" alt="Gráficos: Direct3D 12">
<img src="https://img.shields.io/badge/t%C3%ADtulo-BLES00758-c0392b?style=flat-square" alt="Título: BLES00758">
<img src="https://img.shields.io/badge/licencia-MIT-2ea44f?style=flat-square" alt="Licencia: MIT">
<img src="https://img.shields.io/badge/status-Battle%20Arena%20gameplay%20fixed-green?style=flat-square" alt="Battle Arena gameplay fixed; effects and shaders pending">
</p>

<p><a href="README.md">English</a> · <a href="docs/STATUS.md">Estado</a> · <a href="docs/VIDEO.md">Vídeo</a> · <a href="docs/ARCHITECTURE.md">Arquitectura</a></p>

</div>

> **31/10/2026 — FPS fix gameplay.** Ya hay renderizado 3D y gameplay fluido
> en Battle Arena, con **unos 20 FPS de media** (estimación aproximada del usuario,
> no un benchmark): las vistas detalladas van a 16–18 FPS y el lanzamiento/control
> del Bakugan alcanza 28–30 FPS. Los menús recuperan 30 FPS y las cinemáticas van
> a 27–30 FPS. Se mantiene el **límite original de 30 FPS**. Quedan pequeños
> tirones de carga. [Mejoras y trabajo pendiente](docs/PERFORMANCE_2026-10-31.md).

> **31/10/2026 — Fixed gameplay Battle Arena.** Corregidos el bloqueo al cambiar
> de turno, el granulado del campo y las texturas de cartas. Turnos 1P/CPU
> verificados. [Informe y capturas](docs/BATTLE_ARENA_FIXED.md).
> Próximo trabajo: [efectos y shaders](docs/NEXT_SESSION_EFFECTS_SHADERS.md).

[Bakugan: Battle Brawlers](https://es.wikipedia.org/wiki/Bakugan:_Battle_Brawlers) (2009) nunca salió en PC. Este proyecto levanta el código **PPU (CPU) y SPU (DSP)** del juego a C++ de forma estática con [ps3recomp](https://github.com/sp00nznet/ps3recomp) y lo enlaza contra un runtime HLE pequeño: el resultado es un `.exe` normal que dibuja por Direct3D 12. Los menús alcanzan 30 FPS; el gameplay 3D de Battle Arena ronda los 20 FPS de media en pruebas del usuario, con optimizaciones pendientes.

Aquí no hay emulación: el código máquina se traduce una vez y después se compila como cualquier otro programa.

| | |
|---|---|
| ![Menús de arranque](docs/img/newgame.jpg) | ![Intro narrativa](docs/img/intro.jpg) |
| Flujo de título, aviso de guardado, carga, nueva partida / continuar | Intro narrativa — H.264/AAC con Media Foundation |

## Estado

| Hito | |
|---|---|
| Análisis del disco — 12.834 funciones OPD, 195 imports, 14 librerías | ✅ |
| Lift PPU — 15.682 funciones / 17 unidades de traducción | ✅ |
| Lift SPU — 5 imágenes, 1.331 funciones | ✅ |
| Tabla HLE — 1.204 handlers | ✅ |
| Build — clang-cl + Ninja + Direct3D 12, 30 FPS | ✅ |
| Assets — 607 trabajos de inflate descomprimidos en los SPU | ✅ |
| Logos de distribuidora — Activision, Nowpro, Premium Agency, Nelvana | ✅ |
| Apertura e intro narrativa — H.264/AAC con Media Foundation | ✅ |
| Pantalla de título, todos los menús, selector de modo, texturas 2D | ✅ |
| Teclado del juego (nombre) y pad de teclado/XInput | ✅ |
| Música — partituras PCM `SNDP` mezcladas en el SPU, más el AAC de los vídeos | ✅ |
| Battle Arena — turnos 1P/CPU, campo y cartas corregidos | ✅ |
| *Defecto conocido:* el "gate" del selector de modo — las capas 2D tras el retrato se solapan | ⚠️ |
| **Escena 3D** — campo visible; efectos, shaders y sombras pendientes | ⚠️ |
| **Gameplay** — tutorial e historia pendientes de validación | ⚠️ |
| **Efectos de sonido** — ATRAC3 en `SOUND/*.PAST` | ❌ |

**Battle Arena es jugable y el campo y las cartas se ven correctamente. Quedan efectos, shaders y rendimiento por mejorar.**

## Se busca ayuda

1. **Efectos y shaders.** Aislar diferencias de iluminación, transparencias, reflejos y composición. Las sombras necesitan un depth target offscreen real. Plan: [`docs/NEXT_SESSION_EFFECTS_SHADERS.md`](docs/NEXT_SESSION_EFFECTS_SHADERS.md).
2. **Lo que queda de audio.** Las partituras de menú y el mezclador SPU funcionan; los efectos `.PAST` son ATRAC3 y siguen en silencio, y quedan cortes por revisar. `libs/audio/`.
3. **Testers.** ¿Arranca en tu GPU/driver? Con un `run.log` y una frase basta para abrir un issue.

Se aceptan intentos a medias, hipótesis equivocadas y "esto lo hace RPCS3 así". Para algo grande, abre primero un issue.


## Cómo funciona

```
iso/*.iso  ──extract_iso──▶  game/  ──descifrado EBOOT──▶  EBOOT.elf
                                                          │
                             analysis/  ◀──ppu_loader─────┘
                                  │
                    ┌─────────────┴────────────┐
              lift PPU (17 TUs)           lift SPU (5 imágenes)
                    └─────────────┬────────────┘
                          bakugan-recomp/ + runtime HLE (toolkit)
                                  ▼
                      BakuganRecomp.exe  ──D3D12──▶  tu monitor
```

**Todo lo generado se reconstruye desde tu propio volcado del disco.** Este repositorio no contiene código del juego, ni assets, ni claves, ni código levantado.

## Compilar

Requisitos: Windows 10/11 x64 con Direct3D 12, Python 3.10+ y **Visual Studio 2022 Build Tools** con *Desktop development with C++* y el componente *C++ Clang Compiler for Windows*. Unos 15 GB libres.

```powershell
# 1. Tu propio volcado del disco, en iso/
#    "Bakugan - Battle Brawlers (Europe).iso"

# 2. Entorno de Python
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r ps3recomp-src\requirements.txt
.\.venv\Scripts\python.exe -m pip install cmake ninja

# 3. El toolkit, fijado al commit al que aplican nuestros parches
git clone https://github.com/sp00nznet/ps3recomp.git ps3recomp-src
git -C ps3recomp-src checkout a6d76c8

# 4. Regenerar todo (ISO -> ELF -> análisis -> lifts -> tabla HLE)
.\.venv\Scripts\python.exe tools\pipeline.py

# 5. Compilar y ejecutar
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
```

**Controles:** `X` / Espacio = Cross (confirmar) · `C` `Z` `S` = Circle / Square / Triangle · flechas = D-pad · `Enter` = Start · `Q` `E` = L1/R1 · `1` `2` = L2/R2.

## Estructura

| Ruta | Qué es |
|---|---|
| `bakugan-recomp/` | El port: punto de entrada, stubs HLE, vídeo D3D12, build CMake |
| `patches/` | Nuestros cambios al toolkit ps3recomp; los aplica el pipeline |
| `tools/` | Pipeline, build, lanzador y diagnóstico |
| `docs/` | Estado, vídeo, arquitectura |
| `ps3recomp-src/`, `game/`, `analysis/`, `bakugan-recomp/recompiled/` | Generado o ajeno — **no** versionado |

## Legal

Bakugan: Battle Brawlers es © de sus titulares. Este repositorio **no** distribuye código del juego, assets, claves de cifrado, código levantado ni binarios. Necesitas el disco original. El código propio del port es MIT; el toolkit es MIT de Ned Heller / sp00nznet — ver [CREDITS.md](CREDITS.md) y [LICENSE](LICENSE).
