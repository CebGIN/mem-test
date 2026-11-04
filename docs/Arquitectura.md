# Arquitectura · Laboratorio de concurrencia y memoria

Estado: **diseño congelado para implementación**.
Documentos relacionados: [`Requerimientos.md`](Requerimientos.md) · [`diagrama.drawio`](diagrama.drawio).

---

## 1. Objetivo y principios

La aplicación permite configurar, ejecutar, detener y comparar experimentos controlados de concurrencia y memoria sobre Linux. La carga corre en un proceso hijo aislado para que los límites del sistema operativo se apliquen de verdad y para que un fallo de la carga no derribe la interfaz.

Principios que gobiernan el diseño:

1. **La UI no sabe de Linux.** Solo conoce `IExperimentController`. No incluye `process/`, `metrics/`, `limits/` ni `workload/`.
2. **El worker no sabe de UI.** Solo conoce dominio, límites, runtime y workloads. Se puede ejecutar y probar desde la terminal.
3. **`memtest_core` no depende de Guayaba.** Todo el núcleo se prueba headless; la TUI es una capa de presentación.
4. **Un contrato, una definición.** `ExperimentConfig`, `MetricsSnapshot`, `ExperimentResult` y `LimitReport` se definen una sola vez en `domain/` y los comparten ambos procesos.
5. **Los límites no se fingen.** Si el entorno no permite aplicar un límite, el resultado queda con `NotApplied` o `Unsupported` y la UI no presenta la ejecución como limitada.
6. **La carga es descartable.** El worker puede morir por `ENOMEM`, `SIGXCPU` o `SIGKILL`; el supervisor lo convierte en un resultado, no en una caída de la aplicación.
7. **Nada de lógica en los nodos Guayaba.** Los formularios leen y escriben configuración, emiten señales y muestran estado; el controlador decide.

## 2. Decisiones de arquitectura (ADR)

| ADR | Decisión | Motivo | Consecuencia |
|---|---|---|---|
| 01 | C++17 + GuayabaTUIEngine | El repositorio ya integra el motor y el enunciado no impone otro lenguaje. | La UI es TUI; no hay dependencias gráficas. |
| 02 | La carga corre en `mem-test-worker`, un proceso hijo | `RLIMIT_AS`, `RLIMIT_DATA` y `RLIMIT_CPU` son por proceso. Aplicarlos a la UI rompería la TUI y un OOM mataría la sesión. | Hay que implementar `posix_spawn`, pipes, protocolo y supervisión. |
| 03 | El supervisor muestrea `/proc/<pid>` desde el padre | No hace falta instrumentar el worker para métricas del sistema; se lee la fuente de verdad del kernel. | El padre necesita el `pid` del hijo y permisos de lectura sobre `/proc` (mismo usuario). |
| 04 | Los límites los aplica el propio worker y los verifica con `getrlimit` | El proceso que hereda el límite es el que lo sufre; el padre no puede aplicar `RLIMIT_*` a otro proceso. | El worker reporta `LIMITS` y el resultado registra `Applied/NotApplied/Unsupported`. |
| 05 | “Tareas administradas por un runtime” = pool fijo de hilos nativos | Es una distinción real y verificable: N tareas se reparten sobre un pool fijo de hilos nativos, sin correspondencia 1:1, sin escribir un scheduler completo. | Se implementan `NativeThreadPool` (1:1) y `TaskPool` (pool fijo + cola FIFO). |
| 06 | Guardrails de software independientes de los límites del SO | Evitan que un formulario mal cargado cuelgue la máquina, incluso si el límite del SO no se pudo aplicar. | La app calcula topes al iniciar y rechaza configuraciones fuera de rango. |
| 07 | Historial en memoria + exportación CSV | Cumple “repetir y comparar” sin agregar una base de datos. | Dos CSV: resumen y detalle. La sesión no persiste automáticamente. |
| 08 | Protocolo de líneas `clave=valor` por stdin/stdout | No requiere dependencias externas ni un parser JSON; el worker solo escribe y el padre solo lee. | El worker no recibe comandos por pipe: se controla con señales. |
| 09 | La cuota cgroup (`cpu.max`) es un extra fuera del MVP | En menos de dos semanas el riesgo de permisos/delegación es alto y el enunciado solo exige límite de CPU por tiempo. | El tipo y la interfaz existen desde el inicio; el control se muestra deshabilitado con “no soportado”. |
| 10 | La repetición es manual y se apoya en el historial | Cada ejecución es independiente y queda registrada con su configuración; repetir es relanzar una fila y comparar es leer las filas ya medidas. | El botón “Repetir” relanza la configuración de una fila del historial; el estado muestra la ejecución actual, no un progreso de serie. |

## 3. Vista de componentes

La página **1 · Componentes** de [`diagrama.drawio`](diagrama.drawio) muestra la frontera entre los dos procesos.

```text
┌──────────────────────── Proceso UI (mem-test) ─────────────────────────┐
│  ui/ → IExperimentController → app/                                     │
│                              ├── process/ PosixProcessSupervisor        │
│                              └── metrics/ ProcMetricsSource             │
└─────────────────────────────────────────────────────────────────────────┘
                    │ stdin: config          ▲ stdout: eventos
                    │ SIGTERM / SIGKILL      │
┌──────────────────────── Proceso worker (mem-test-worker) ──────────────┐
│  worker/ main → limits/ ResourceLimiter → runtime/ → workload/          │
└─────────────────────────────────────────────────────────────────────────┘
                    domain/ · contratos compartidos por ambos
```

Responsabilidades:

| Módulo | Responsabilidad | No hace |
|---|---|---|
| `domain/` | Tipos, enums, validación pura de configuración | No lee `/proc`, no crea procesos, no dibuja |
| `core/` | Guardrails, historial, exportación CSV, información del sistema | No conoce la UI ni el protocolo del worker |
| `runtime/` | Pools de hilos, cola de tareas, cancelación | No conoce el SO ni los workloads concretos |
| `workload/` | Las cuatro cargas y sus resultados específicos | No aplica límites ni dibuja |
| `metrics/` | Lectura y parseo de `/proc/<pid>` y `/proc/meminfo` | No decide límites ni muestra nada |
| `limits/` | `setrlimit`, sondeo de capacidades | No lanza la carga |
| `process/` | `posix_spawn`, pipes, señales, protocolo | No mide ni interpreta resultados de negocio |
| `app/` | Orquestación, estado vivo, historial | No dibuja ni lee `/proc` directamente |
| `ui/` | Formularios, paneles, gráficas, tabla comparativa | No crea procesos ni aplica límites |
| `worker/` | Punto de entrada del hijo, `ProbeMode` | No incluye Guayaba |

## 4. Modelo de dominio

La página **2 · Clases · Dominio** muestra las estructuras. Son tipos de datos puros, sin herencia y sin dependencias de plataforma.

```cpp
namespace memtest {

enum class WorkloadKind { Concurrency, CpuLimit, MemoryPressure, SharedCounter };
enum class WorkerMode   { NativeThreads, RuntimeTasks };
enum class SyncMode     { None, Mutex, Atomic };
enum class MemoryLimitKind { None, AddressSpace, DataSegment };
enum class RunOutcome   { Completed, Stopped, MemoryLimitHit, CpuLimitHit, ExternalKill, Error };
enum class LimitStatus  { NotRequested, Applied, NotApplied, Unsupported };
enum class CompareParameter { Threads, MemoryBytes, CpuSeconds }; // eje de la gráfica comparativa
enum class RunState     { Idle, Validating, Starting, Running, Stopping, Collecting, Done };

struct SystemInfo {
    std::uint64_t total_ram = 0;
    std::uint64_t available_ram = 0;
    std::uint64_t swap_total = 0;
    int cores = 0;
};

struct ValidationIssue {
    std::string field;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;
    bool ok() const { return issues.empty(); }
};

struct CpuLimitConfig {
    std::optional<int> seconds;        // RLIMIT_CPU: soft=N, hard=N+1
    std::optional<int> quota_percent;  // cpu.max; extra fuera del MVP
};

struct ExperimentConfig {
    WorkloadKind workload = WorkloadKind::Concurrency;
    WorkerMode worker_mode = WorkerMode::NativeThreads;
    SyncMode sync_mode = SyncMode::None;
    int threads = 4;
    std::uint64_t work_units = 100000;
    std::uint64_t increments_per_thread = 100000;
    std::uint64_t memory_bytes = 0;
    std::uint64_t page_stride_bytes = 4096;
    int wall_timeout_ms = 5000;
    MemoryLimitKind memory_limit_kind = MemoryLimitKind::None;
    std::uint64_t memory_limit_bytes = 0;
    CpuLimitConfig cpu_limit;
};

struct MetricsSnapshot {
    std::chrono::milliseconds elapsed{0};
    std::uint64_t vm_size = 0;      // VmSize
    std::uint64_t vm_rss = 0;       // VmRSS
    std::uint64_t vm_hwm = 0;       // VmHWM
    std::uint64_t vm_swap = 0;      // VmSwap
    std::uint64_t rss_anon = 0;     // RssAnon
    std::uint64_t rss_file = 0;     // RssFile
    std::uint64_t rss_shmem = 0;    // RssShmem
    std::uint64_t minor_faults = 0; // minflt acumulado
    std::uint64_t major_faults = 0; // majflt acumulado
    double cpu_user_s = 0.0;
    double cpu_sys_s = 0.0;
    int threads = 0;                // Threads
    std::uint64_t voluntary_ctxt = 0;
    std::uint64_t involuntary_ctxt = 0;
    std::uint64_t sys_swap_total = 0;
    std::uint64_t sys_swap_free = 0;
    double load_1m = 0.0;
};

struct LimitReport {
    MemoryLimitKind memory_kind = MemoryLimitKind::None;
    LimitStatus memory_status = LimitStatus::NotRequested;
    std::uint64_t memory_applied = 0;

    LimitStatus cpu_seconds_status = LimitStatus::NotRequested;
    int cpu_seconds_applied = 0;

    LimitStatus cpu_quota_status = LimitStatus::NotRequested;
    int cpu_quota_applied = 0;
    std::uint64_t nr_throttled = 0;
    std::uint64_t throttled_usec = 0;
};

struct ExperimentResult {
    std::string run_id;
    ExperimentConfig config;
    LimitReport limits;
    RunOutcome outcome = RunOutcome::Error;
    std::string detail;

    MetricsSnapshot peak;
    MetricsSnapshot last;
    std::vector<MetricsSnapshot> series;

    std::uint64_t expected_value = 0;
    std::uint64_t observed_value = 0;
    std::chrono::milliseconds duration{0};
    int exit_code = 0;
    int term_signal = 0;
};

} // namespace memtest
```

Reglas:

- Todos los tamaños son **bytes** en el dominio. La UI convierte a MiB solo al mostrar y al leer el formulario.
- Los tiempos de CPU son segundos `double`; la duración de pared es `milliseconds`.
- `expected_value` y `observed_value` solo tienen sentido en `SharedCounterWorkload` y en `MemoryPressureWorkload` (páginas esperadas vs tocadas). En los demás quedan en cero.
- `peak` es el máximo campo a campo de la serie; `last` es la última muestra válida.
- La serie se acota a 2000 muestras. Con 250 ms son 500 s, más que el máximo de 120 s del guardrail.

## 5. Interfaces y adaptadores

La página **3 · Clases · Servicios** muestra las interfaces y sus implementaciones. Estas son las fronteras que se congelan el día 1.

```cpp
// metrics/IMetricsSource.hpp
class IMetricsSource {
public:
    virtual ~IMetricsSource() = default;
    virtual bool sample(pid_t pid, MetricsSnapshot& out) const = 0;
    virtual SystemInfo system_info() const = 0;
};

// process/IProcessSupervisor.hpp
struct WorkerEvent {
    enum class Type { Hello, Ready, Limits, Progress, Result, Error };
    Type type;
    std::map<std::string, std::string> fields;
};

struct ProcessExit {
    int exit_code = -1;
    int term_signal = 0;
    bool timed_out = false;
};

class IProcessSupervisor {
public:
    virtual ~IProcessSupervisor() = default;
    virtual bool spawn(const ExperimentConfig&, std::string* error) = 0;
    virtual bool running() const = 0;
    virtual pid_t pid() const = 0;
    virtual std::vector<WorkerEvent> poll_events() = 0;
    virtual void request_stop() = 0;
    virtual std::optional<ProcessExit> wait_for_exit(std::chrono::milliseconds) = 0;
};

// runtime/IWorkerPool.hpp
class IWorkerPool {
public:
    virtual ~IWorkerPool() = default;
    virtual void parallel_for(
        std::size_t task_count,
        const std::function<void(std::size_t, const CancellationToken&)>& body) = 0;
    virtual std::size_t worker_count() const = 0;
};

std::unique_ptr<IWorkerPool> make_worker_pool(WorkerMode mode, int tasks);

// workload/IWorkload.hpp
struct WorkloadContext {
    const ExperimentConfig* config;
    const CancellationToken* cancel;
    std::function<void(std::uint64_t done, std::uint64_t total)> progress;
};

struct WorkloadOutcome {
    RunOutcome outcome = RunOutcome::Completed;
    std::uint64_t expected = 0;
    std::uint64_t observed = 0;
    std::string detail;
};

class IWorkload {
public:
    virtual ~IWorkload() = default;
    virtual WorkloadOutcome run(const WorkloadContext&) = 0;
};

std::unique_ptr<IWorkload> make_workload(WorkloadKind kind);

// app/IExperimentController.hpp
struct LiveState {
    RunState state = RunState::Idle; // Idle, Validating, Starting, Running, Stopping, Collecting, Done
    MetricsSnapshot last;
    std::vector<MetricsSnapshot> recent; // últimas N para la gráfica
    std::string message;
};

class IExperimentController {
public:
    virtual ~IExperimentController() = default;
    virtual ValidationReport validate(const ExperimentConfig&) const = 0;
    virtual bool start(const ExperimentConfig&, std::string* error) = 0;
    virtual void stop() = 0;
    virtual LiveState live() const = 0;
    virtual std::vector<ExperimentResult> take_results() = 0;
    virtual std::vector<ExperimentResult> history() const = 0;
    virtual bool export_csv(const std::filesystem::path& dir, std::string* error) = 0;
};
```

Reglas de propiedad y threading:

- `IExperimentController` es la **única** puerta de la UI. Se usa desde el hilo de la TUI; sus métodos de lectura son baratos y no bloquean.
- `start()` no bloquea: lanza el hilo de ejecución y vuelve. `stop()` solo marca la solicitud.
- “Repetir” usa `start()` con una copia del `config` del `ExperimentResult` seleccionado. Cada ejecución es independiente y se compara contra las anteriores en el historial.
- `live()` devuelve una copia protegida por mutex. `take_results()` drena una cola thread-safe. `history()` devuelve una copia; el historial completo no se comparte por referencia mutable entre hilos.
- La UI nunca conserva punteros a `ExperimentResult` que puedan quedar invalidados por un `push_back`; usa `NodeRef`/ids y refresca por frame.
- El supervisor y el runner viven en el hilo de ejecución; la UI no los toca.
- `IWorkerPool::parallel_for` captura las excepciones de las tareas y las reporta al llamador; la cancelación impide programar tareas nuevas, pero espera a las que ya empezaron.

## 6. Ciclo de vida, secuencia y estados

La página **4 · Secuencia** muestra el orden de los mensajes. La página **5 · Estados** muestra la máquina de estados de una ejecución.

### 6.1 Ejecución individual

```text
Idle → Validating → Starting → Running → Collecting → Done
         │            │          │
         │            └→ Error   ├→ Stopping → Collecting
         └→ Idle (config inválida)└→ MemoryLimitHit / CpuLimitHit / ExternalKill
```

- `Validating`: `Guardrails::validate`. Si falla, se muestran los errores y no se lanza nada.
- `Starting`: `spawn` del worker, envío de la config por stdin y espera de `READY`/`LIMITS` con un timeout corto (2 s).
- `Running`: muestreo cada 250 ms, drenaje de eventos, watchdog de pared.
- `Stopping`: se envió `SIGTERM`; se espera hasta 500 ms y se escala a `SIGKILL`.
- `Collecting`: se arma el `ExperimentResult` con la serie, el pico, la última muestra, los límites y el resultado del worker.
- `Done`: el resultado se publica en la cola y se actualiza el historial.

Los cinco estados de salida acordados son `Completed`, `Stopped`, `MemoryLimitHit`, `CpuLimitHit` y `Error`. `ExternalKill` es un caso distinguido de `Error` (muerte por `SIGKILL` no enviado por el padre, típicamente OOM) que se muestra por separado para no perder la causa.

Cada ejecución es independiente: al terminar, el resultado se agrega al historial y el estado vuelve a `Idle`. Comparar configuraciones es elegir filas del historial.

## 7. Protocolo supervisor ↔ worker

### 7.1 Configuración (padre → hijo)

El padre escribe **una línea** por `stdin` y cierra el pipe. Formato `clave=valor`, separado por espacios; los valores no contienen espacios (los textos libres no se envían en la config).

```text
workload=concurrency worker_mode=runtime sync_mode=none threads=4
work_units=100000 increments=0 memory_bytes=0 page_stride=4096
wall_timeout_ms=5000 memory_limit_kind=as memory_limit_bytes=268435456
cpu_seconds=3 cpu_quota=0
```

### 7.2 Eventos (hijo → padre)

Una línea por evento, terminada en `\n`, con un límite de 4 KiB por línea:

```text
HELLO worker=1 pid=4242
READY baseline_vm=15204352 baseline_rss=8912896
LIMITS memory=applied:268435456 cpu_seconds=applied:3 cpu_quota=unsupported
PROGRESS done=500 total=1000
RESULT outcome=completed expected=400000 observed=397123
ERROR code=mmap_failed detail="cannot allocate memory"
END
```

- `HELLO` es lo primero que escribe el worker.
- `READY` confirma que los límites se aplicaron y que el workload va a empezar; incluye la línea base.
- `LIMITS` puede repetirse si el worker aplica más de un mecanismo. Estados: `applied`, `not_applied`, `unsupported`, `not_requested`.
- `PROGRESS` es informativo; el padre no lo usa para calcular métricas.
- `RESULT` es obligatorio en una salida limpia. `ERROR` reemplaza a `RESULT` si el worker no pudo completar.
- `END` cierra el protocolo; el padre no espera más líneas después.

### 7.3 Señales y códigos de salida

| Señal / código | Significado | Acción del padre |
|---|---|---|
| `SIGTERM` (padre → hijo) | Solicitud de detención cooperativa | El worker termina el workload en el próximo checkpoint y sale con 12 |
| `SIGXCPU` (kernel → hijo) | Se alcanzó el límite blando de CPU | El worker marca la cancelación, reporta `cpu_limit` y sale con 11 |
| `SIGKILL` (padre → hijo) | El worker no respondió en 500 ms | El resultado es `Stopped` (escalado), no `ExternalKill` |
| `SIGKILL` (kernel → hijo) | OOM killer u otra causa externa | El resultado es `ExternalKill`, detalle “posible OOM” |
| 0 | Ejecución completada | `Completed` |
| 10 | Límite de memoria alcanzado | `MemoryLimitHit` |
| 11 | Límite de CPU alcanzado | `CpuLimitHit` |
| 12 | Cancelada por el usuario | `Stopped` |
| 13 | Error del worker | `Error` |

Si el worker muere por una señal distinta de las anteriores y el padre no la envió, se reporta `ExternalKill` con el número de señal en `term_signal`. El padre distingue “SIGKILL que envié yo” con una bandera interna de escalado.

### 7.4 Cierre y limpieza

- El padre lee `stdout` y `stderr` del hijo. `stderr` se conserva en un buffer acotado (últimos 4 KiB) y se usa como `detail` si el worker no reporta un error estructurado.
- Al terminar, el padre cierra los pipes, `waitpid` reap the child y libera la entrada de la sesión.
- El worker llama a `prctl(PR_SET_PDEATHSIG, SIGKILL)` al arrancar para no quedar huérfano si la UI muere. La carrera entre el `posix_spawn` y el `prctl` se acepta y se documenta; el padre además hace `waitpid` en su hilo de ejecución.
- El supervisor crea un grupo de procesos propio (`POSIX_SPAWN_SETPGROUP`) y envía señales al grupo (`kill(-pid, ...)`), no solo al líder.

## 8. Medición

### 8.1 Fuentes

| Dato | Fuente | Notas |
|---|---|---|
| VSZ, RSS, RSS pico, SWAP del proceso | `/proc/<pid>/status` | `VmSize`, `VmRSS`, `VmHWM`, `VmSwap` |
| RSS anónimo, de archivo, compartido | `/proc/<pid>/status` | `RssAnon`, `RssFile`, `RssShmem` |
| Fallos de página | `/proc/<pid>/stat` | Campos 10 (`minflt`) y 12 (`majflt`); acumulados |
| CPU de usuario y sistema | `/proc/<pid>/stat` | Campos 14 y 15; convertir ticks con `sysconf(_SC_CLK_TCK)` |
| Hilos vivos | `/proc/<pid>/status` | `Threads` |
| Cambios de contexto | `/proc/<pid>/status` | `voluntary_ctxt_switches`, `nonvoluntary_ctxt_switches` |
| SWAP del sistema | `/proc/meminfo` | `SwapTotal`, `SwapFree`; si `SwapTotal == 0`, SWAP no disponible |
| Carga del sistema | `/proc/loadavg` | Primer campo |
| RAM y núcleos | `sysinfo(2)` / `sysconf` | Para guardrails y para la línea base |

El parser de `/proc/<pid>/stat` **no** puede separar por espacios a ciegas: el campo `comm` está entre paréntesis y puede contener espacios. Se busca el último `)` y se parsean los campos desde ahí. Hay un test específico con un `comm` con espacios.

### 8.2 Unidades y presentación

- El dominio usa bytes y segundos. La UI muestra MiB (`1 MiB = 1 048 576 bytes`), segundos con dos decimales y porcentajes con un decimal.
- La tabla y los CSV usan las mismas unidades que el dominio (bytes, segundos, milisegundos) para no perder precisión; la UI formatea.
- La serie temporal se muestrea cada **250 ms**. En la UI se grafican las últimas 120 muestras (30 s); el resultado conserva hasta 2000.
- Los fallos de página son acumulados del proceso. La UI muestra el total y, si hay una muestra anterior, el delta del intervalo.
- El SWAP del proceso (`VmSwap`) y el del sistema son números distintos. La UI los muestra por separado. Si no hay swap, se muestra “SWAP no disponible”, nunca un cero ambiguo.

### 8.3 Línea base

El worker envía `READY baseline_vm=... baseline_rss=...` después de aplicar límites y antes de ejecutar el workload. Esa línea base permite:

- avisar en el formulario si `baseline + memory_bytes` no entra en `memory_limit_bytes`;
- calcular el crecimiento de RSS atribuible a la carga;
- distinguir “el worker ni siquiera pudo arrancar” de “el workload falló al reservar”.

## 9. Límites

### 9.1 Memoria

- `MemoryLimitKind::AddressSpace` → `setrlimit(RLIMIT_AS, {limit, limit})`.
- `MemoryLimitKind::DataSegment` → `setrlimit(RLIMIT_DATA, {limit, limit})`.
- `MemoryLimitKind::None` → no se toca el límite.
- El valor es **absoluto**: es el tope que ve el sistema operativo para el proceso worker, no un presupuesto adicional sobre la línea base. El formulario tiene dos campos independientes: “límite del proceso” y “memoria a reservar”.
- Si `setrlimit` falla, el worker responde `LIMITS memory=not_applied` y continúa. El resultado queda con `NotApplied`; la UI muestra “sin límite aplicado”. No se presenta como una prueba limitada.
- `RLIMIT_AS` incluye bibliotecas, stack y mapeos; por eso el límite debe superar la línea base con margen. La validación avisa si `memory_limit_bytes < baseline_estimado + memory_bytes`.
- `RLIMIT_DATA` puede comportarse distinto según la versión del kernel; el resultado registra el tipo aplicado y la versión no se interpreta como equivalente a AS.

### 9.2 CPU por tiempo

- `cpu_limit.seconds = N` (entero ≥ 1) → `setrlimit(RLIMIT_CPU, {N, N + 1})`.
- El worker instala un handler para `SIGXCPU` que solo pone una bandera `volatile sig_atomic_t`. El bucle del workload consulta esa bandera y el `CancellationToken`.
- Al alcanzar el blando, el worker reporta `outcome=cpu_limit` y sale con 11. Si no llega a reportar, el duro (`N+1`) manda `SIGKILL`; el padre lo interpreta como `CpuLimitHit` si el tiempo de CPU medido está dentro del margen del límite duro y no fue un kill pedido por el padre.
- `RLIMIT_CPU` cuenta el tiempo de CPU de **todos los hilos** del proceso. No cambia la cantidad de núcleos ni la afinidad; el enunciado lo pide explícitamente así.

### 9.3 CPU por cuota (fuera del MVP)

- `cpu_limit.quota_percent = P` → cgroup v2 `cpu.max` con `quota = P% * period`, `period = 100000 µs`.
- Requiere un cgroup delegado con permiso de escritura. El sondeo (`mem-test-worker --probe`) comprueba: existe `/sys/fs/cgroup/cgroup.controllers`, se puede crear un subcgroup, escribir `cpu.max` y mover el proceso a `cgroup.procs`.
- Si no se puede, `LimitStatus::Unsupported`; la UI deshabilita el control y muestra el motivo.
- La cuota **no mata**: estrangula. El resultado sigue siendo `Completed` y se agregan `nr_throttled` y `throttled_usec` leídos de `cpu.stat`. La UI muestra una insignia “cuota aplicada · N throttles”.
- Puede convivir con `RLIMIT_CPU`: el resultado registra ambos estados por separado y el detalle indica cuál cortó si el proceso terminó por `SIGXCPU`.

### 9.4 Sondeo de límites

`mem-test-worker --probe` lee una configuración por stdin, aplica los límites, reporta `LIMITS`, no ejecuta ningún workload y sale con 0. El controlador lo usa para:

- el botón “Probar límites” del formulario;
- la validación previa al `start`;
- detectar `Unsupported` sin lanzar una ejecución completa.

El estado que se muestra en el resultado es el que reporta el worker en la ejecución real, no el del sondeo. El sondeo es informativo.

## 10. Guardrails

Se calculan al iniciar con `SystemInfo` y se muestran en la UI. No son editables en el MVP.

| Regla | Valor | Motivo |
|---|---|---|
| Memoria máxima solicitada o límite | `min(1 GiB, 25% de RAM total)` | Deja margen para el SO, la TUI y el propio worker |
| Hilos / tareas | `min(8 × núcleos, 64)` | Evita saturar la tabla de hilos en equipos grandes |
| Duración de pared | ≤ 120 s | Una ejecución no puede quedar colgada |
| Segundos de CPU | 1..120 si está activo | `RLIMIT_CPU` tiene granularidad de segundos |
| Memoria a reservar | ≥ 0 y ≤ tope de memoria | No se permite pedir más que el guardrail |
| Hilos | ≥ 1 | Un pool vacío no es un experimento |
| Unidades / incrementos | ≥ 1 | Evita ejecuciones triviales por error |

Además, la UI valida:

- que el límite de memoria sea mayor que 0 si se seleccionó un tipo distinto de `None`;
- que `memory_bytes` y `memory_limit_bytes` no se confundan (son campos distintos);
- que la cuota, si está habilitada, sea un porcentaje entre 1 y 100.

Una configuración inválida **no lanza** el worker. El botón “Iniciar” se deshabilita y el error se muestra junto al campo.

## 11. Concurrencia y cancelación

### 11.1 Hilos

| Contexto | Quién | Qué hace |
|---|---|---|
| Hilo de la TUI | Guayaba | Dibuja, lee `live()`, drena resultados; nunca bloquea |
| Hilo de ejecución | `ExperimentController` | `spawn`, muestreo, watchdog, `waitpid`, armado del resultado |
| Hilos del worker | `NativeThreadPool` o `TaskPool` | Ejecutan las tareas del workload |
| Hilo del worker (principal) | `worker/main.cpp` | Aplica límites, instala señales, espera a los pools, reporta |

No hay un hilo de muestreo separado: el hilo de ejecución alterna muestreo y `wait_for_exit(50ms)`. Eso mantiene el diseño simple y suficiente para 250 ms.

### 11.2 Cancelación

- `CancellationToken` es un `std::atomic<bool>` con `cancel()` y `cancelled()`.
- El handler de `SIGTERM`/`SIGXCPU` solo escribe `g_signal_flag` (`volatile sig_atomic_t`). El worker traduce esa bandera a la cancelación cooperativa.
- Los workloads consultan el token cada 256 unidades de trabajo o cada 64 páginas tocadas, lo que ocurra primero. `parallel_for` deja de programar tareas nuevas y espera a las que ya empezaron.
- El padre envía `SIGTERM` al grupo y espera 500 ms. Si el hijo sigue vivo, envía `SIGKILL` y marca el resultado como `Stopped` con detalle de escalado.
- El watchdog de pared usa el mismo camino: `request_stop()` y, si no termina, `SIGKILL`.

### 11.3 Sincronización del estado

- `LiveState` se protege con `std::mutex`; la UI pide una copia por frame.
- La cola de resultados es `std::deque<ExperimentResult>` + mutex + condition_variable opcional; la UI drena sin bloquear.
- El historial se guarda en el controlador; `history()` devuelve una copia. La exportación se hace en el hilo de la UI sobre una copia, o en el hilo de ejecución al terminar cada ejecución; se elige lo primero para no bloquear el cierre.
- No se comparten referencias a `ExperimentConfig` entre hilos: se copia al lanzar.

## 12. Workloads

### 12.1 Concurrencia

- Cada tarea ejecuta un bucle de trabajo determinista (hash entero) `work_units` veces y acumula un checksum local.
- `NativeThreads`: `threads` tareas = `threads` hilos.
- `RuntimeTasks`: `threads` tareas sobre un pool fijo de `min(threads, max(1, núcleos))` hilos nativos, con cola FIFO; el resultado muestra ambos números.
- Métricas esperadas: duración, CPU, cambios de contexto, hilos vivos. No hay un valor “correcto” que comparar; el experimento es la comparación entre configuraciones.

### 12.2 Límite de CPU

- Misma carga CPU-bound, sin allocaciones grandes.
- `cpu_seconds = N` corta a los N segundos de CPU; `quota_percent` (si se enciende) estrangula.
- Resultado esperado: `CpuLimitHit` con el tiempo de CPU consumido y la duración de pared; o `Completed` si el trabajo termina antes.

### 12.3 Presión de memoria

- `mmap(nullptr, memory_bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)`.
- Toque de una página cada `page_stride_bytes` (por defecto 4096) para forzar el fallo de página y la residency.
- Opcional: mantener las páginas durante una pausa corta para que el SO tenga oportunidad de swapear; la pausa se controla con `wall_timeout_ms` y la cancelación.
- `munmap` al final. Si `mmap` devuelve `MAP_FAILED` con `ENOMEM`, el workload reporta `MemoryLimitHit`.
- `expected_value = ceil(memory_bytes / page_stride_bytes)` (páginas objetivo), `observed_value` = páginas tocadas. Si el tamaño no es múltiplo exacto del paso, la última página también cuenta.
- Si `memory_bytes == 0`, el workload no hace nada y termina `Completed` con una advertencia en `detail`.

### 12.4 Acceso compartido y sincronización

- `tasks = threads`, `increments = increments_per_thread`; cada tarea incrementa un contador compartido.
- `SyncMode::None`: `load(relaxed)`, suma local, `store(relaxed)` sin CAS. No hay data race según el modelo de C++ (las operaciones son atómicas), pero sí actualizaciones perdidas reales. Se intercala `std::this_thread::yield()` cada 1024 incrementos para aumentar la contención.
- `SyncMode::Mutex`: `std::mutex` + `std::lock_guard`.
- `SyncMode::Atomic`: `fetch_add(1, std::memory_order_relaxed)`.
- `expected_value = threads * increments`; `observed_value = counter`; `detail` incluye `lost = expected - observed`.
- El resultado de `None` no se considera un fallo: es el comportamiento observado. La UI lo muestra como “actualizaciones perdidas” y permite comparar con mutex/atómico.

## 13. UI

La página **1 · Componentes** y el guion de demo del plan de trabajo describen la disposición. La UI se construye con nodos Guayaba:

- `SplitContainer` horizontal: zona izquierda (formulario) y zona derecha (métricas + historial).
- `PanelContainer` + `VBox` para cada zona.
- Selector de experimento: `ListView` con las cuatro opciones.
- Campos: `SpinBox` para números, `CheckBox` para límites opcionales, `ListView` o fila de `Button` para enums.
- Acciones: `Button` Iniciar, Detener, Repetir, Exportar. “Iniciar” se deshabilita si la validación falla o hay una ejecución activa. “Repetir” recarga en el formulario la configuración de la fila seleccionada y la lanza de nuevo.
- Métricas: `Label` por métrica y `Graph` para la serie temporal (RSS y CPU; el usuario puede cambiar la métrica).
- Historial: `ListView` con una fila formateada por ejecución; selección de dos o más filas para comparar.
- Comparación: `Graph` de la métrica elegida contra el parámetro seleccionado (`CompareParameter`) y tabla.
- Estado: `Label` con el estado de la ejecución, insignias de límites y mensaje de error/estado.

Reglas de presentación:

- Los textos de la UI están en español.
- Las unidades se muestran siempre: `MiB`, `s`, `ms`, `%`.
- Los límites no aplicados se muestran como `sin límite` o `no soportado`, nunca como si estuvieran activos.
- Un `MemoryLimitHit` o `CpuLimitHit` se pinta como resultado esperado (ámbar), no como error rojo.
- La gráfica no dibuja puntos fuera de la serie; si aún no hay muestras, muestra “esperando muestras”.

### 13.1 Árbol de nodos Guayaba

```text
MainScene
└─ VBox root
   ├─ HBox header
   │  ├─ Label "mem-test"
   │  ├─ Label estado (Idle / Ejecutando / Detenida)
   │  └─ Label guardrails (RAM, hilos, duración, ejecuciones)
   ├─ SplitContainer body
   │  ├─ PanelContainer izquierda
   │  │  └─ VBox
   │  │     ├─ ListView selector de experimento
   │  │     ├─ VBox formulario (se reemplazan las filas según el experimento)
   │  │     │  ├─ HBox Label + SpinBox (hilos, unidades, MiB, segundos)
   │  │     │  ├─ HBox Label + ListView/Button (worker, sync)
   │  │     │  └─ HBox CheckBox + SpinBox (límite CPU/memoria)
   │  │     ├─ Label errores de validación
   │  │     └─ HBox acciones
   │  │        ├─ Button Iniciar
   │  │        ├─ Button Detener
   │  │        ├─ Button Repetir
   │  │        └─ Button Exportar
   │  └─ VBox derecha
   │     ├─ PanelContainer métricas
   │     │  ├─ GridContainer etiquetas (VSZ, RSS, pico, SWAP, fallos, CPU, hilos, ctxt)
   │     │  └─ Graph serie temporal
   │     ├─ PanelContainer historial
   │     │  └─ ListView filas de ejecuciones
   │     └─ PanelContainer comparación
   │        ├─ Graph métrica vs parámetro
   │        └─ GridContainer tabla comparativa
   └─ Label status bar (mensaje, límites aplicados, estado)
```

La UI no crea ni destruye workers: solo llama a `start`, `stop` y `export_csv` en `IExperimentController`. Los formularios se reconstruyen al cambiar de experimento; el estado de cada campo vive en un `ExperimentConfig` local hasta que se pulsa Iniciar.

## 14. Persistencia y exportación

- `ResultStore` mantiene el historial de la sesión en memoria. No hay base de datos.
- `export_csv(dir)` crea `dir` si no existe y escribe:
  - `resumen_YYYYMMDD-HHMMSS.csv`: una fila por ejecución.
  - `detalle_<run_id>.csv`: una fila por muestra de la ejecución seleccionada.
- Columnas del resumen (mínimo):

```text
run_id, timestamp, workload, worker_mode, sync_mode, threads, work_units,
increments, memory_bytes, page_stride, memory_limit_kind, memory_limit_bytes,
cpu_seconds, cpu_quota, outcome, duration_ms, vm_size_peak, vm_rss_peak,
vm_hwm, vm_swap_peak, minor_faults, major_faults, cpu_user_s, cpu_sys_s,
threads_peak, voluntary_ctxt, involuntary_ctxt, expected, observed,
limit_memory_status, limit_cpu_status, detail
```

- Columnas del detalle:

```text
run_id, elapsed_ms, vm_size, vm_rss, vm_hwm, vm_swap, rss_anon, rss_file,
rss_shmem, minor_faults, major_faults, cpu_user_s, cpu_sys_s, threads,
voluntary_ctxt, involuntary_ctxt, load_1m
```

- Los campos de texto se escapan con comillas dobles y se duplican las comillas internas. Se usa coma como separador y punto como separador decimal. UTF-8 sin BOM.
- Si la escritura falla, `export_csv` devuelve `false` y un mensaje; la UI no se cierra ni pierde el historial.

## 15. Mapeo de requisitos

| Requisito del enunciado | Componente | Evidencia / prueba |
|---|---|---|
| Interfaz TUI para configurar, iniciar, detener y repetir | `ui/`, `IExperimentController` | Demo del guion; formulario adaptativo; botones |
| Cambiar cantidad de hilos y tipo (nativos vs runtime) | `ExperimentConfig`, `IWorkerPool`, `TaskPool` | Prueba de concurrencia con 1/2/4/8; `threads` reales en el resultado |
| Carga que reserva memoria virtual y toca páginas | `MemoryPressureWorkload` | VSZ/RSS y fallos de página antes/después |
| Límites de memoria y CPU aplicables, con aviso si no | `ResourceLimiter`, `LimitProbe`, `LimitReport` | Ejecución con AS/DATA y CPU; estados `Applied/NotApplied/Unsupported` |
| Mostrar RSS, VSZ, fallos, SWAP, duración y resultado | `ProcMetricsSource`, `MetricsPanel` | Panel en vivo + fila de historial + CSV |
| Repetir y comparar configuraciones | `ComparePanel`, `ResultStore` | Repetición manual de `1,2,4,8` y tabla/gráfica comparativa |
| Distinguir VM, RAM residente, SWAP y fallos | `MetricsSnapshot`, UI | Etiquetas y unidades explícitas; SWAP “no disponible” si falta |
| Carga controlada, detenible y sin agotar el equipo | Guardrails, `SIGTERM`, watchdog | Detener en <500 ms + resultados parciales; topes de 25% RAM |
| Aplicación ejecutable para revisión | CMake, README, worker | `cmake --build` + `./build/mem-test`; `ctest` |

## 16. Estrategia de pruebas

No se agrega un framework externo: un `CHECK` mínimo en `tests/TestHarness.hpp` y objetivos de CTest.

| Prueba | Tipo | Qué verifica |
|---|---|---|
| `test_guardrails` | unitaria | Topes dinámicos, configuraciones inválidas, límites de hilos/memoria/duración |
| `test_proc_metrics` | unitaria + integración | Parseo de fixtures de `/proc` y de un hijo real que asigna memoria |
| `test_protocol_roundtrip` | unitaria | Codificación/decodificación de config y eventos; líneas malformadas; límite de 4 KiB |
| `test_workload_sync` | unitaria | `None` pierde actualizaciones; mutex/atómico no; cancelación |
| `test_process_supervisor` | integración | `spawn`, `READY`, `SIGTERM`, escalado a `SIGKILL`, `waitpid` |
| `test_limits` | integración | `RLIMIT_AS` hace fallar una reserva; `RLIMIT_CPU` corta una carga CPU-bound |
| `test_integration_smoke` | integración | Ejecución real con 1 hilo y trabajo pequeño; resultado con métricas no nulas |
| Aceptación manual | manual | Las cuatro pruebas del enunciado, con las configuraciones del plan |

Criterios de aceptación transversales:

- `ctest --output-on-failure` pasa en menos de 60 s en una máquina de 4 núcleos.
- La app no deja procesos worker huérfanos (`pgrep mem-test-worker` vacío al cerrar).
- Ningún test requiere red, root ni un cgroup delegado; las pruebas de cuota se saltan si no está disponible.

## 17. Riesgos y no objetivos

| Riesgo | Impacto | Mitigación |
|---|---|---|
| `RLIMIT_CPU` granularidad de segundos | El límite de 1 s puede ser brusco | Documentar; usar soft=N, hard=N+1; medir CPU real |
| `RLIMIT_AS` incluye bibliotecas y stack | El límite absoluto puede fallar antes de lo esperado | Mostrar línea base; validar con margen; dos campos separados |
| `RLIMIT_DATA` depende del kernel | Comparaciones engañosas | Registrar el tipo; no prometer equivalencia con AS |
| SWAP ausente en la máquina | `VmSwap` siempre 0 | Mostrar “SWAP no disponible”; no interpretar 0 como medición |
| `/proc/<pid>/stat` con `comm` con espacios | Parseo incorrecto | Buscar el último `)`; test específico |
| Carrera intencional en `SyncMode::None` | Resultado no determinista | Usar load/store atómicos relajados; documentar que no es UB; comparar tendencia |
| Worker huérfano | Consumo de recursos tras cerrar | `PR_SET_PDEATHSIG`, grupo de procesos, `SIGKILL` de limpieza |
| El entorno no permite `cpu.max` | La cuota no se puede mostrar | `Unsupported` + control deshabilitado; el MVP no depende de ella |
| Ramas largas | Merge tardío | Ramas de 1–2 días, integración el día 5 y el día 8, contratos congelados |
| Plazo de menos de 2 semanas | Recortes a último momento | Líneas de corte definidas en el plan; nunca recortar límites/métricas/stop |

No objetivos del MVP:

- GUI, web, ejecución remota o multiusuario.
- Forzar el uso de swap o agotar la memoria del equipo.
- Persistencia automática entre sesiones (solo CSV manual).
- Cuota cgroup obligatoria.
- Perfiles de rendimiento a nivel de instrucción.

## 18. Verificación del diagrama

El archivo [`diagrama.drawio`](diagrama.drawio) se genera con un script y no se edita a mano. La verificación automática comprueba:

- que ningún segmento de arista atraviese el interior de una figura semántica;
- que no haya cruces entre aristas en las páginas de componentes, clases y estados;
- que no se solapen dos figuras semánticas (clases, componentes, estados);
- que los mensajes de la página de secuencia tengan `y` estrictamente creciente;
- que no haya ids duplicados.

La página de secuencia excluye los cruces entre aristas porque los mensajes cruzan las líneas de vida por definición. Las figuras usan esténciles UML nativos de draw.io: `shape=component` para módulos, `swimlane` con compartimentos para clases, `umlLifeline`/`umlActor`/`umlActivation` para secuencia y estados redondeados con pseudoinicial/final. Los hijos de los swimlanes, los marcos y las barras de activación están marcados con `data-check="skip"` para que el verificador no los trate como figuras independientes.

Exportación de vistas previas con el CLI de drawio (el snap no puede escribir en `/tmp`; usar una ruta dentro del proyecto):

```bash
mkdir -p build/diagramas
for i in 1 2 3 4 5 6; do
  drawio -x -f png -o build/diagramas/p$i.png -p $i -s 1.5 docs/diagrama.drawio
done
```

Páginas:

| # | Página | Contenido | Vista previa |
|---|---|---|---|
| 1 | Componentes | Frontera UI/worker, módulos, dependencias y contratos compartidos | [PNG](diagramas/01-componentes.png) |
| 2 | Clases · Dominio | `ExperimentConfig`, `MetricsSnapshot`, `LimitReport`, `ExperimentResult`, enums | [PNG](diagramas/02-clases-dominio.png) |
| 3 | Clases · Servicios | Interfaces, implementaciones y colaboradores | [PNG](diagramas/03-clases-servicios.png) |
| 4 | Secuencia | Orden de mensajes de una ejecución y salida alternativa por Detener | [PNG](diagramas/04-secuencia.png) |
| 5 | Estados | Máquina de estados de una ejecución | [PNG](diagramas/05-estados.png) |
| 6 | Leyenda | Notación UML, visibilidad y tipos de relación | [PNG](diagramas/06-leyenda.png) |

Convenciones del diagrama:

- Cabecera azul oscuro: clase, interfaz o enumeración.
- Blanco: cuerpo de clase, componentes y estados.
- Gris claro: contenedores de proceso y estados terminales.
- Negro: bordes, líneas, estado inicial y estado final.
- Diamante relleno: composición; diamante vacío: agregación; triángulo hueco: realización; flecha abierta: asociación/dependencia; flecha rellena: mensaje.
- Las aristas discontinuas indican dependencia, realización o retorno.
- La página **6 · Leyenda** repite la notación y los símbolos de visibilidad.

## 19. Estructura de archivos objetivo

```text
src/
  domain/     Types.hpp · Validation.hpp
  core/       Guardrails · ResultStore · CsvExporter · SystemInfo
  runtime/    CancellationToken · IWorkerPool · NativeThreadPool · TaskPool
  workload/   IWorkload · WorkloadFactory · MemoryRegion
              ConcurrencyWorkload · CpuLimitWorkload
              MemoryPressureWorkload · SharedCounterWorkload
  metrics/    IMetricsSource · ProcMetricsSource · ProcFs
  limits/     ResourceLimiter · LimitProbe
  process/    WorkerProtocol · IProcessSupervisor · PosixProcessSupervisor
  app/        IExperimentController · ExperimentController
              ExperimentRunner · FakeExperimentController
  ui/         MainScene · ExperimentForm · MetricsPanel
              HistoryPanel · ComparePanel · StatusBar · UiFormat
  worker/     main.cpp · ProbeMode
tests/
docs/
  Requerimientos.md · Arquitectura.md · diagrama.drawio · diagramas/
```

Targets CMake:

- `memtest_core`: estática, sin Guayaba.
- `mem-test`: ejecutable de la TUI; enlaza `memtest_core` + `GuayabaTUIEngine`.
- `mem-test-worker`: ejecutable del hijo; enlaza `memtest_core`.
- `memtest_tests`: objetivos de CTest, headless.
