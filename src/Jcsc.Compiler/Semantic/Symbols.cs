namespace Jcsc.Compiler.Semantic;

public sealed record VariableSymbol(string Name, string TypeName);

public sealed class Scope(Scope? parent = null)
{
    private readonly Dictionary<string, VariableSymbol> _variables = new(StringComparer.Ordinal);
    public Scope? Parent { get; } = parent;

    public bool TryDeclare(VariableSymbol symbol)
    {
        if (_variables.ContainsKey(symbol.Name)) return false;
        _variables[symbol.Name] = symbol;
        return true;
    }

    public VariableSymbol? Lookup(string name)
    {
        if (_variables.TryGetValue(name, out var symbol)) return symbol;
        return Parent?.Lookup(name);
    }
}
