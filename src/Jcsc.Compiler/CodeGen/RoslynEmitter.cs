using System.Reflection;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Jcsc.Compiler.Diagnostics;

namespace Jcsc.Compiler.CodeGen;

/// <summary>
/// Emits .dll/.exe artifacts using Roslyn, reusing metadata from the current runtime.
/// </summary>
public static class RoslynEmitter
{
    public static bool Emit(string sourceCode, string outputPath, bool asLibrary, bool debug, DiagnosticBag diagnostics)
    {
        var parseOptions = CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.Preview);
        var syntaxTree = CSharpSyntaxTree.ParseText(sourceCode, parseOptions, path: "input.cs");

        var compilationOptions = new CSharpCompilationOptions(
            asLibrary ? OutputKind.DynamicallyLinkedLibrary : OutputKind.ConsoleApplication,
            optimizationLevel: debug ? OptimizationLevel.Debug : OptimizationLevel.Release,
            nullableContextOptions: NullableContextOptions.Enable);

        var references = ResolveFrameworkReferences();

        var compilation = CSharpCompilation.Create(
            Path.GetFileNameWithoutExtension(outputPath),
            [syntaxTree],
            references,
            compilationOptions);

        using var fs = File.Create(outputPath);
        var emitResult = compilation.Emit(fs);
        foreach (var d in emitResult.Diagnostics.Where(d => d.Severity == Microsoft.CodeAnalysis.DiagnosticSeverity.Error))
        {
            var lineSpan = d.Location.GetLineSpan();
            var line = lineSpan.StartLinePosition.Line + 1;
            var col = lineSpan.StartLinePosition.Character + 1;
            diagnostics.Error(new Jcsc.Compiler.Common.TextSpan(0, 0, line, col), "RSL001", d.ToString());
        }

        return emitResult.Success;
    }

    private static IEnumerable<MetadataReference> ResolveFrameworkReferences()
    {
        var tpa = (string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")
                  ?? throw new InvalidOperationException("Unable to read trusted platform assemblies.");

        var assemblies = tpa.Split(Path.PathSeparator)
            .Where(path =>
            {
                var file = Path.GetFileName(path);
                return file.StartsWith("System.", StringComparison.Ordinal)
                    || file == "mscorlib.dll"
                    || file == "netstandard.dll"
                    || file == "System.Private.CoreLib.dll";
            })
            .Distinct(StringComparer.OrdinalIgnoreCase);

        foreach (var asm in assemblies)
            yield return MetadataReference.CreateFromFile(asm);

        yield return MetadataReference.CreateFromFile(typeof(object).Assembly.Location);
        yield return MetadataReference.CreateFromFile(typeof(Console).Assembly.Location);
        yield return MetadataReference.CreateFromFile(Assembly.Load("System.Runtime").Location);
    }
}
