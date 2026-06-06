using Jcsc.Compiler.Common;

namespace Jcsc.Compiler.Diagnostics;

/// <summary>
/// Represents a compiler diagnostic error or warning.
/// </summary>
public sealed class Diagnostic(TextSpan span, string code, string message, DiagnosticSeverity severity = DiagnosticSeverity.Error)
{
    public TextSpan Span { get; } = span;
    public string Code { get; } = code;
    public string Message { get; } = message;
    public DiagnosticSeverity Severity { get; } = severity;

    public override string ToString() => $"{Severity} {Code} ({Span.Line},{Span.Column}): {Message}";
}

public enum DiagnosticSeverity
{
    Info,
    Warning,
    Error,
}
