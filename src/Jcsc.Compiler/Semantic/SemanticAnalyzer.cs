using Jcsc.Compiler.Ast;
using Jcsc.Compiler.Diagnostics;

namespace Jcsc.Compiler.Semantic;

/// <summary>
/// Performs symbol declaration checks and simple type validation across statements/expressions.
/// </summary>
public sealed class SemanticAnalyzer(DiagnosticBag diagnostics)
{
    public void Analyze(CompilationUnit unit)
    {
        foreach (var member in unit.Members)
            AnalyzeMember(member, new Scope());
    }

    private void AnalyzeMember(MemberDeclaration member, Scope scope)
    {
        switch (member)
        {
            case ClassDeclaration c:
                foreach (var inner in c.Members) AnalyzeMember(inner, new Scope(scope));
                break;
            case MethodDeclaration m:
                var methodScope = new Scope(scope);
                foreach (var p in m.Parameters)
                    methodScope.TryDeclare(new VariableSymbol(p.Name, p.TypeName));
                AnalyzeStatement(m.Body, methodScope, m.ReturnType);
                break;
            case FieldDeclaration f when f.Initializer is not null:
                var type = AnalyzeExpression(f.Initializer, scope);
                EnsureAssignable(f.TypeName, type, f.Span, "SEM006", "Field initializer type mismatch.");
                break;
        }
    }

    private void AnalyzeStatement(Statement statement, Scope scope, string expectedReturnType)
    {
        switch (statement)
        {
            case BlockStatement block:
                var nested = new Scope(scope);
                foreach (var s in block.Statements) AnalyzeStatement(s, nested, expectedReturnType);
                break;
            case VariableDeclarationStatement v:
                if (!scope.TryDeclare(new VariableSymbol(v.Name, v.TypeName)))
                    diagnostics.Error(v.Span, "SEM001", $"Variable '{v.Name}' already declared in this scope.");
                if (v.Initializer is not null)
                {
                    var initType = AnalyzeExpression(v.Initializer, scope);
                    EnsureAssignable(v.TypeName, initType, v.Span, "SEM002", "Type mismatch in variable initializer.");
                }
                break;
            case ExpressionStatement e:
                AnalyzeExpression(e.Expression, scope);
                break;
            case IfStatement i:
                EnsureAssignable("bool", AnalyzeExpression(i.Condition, scope), i.Condition.Span, "SEM003", "If condition must be bool.");
                AnalyzeStatement(i.Then, new Scope(scope), expectedReturnType);
                if (i.Else is not null) AnalyzeStatement(i.Else, new Scope(scope), expectedReturnType);
                break;
            case WhileStatement w:
                EnsureAssignable("bool", AnalyzeExpression(w.Condition, scope), w.Condition.Span, "SEM004", "While condition must be bool.");
                AnalyzeStatement(w.Body, new Scope(scope), expectedReturnType);
                break;
            case ForStatement f:
                var loopScope = new Scope(scope);
                if (f.Initializer is not null) AnalyzeStatement(f.Initializer, loopScope, expectedReturnType);
                if (f.Condition is not null)
                    EnsureAssignable("bool", AnalyzeExpression(f.Condition, loopScope), f.Condition.Span, "SEM005", "For condition must be bool.");
                if (f.Increment is not null) AnalyzeExpression(f.Increment, loopScope);
                AnalyzeStatement(f.Body, loopScope, expectedReturnType);
                break;
            case ReturnStatement r:
                var actual = r.Expression is null ? "void" : AnalyzeExpression(r.Expression, scope);
                EnsureAssignable(expectedReturnType, actual, r.Span, "SEM007", $"Return type '{actual}' is not assignable to '{expectedReturnType}'.");
                break;
        }
    }

    private string AnalyzeExpression(Expression expression, Scope scope)
    {
        return expression switch
        {
            LiteralExpression { Value: int } => "int",
            LiteralExpression { Value: string } => "string",
            LiteralExpression { Value: bool } => "bool",
            LiteralExpression => "object",
            NameExpression n => scope.Lookup(n.Name)?.TypeName ?? UnknownName(n),
            AssignmentExpression a => AnalyzeAssignment(a, scope),
            UnaryExpression u => AnalyzeUnary(u, scope),
            BinaryExpression b => AnalyzeBinary(b, scope),
            ParenthesizedExpression p => AnalyzeExpression(p.Expression, scope),
            CallExpression => "object",
            MemberAccessExpression => "object",
            _ => "object"
        };
    }

    private string AnalyzeAssignment(AssignmentExpression a, Scope scope)
    {
        var symbol = scope.Lookup(a.Name);
        if (symbol is null)
        {
            diagnostics.Error(a.Span, "SEM008", $"Variable '{a.Name}' is not defined.");
            return "object";
        }

        var rhs = AnalyzeExpression(a.Expression, scope);
        EnsureAssignable(symbol.TypeName, rhs, a.Span, "SEM009", $"Cannot assign '{rhs}' to '{symbol.TypeName}'.");
        return symbol.TypeName;
    }

    private string AnalyzeUnary(UnaryExpression u, Scope scope)
    {
        var operand = AnalyzeExpression(u.Operand, scope);
        return u.Operator switch
        {
            "!" when operand == "bool" => "bool",
            "+" or "-" when operand == "int" => "int",
            _ => ReportUnaryType(u, operand)
        };
    }

    private string AnalyzeBinary(BinaryExpression b, Scope scope)
    {
        var left = AnalyzeExpression(b.Left, scope);
        var right = AnalyzeExpression(b.Right, scope);

        return b.Operator switch
        {
            "+" or "-" or "*" or "/" or "%" when left == "int" && right == "int" => "int",
            "==" or "!=" => "bool",
            "<" or "<=" or ">" or ">=" when left == "int" && right == "int" => "bool",
            "&&" or "||" when left == "bool" && right == "bool" => "bool",
            _ => ReportBinaryType(b, left, right)
        };
    }

    private string UnknownName(NameExpression n)
    {
        diagnostics.Error(n.Span, "SEM010", $"Variable '{n.Name}' is not defined.");
        return "object";
    }

    private string ReportUnaryType(UnaryExpression u, string operand)
    {
        diagnostics.Error(u.Span, "SEM011", $"Operator '{u.Operator}' is not valid for type '{operand}'.");
        return "object";
    }

    private string ReportBinaryType(BinaryExpression b, string left, string right)
    {
        diagnostics.Error(b.Span, "SEM012", $"Operator '{b.Operator}' cannot be applied to '{left}' and '{right}'.");
        return "object";
    }

    private void EnsureAssignable(string target, string actual, Jcsc.Compiler.Common.TextSpan span, string code, string message)
    {
        if (target == "var" || target == actual || target == "object") return;
        diagnostics.Error(span, code, message + $" Expected '{target}', actual '{actual}'.");
    }
}
