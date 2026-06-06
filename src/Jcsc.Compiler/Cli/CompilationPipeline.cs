using Jcsc.Compiler.CodeGen;
using Jcsc.Compiler.Diagnostics;
using Jcsc.Compiler.Lexing;
using Jcsc.Compiler.Parsing;
using Jcsc.Compiler.Semantic;

namespace Jcsc.Compiler.Cli;

/// <summary>
/// Orchestrates front-end phases (lex/parse/semantic) and Roslyn emission.
/// </summary>
public static class CompilationPipeline
{
    public static int Compile(string sourcePath, string outputPath, bool emitLibrary, bool debug)
    {
        var source = File.ReadAllText(sourcePath);
        var diagnostics = new DiagnosticBag();

        var tokens = new Lexer(source, diagnostics).Lex().ToList();
        var ast = new Parser(tokens, diagnostics).ParseCompilationUnit();
        new SemanticAnalyzer(diagnostics).Analyze(ast);

        if (diagnostics.HasErrors)
        {
            PrintDiagnostics(sourcePath, diagnostics.Items);
            return 1;
        }

        if (!RoslynEmitter.Emit(source, outputPath, emitLibrary, debug, diagnostics))
        {
            PrintDiagnostics(sourcePath, diagnostics.Items);
            return 1;
        }

        Console.WriteLine($"Compilation succeeded: {outputPath}");
        return 0;
    }

    private static void PrintDiagnostics(string sourcePath, IEnumerable<Diagnostic> diagnostics)
    {
        foreach (var d in diagnostics.OrderBy(d => d.Span.Line).ThenBy(d => d.Span.Column))
            Console.Error.WriteLine($"{sourcePath}({d.Span.Line},{d.Span.Column}): {d.Severity.ToString().ToLowerInvariant()} {d.Code}: {d.Message}");
    }
}
