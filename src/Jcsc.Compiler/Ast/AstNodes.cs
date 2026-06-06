using Jcsc.Compiler.Common;

namespace Jcsc.Compiler.Ast;

public abstract record AstNode(TextSpan Span);

public abstract record MemberDeclaration(TextSpan Span) : AstNode(Span);
public abstract record Statement(TextSpan Span) : AstNode(Span);
public abstract record Expression(TextSpan Span) : AstNode(Span);

public sealed record CompilationUnit(IReadOnlyList<MemberDeclaration> Members, TextSpan Span) : AstNode(Span);

public sealed record ClassDeclaration(string Name, IReadOnlyList<MemberDeclaration> Members, TextSpan Span) : MemberDeclaration(Span);

public sealed record Parameter(string TypeName, string Name, TextSpan Span) : AstNode(Span);

public sealed record MethodDeclaration(string ReturnType, string Name, IReadOnlyList<Parameter> Parameters, BlockStatement Body, bool IsStatic, TextSpan Span) : MemberDeclaration(Span);

public sealed record FieldDeclaration(string TypeName, string Name, Expression? Initializer, TextSpan Span) : MemberDeclaration(Span);

public sealed record BlockStatement(IReadOnlyList<Statement> Statements, TextSpan Span) : Statement(Span);
public sealed record VariableDeclarationStatement(string TypeName, string Name, Expression? Initializer, TextSpan Span) : Statement(Span);
public sealed record ExpressionStatement(Expression Expression, TextSpan Span) : Statement(Span);
public sealed record IfStatement(Expression Condition, Statement Then, Statement? Else, TextSpan Span) : Statement(Span);
public sealed record WhileStatement(Expression Condition, Statement Body, TextSpan Span) : Statement(Span);
public sealed record ForStatement(Statement? Initializer, Expression? Condition, Expression? Increment, Statement Body, TextSpan Span) : Statement(Span);
public sealed record ReturnStatement(Expression? Expression, TextSpan Span) : Statement(Span);

public sealed record LiteralExpression(object? Value, TextSpan Span) : Expression(Span);
public sealed record NameExpression(string Name, TextSpan Span) : Expression(Span);
public sealed record AssignmentExpression(string Name, Expression Expression, TextSpan Span) : Expression(Span);
public sealed record BinaryExpression(Expression Left, string Operator, Expression Right, TextSpan Span) : Expression(Span);
public sealed record UnaryExpression(string Operator, Expression Operand, TextSpan Span) : Expression(Span);
public sealed record CallExpression(Expression Target, IReadOnlyList<Expression> Arguments, TextSpan Span) : Expression(Span);
public sealed record MemberAccessExpression(Expression Target, string Member, TextSpan Span) : Expression(Span);
public sealed record ParenthesizedExpression(Expression Expression, TextSpan Span) : Expression(Span);
