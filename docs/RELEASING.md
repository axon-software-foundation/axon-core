# Preparar la primera alpha de Core

Esta etapa genera candidatos locales. No crea tags, hace push, cambia remotes
ni publica releases. La organización oficial es axon-software-foundation y el repo es axon-core. La guía completa de repositorio, borrador de prerelease y comunidad está en
GITHUB_LAUNCH.md.

## Versión y herramientas

VERSION contiene una sola línea, actualmente 0.4.0-alpha.1. CMake toma la
parte numérica para project() y genera el encabezado que consume la CLI con
la versión completa. No edites macros en cli/main.cpp. Los formatos permitidos
son X.Y.Z, X.Y.Z-alpha.N, X.Y.Z-beta.N y X.Y.Z-rc.N, sin ceros iniciales.

Se necesitan CMake >= 3.20 (incluye CTest), Ninja y el mismo toolchain MSYS2
UCRT64 GCC de las pruebas. No hacen falta Python, NSIS ni WiX. La primera
ruta Windows usa enlace estático de GCC/libstdc++/winpthreads y solamente DLL
del sistema; el empaquetador aborta ante una dependencia desconocida.

## Generar en Windows — Git Bash desde la raíz

```bash
bash scripts/package.sh -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
```

Ajusta C:/msys64 si tu instalación usa otra ubicación. Deben estar disponibles
Ninja/CMake y las bibliotecas estáticas de winpthreads. Los avisos del CRT y
winpthreads se toman de share/licenses/ dentro del toolchain usado. Si falta
alguno, el error señala la carpeta; no se genera un ZIP sin sus avisos.

Se usa un build Release separado en build-alpha. El script compila, exige que
CTest pase y llama al target axon_portable. Este último crea un área temporal,
selecciona tres ejecutables, ejemplos autocontenidos, documentos y licencias;
inspecciona imports PE; crea y extrae el archivo; compara sus hashes; ejecuta
version, test, ejemplos y un error controlado desde una carpeta con espacios.
En Windows esas ejecuciones usan PATH con solo Windows y System32.

Resultado en dist/:

- axon-v0.4.0-alpha.1-windows-x64.zip
- axon-v0.4.0-alpha.1-windows-x64.zip.sha256

Cada candidato incluye BUILD_INFO.txt, MANIFEST.sha256 y avisos de terceros.
BUILD_INFO registra commit y estado clean/dirty cuando existe un checkout Git;
si no puede determinarlo, dice unavailable y no inventa un commit.
No se incluyen Ollama, pesos, Tesseract, archivos del usuario ni fixtures CLI.
Si falla una reconstrucción, un ZIP anterior en dist sigue siendo anterior:
la salida PASS del proceso completo y su checksum identifican el nuevo candidato.

Para usar otra carpeta de build sin reutilizar el cache de compilador:

```bash
AXON_PACKAGE_BUILD_DIR="$PWD/build-alpha-next" bash scripts/package.sh -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
```

Linux puede ejecutar `bash scripts/package.sh` para producir un .tar.gz
nativo de validación. No certifica compatibilidad en otras distribuciones.
macOS y MSVC quedan fuera de esta primera ruta de empaquetado.

## Estado confirmado

Darío confirmó el build Windows GCC 16.2 sin warnings, 22/22 CTest y ejecución
correcta del portable en otro Windows. Un nuevo build/tag produce otro archivo:
repetir su aceptación y verificar su propio checksum antes de publicarlo.
Linux sigue limitado a validación nativa; macOS/Docker requieren sus ejecuciones.

## Aceptación y publicación — siguiente etapa

1. Seguir PORTABLE.md en otro Windows sin toolchain ni el repositorio fuente.
   Registrar Windows/arquitectura, versión, resultados y Ollama opcional.
2. Revisar procedencia/licencias de los terceros del snapshot, incluidas
   modificaciones locales y el pequeño lector ZIP; conservar avisos aplicables.
3. Consolidar cambios en el repo oficial y crear CI para el alcance declarado.
   Compilar el candidato desde el commit exacto que se etiquetará.
4. Revisar LICENSE/README, guía de contribución, reportes de bugs, seguridad,
   roadmap e issues iniciales para colaboradores.
5. Crear tag v0.4.0-alpha.1 y GitHub prerelease con ZIP y .sha256 obtenidos de
   ese código, instrucciones, alcance, límites y resultados reales. No usar
   este número si ese tag ya existe; avanzar alpha.N y repetir las pruebas.
6. Actualizar axon-pl.org para enlazar esa descarga real y su código fuente;
   mantener el aviso de playground todavía no disponible.

Las firmas y la reproducibilidad binaria requieren trabajo adicional. Este
proceso comprueba contenido y ejecución del candidato, sin certificar aún
construcciones idénticas entre máquinas ni todas las plataformas del roadmap.
