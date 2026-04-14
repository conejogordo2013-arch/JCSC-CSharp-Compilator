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
```

## Testing

```bash
make test
```

Incluye una batería amplia de tests de éxito y error bajo `tests/cases`.

## Alcance actual

- Tipos básicos: `int`, `bool`, `string`, `void`
- Clases y métodos estáticos
- Variables, `if/else`, `while`, `for`, `return`, `break`, `continue`
- Expresiones aritméticas y lógicas
- Literales booleanos: `true`, `false`
- Built-in: `Console.WriteLine(...)`

## Nota

No depende de Roslyn ni de invocar GCC/Clang para traducir C#.
La ejecución se hace en el runtime propio dentro de `jccsc`.

## Objetivo de largo plazo

El objetivo es seguir ampliando el subset hasta acercarse a C# completo y
disponer de backend nativo real (sin VM). Esta versión todavía usa runtime interno.
