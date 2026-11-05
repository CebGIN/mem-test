# mem-test · Laboratorio de concurrencia y memoria

Aplicación TUI para Linux que configura, ejecuta, detiene y compara experimentos
controlados de concurrencia y memoria. La carga corre en un proceso hijo aislado;
el proceso principal muestrea `/proc/<pid>` y presenta los resultados sin
bloquearse ni arriesgar la sesión.

Proyecto de la materia Sistemas Operativos. Interfaz construida con
[GuayabaTUIEngine](https://github.com/CebGIN/GuayabaTUIEngine).

## Estado

**Aplicación Linux integrada.** WP0 (contratos y esqueleto), WP1 (métricas y
límites), WP2 (runtime y workloads), WP3 (UI, historial y CSV) y WP4 (worker
real, supervisor y controlador) están en `main`. La TUI usa el controlador real,
lanza `mem-test-worker` como proceso hijo y mantiene un monitor del sistema en
vivo: CPU, RAM, SWAP, load, fallos de página y cambios de contexto, incluso
cuando no hay una ejecución. Las cargas usan una duración real configurable y
guardan el impacto del sistema en el historial, la comparación y el CSV. El
soporte Windows quedó estacionado: el MVP compila solo en Linux.

| Documento | Contenido |
|---|---|
| [`docs/Requerimientos.md`](docs/Requerimientos.md) | Enunciado y pruebas exigidas |
| [`docs/Arquitectura.md`](docs/Arquitectura.md) | ADRs, componentes, interfaces, protocolo, medición, límites y pruebas |
| [`docs/diagrama.drawio`](docs/diagrama.drawio) | Componentes, clases, secuencia, estados y leyenda |

## Requisitos

- Linux (el proyecto usa `posix_spawn`, señales y `/proc`).
- CMake 3.14 o superior.
- Compilador con C++17.
- Git, para el submódulo del motor.
- Opcional: draw.io 24 o superior para editar/exportar el diagrama.

## Clonar

GuayabaTUIEngine es un submódulo de Git, fijado en el tag `v1.3.0-alpha`:

```bash
git clone --recurse-submodules <url-del-repositorio>
```

Si el repositorio ya estaba clonado:

```bash
git submodule update --init --recursive
```

## Compilar

```bash
cmake -S . -B build
cmake --build build
```

Targets:

| Target | Descripción |
|---|---|
| `memtest_core` | Núcleo headless (dominio, métricas, límites, runtime y workloads) |
| `memtest_ui` | Escena de la TUI sobre Guayaba |
| `mem-test` | Aplicación TUI |
| `mem-test-worker` | Proceso hijo que aplica límites y ejecuta la carga |
| `memtest_tests` | Suites headless registradas en CTest |

El motor se agrega como subdirectorio de CMake y se enlaza como
`GuayabaTUIEngine::GuayabaTUIEngine`. La API pública queda disponible con:

```cpp
#include <guayaba/Guayaba.hpp>
```

## Ejecutar

```bash
./build/mem-test
```

El worker lee una configuración `clave=valor` por `stdin`, aplica los límites,
ejecuta la carga y reporta eventos por `stdout`. Para probar únicamente los
límites, sin ejecutar la carga, se le pasa la misma configuración por `stdin`:

```bash
./build/mem-test-worker --probe < config.txt
```

La exportación CSV (F8) genera en `resultados/`:

- `resumen_YYYYMMDD-HHMMSS.csv`: una fila por ejecución, con configuración,
  resultado e impacto sobre el sistema.
- `detalle_<run_id>.csv`: la serie temporal de métricas del worker.

La pantalla principal muestra siempre el monitor del sistema y dos gráficas:
CPU total y una métrica seleccionable (RAM, SWAP, fallos por segundo o RSS de la
carga). Los cuatro experimentos se eligen con tabs superiores en el formulario:
**Concur.**, **CPU**, **Memoria** y **Compart.**. Al ejecutar una prueba, la
duración es real: la carga corre ese tiempo, salvo que la corte un límite, el
watchdog o el botón Detener.

Para comparar dos corridas, elegí una fila del historial y pulsá **Comparar**,
**Enter** (o doble clic) sobre la fila, o **F9**. Se abre una vista aparte dentro
de la misma escena: a la izquierda queda la ficha de esa ejecución y a la derecha
un selector compacto con las demás corridas; al elegir una, se muestra su ficha
con los deltas respecto de la primera. Las fichas llevan una insignia `A`/`B`,
el título se colorea según el resultado y el selector ocupa solo las filas que
necesita, así la segunda ficha queda pegada debajo. `Esc` o el botón `Volver`
regresan a la pantalla principal sin perder el formulario ni el historial.

## Pruebas

```bash
ctest --test-dir build --output-on-failure
```

Las pruebas son headless, no requieren red ni root y no dependen de Guayaba.
Las que necesitan un límite del sistema operativo se saltan si el entorno no lo
permite.

## Diagrama

Páginas del diagrama:

1. **Componentes**: frontera UI/worker, módulos y contratos compartidos.
2. **Clases · Dominio**: configuración, métricas, límites y resultados.
3. **Clases · Servicios**: interfaces, implementaciones y colaboradores.
4. **Secuencia**: orden de mensajes de una ejecución y salida por Detener.
5. **Estados**: máquina de estados de una ejecución.
6. **Leyenda**: notación UML, visibilidad y tipos de relación.

Las vistas previas en PNG están en [`docs/diagramas/README.md`](docs/diagramas/README.md).

## Estructura

```text
src/
  domain/     Tipos, enums y validación pura
  core/       Guardrails, historial, CSV, información del sistema
  runtime/    Pools de hilos, cola de tareas, cancelación
  workload/   Concurrencia, CPU, memoria y contador compartido
  metrics/    Lectura y parseo de /proc
  limits/     setrlimit, cgroup v2 y sondeo de capacidades
  process/    posix_spawn, pipes, señales y protocolo
  app/        Controlador, runner, historial y mocks
  ui/         Escenas, formularios, paneles y gráficas
  worker/     Punto de entrada del proceso hijo
tests/        Pruebas headless con CTest
docs/         Requerimientos, arquitectura y diagrama
```

## Seguridad

- La carga nunca se ejecuta en el proceso de la TUI.
- Los guardrails limitan memoria, hilos, duración y cantidad de ejecuciones.
- El worker se crea en un grupo de procesos propio y se limpia con
  `PR_SET_PDEATHSIG` y `SIGKILL` si no responde.
- La aplicación deja libre la mitad de la RAM disponible (y nunca supera la
  mitad de la RAM total) para el SO y la TUI.
- La cuota de CPU usa cgroup v2 `cpu.max`; si el entorno no permite crear el
  cgroup, la prueba se rechaza y se sugiere `RLIMIT_CPU`.
- Si un límite no se puede aplicar, se informa como `NotApplied` o
  `Unsupported`; la ejecución no se presenta como limitada.
