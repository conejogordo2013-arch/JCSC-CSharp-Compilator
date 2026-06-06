# JCSC C# Compiler (.NET 8)

Compilador de C# funcional con arquitectura modular de compilador real:

- **Front-end propio**: Lexer, Parser (AST), Semantic Analyzer.
- **Back-end**: emisión de ensamblados usando **Roslyn** (`Microsoft.CodeAnalysis.CSharp`).
- **CLI** para compilar `.cs` a `.exe` o `.dll`.

> Enfoque: usar un front-end controlado para diagnósticos/extensibilidad y Roslyn como backend estable para generar IL/ensamblados de producción.

## Estructura del proyecto

```text
src/
  Jcsc.Compiler/
    Ast/
      AstNodes.cs
    Cli/
      CompilationPipeline.cs
    CodeGen/
      RoslynEmitter.cs
    Common/
      TextSpan.cs
    Diagnostics/
      Diagnostic.cs
      DiagnosticBag.cs
    Lexing/
      Lexer.cs
      Token.cs
      TokenKind.cs
    Parsing/
      Parser.cs
    Semantic/
      SemanticAnalyzer.cs
      Symbols.cs
    Program.cs
    Jcsc.Compiler.csproj
examples/
  HelloWorld.cs
  FunctionsDemo.cs
```

## Módulos

1. **Lexer**
   - Tokeniza keywords, identificadores, literales, operadores y símbolos.
   - Ignora espacios y comentarios `//` y `/* ... */`.
   - Reporta errores léxicos con línea/columna.

2. **Parser + AST**
   - Parser recursivo descendente con precedencias para expresiones.
   - Construye AST con separación clara entre `Expression` y `Statement`.
   - Soporta:
     - variables
     - funciones
     - clases
     - `if/else`
     - `for/while`
     - expresiones aritméticas/lógicas

3. **Análisis semántico**
   - Tabla de símbolos con scopes anidados.
   - Validación básica de tipos (`int`, `string`, `bool`, `void`, `var`).
   - Detección de no-definidos, redeclaraciones y asignaciones incompatibles.

4. **Generación de código (Roslyn)**
   - Convierte texto fuente a `CSharpCompilation`.
   - Emite `.exe` o `.dll`.
   - Recolecta errores de Roslyn y los unifica en diagnósticos del compilador.

5. **CLI**
   - Entrada: archivo `.cs`
   - Salida: ensamblado configurable
   - Flags: `--output/-o`, `--dll`, `--debug`

## Uso

### Build del compilador

```bash
dotnet build src/Jcsc.Compiler/Jcsc.Compiler.csproj
```

### Compilar ejemplo Hello World

```bash
dotnet run --project src/Jcsc.Compiler/Jcsc.Compiler.csproj -- examples/HelloWorld.cs -o artifacts/hello.exe
```

### Ejecutar binario generado

```bash
dotnet artifacts/hello.exe
```

### Compilar librería

```bash
dotnet run --project src/Jcsc.Compiler/Jcsc.Compiler.csproj -- examples/FunctionsDemo.cs --dll -o artifacts/functions.dll
```

## Ejemplos

- `examples/HelloWorld.cs`: hello world mínimo.
- `examples/FunctionsDemo.cs`: funciones, loops y condicionales.

## Extensibilidad (cómo crecer hacia compilador aún más completo)

1. **Tipos enriquecidos**: introducir sistema de tipos nominales (clases/interfaces) y conversiones implícitas.
2. **Binder dedicado**: separar `binding` de `semantic` para producir árbol enlazado tipado.
3. **IR intermedia**: agregar IR propia antes de Roslyn o IL Emit para optimizaciones.
4. **Lowering**: transformar `for`, `if`, etc. a constructos canónicos para simplificar backend.
5. **Análisis avanzado**: flujo de control, definite assignment, nullability personalizada.
6. **Incremental compilation**: cache de syntax trees y symbols por módulo.

