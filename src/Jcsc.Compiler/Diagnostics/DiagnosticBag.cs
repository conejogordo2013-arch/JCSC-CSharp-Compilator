using Jcsc.Compiler.Common;

namespace Jcsc.Compiler.Diagnostics;

/// <summary>
/// Centralized mutable collection used by phases to report diagnostics.
/// </summary>
public sealed class DiagnosticBag
{
    private readonly List<Diagnostic> _items = [];

    public IReadOnlyList<Diagnostic> Items => _items;
    public bool HasErrors => _items.Any(d => d.Severity == DiagnosticSeverity.Error);

    public void Add(Diagnostic diagnostic) => _items.Add(diagnostic);

    public void Error(TextSpan span, string code, string message) => Add(new Diagnostic(span, code, message));

    public void AddRange(IEnumerable<Diagnostic> diagnostics) => _items.AddRange(diagnostics);
}
