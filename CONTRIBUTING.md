# Contribuir a Axon

Aceptamos contribuciones y reportes en español e inglés. Core está en alpha:
revisa [el roadmap](docs/ROADMAP.md) y abre una propuesta antes de dedicar
trabajo a sintaxis nueva, dependencias o cambios de arquitectura.

## Entorno y validación

CMake >= 3.20, compilador C++17, Bash y Make/Ninja. En Windows usamos MSYS2
UCRT64 GCC con Ninja. No hacen falta Ollama ni Tesseract para la suite básica.

Git Bash en Windows, desde la raíz (ajusta C:/msys64 si corresponde):

```bash
bash scripts/validate.sh Debug -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DAXON_WARNINGS_AS_ERRORS=ON
```

Linux, o macOS como plataforma todavía experimental:

```bash
bash scripts/validate.sh Debug -DAXON_WARNINGS_AS_ERRORS=ON
```

Repite con Release para cambios de ejecución, sincronización o empaquetado.
La referencia actual es 22 casos CTest, 31 runtime y 78 del intérprete.
Las cantidades pueden aumentar al añadir regresiones útiles. Los tests de
modelo real son una comprobación adicional: ver examples/validation/ollama_tasks.axon.

## Qué incluir en un PR

1. Una rama enfocada en un problema o comportamiento. Usa un fork si no tienes
   acceso de escritura. Evita formatear archivos ajenos al cambio.
2. Una explicación del problema y del comportamiento resultante, con ejemplo
   mínimo cuando ayude a revisar el contrato del lenguaje.
3. Una regresión para errores de ejecución y pruebas relevantes para funciones
   nuevas; no cambies expectativas para ocultar un fallo.
4. Comandos ejecutados y resultados, incluido el sistema/compiler usado.
   Indica explícitamente las plataformas o servicios que no pudiste probar.
5. Documentación y changelog si cambia el comportamiento público.

Conserva las formas de invocación de agente completo, task específica y
pipeline. Cambios de input, resultados, imports y errores requieren cubrir
el contrato existente. Una prueba con un provider simulado no reemplaza una
implementación real; los dobles de prueba pertenecen a tests.

## Calidad y dependencias

Las advertencias propias se corrigen, con AXON_WARNINGS_AS_ERRORS=ON.
HNSWlib/stb se tratan como encabezados externos: no desactives diagnósticos
globalmente para ocultar advertencias de Axon. Revisa docs/BUILD_WARNINGS.md.
Las dependencias nuevas necesitan motivo, alcance y licencia. Conserva avisos
y registra procedencia/modificaciones de código vendorizado.

No incluyas build-*/, dist/, pesos de modelos, secretos, archivos personales
ni documentos de terceros usados para pruebas privadas. Usa datos mínimos
sintéticos y revisa el diff antes del commit.

## Revisión y licencia

Un maintainer revisa los PR antes de integrarlos. Pasar CI no garantiza que
una función esté lista ni que todas las plataformas estén soportadas.
El código propio conserva MIT; los componentes externos conservan sus
licencias y avisos. Al contribuir declara cualquier restricción de licencia.
Consulta CODE_OF_CONDUCT.md y SECURITY.md para convivencia y vulnerabilidades.
