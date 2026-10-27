# mem-test

Proyecto base para el Laboratorio de concurrencia y memoria, usando
[GuayabaTUIEngine](https://github.com/CebGIN/GuayabaTUIEngine) como motor de
interfaz TUI.

## Clonar

GuayabaTUIEngine es un submódulo de Git, fijado en el tag `v1.2.1-alpha`:

```bash
git clone --recurse-submodules <url-del-repositorio>
```

Si el repositorio ya estaba clonado:

```bash
git submodule update --init --recursive
```

## Compilacion

```bash
cmake -S . -B build
cmake --build build
./build/mem-test
```

## Integracion

El motor se agrega como subdirectorio de CMake y se enlaza como
`GuayabaTUIEngine::GuayabaTUIEngine`. La API publica queda disponible con:

```cpp
#include <guayaba/Guayaba.hpp>
```
