# Publicar la primera alpha y abrir la comunidad

Esta guía prepara la publicación. Los archivos de esta entrega no hacen push,
crean tags ni publican por su cuenta. El repositorio oficial confirmado es
https://github.com/axon-software-foundation/axon-core. La importación se
revisa mediante un PR antes de integrar cambios o publicar una release.

## 1. Consolidar el código

La rama local reportada es feature/axon-base. Conserva su historial; no hace
falta reiniciar el repo. Antes de preparar el commit, revisa desde la raíz:

```bash
git status --short
git remote -v
git diff --check
git diff --stat
```

Comprueba que los parches están aplicados, sin binarios/dist/builds/modelos ni
secretos. Revisa los cambios antes de añadirlos, usando `git add -p` y
`git add <rutas revisadas>` para archivos nuevos. No incluyas cambios personales
sin relación. Revisa `git diff --cached` antes del commit.

Cuando el remoto oficial esté confirmado, sube una rama y abre un PR hacia la
rama predeterminada del repo. El nombre de esa rama se toma del repositorio;
no suponemos que ya exista main ni que Dev-Mw sea la organización final.

## 2. Configurar GitHub

- Habilitar Issues y los workflows de Actions revisados. La CI usa solo lectura
  y no necesita secretos; para contribuciones de forks, revisar las solicitudes
  de ejecución según la configuración del repo.
- Habilitar reportes privados de vulnerabilidades y comprobar que el botón
  Report a vulnerability aparece en Security. Configurar notificaciones para
  los responsables. SECURITY.md no activa esa función por sí solo.
- Establecer responsables y contacto privado de moderación de la comunidad.
- Ejecutar CI y configurar reglas de la rama con los nombres reales de los
  checks Linux/Windows/Docker que hayan corrido. macOS es experimental y no
  debe contarse como soporte validado aunque el workflow global esté verde.
- Habilitar Discussions si se quiere separar preguntas y propuestas de bugs.
  Revisar Contributions, Issues y PR desde una cuenta sin permisos especiales.

No se incluyen handles, emails, URLs de organización ni CODEOWNERS inventados.

## 3. Cerrar alcance de la alpha

Windows x64: Darío confirmó build GCC 16.2 sin warnings, 22/22 CTest,
prueba del portable extraído y ejecución correcta en otro Windows. Este
resultado corresponde al candidato local anterior a los cambios de CI.
Si reconstruyes otro binario para publicar, prueba ese nuevo archivo también.

Linux tiene validación nativa en el host de revisión; no se anuncia todavía
portabilidad entre distribuciones. Docker y macOS requieren sus ejecuciones
reales en CI. Para el primer lanzamiento, la única descarga portable que se
propone anunciar es Windows x64.

Revisar procedencia/licencias de los archivos vendorizados y posibles cambios
locales antes de abrir el repo. No está resuelto solo por añadir textos de
licencia. Confirma el canal de seguridad antes de publicar.

## 4. Etiquetar el código aprobado

Después de integrar el PR, desde un checkout limpio del commit aprobado:

```bash
bash scripts/check-release.sh
git status --short
git log -1 --oneline
git tag -l v0.4.0-alpha.1
```

El estado debe estar limpio y el tag no debe existir. Si existe, no lo muevas:
avanza VERSION a la siguiente alpha, actualiza su entrada de changelog y repite
la validación. Cuando el commit y remoto estén verificados:

```bash
git tag -a v0.4.0-alpha.1 -m "Axon 0.4.0-alpha.1"
git push origin v0.4.0-alpha.1
```

El push del tag ejecuta CI y exige coincidencia entre tag, VERSION y changelog.
No crea una release. La URL del origen debe ser la oficial ya revisada.

## 5. Revisar los artefactos exactos del tag

En Actions, elige la ejecución del tag oficial y comprueba su commit, los
checks bloqueantes y el artefacto windows-x64-candidate-<SHA>. Descárgalo y
extrae el contenedor de Actions. Dentro deben estar el portable y su .sha256.

Desde la carpeta que contiene ambos, con Git Bash:

```bash
sha256sum -c axon-v0.4.0-alpha.1-windows-x64.zip.sha256
```

Prueba ese ZIP exacto siguiendo PORTABLE.md y revisa BUILD_INFO.txt:
Source commit debe coincidir con el tag y Source state debe ser clean.
Una reconstrucción cambia el checksum: usa siempre el .sha256 de ese archivo,
no el de otro candidato previo. Registra los resultados de aceptación y las
versiones de Windows probadas. La firma de código sigue pendiente; conserva
la incidencia observada de Smart App Control en las notas.

## 6. Crear una prerelease en borrador

Desde la interfaz GitHub, crea una release para el tag existente, marca
prerelease y guarda primero un borrador. Adjunta el ZIP portable y su .sha256,
usa docs/RELEASE_NOTES.md y comprueba que no diga estar publicado antes de
terminar la revisión. No marques esta alpha como Latest estable.

Si usas GitHub CLI, con el repo oficial ya seleccionado y los archivos
descargados en dist/ del checkout revisado:

```bash
gh release create v0.4.0-alpha.1 \
  --verify-tag --draft --prerelease \
  --title "Axon 0.4.0-alpha.1" \
  --notes-file docs/RELEASE_NOTES.md \
  dist/axon-v0.4.0-alpha.1-windows-x64.zip \
  dist/axon-v0.4.0-alpha.1-windows-x64.zip.sha256
```

Antes de publicar, revisar tag/commit, adjuntos, notas, licencia/procedencia,
canal privado de seguridad, resultados de CI y prueba del ZIP descargado.
La publicación final se realiza tras esa revisión explícita.

## 7. Abrir la comunidad y actualizar Axon Web

Crear issues enfocados, sin marcarlos hechos solo por existir:

- Resultados y compatibilidad Linux por distribución/arquitectura.
- Resolver y validar macOS; definir Intel/Apple Silicon y empaquetado.
- Completar metadatos de procedencia y revisión de terceros.
- Firma de código Windows y documentación de bloqueos del sistema.
- Contrato/pruebas del módulo HTTP/REST pendiente.
- Diseño del módulo MCP junto al bloque de comunicaciones.
- Timeouts/cancelación y concurrencia real, como problemas separados.

No se asigna good first issue automáticamente a seguridad, transportes o
concurrencia. Elegir primeras tareas pequeñas con criterios de aceptación.

En axon-pl.org, enlazar la release real y su código, mostrar 0.4.0-alpha.1
como alpha y anunciar solo el Windows publicado. Reemplazar URLs, versiones y
hashes de ejemplo por los del artefacto real. Mantener Linux/macOS como
pendientes y el playground como no disponible. No hay cambios de hosting en
esta entrega; falta confirmar repositorio y revisar el código de Axon Web.

## Referencias de configuración

- GitHub Actions: https://docs.github.com/en/actions
- Reportes privados: https://docs.github.com/en/code-security/security-advisories/working-with-repository-security-advisories/configuring-private-vulnerability-reporting-for-a-repository
- GitHub CLI: https://cli.github.com/manual/gh_release_create
- Setup MSYS2: https://github.com/msys2/setup-msys2
