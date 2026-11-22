# Probar el portable

Este es un candidato local de Axon alpha. No requiere modificar el PATH ni
instalar Axon globalmente. Conserva toda la carpeta extraída, incluido bin/.

## Windows x64 — PowerShell

Extrae el ZIP en una carpeta fuera del proyecto; preferiblemente en otro
Windows 10/11 x64 sin MSYS2, GCC ni Git. Desde la raíz extraída:

```powershell
.\bin\axon.exe version
.\bin\axon.exe test
.\bin\axon.exe run .\examples\validation\functions.axon
.\bin\axon.exe run .\examples\validation\main.axon
.\bin\axon.exe run .\examples\validation\tasks.axon
.\bin\axon.exe run .\examples\validation\delays.axon
.\bin\axon.exe run .\examples\flows.axon
```

La versión debe coincidir con VERSION, las dos suites deben pasar y los
ejemplos deben imprimir PASS. `axon test` necesita los dos ejecutables de
pruebas incluidos junto a axon.exe. No copies solamente el ejecutable principal.

Para probar el modelo real, instala Ollama aparte y descarga llama3.2:

```powershell
ollama pull llama3.2
.\bin\axon.exe run .\examples\validation\ollama_tasks.axon
```

Ollama debe estar iniciado. Sin servicio local, el ejemplo de LLM no acredita
una prueba real del provider. Las pruebas anteriores no lo necesitan.

## Linux — archivo de validación nativo

```bash
./bin/axon version
./bin/axon test
./bin/axon run examples/validation/tasks.axon
```

El archivo Linux depende de las bibliotecas del sistema de compilación. No
declara todavía un mínimo de glibc ni portabilidad entre distribuciones.

## Integridad y alcance

Junto al archivo comprimido se genera un archivo .sha256 con el hash del
comprimido. En Git Bash, desde la carpeta donde están ambos:

```bash
sha256sum -c axon-v0.4.0-alpha.1-windows-x64.zip.sha256
```

MANIFEST.sha256 contiene hashes de los archivos internos; desde la raíz
extraída, Git Bash/Linux permite comprobarlos con `sha256sum -c MANIFEST.sha256`.
Un checksum detecta diferencias de bytes; la firma de código queda pendiente.
Consulta RELEASE_NOTES.md para las limitaciones de esta alpha.
