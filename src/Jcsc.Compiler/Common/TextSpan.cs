namespace Jcsc.Compiler.Common;

/// <summary>
/// Represents a region in source text with line and column information.
/// </summary>
public readonly record struct TextSpan(int Start, int Length, int Line, int Column)
{
    public int End => Start + Length;
}
