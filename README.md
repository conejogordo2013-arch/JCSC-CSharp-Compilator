# JCCSC - compilador C# subset en C (sin GCC/Clang como backend del lenguaje)

Este proyecto compila un subconjunto de C# y produce un artefacto propio `.jccsc`.
Además puede ejecutar directamente el programa compilado con su runtime interno (`--run`).

## Pipeline

1. Lexer
2. Parser + AST
3. Análisis semántico
4. Compilación a formato propio `JCCSC-BC-1`
5. Runtime interno para ejecutar `Program.Main()`

## Uso

```bash
make
./jccsc examples/hello.cs -o hello.jccsc --run
./jccsc examples/functions.cs -o functions.jccsc --run
./jccsc examples/bool_loops.cs -o bool_loops.jccsc --run
./jccsc examples/hello.cs -o native_x64.s --backend native --target x86_64
./jccsc examples/hello.cs -o native_x86.s --backend native --target x86_32
./jccsc examples/hello.cs -o native_arm64.s --backend native --target arm64
./jccsc examples/hello.cs -o native_arm32.s --backend native --target arm32
./jccsc examples/hello.cs --backend native
./hello
```

En backend `native`, si no pasas `--target`, JCCSC detecta automáticamente la arquitectura del host.
Si no pasas `-o`, el ejecutable toma el nombre base del archivo de entrada.
Usa `--emit-asm` para forzar salida ensamblador en lugar de ejecutable.

## Testing

```bash
make test
```

Incluye una batería amplia de tests de éxito y error bajo `tests/cases`.

## Alcance actual

- Tipos básicos: `int`, `bool`, `string`, `void`
- Clases y métodos estáticos
- Sobrecarga de métodos por cantidad y tipos de parámetros (int/bool/string/arrays/clases por nombre exacto)
- Variables, `if/else`, `while`, `do/while`, `for`, `switch`, `return`, `break`, `continue`
- `using` y bloque `namespace ... { ... }`
- Objetos por referencia con `new`, campos y metodos de instancia basicos
- Declaraciones `struct` básicas (tratadas con semántica de objeto en runtime actual)
- Declaraciones `interface` básicas (firmas + `class : Interface` parseable)
- Arrays `int[]`, `new int[n]` e indexación `a[i]`
- `foreach (int x in arr)` sobre `int[]`
- Propiedad `arr.Length` para arrays
- Expresiones aritméticas y lógicas
- Operadores de actualización/asignación: `++`, `--`, `+=`, `-=`, `*=`, `/=`, `%=`
- Literales booleanos: `true`, `false`
- Literal `null` para tipos por referencia (`class`, `int[]`) y comparaciones `==`/`!=`
- Built-in: `Console.WriteLine(...)`
- Backend nativo con auto-detección de host para generar ejecutable local
- Salida ensamblador (`--emit-asm`) para `x86_32`, `x86_64`, `arm32`, `arm64`

## Nota

No depende de Roslyn ni de invocar GCC/Clang para traducir C#.
La ejecución se hace en el runtime propio dentro de `jccsc`.

## Objetivo de largo plazo

El objetivo es seguir ampliando el subset hasta acercarse a C# completo.
Actualmente **no implementa .NET 8 ni todo C#**: faltan, entre otros, generics,
LINQ, async/await, excepciones completas, delegates/events, attributes, structs,
interfaces avanzadas, nullable reference types, reflection, librería base completa
y backend nativo AOT real con ABI completo.
