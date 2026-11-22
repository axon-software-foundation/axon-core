# Validación local: funciones, imports y CLI

Esta revisión parte de `Axon-core(1).zip`, el primer parche de validación y la corrección incremental de raise/flows del 19 de noviembre de 2026. Copia su contenido en la raíz de tu proyecto y conserva las rutas. No es el proyecto completo ni contiene binarios. Si cambiaste estos archivos después de enviar el ZIP, compara antes de reemplazarlos.

## Compilar y ejecutar desde Git Bash

Requisitos: CMake 3.20 o superior (incluye CTest), un compilador C++17 y su herramienta de build. Usa el mismo toolchain con el que ya compilas Axon. El script no instala herramientas ni necesita Ollama, Python o red para ejecutar sus pruebas.

Desde la raíz de Axon Core:

```bash
bash scripts/validate.sh Debug
```

El script configura en `build-validation`, compila con dos jobs, ejecuta CTest, ejecuta ambas suites mediante `axon test` y corre los dos nuevos ejemplos de validación. Se detiene si cualquier paso falla. Usa un directorio separado de tu build habitual.

Si usas MSYS2 UCRT64 y MinGW, ejecuta en Git Bash:

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
bash scripts/validate.sh Debug -G "MinGW Makefiles"
```

Ese bloque supone MSYS2 en `C:\msys64`, con GCC/G++, CMake y `mingw32-make` instalados. Si tu instalación está en otra ruta, ajústala. Si prefieres Ninja y lo tienes instalado, usa `-G Ninja`. No cambies de generador o compilador dentro de un directorio ya configurado; usa, por ejemplo, `AXON_BUILD_DIR="$PWD/build-validation-mingw" bash scripts/validate.sh Debug -G "MinGW Makefiles"`.

Equivalente manual con herramientas ya disponibles en PATH:

```bash
cmake -S . -B build-validation -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-validation --config Debug --parallel 2
ctest --test-dir build-validation -C Debug --output-on-failure
./build-validation/axon.exe test
./build-validation/axon.exe run examples/validation/functions.axon
./build-validation/axon.exe run examples/validation/main.axon
./build-validation/axon.exe run examples/flows.axon
```

Ejecuta los comandos manuales uno por uno y continúa solo cuando el anterior termine correctamente. En Linux/macOS el ejecutable se llama `axon`, sin `.exe`. CMake mantiene los ejecutables principales de Windows directamente dentro de `build-validation`, también con generadores multiconfiguración.

## Qué resultados esperar

- Runtime: **31 passed, 0 failed**.
- Intérprete: **78 passed, 0 failed** — incluye 24 nuevas regresiones de tasks, parámetros, delays y ausencia de HTTP.
- CTest: **22/22 casos aprobados en Linux y Windows**, que incluyen las dos suites anteriores y 20 comprobaciones de CLI. Los casos CTest reutilizan las suites; no se suman como pruebas unitarias independientes.
- Ejemplo de funciones: termina con `PASS functions`.
- Ejemplo de imports/módulos: termina con `PASS imports`.
- Ejemplo de flows: termina con `PASS flows`.

El mensaje de transición ilegal de actor durante el test negativo de runtime es esperado si la suite termina en PASS. La CLI todavía puede anunciar que Ollama no está disponible; estos ejemplos no ejecutan tareas LLM.

## Probar por bloques

```bash
# Funciones, recursión, closures, funciones anónimas, arrows, orden superior.
./build-validation/axon.exe run examples/validation/functions.axon

# Imports con y sin ./, imports anidados, privados, SHA-256 y Base64.
./build-validation/axon.exe run examples/validation/main.axon

# Dos configuraciones de flow y llamadas explícitas de agentes sin LLM.
./build-validation/axon.exe run examples/flows.axon

# Casos de imports correctos e incorrectos. Las comillas importan en Git Bash.
ctest --test-dir build-validation -C Debug --output-on-failure -R 'cli\.(import_|missing_import|private_import|nested_import|duplicate_import|circular_import|smoke)'

# Comportamiento de axon test: rutas con espacios, ausencia y fallo de un hijo.
ctest --test-dir build-validation -C Debug --output-on-failure -R 'cli\.(suites|missing_suite|failing_suite)'
```

Los casos negativos generan archivos bajo `build-validation/cli-cases/`. CTest comprueba que esos programas fallen con el código y diagnóstico esperados: el caso de prueba debe aparecer aprobado, aunque el programa probado falle intencionalmente.

`axon check` sigue comprobando el archivo de entrada; no resuelve transitivamente los imports. Usa `axon run` o las pruebas anteriores para validar los archivos importados.

## Cambios incluidos

1. `axon test` localiza ambas suites junto al ejecutable y usa procesos nativos, sin depender del directorio de terminal ni del shell. Propaga errores del hijo y falla cuando faltan ejecutables.
2. Imports relativos se resuelven desde el archivo importador; las rutas se normalizan y se conserva el comportamiento existente de omitir imports repetidos/circulares.
3. Los imports rechazan errores léxicos y de parser, con nombre de archivo y línea. Se restaura el contexto de origen ante excepciones y se liberan las arenas importadas al destruir el intérprete.
4. `hash->sha1` y `hash->sha512` informan explícitamente que no están implementados. No se presenta un prefijo de SHA-256 como SHA-1. SHA-256 y MD5 tienen vectores de prueba.
5. CMake registra pruebas en CTest, enlaza `ws2_32` explícitamente en Windows e instala ambas suites para que `axon test` esté completo.
6. Se reemplaza el antiguo `examples/flows.axon`, que tenía `...` y sintaxis no implementada, por un ejemplo ejecutable de las capacidades actuales.
7. Se añaden ejemplos sin servicios externos y este procedimiento reproducible.

## Límites de esta entrega

- La base se comprobó en Linux; posteriormente Darío confirmó Windows Release con GCC 16.2 sin warnings, 22/22 CTest y ejecución del portable en otro Windows. macOS y Docker siguen pendientes de ejecución. La CI nueva necesita correr en GitHub.
- Las nuevas pruebas no verifican Ollama real, OCR, PDFs, DOCX/XLSX ni rendimiento.
- `parallel {}` continúa secuencial; no se implementó concurrencia.
- `Flow->run(...)` automático, aridad/tipado nuevos, providers y otros módulos pendientes no se añaden en este parche.
- `string.find()` conserva búsqueda literal y devuelve un índice o -1. La documentación general que lo describe como regex necesita reconciliación en el bloque de documentación.
- La representación de funciones continúa siendo Dict; `typeof(función)` devuelve `dict`.
- La unificación de versiones y el empaquetado de release siguen en el roadmap. Verás los identificadores anteriores en CMake/CLI: este paquete es una corrección de trabajo, no una release alpha publicada.
- El ejemplo anterior `examples/functions.axon` se conserva: incluye una invocación de Python y escribe bajo `tmp/`. Para esta validación usa `examples/validation/functions.axon`, que no tiene esos requisitos.

## Archivos del primer parche (referencia)

Modificados: `CMakeLists.txt`, `cli/main.cpp`, `runtime/include/interpreter.h`, `runtime/src/interpreter.cpp`, `runtime/src/modules.cpp`, `tests/test_interpreter.cpp`, `examples/flows.axon`.

Nuevos: `tests/test_cli.cmake`, `tests/test_exit.cpp`, `scripts/validate.sh`, `examples/validation/functions.axon`, `examples/validation/main.axon`, `examples/validation/math.axon`, `examples/validation/nested/format.axon`, `docs/VALIDATION.md`.

La web no cambia en esta entrega. El siguiente paso será revisar tu resultado en Windows y continuar con versión única, CI y empaquetado conforme al roadmap acordado.

## Corrección incremental: raise y diagnóstico de flows

Este segundo parche se aplica sobre el primero. Cambia únicamente CMakeLists.txt, runtime/src/interpreter.cpp, tests/test_interpreter.cpp, tests/test_cli.cmake, examples/flows.axon y este documento.

Se confirmó que un `raise` sin capturar escapaba al host como `axon::RaiseSignal`, que no hereda de std::exception. La CLI solo capturaba std::exception, por lo que el proceso terminaba abruptamente. Ahora el límite de ejecución del intérprete convierte ese raise no manejado en un error estándar, conserva su mensaje y la CLI devuelve código 1. Los try/catch del lenguaje siguen recibiendo la señal original dentro del programa.

La comparación del ejemplo flows falla en el Windows del usuario, pero el ejemplo produce el resultado esperado en este Linux. No se ha establecido aún por qué difiere. El ejemplo ahora muestra ambos inputs, fetched, result, sus tipos y longitudes antes de mantener las comprobaciones. Si vuelve a fallar, debe informar un error de runtime y esos valores, en lugar de abortar. No se han eliminado assertions para hacer pasar el test.

Se añadieron seis regresiones de intérprete: raises no capturados en main y función, raise capturado, inputs de múltiples flows, llamada directa de agente y pipeline de agentes. También cuatro casos CLI: raise en main, raise en función, try/catch y división entre cero.

Recompila: este parche cambia C++.

```bash
cmake --build build-validation --config Debug --parallel 2
./build-validation/axon.exe run examples/flows.axon
ctest --test-dir build-validation -C Debug --output-on-failure
```

La construcción de CMake debe regenerar automáticamente las pruebas al detectar el cambio en CMakeLists.txt. La confirmación de la corrección del pipeline en Windows requiere revisar la nueva salida. Los 19 casos en verde de Linux no certifican ese resultado en Windows.

## Contrato de tasks y delays — parche incremental actual

Aplicar sobre el parche raise/flows anterior. Requiere recompilar. `docs/ROADMAP.md` consolida el alcance y registra HTTP/MCP para implementación posterior.

- `Processor(arg)` ejecuta el agente completo. Un `input=__input__` de task usa el argumento del agente; `input=flow.input` sigue pidiendo explícitamente el valor del flow.
- `Processor->Task(arg)` ejecuta solo esa task con el argumento explícito. No aplica el delay del agente completo.
- `arg >> Processor` entrega arg al agente; cada etapa recibe el resultado de la anterior.
- Sin campo input, una task hereda el resultado anterior (o la entrada del agente para la primera).
- `input=null` es explícito y no activa un fallback.
- `Task.output`, `Agent.Task` y `Agent.Task.output` leen resultados publicados. En una ejecución activa no se permite leer una task que aún no corrió ni reutilizar su resultado de una ejecución anterior.
- `Agent->Task` sin argumentos dentro de un campo input lee la salida ya disponible y no vuelve a llamar al modelo. Con argumentos sí ejecuta la task explícitamente.
- Fuera de ese campo, una llamada sin argumentos conserva el comportamiento previo: lee caché si existe y en caso contrario ejecuta con el input predeterminado.
- Task_graph y ejecución secuencial comparten resolución de input, prompts, registro de salida y delay.
- Los delays son segundos finitos no negativos y se aplican tras éxito: task antes de continuar; agente completo antes de devolver; flow activo al finalizar el programa. No se reinterpretan como timeouts ni se fuerza espera tras errores.
- En este snapshot el flow activo sigue siendo el seleccionado por la lógica de declaraciones existente; la propiedad/selección completa de múltiples flows sigue en roadmap.
- `parallel` continúa secuencial. HTTP y MCP no se implementan en esta entrega.

Pruebas manuales sin LLM:

```bash
./build-validation/axon.exe run examples/validation/tasks.axon
./build-validation/axon.exe run examples/validation/delays.axon
ctest --test-dir build-validation -C Debug --output-on-failure
```

La suite interna usa un adaptador de registro exclusivamente en tests para verificar los mensajes que recibiría el modelo. No reemplaza a Ollama en producción ni simula su implementación. Los timers se miden dentro del intérprete para separar el delay de la latencia de arranque y detección de Ollama de la CLI.

HTTP: `cli.http_unavailable` y la regresión interna deben aprobar porque confirman que se informa **no implementado**. No cuentan como pruebas de HTTP funcional. Ver `HTTP_STATUS.md` para el alcance pendiente.

Compatibilidad: `output=` sigue ignorado en ejecución secuencial/task_graph como antes. Se conserva el manejo de formato en llamadas directas de task; se reconocen sus identificadores de formato literales sin tratarlos como variables. Este parche no introduce un nuevo contrato general de salida tipada.

Integración manual opcional con Ollama real:

```bash
./build-validation/axon.exe run examples/validation/ollama_tasks.axon
```

Requiere Ollama activo con llama3.2 disponible. Aquí se comprobó su sintaxis, no la generación real. Si la CLI informa que no detecta Ollama, la política heredada de passthrough no cuenta como una prueba de generación; revisa el servicio antes de interpretar la salida.
