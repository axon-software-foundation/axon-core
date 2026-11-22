# Axon: roadmap operativo de Core y Web

Actualizado el 21 de noviembre de 2026. Consolida lo revisado del ZIP, los parches de validación y las decisiones de esta conversación. Los RFC antiguos y la web todavía requieren reconciliación; su numeración de fases no debe usarse como evidencia de implementación.

Estados: **implementado** describe código existente; **validado** exige pruebas; **publicado** exige artefactos disponibles. No son sinónimos.

## 1. Estabilización del contrato de ejecución — base validada

- Base: lexer/parser, intérprete, funciones, closures, imports, CLI y ejemplos; 19 casos CTest anteriores confirmados en Windows y Linux.
- Corrección actual: argumento del agente en `__input__`, referencias explícitas `Task.output`, `Agent.Task` y `Agent.Task.output`, referencias no adyacentes y separación de resultados entre invocaciones.
- Mantener las tres formas: agente completo, task específica y pipeline.
- Unificar tareas secuenciales y task_graph bajo el mismo dispatch de input/prompts/resultados.
- Aplicar delays posteriores de task/agente/flow y comprobar tiempos sin depender de un modelo.
- Base confirmada en Linux y Windows: 22 CTest, 31 casos runtime y 78 del intérprete; encadenamiento real con Ollama confirmado en Windows. Apagado del scheduler corregido y compilación estricta sin warnings.
- Timeouts efectivos/cancelación y concurrencia real de `parallel` siguen pendientes. Los delays no implementan timeouts.

## 2. Primera alpha distribuible — bloque actual

- Resolver cambios sin consolidar y trabajar en la organización oficial `axon-software-…`.
- Fuente única VERSION `0.4.0-alpha.1`, derivada para CLI/CMake/archivos; parche de empaquetado añade el caso cli.version.
- Preparación del ZIP portable Windows x64 con runtime MinGW estático, avisos/licencias, hashes y ejecución del archivo extraído. Darío confirmó ejecución correcta en un segundo Windows. Cada nuevo candidato debe volver a probarse; ver RELEASING.md.
- Tag, changelog, notas, checksums y artefactos vinculados al mismo código.
- CI y guía para compilar, probar y contribuir; issues/PR, conducta y seguridad preparados en esta entrega. Falta ejecutarlos/configurarlos en el repositorio oficial; ver CI.md y GITHUB_LAUNCH.md.
- README corregido para reflejar el MIT existente en LICENSE.txt. Avisos de terceros añadidos; revisar procedencia de los archivos vendorizados antes de publicación.
- Alpha no exige terminar todo este roadmap; exige alcance y limitaciones claros.

## 3. Plataformas, estabilidad y beta

- Construcción y paquetes Linux/macOS con arquitecturas declaradas y comprobadas. Docker Linux y CI nativa están preparados; falta ejecutarlos en sus entornos antes de afirmar soporte.
- macOS Intel/Apple Silicon o universal únicamente cuando esté verificado.
- Beta cuando el alcance comprometido esté completo y las plataformas declaradas funcionen.
- RC y estable después de resolver bloqueos, con regresiones y compatibilidad documentadas.

## 4. Comunicaciones e integraciones — HTTP y MCP añadidos explícitamente

- **HTTP/REST:** módulo público pendiente de implementar; GET/POST/JSON, HTTPS, errores, timeouts y límites, con contrato y pruebas reales. Ver HTTP_STATUS.md.
- **gRPC, WebSockets y sockets:** distinguir sintaxis/interfaces previstas de transportes funcionales; completar integración y pruebas.
- **MCP:** módulo pendiente solicitado por Darío. Definir rol, versión, capacidades, transporte, ciclo de conexión, autorización y errores. No es un alias de los otros transportes.
- No se implementa HTTP ni MCP en el parche de paso de parámetros.

## 5. Lenguaje y multi-flow

- Aridad, retorno, representación de funciones, tipos y diagnósticos coherentes.
- `*args`, `**kwargs`, argumentos nombrados, desempaquetado, imports selectivos y anotaciones opcionales.
- Propiedad de agentes por flow, aislamiento de contexto e invocación/encadenamiento explícito de flows.
- Normalización y casos de error de módulos, colisiones y reglas de exportación.

## 6. Providers y multimodalidad

- Consolidar Ollama con pruebas reales separadas de la suite sin servicios externos.
- Providers OpenAI, Anthropic y Cohere; Google figura en documentos anteriores y requiere reconciliar alcance.
- Autenticación, HTTPS, errores, límites, retries, streaming e imágenes como entrada.

## 7. Datos, documentos e interoperabilidad

- Completar pruebas reales de memoria, persistencia, embeddings/HNSW, PDF, DOCX/XLSX y OCR.
- Consolidar Python/shell y completar SQL/storage con drivers previstos PostgreSQL/MySQL/MongoDB/DuckDB/Arrow según prioridades.
- Resolver diferencias documentación/código, incluidos regex y algoritmos de hash todavía no implementados.
- La documentación menciona MuPDF, pero la revisión encontró un lector PDF propio: decidir/documentar arquitectura real.

## 8. RAG y aprendizaje

- Ingestión, recuperación, construcción de contexto y evaluación de RAG nativo.
- `finetune {}`, LoRA/backend previsto, datasets, checkpoints y carga/exportación.
- Cognitive Imprinting y Federated Imprinting necesitan especificación verificable antes de implementación.

## 9. Mesh distribuida

- Descubrimiento, topología, comunicación entre máquinas, cifrado, gossip y consenso.
- Transporte ultrasónico como frente de investigación separado.

## 10. Observabilidad y ecosistema

- Métricas/trazas consistentes, OpenTelemetry y estimación de costos.
- Gramática/editor, VS Code, diagnósticos, binding Cython y herramientas de desarrollo.
- Registry y comandos de paquetes/init/new/publish según diseño posterior.

## 11. Axon Web

- Versiones y estados coherentes con artefactos reales.
- Rutas, enlaces, descargas/checksums y comunidad conectada a Issues/Discussions.
- Documentación con origen autoritativo y derechos/licencias de código/assets claros.
- Playground pendiente: backend aislado y limitado antes de habilitar ejecución pública.

Los documentos de nuevas features que Darío enviará después pueden ampliar o precisar estos bloques. No se agregan como implementadas capacidades solo por figurar en el roadmap.
