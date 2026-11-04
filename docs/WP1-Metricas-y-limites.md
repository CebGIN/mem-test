# WP1 · Métricas y límites

Qué entrega este paquete de trabajo y cómo se comporta en **Linux** y en
**Windows**. Los contratos (`MetricsSnapshot`, `SystemInfo`, `LimitReport`,
`ExperimentConfig`) son los de [`Arquitectura.md`](Arquitectura.md) §4; este
documento solo describe las implementaciones.

## Piezas

| Archivo | Plataforma | Qué hace |
|---|---|---|
| `src/platform/Pid.hpp` | ambas | `pid_t` portable (MSVC no lo define) |
| `src/metrics/ProcFs.*` | Linux | Lectura y parseo de `stat`, `meminfo` y `loadavg` |
| `src/metrics/ProcMetricsSource.*` | Linux | `parse_status` + `IMetricsSource` sobre `/proc` |
| `src/metrics/WinMetricsSource.*` | Windows | `IMetricsSource` con psapi, `GetProcessTimes`, Toolhelp |
| `src/metrics/MetricsSourceFactory.*` | ambas | `make_metrics_source()` elige la implementación |
| `src/limits/ResourceLimiter.*` | ambas | `apply(config)` aplica los límites al proceso actual |
| `src/limits/PlatformLimits*.cpp` | una por plataforma | `setrlimit` (Linux) / Job Object (Windows) |
| `src/limits/CpuLimitNotifier*.*` | una por plataforma | Aviso del límite blando de CPU |

El resto del código solo usa `IMetricsSource` y `ResourceLimiter`; no necesita
`#ifdef`.

## Métricas: de dónde sale cada campo

| `MetricsSnapshot` | Linux | Windows |
|---|---|---|
| `vm_size` | `VmSize` | `PrivateUsage` (memoria privada comprometida), nunca menor que RSS |
| `vm_rss` | `VmRSS` | working set |
| `vm_hwm` | `VmHWM` | pico del working set |
| `vm_swap` | `VmSwap` | **0: no disponible** (Windows no da lo paginado por proceso) |
| `rss_anon/file/shmem` | `RssAnon/RssFile/RssShmem` | **0: no disponible** |
| `minor_faults` | `minflt` | `PageFaultCount` (incluye los duros) |
| `major_faults` | `majflt` | **0: no se pueden separar** |
| `cpu_user_s`, `cpu_sys_s` | `utime`/`stime` ÷ `sysconf(_SC_CLK_TCK)` | `GetProcessTimes` |
| `threads` | `Threads` | hilos del proceso (Toolhelp) |
| `voluntary/involuntary_ctxt` | `*_ctxt_switches` | **0: no disponible** |
| `sys_swap_total/free` | `SwapTotal/SwapFree` | aproximado: límite de commit − RAM |
| `load_1m` | `/proc/loadavg` | **0: Windows no tiene load average** |

`sample()` no toca `elapsed`: lo pone quien conoce el instante de inicio.
Un `0` en un campo marcado "no disponible" **no es una medición**; la UI debe
mostrar "no disponible", igual que con SWAP cuando `sys_swap_total == 0`.

Zombis e hilos del kernel no tienen líneas `Vm*` en `status`; en Linux se usa
entonces `vsize` y `rss` de `stat`.

## Límites

| `ExperimentConfig` | Linux | Windows |
|---|---|---|
| `AddressSpace` | `setrlimit(RLIMIT_AS, {L, L})` | Job Object `JOB_OBJECT_LIMIT_PROCESS_MEMORY = L` |
| `DataSegment` | `setrlimit(RLIMIT_DATA, {L, L})` | igual que arriba |
| `cpu_limit.seconds = N` | `setrlimit(RLIMIT_CPU, {N, N+1})`; `SIGXCPU` en N, `SIGKILL` en N+1 | Job Object `PerProcessUserTimeLimit = N+1`; el aviso en N lo da un hilo vigía |
| `cpu_limit.quota_percent` | `Unsupported` (fuera del MVP) | `Unsupported` (fuera del MVP) |

Diferencias a tener presentes:

- **Windows no distingue AS de DATA.** Su límite es de memoria *comprometida*
  (lo que `VirtualAlloc` con `MEM_COMMIT` consume), no de espacio de
  direcciones reservado. El `detail` de `apply()` lo indica.
- **El límite duro de CPU en Windows cuenta solo tiempo de usuario**;
  `RLIMIT_CPU` cuenta usuario + sistema. El vigía del blando sí suma ambos.
- Una reserva que supera el límite falla (`mmap` → `ENOMEM` en Linux,
  `VirtualAlloc` → `NULL` en Windows); el workload lo traduce a `MemoryLimitHit`.
- Si el entorno no deja aplicar un límite (`setrlimit` con `EPERM`, el proceso ya
  está en un job que lo impide, etc.) el estado queda `NotApplied` con la causa
  en `detail`, y la ejecución continúa. Nunca se presenta como limitada.

### Cómo lo usa el worker

```cpp
memtest::limits::ResourceLimiter limiter;
std::string detail;
LimitReport report = limiter.apply(config, &detail);   // antes de la carga

if (report.cpu_seconds_status == LimitStatus::Applied)
    memtest::limits::install_cpu_limit_notifier(*config.cpu_limit.seconds);

// dentro del bucle del workload, junto al CancellationToken:
if (memtest::limits::cpu_limit_reached()) { /* outcome=cpu_limit, salir con 11 */ }
```

## Pruebas

| Prueba | Plataforma | Qué verifica |
|---|---|---|
| `test_proc_metrics` | Linux | Fixtures de `status`, `stat` (incluye `comm` con espacios), `meminfo`, `loadavg`; un hijo que reserva y toca 64 MiB muestra RSS, VSZ y fallos crecientes |
| `test_metrics_source` | ambas | El proceso se mide a sí mismo antes/después de tocar 64 MiB; CPU creciente; PID inexistente devuelve `false` |
| `test_limits` | ambas | `AS`/`DATA` hacen fallar una reserva; `CPU` termina por aviso blando y por límite duro; `NotApplied` sin abortar; cuota `Unsupported` |

Las pruebas de límites relanzan el propio ejecutable como hijo
(`--child <escenario>`), porque un límite afecta al proceso que lo aplica. Si el
entorno no permite aplicar un límite, la prueba se salta y lo indica en la salida.
