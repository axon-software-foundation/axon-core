# Corrección de apagado del scheduler — alpha local

## Evidencia

La primera construcción Release Windows con GCC 16.2 terminó de compilar.
CTest pasó 20 de 22 casos: runtime agotó 30 segundos y cli.suites agotó su
espera al lanzar esa misma suite. El empaquetado se detuvo antes de generar
un nuevo ZIP. Las advertencias del compilador no son esos fallos de ejecución.

La salida capturada terminaba en actor_invalid_transition, pero stdout estaba
almacenado en búfer. Esa línea no localiza por sí sola la llamada bloqueada.
Los encabezados y resultados de cada test ahora fuerzan el vaciado del búfer.

## Corrección

Scheduler::stop cambiaba _running sin adquirir _queue_mu. Un worker podía
comprobar el predicado, perder notify_all antes de quedar esperando y dormir
indefinidamente; stop quedaba bloqueado en join. El estado del predicado se
modifica ahora bajo el mismo mutex usado por condition_variable::wait. Se
libera el mutex antes de notificar y esperar la terminación de los hilos.

También se incluye unordered_set directamente en scheduler.cpp, que usa ese
tipo y dependía de un include previo de otro archivo.

Se añade scheduler_stop_waiting_workers: 128 ciclos de arranque/apagado con dos
workers y comprobación de stop idempotente. CTest mantiene su watchdog; no se
aumentan límites ni se omiten pruebas. La suite runtime pasa de 30 a 31 casos;
CTest conserva 22 casos y la suite del intérprete conserva 78.

## Comprobación

El código original se bloqueó en Linux Release durante una prueba de estrés
de hasta 5000 ciclos, terminada por un watchdog de 15 segundos. La versión
corregida completó los 5000 ciclos. La suite runtime corregida pasó además
30 ejecuciones consecutivas. La corrección necesita confirmarse en el Windows
del reporte; esa prueba local en Linux no certifica la resolución en Windows.

## Repetir en Windows — Git Bash desde la raíz del proyecto

Aplicar este parche después del parche alpha-empaquetado y ejecutar:

```bash
bash scripts/package.sh -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
```

El build existente se actualiza automáticamente. Se esperan 22/22 CTest y,
después, PASS del paquete extraído. Si aparece otro error, conservar su salida
completa: las comprobaciones de dependencias/licencias y la ejecución con PATH
reducido ocurren después de CTest y aún requieren validación en ese equipo.
