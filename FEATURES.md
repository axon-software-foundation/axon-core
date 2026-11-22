> Documento histórico/de diseño: las fases y capacidades descritas no acreditan implementación ni validación. El estado vigente se mantiene en [docs/ROADMAP.md](docs/ROADMAP.md).

# Axon Language — Feature Registry
# Version 0.3.0 / Phase 4B
# Last updated: 2026-07-05

---

## CORE LANGUAGE

### Entry Point
- `main {}` — único punto de entrada (estilo Go), un solo main por proyecto
- `flow {}` — definición declarativa del flujo (input, version, telemetry, debug, output)
- `agent {}` — definición de agente LLM (puro, sin lógica)
- `task {}` dentro de agent — unidad de trabajo del agente
- `embedding {}` — configuración de memoria semántica

### Tipos de datos
- `string` — comillas dobles `"..."` o simples `'...'`
- `number` — entero o decimal (`42`, `3.14`)
- `bool` — `true` / `false`
- `null` — valor nulo asignable (`n = null`)
- `list` — `[1, 2, 3]`
- `dict` — `{"key": "value"}`

### Operadores
- Aritméticos: `+`, `-`, `*`, `/`, `%`, `**` (potencia)
- Comparación: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Lógicos: `&&`, `||`
- Pipeline: `>>` (encadenamiento de agentes)
- Invocación: `->` (agentes, módulos, tareas)
- Acceso: `.` (propiedades y métodos nativos)
- Asignación: `=`

### F-strings
- Comillas dobles: `f"Hola {name}, score={score*100}%"`
- Comillas simples: `f'resultado: {2**10}'`
- Expresiones dentro de `{}`: `f"val: {a + b}"`

### Control de flujo
- `if / else if / else`
- `for item in list {}`
- `while condition {}`
- `match value { "case" -> {} _ -> {} }` (wildcard `_`)
- `try {} catch err {}`
- `raise "mensaje"`
- `parallel {}` — ejecución paralela de agentes

### Variables y scope
- Variables comparten scope del padre (estilo Python)
- `_` válido como nombre de variable (`_ = "discard"`, `_x = 42`)
- Variables no definidas arrojan error con contexto archivo:línea
- Variables asignadas a `null` NO arrojan error

### Strings — métodos nativos
- `.upper()` `.lower()` `.strip()`
- `.length()` `.contains(s)` `.startswith(s)` `.endswith(s)`
- `.find(s)` `.repeat(n)` `.tostr()`
- `.replace(pattern, repl)` — literal o regex
- `.split(pattern)` — literal o regex
- `.find(pattern)` — primer match regex
- `.find_all(pattern)` — todos los matches
- `.test(pattern)` — bool
- `.sub(pattern, repl, n)` — primeras N ocurrencias
- ⚠️ Nota: `match` renombrado a `find` para evitar colisión con keyword `match`

### Números — métodos nativos
- `.abs()` `.floor()` `.ceil()` `.round()` `.tostr()`

### Listas — métodos nativos
- `.append(val)` `.pop()` `.first()` `.last()`
- `.size()` `.contains(v)` `.join(sep)` `.reverse()`

### Asignación de índices
- Dict: `d['key'] = 'value'`
- Dict anidado: `d['user']['name'] = 'Alice'` (auto-crea nodos intermedios)
- Lista: `lst[0] = 99`

### typeof
- `typeof(val)` → `"string"`, `"number"`, `"bool"`, `"null"`, `"list"`, `"dict"`

### Comentarios
- `#` — comentario de línea
- `/* */` — comentario de bloque

---

## AGENTES Y TASKS

### Definición de agente
```axon
agent Analyzer {
    provider    ollama
    model       llama3.2
    transport   rest
    endpoint    "localhost:11434"
    temperature = 0.7
    max_tokens  = 2048
    num_ctx     = 32768

    memory { runtime = enabled  session = enabled }

    validation {
        confidence  > 0.7
        max_retries = 1
        on_fail     = skip
    }

    task Analyze {
        input   = __input__
        prompt  = ["Analiza esto: "]   # también: prompts = [...]
    }

    task Summarize {
        input  = Processor->Analyze    # lee output cacheado (no re-ejecuta)
        prompt = ["Resume en 3 párrafos"]
    }
}
```

### Ejecución de agentes — 3 formas
```axon
# Forma 1: agente completo (corre tasks en orden de declaración)
result = Processor(flow.input)

# Forma 2: task específica
result = Processor->Analyze(flow.input)

# Forma 3: pipeline
result = flow.input >> Processor >> Reviewer
```

### task_graph (encadenamiento personalizado)
```axon
task_graph {
    Analyze -> Validate [on_success]
    Analyze -> Retry    [on_fail]
    Validate -> done
}
```
- Sin `task_graph`: tasks corren en orden de declaración
- Con `task_graph`: control total del flujo

### Acceso a outputs de tasks
```axon
result = Processor(flow.input)
print(Processor.Analyze)    # output de task Analyze
print(Processor.Summarize)  # output de task Summarize
```

### Campos deprecated
- `output = text` en tasks — deprecado, ignorado silenciosamente
  → usar `"text"` con comillas si se necesita, o recoger en main

---

## COMPUTE Y GPU/CPU

```axon
agent Processor {
    # Tokens
    max_tokens = 2048      # tokens a generar
    num_ctx    = 32768     # ventana de contexto

    # GPU / CPU
    num_gpu    = -1        # -1=todo GPU, 0=solo CPU, N=N capas GPU
    num_thread = 8         # hilos CPU
    main_gpu   = 0         # GPU a usar (multi-GPU)

    # Rendimiento
    num_batch  = 512
    f16_kv     = true      # float16 KV cache (menos VRAM)
    low_vram   = false

    # Modo automático
    compute      = auto    # detecta GPU, fallback a CPU si falla
    gpu_fallback = true    # reintenta en CPU si GPU crashea (HTTP 500)
}
```

### compute = auto/gpu/cpu
- `auto` — intenta GPU, si falla → CPU automático
- `gpu` — fuerza GPU
- `cpu` — fuerza CPU

---

## FLUJO Y DEBUG

```axon
flow MyFlow {
    version   = "1.0"
    input     = "data"
    telemetry = true
    debug     = false    # false = silencia INFO/WARN (producción)
                         # true  = muestra conexiones, skips, warnings

    output {
        format  = structured
        level   = info
        target  = stdout
        metrics = true
    }
}
```

---

## FILE I/O

```axon
# Lectura con auto-detect por extensión
data = file->read("./sales.csv")      # → List de Dicts
cfg  = file->read("./config.json")    # → Dict o List
doc  = file->read("./report.docx")    # → string texto plano
rows = file->read("./data.xlsx")      # → List de Dicts
raw  = file->read("./data.csv", true) # forzar texto plano

# Escritura
file->write("./out.txt", content)
file->append("./out.txt", more)
file->delete(path)
file->mkdir(path)
file->rmdir(path)
entries = file->ls(path)
ok = file->exists(path)
```

### CSV
```axon
rows = csv->parse(raw_text)
rows = csv->parse(raw_text, ";")       # delimitador personalizado
txt  = csv->stringify(rows)
csv->write("./out.csv", rows)
```

### JSON
```axon
obj = json->parse(raw_text)            # no-op si ya es Dict/List
txt = json->stringify(obj)             # compacto
txt = json->stringify(obj, 2)          # pretty-print
json->write("./out.json", obj, 2)
```

### Tipos auto-detectados en CSV/XLSX
- Números → `number`
- `"true"/"false"` → `bool`
- Resto → `string`

### Formatos soportados
- `.csv` / `.tsv` — CSV (delimitador auto)
- `.json` / `.jsonl` — JSON
- `.docx` — Word (extracción de texto)
- `.xlsx` — Excel (hoja 1 como List de Dicts)
- `.txt` `.md` `.log` — texto plano

---

## MEMORIA

### Tipos de memoria
| Scope | Persiste | Alcance |
|-------|----------|---------|
| `runtime` | ❌ | Por agente, muere con proceso |
| `session` | ❌ | Todo el flow, muere con proceso |
| `shared` | ❌ | Todos los agentes del flow |
| `persistent` | ✅ `.bin` | Global, nunca se borra |
| `semantic` | ✅ `.idx` | HNSW + embeddings |

### Memoria key-value
```axon
memory->store("key", "val")                    # runtime (default)
memory->store("key", "val", "session")
memory->store("key", "val", "shared")
memory->store("key", "val", "persistent")
memory->store("key", "val", "runtime", 30)     # TTL segundos

val  = memory->get("key")
ok   = memory->has("key")
memory->remove("key")
memory->clear("runtime")   # o "all"
keys = memory->keys("session")
sz   = memory->size()

memory->persist("./mem.bin")
memory->load("./mem.bin")
```

### Memoria semántica (HNSW + Ollama embeddings)
```axon
embedding DocMemory {
    provider = "ollama"
    model    = "nomic-embed-text"
    endpoint = "http://localhost:11434"
    dim      = 768
}

memory->index("texto del documento", "metadata")
results = memory->search("query", 5)           # top_k=5
for r in results {
    print(r["text"], r["score"], r["metadata"])
}
memory->sem_persist("./docs.idx")              # un solo archivo
memory->sem_load("./docs.idx")
print(memory->sem_size())
```

---

## SHELL INTEROP

```axon
shell { echo "hello world" }
out  = shell.stdout
code = shell.exitcode
```

---

## PIPELINE OPERATOR

```axon
# Secuencial
result = flow.input >> AgentA >> AgentB >> AgentC

# Con grupos paralelos
result = flow.input >> [AgentA, AgentB] >> Merger
```

---

## ERRORES Y DEBUGGING

### Runtime errors
```
[runtime error] undefined variable 'foo'
  --> examples/main.axon:14
   14 |     result = foo
```

### Syntax errors
```
[syntax error] unterminated string
  --> examples/main.axon:6
    6 |     x = "hello
```

### LLM errors
```
[llm error] HTTP 400: request exceeds context size
  Tip: reduce input size or add 'num_ctx = N' to your agent

[axon] GPU crash detected — retrying on CPU
```

---

## FECHAS

```axon
now  = date->now()                       # "2026-07-05"
ts   = date->timestamp()                 # "2026-07-05T14:32:10"
unix = date->unix()                      # 1751723530 (numero)

fmt  = date->format(now, "DD/MM/YYYY")   # "05/07/2026"
fmt2 = date->format(now, "YYYY-MM")      # "2026-07"
# Tokens: YYYY YY MM DD HH mm SS

parts = date->parse("2026-07-05")
# dict: {year, month, day, hour, minute, second}

tomorrow  = date->add(now, 1, "days")
last_week = date->sub(now, 7, "days")
# units: days, months, years, hours

diff = date->diff("2026-01-01", now, "days")  # 185.0

ok = date->before("2025-01-01", now)   # true
ok = date->after(now, "2025-01-01")    # true
```

Internamente todo es string o number — no hay tipo Date nativo.

---

## PENDIENTES (próximas fases)

```
✅ rename: .match() → .find()     (ambos aceptados)
✅ date-> module                   (now, timestamp, unix, format, parse, add, sub, diff, before, after)
✅ Phase 4C  PDF (MuPDF)
⏸ Phase 4C  Web scraping / HTML
⏸ Phase 5   Providers: OpenAI, Anthropic, Cohere (API keys)
✅ Phase 6   Multi-flow, imports, flow ownership de agentes
             Flow1()->Flow2(Flow1.output)
             import "./flows/research.axon" { Research }
⏸ Phase 7   RAG pipeline, LoRA fine-tuning
⏸ Phase 8   Mesh ultrasónico, gossip protocol
⏸ Release   Quitar output = text del parser
⏸ Web       Documentación pública
```
