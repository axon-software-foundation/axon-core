# Estado de HTTP y comunicaciones

Fecha de revisión: 19 de noviembre de 2026.

## Resultado de la validación

En el Core recibido no hay implementación pública de `http->get`, `http->post` ni `http->json`. Esas operaciones aparecen anunciadas en el roadmap de la web. El adaptador Ollama contiene un cliente HTTP específico para sus llamadas; no constituye un módulo HTTP general del lenguaje.

Antes de esta corrección, `http->get(...)` podía terminar tratado como una llamada a un agente inexistente y devolver null. Ahora `http->...` produce un error explícito:

```text
HTTP module is not implemented in this Core snapshot
```

Las pruebas comprueban esa ausencia de forma controlada. **No verifican peticiones HTTP/HTTPS reales ni certifican el módulo como funcional.** No se añadieron dependencias de red, respuestas simuladas a producción ni un cliente HTTP incompleto.

## Implementación futura acordada

El bloque de comunicaciones e integraciones incluirá HTTP/REST, gRPC, WebSockets y el módulo MCP. MCP tendrá un contrato propio de protocolo e integración; no será un alias de HTTP/REST o gRPC.

Para HTTP se debe fijar el contrato antes de implementarlo: firmas, tipos de entrada, representación de respuesta, status/cabeceras/body, JSON, HTTPS y certificados, timeouts, límites, redirects y errores. Una suite con servidor local debe verificar GET, POST, JSON, códigos de error, conexión fallida, respuestas incompletas y límites. Las pruebas TLS necesitarán certificados de test y verificación activa; no se desactivará la verificación para aparentar compatibilidad.

Para MCP queda definir rol cliente/servidor, versión de protocolo, capacidades, transporte, ciclo de conexión, autorización, cancelación, errores y compatibilidad. La implementación de este módulo y de HTTP se hará en un bloque posterior, como pidió Darío. Este parche solo registra el alcance y evita anunciar HTTP como disponible.
