# mem-test · Laboratorio de concurrencia y memoria

Aplicación TUI para Linux que configura, ejecuta, detiene y compara experimentos
controlados de concurrencia y memoria. La carga corre en un proceso hijo aislado;
el proceso principal muestrea `/proc/<pid>` y presenta los resultados sin
bloquearse ni arriesgar la sesión.

Proyecto de la materia Sistemas Operativos. Interfaz construida con
[GuayabaTUIEngine](https://github.com/CebGIN/GuayabaTUIEngine).

## Estado

**Base Linux integrada.** WP0 (contratos y esqueleto), WP1 (métricas y límites)
y WP2 (runtime y workloads) ya están en `main`. La TUI corre contra
`FakeExperimentController`; el worker real, el supervisor y la exportación CSV
llegan en WP4 y WP3. El soporte Windows quedó estacionado: el MVP compila solo
en Linux.

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
| `mem-test-worker` | Proceso hijo; hoy es el stub de WP0 |
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

El worker actual es el stub de WP0: lee una configuración `clave=valor` por
`stdin`, responde `ERROR code=not_implemented` y sale con 13. El worker real,
el supervisor y `--probe` se implementan en WP4.

La exportación CSV está prevista para WP3; todavía no está disponible. Cuando
se implemente, generará en `resultados/`:

- `resumen_YYYYMMDD-HHMMSS.csv`: una fila por ejecución.
- `detalle_<run_id>.csv`: la serie temporal de la ejecución.

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
  limits/     setrlimit y sondeo de capacidades
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
- La aplicación no está diseñada para agotar la memoria del equipo: el tope por
  defecto es el 25 % de la RAM, con un máximo de 1 GiB.
- Si un límite no se puede aplicar, se informa como `NotApplied` o
  `Unsupported`; la ejecución no se presenta como limitada.
