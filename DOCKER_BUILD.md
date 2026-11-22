# Docker para Linux

Esta ruta prepara compilación/pruebas e imagen de ejecución Linux. Requiere
Docker con contenedores Linux y Compose v2 para los comandos compose. En
Windows puede usarse Docker Desktop con backend Linux/WSL2. No genera un
paquete Windows ni macOS. No hay imágenes publicadas todavía.

## Compilar y probar el código montado

Desde la raíz, en PowerShell, Git Bash o una terminal Linux:

```bash
docker compose run --build --rm build
docker compose run --rm test
```

La compilación se guarda en un volumen Docker, no en build-alpha de Windows.
No mezcles caches CMake de plataformas distintas. No ejecutes build y test
simultáneamente sobre el mismo volumen. Los errores se muestran completos;
un fallo del compilador o de CTest devuelve un código distinto de cero.

Para abrir un shell del entorno:

```bash
docker compose run --build --rm shell
```

## Crear una imagen de ejecución con pruebas obligatorias

```bash
docker build --target runtime -t axon:dev .
docker run --rm axon:dev version
docker run --rm axon:dev test
docker run --rm axon:dev run /opt/axon/share/axon/examples/validation/tasks.axon
```

El stage build ejecuta check-release.sh, el empaquetador, CTest y las pruebas
del archivo nativo extraído. La imagen runtime contiene los tres ejecutables,
bibliotecas del sistema, ejemplos, documentación y avisos de licencia. Usa un
usuario sin privilegios; no incluye compilador, repositorio fuente, Ollama,
pesos de modelos ni Tesseract. Los ejemplos LLM necesitan un servicio accesible
según la configuración que soporte el provider; localhost dentro del contenedor
no es localhost del host.

Para tu propio script, monta solo la carpeta necesaria como lectura. PowerShell:

```powershell
docker run --rm --mount "type=bind,source=$($PWD.Path),target=/work,readonly" axon:dev run /work/examples/validation/tasks.axon
```

Git Bash convierte rutas para programas Windows; para el mismo montaje:

```bash
MSYS_NO_PATHCONV=1 docker run --rm --mount "type=bind,source=$(pwd -W),target=/work,readonly" axon:dev run /work/examples/validation/tasks.axon
```

En Linux:

```bash
docker run --rm --mount "type=bind,source=$PWD,target=/work,readonly" axon:dev run /work/examples/validation/tasks.axon
```

Un montaje readonly impide escribir allí, pero no convierte scripts hostiles
en seguros. Consulta SECURITY.md; no montes credenciales ni datos sensibles.

## Exportar el archivo de validación Linux

Con BuildKit/buildx y salida local:

```bash
docker build --target artifacts --output type=local,dest=dist-docker .
```

El .tar.gz y su .sha256 pertenecen al Linux/arquitectura del build. No se
promete aún portabilidad entre distribuciones. Una imagen multiarquitectura
requiere builds y pruebas para cada arquitectura antes de anunciar soporte.

## Estado de validación

Los archivos se revisan estáticamente en esta entrega. Docker no está
instalado en el entorno de revisión; falta ejecutar la imagen y Compose.
El job Linux Docker de CI realizará el build y las pruebas cuando se suba el
workflow. Una configuración preparada no es una imagen validada/publicada.
Las bases usan etiquetas ubuntu:24.04 actualizables, no digest inmutable;
no se declara reproducibilidad bit a bit. Dependabot revisa cambios de base.
