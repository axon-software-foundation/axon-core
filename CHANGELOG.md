# Changelog

## [Unreleased]

- CI y herramientas de comunidad preparados para validación en el repo oficial.
- Ruta Docker Linux actualizada; ejecución en Docker pendiente de CI.

## [0.4.0-alpha.1] — candidato, todavía sin publicar

### Cambios incluidos

- Fuente única VERSION para CLI, CMake y nombres de archivos comprimidos.
- Funciones, imports relativos/canónicos y diagnósticos CLI con códigos de salida.
- Paso de input/resultados entre tasks, referencias explícitas y aislamiento
  entre invocaciones; agente completo, task específica y pipeline.
- Delays posteriores de task/agente/flow y manejo controlado de raise sin abort.
- Corrección de la señal de apagado del scheduler y prueba de regresión.
- Limpieza de advertencias propias y comprobación estricta del build.
- Windows x64 portable con runtime MinGW estático, avisos, hashes y pruebas
  automáticas del archivo extraído.

### Validación disponible

- Windows: GCC 16.2, Release sin warnings con -Werror, 22/22 CTest y portable
  extraído. Darío confirmó ejecución correcta en otro Windows.
- Linux: GCC 14.2, Release sin warnings, 22/22 CTest y archivo nativo extraído;
  no certifica portabilidad entre distribuciones.
- Suites: 31 casos runtime y 78 del intérprete. Encadenamiento real con Ollama
  comprobado manualmente en Windows en la etapa anterior.

### Límites

HTTP/REST, MCP, timeouts efectivos, concurrencia real de parallel y otros
frentes siguen pendientes en el roadmap. macOS no tiene paquete validado.
No se incluyen modelos, Ollama ni Tesseract. Firma de código pendiente.
No hay todavía una GitHub release ni una imagen Docker publicada.
