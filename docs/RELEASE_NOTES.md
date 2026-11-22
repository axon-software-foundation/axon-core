# 0.4.0-alpha.1 — candidato local, sin publicación

Primera preparación de un paquete portable de Core. La versión completa sale
de VERSION y se refleja en CLI y nombres de archivos. Esta alpha admite cambios
de sintaxis/API y no ofrece aún un contrato de compatibilidad estable.

## Alcance validado de la base

- Funciones, imports relativos/canónicos, diagnósticos y códigos de salida.
- Invocación de agente, task específica y pipeline; paso de input/resultados.
- Referencias a resultados, aislamiento entre invocaciones y delays posteriores.
- Apagado del scheduler corregido y regresión de arranque/apagado de workers.
- Build estricto sin warnings, con AXON_WARNINGS_AS_ERRORS=ON.
- 22 CTest, 31 casos runtime y 78 del intérprete confirmados en Windows y Linux.
- Ejemplo real de encadenamiento con Ollama confirmado manualmente en Windows.
- ZIP Windows extraído/ejecutado por el empaquetador y confirmado por Darío
  en otro equipo Windows. El candidato local confirmado tenía SHA256
  `ddfb7d7f8c64d9c4df7b410eb2e433ed278a308da53dc47ff438857aebcdf9ea`.

Este hash identifica ese archivo previo; no debe copiarse al .sha256 de un
nuevo build. La CI y los cambios de metadatos de esta entrega requieren una
nueva construcción desde el commit/tag aprobado y prueba de ese ZIP exacto.
Los archivos preparados de CI/Docker no acreditan ejecuciones todavía no realizadas.

## Limitaciones conocidas

- Windows x64: construcción portable inicial con MinGW GCC; portable confirmado por el autor en otro Windows. Sin instalador, firma de código ni actualización automática.
- Linux: archivo de validación del host; compatibilidad entre distribuciones pendiente.
- macOS y otras arquitecturas: paquetes pendientes de construcción y validación.
- HTTP/REST y MCP pendientes; no se publicitan como módulos funcionales.
- Timeouts/cancelación y concurrencia real de parallel pendientes.
- Providers adicionales, multi-flow avanzado, RAG y mesh siguen el roadmap.
- OCR/documentos/memoria y demás módulos requieren ampliar pruebas de integración.
- Ollama, modelos y Tesseract no están incluidos.

Antes de publicar: consolidar el repositorio, revisar procedencia de terceros,
ejecutar CI y aceptación del nuevo candidato Windows, activar el canal privado
de seguridad y asociar binarios/checksums al mismo tag y commit.
La web debe anunciar únicamente plataformas y descargas realmente disponibles.
