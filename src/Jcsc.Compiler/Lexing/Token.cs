using Jcsc.Compiler.Common;

namespace Jcsc.Compiler.Lexing;

/// <summary>
/// Immutable lexical token with optional literal value.
/// </summary>
public sealed record Token(TokenKind Kind, string Text, object? Value, TextSpan Span);
