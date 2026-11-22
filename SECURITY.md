# Seguridad

Axon está en desarrollo alpha. Solo la revisión de desarrollo más reciente se
mantiene activamente; no existe todavía una línea estable con parches de
seguridad garantizados ni un compromiso de tiempos de respuesta.

## Reportar una vulnerabilidad

Usa la pestaña **Security → Advisories → Report a vulnerability** del repo
oficial cuando los maintainers hayan habilitado los reportes privados de GitHub.
No incluyas exploits, credenciales o datos sensibles en issues/PR públicos.

Si el botón no está disponible, solicita públicamente solo un canal privado
de seguridad, sin detalles técnicos sensibles. Espera a que los responsables
lo establezcan antes de compartir el reporte. Activar y probar ese canal es
un requisito pendiente de configuración del repositorio antes del lanzamiento.

Incluye versión/commit, OS y arquitectura, un ejemplo mínimo con datos
sintéticos, impacto observado y pasos de reproducción. Los responsables
revisarán alcance, mitigación y divulgación coordinada con quien reporta.

## Límites de ejecución

Los scripts .axon deben tratarse como código con los permisos del usuario:
Core incluye operaciones sobre archivos, shell y servicios locales. No hay
una sandbox para ejecutar código ajeno de forma segura. No ejecutes ejemplos
no confiables ni montes datos sensibles en contenedores de pruebas.

El playground público sigue pendiente; requiere aislamiento y límites antes
de habilitar ejecución de terceros. Docker es una herramienta de despliegue,
no una garantía de aislamiento frente a código hostil.

Las descargas alpha aún no tienen firma de código. SHA256 comprueba igualdad
de bytes, no identidad del publicador. Smart App Control bloqueó un ejecutable
local durante las pruebas Windows; reconstruir no constituye una solución
garantizada ni una política de distribución. La firma sigue pendiente.
