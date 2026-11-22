# Política de advertencias del build

## Código de Axon

Se mantienen -Wall y -Wextra en GCC/Clang, junto con las opciones existentes.
AXON_WARNINGS_AS_ERRORS está activado por defecto: añade -Werror en GCC/Clang
y /WX en MSVC. Una advertencia habilitada en el código propio impide completar
el build; no se ha añadido -w ni una desactivación global de advertencias.

Para diagnóstico temporal de un toolchain puede configurarse
`-DAXON_WARNINGS_AS_ERRORS=OFF`. Esto conserva las advertencias y permite
compilar, pero no corresponde a la comprobación estricta de esta entrega.
Un cache que ya tenga esa opción en OFF debe reconfigurarse explícitamente en ON.

## Limpieza aplicada

- Pragmas de enlace y diagnóstico exclusivos de MSVC protegidos con _MSC_VER.
- Eliminadas funciones internas sin llamadas (decoder base64 duplicado y
  extractor XML sin uso), así como variables locales sin uso.
- Separadas instrucciones y aclarados bloques en el lector PDF, conservando
  las condiciones y el orden de ejecución.
- En el pequeño lector ZIP local, sustituido strncpy por una copia de los
  buffers de igual capacidad y terminación NUL explícita del destino.

## Dependencias externas

HNSWlib y stb se incluyen por rutas SYSTEM de CMake. El compilador los trata
como encabezados externos y excluye sus diagnósticos habituales de la salida
del código propio. Eso no corrige su implementación ni demuestra ausencia de
problemas en esas bibliotecas; sus revisiones se gestionan por separado.

Los archivos de HNSWlib/stb no se modifican. La excepción es el lector ZIP
local en third_party/miniz, que sí recibe la corrección indicada arriba y no
se clasifica como encabezado SYSTEM. zlib conserva su configuración existente.

## Verificación de esta entrega

- Build Release completo con GCC 14.2 en Linux, con cero advertencias y -Werror.
- 22/22 CTest, suites de 31 casos runtime y 78 del intérprete.
- Archivo nativo extraído y ejecutado mediante axon_portable.
- Comprobación específica de nombres ZIP vacíos, cortos y de 259 caracteres.
- Prueba de que los flags reales del proyecto rechazan una variable sin uso.

Falta repetir la compilación estricta con GCC 16.2 en Windows. Si esa versión
señala una advertencia adicional, se debe revisar su diagnóstico y corregirla.
MSVC y Clang no se han validado en esta entrega.

Desde Git Bash, después de aplicar este parche sobre el estado con la
corrección de scheduler-shutdown:

```bash
bash scripts/package.sh -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DAXON_WARNINGS_AS_ERRORS=ON
```

El cambio de flags y de rutas de inclusión hace que Ninja recompile las
unidades afectadas. Se esperan 22/22 CTest y PASS del paquete extraído.
