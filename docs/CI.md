# CI de Axon Core

El workflow .github/workflows/ci.yml se activa con push, pull_request o
ejecución manual. Está preparado para GitHub.com; todavía debe ejecutarse
en el repositorio oficial para validar herramientas, permisos y runners.

| Job | Qué exige | Estado de distribución |
| --- | --- | --- |
| Linux GCC / Debug y Release | CTest y ejemplos; Release prueba el archivo nativo | Archivo de validación; sin garantía entre distribuciones |
| Windows UCRT64 / Debug | CTest y ejemplos con GCC | Validación del código |
| Windows UCRT64 / Release | Paquete estático, imports PE, hashes, ejecución extraída con PATH reducido | Candidato Windows para revisión |
| macOS Apple Clang / experimental | Build Release, CTest y ejemplos | No bloqueante; sin paquete ni soporte certificado |
| Linux Docker / candidate | Build multietapa con tests y ejecución de imagen como usuario no root | Imagen local de CI; no se publica |

Las verificaciones estrictas usan AXON_WARNINGS_AS_ERRORS=ON. Ningún job
requiere Ollama, modelos, OCR, credenciales de servicios ni secretos del repo.
Los checks de modelo real son manuales y adicionales.

Los jobs Linux/Windows/Docker son bloqueantes. macOS usa continue-on-error
explícitamente hasta resolver portabilidad: un workflow verde no certifica
macOS. El runner macos-15 usa la arquitectura que reporte su imagen; no se
afirma compatibilidad universal, Intel ni Apple Silicon sin pruebas específicas.

## Integridad y permisos

- Las Actions están fijadas a commits completos, con versiones comentadas.
  Dependabot propone actualizaciones; necesitan revisión y ejecución de CI.
- El token tiene solo contents:read y checkout no conserva credenciales.
- Los PR de forks se prueban mediante pull_request; no usamos
  pull_request_target para ejecutar código del contribuidor con privilegios.
- Se conservan artefactos candidatos 14 días y logs de CTest cuando falla un job.
- En un tag, check-release.sh exige que su nombre sea v seguido de VERSION
  y que exista la entrada del changelog.
- No hay pasos para crear releases, hacer push, firmar o publicar imágenes.

## Leer un fallo

Abre Actions, la ejecución y el primer step fallido. Descarga los logs cuando
existan. Un BAD_COMMAND de Windows significa que no se inició el proceso;
revisa el error del sistema/CodeIntegrity antes de atribuirlo a una prueba.
Un timeout requiere localizar la espera; no se resuelve subiendo límites por
defecto. Los logs de configure/build permanecen visibles en el job.

## Antes de usar artefactos para una release

Usa una ejecución confiable del tag en el repo oficial, no un artefacto de PR
o fork. Verifica commit/tag, estado de todos los checks bloqueantes, ZIP y su
checksum. El hash mostrado por upload-artifact corresponde al contenedor del
artefacto de Actions; el .zip.sha256 de Axon corresponde al ZIP portable.
No son intercambiables. Sigue GITHUB_LAUNCH.md para la revisión y publicación.

Antes de activar protecciones de rama, deja que CI se ejecute y selecciona
los nombres reales de los checks bloqueantes. No hagas obligatorio el check
experimental de macOS hasta completar su validación.
