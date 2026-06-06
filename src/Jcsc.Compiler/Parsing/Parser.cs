using Jcsc.Compiler.Ast;
using Jcsc.Compiler.Common;
using Jcsc.Compiler.Diagnostics;
using Jcsc.Compiler.Lexing;

namespace Jcsc.Compiler.Parsing;

/// <summary>
/// Recursive-descent parser that produces a custom AST from token stream.
/// </summary>
public sealed class Parser(IReadOnlyList<Token> tokens, DiagnosticBag diagnostics)
{
    private int _position;

    public CompilationUnit ParseCompilationUnit()
    {
        var members = new List<MemberDeclaration>();
        while (Current().Kind != TokenKind.EndOfFile)
            members.Add(ParseMember());

        var span = members.Count == 0 ? new TextSpan(0, 0, 1, 1) : members[0].Span with { Length = members[^1].Span.End - members[0].Span.Start };
        return new CompilationUnit(members, span);
    }

    private MemberDeclaration ParseMember()
    {
        if (Current().Kind == TokenKind.Class) return ParseClassDeclaration();
        return ParseMethodOrField();
    }

    private ClassDeclaration ParseClassDeclaration()
    {
        var classToken = Expect(TokenKind.Class, "PAR001", "Expected 'class'.");
        var name = Expect(TokenKind.Identifier, "PAR002", "Expected class identifier.");
        Expect(TokenKind.OpenBrace, "PAR003", "Expected '{' after class declaration.");

        var members = new List<MemberDeclaration>();
        while (Current().Kind is not TokenKind.CloseBrace and not TokenKind.EndOfFile)
            members.Add(ParseMethodOrField());

        var close = Expect(TokenKind.CloseBrace, "PAR004", "Expected '}' after class body.");
        return new ClassDeclaration(name.Text, members, new TextSpan(classToken.Span.Start, close.Span.End - classToken.Span.Start, classToken.Span.Line, classToken.Span.Column));
    }

    private MemberDeclaration ParseMethodOrField()
    {
        var start = Current().Span;
        var isStatic = Match(TokenKind.Static);
        var typeName = ParseTypeName();
        var name = Expect(TokenKind.Identifier, "PAR005", "Expected member name.");

        if (Current().Kind == TokenKind.OpenParen)
        {
            var parameters = ParseParameters();
            var body = ParseBlockStatement();
            return new MethodDeclaration(typeName, name.Text, parameters, body, isStatic, new TextSpan(start.Start, body.Span.End - start.Start, start.Line, start.Column));
        }

        Expression? init = null;
        if (Match(TokenKind.Equals)) init = ParseExpression();
        var semi = Expect(TokenKind.Semicolon, "PAR006", "Expected ';' after field declaration.");
        return new FieldDeclaration(typeName, name.Text, init, new TextSpan(start.Start, semi.Span.End - start.Start, start.Line, start.Column));
    }

    private IReadOnlyList<Parameter> ParseParameters()
    {
        Expect(TokenKind.OpenParen, "PAR007", "Expected '('.");
        var list = new List<Parameter>();
        while (Current().Kind is not TokenKind.CloseParen and not TokenKind.EndOfFile)
        {
            var type = ParseTypeName();
            var name = Expect(TokenKind.Identifier, "PAR008", "Expected parameter name.");
            list.Add(new Parameter(type, name.Text, name.Span));
            if (!Match(TokenKind.Comma)) break;
        }
        Expect(TokenKind.CloseParen, "PAR009", "Expected ')'.");
        return list;
    }

    private string ParseTypeName()
    {
        var token = Current();
        if (token.Kind is TokenKind.Int or TokenKind.String or TokenKind.Bool or TokenKind.Void or TokenKind.Identifier or TokenKind.Var)
        {
            Next();
            return token.Text;
        }

        diagnostics.Error(token.Span, "PAR010", "Expected type name.");
        Next();
        return "error";
    }

    private BlockStatement ParseBlockStatement()
    {
        var open = Expect(TokenKind.OpenBrace, "PAR011", "Expected '{'.");
        var statements = new List<Statement>();
        while (Current().Kind is not TokenKind.CloseBrace and not TokenKind.EndOfFile)
            statements.Add(ParseStatement());
        var close = Expect(TokenKind.CloseBrace, "PAR012", "Expected '}' to close block.");
        return new BlockStatement(statements, new TextSpan(open.Span.Start, close.Span.End - open.Span.Start, open.Span.Line, open.Span.Column));
    }

    private Statement ParseStatement()
    {
        return Current().Kind switch
        {
            TokenKind.OpenBrace => ParseBlockStatement(),
            TokenKind.If => ParseIfStatement(),
            TokenKind.While => ParseWhileStatement(),
            TokenKind.For => ParseForStatement(),
            TokenKind.Return => ParseReturnStatement(),
            TokenKind.Int or TokenKind.String or TokenKind.Bool or TokenKind.Var => ParseVariableDeclarationStatement(),
            _ => ParseExpressionStatement(),
        };
    }

    private Statement ParseVariableDeclarationStatement()
    {
        var start = Current().Span;
        var type = ParseTypeName();
        var name = Expect(TokenKind.Identifier, "PAR013", "Expected variable name.");
        Expression? init = null;
        if (Match(TokenKind.Equals)) init = ParseExpression();
        var semi = Expect(TokenKind.Semicolon, "PAR014", "Expected ';' after variable declaration.");
        return new VariableDeclarationStatement(type, name.Text, init, new TextSpan(start.Start, semi.Span.End - start.Start, start.Line, start.Column));
    }

    private IfStatement ParseIfStatement()
    {
        var ifTok = Expect(TokenKind.If, "PAR015", "Expected if.");
        Expect(TokenKind.OpenParen, "PAR016", "Expected '('.");
        var cond = ParseExpression();
        Expect(TokenKind.CloseParen, "PAR017", "Expected ')'.");
        var thenStatement = ParseStatement();
        Statement? elseStmt = null;
        if (Match(TokenKind.Else)) elseStmt = ParseStatement();
        return new IfStatement(cond, thenStatement, elseStmt, ifTok.Span);
    }

    private WhileStatement ParseWhileStatement()
    {
        var tok = Expect(TokenKind.While, "PAR018", "Expected while.");
        Expect(TokenKind.OpenParen, "PAR019", "Expected '('.");
        var cond = ParseExpression();
        Expect(TokenKind.CloseParen, "PAR020", "Expected ')'.");
        var body = ParseStatement();
        return new WhileStatement(cond, body, tok.Span);
    }

    private ForStatement ParseForStatement()
    {
        var tok = Expect(TokenKind.For, "PAR021", "Expected for.");
        Expect(TokenKind.OpenParen, "PAR022", "Expected '('.");

        Statement? init = null;
        if (Current().Kind != TokenKind.Semicolon)
            init = Current().Kind is TokenKind.Int or TokenKind.String or TokenKind.Bool or TokenKind.Var
                ? ParseVariableDeclarationStatement()
                : ParseExpressionStatement();
        else Expect(TokenKind.Semicolon, "PAR023", "Expected ';'.");

        Expression? cond = null;
        if (Current().Kind != TokenKind.Semicolon) cond = ParseExpression();
        Expect(TokenKind.Semicolon, "PAR024", "Expected ';'.");

        Expression? inc = null;
        if (Current().Kind != TokenKind.CloseParen) inc = ParseExpression();
        Expect(TokenKind.CloseParen, "PAR025", "Expected ')'.");

        var body = ParseStatement();
        return new ForStatement(init, cond, inc, body, tok.Span);
    }

    private ReturnStatement ParseReturnStatement()
    {
        var tok = Expect(TokenKind.Return, "PAR026", "Expected return.");
        Expression? expr = null;
        if (Current().Kind != TokenKind.Semicolon) expr = ParseExpression();
        Expect(TokenKind.Semicolon, "PAR027", "Expected ';'.");
        return new ReturnStatement(expr, tok.Span);
    }

    private ExpressionStatement ParseExpressionStatement()
    {
        var expr = ParseExpression();
        var semi = Expect(TokenKind.Semicolon, "PAR028", "Expected ';' after expression.");
        return new ExpressionStatement(expr, new TextSpan(expr.Span.Start, semi.Span.End - expr.Span.Start, expr.Span.Line, expr.Span.Column));
    }

    private Expression ParseExpression(int parentPrecedence = 0)
    {
        Expression left;
        var unaryPrecedence = UnaryPrecedence(Current().Kind);
        if (unaryPrecedence > 0 && unaryPrecedence >= parentPrecedence)
        {
            var op = Next();
            var operand = ParseExpression(unaryPrecedence);
            left = new UnaryExpression(op.Text, operand, op.Span);
        }
        else left = ParsePrimaryExpression();

        while (true)
        {
            var precedence = BinaryPrecedence(Current().Kind);
            if (precedence == 0 || precedence <= parentPrecedence) break;
            var op = Next();
            var right = ParseExpression(precedence);
            left = new BinaryExpression(left, op.Text, right, left.Span);
        }

        if (Current().Kind == TokenKind.Equals && left is NameExpression name)
        {
            var eq = Next();
            var value = ParseExpression();
            left = new AssignmentExpression(name.Name, value, eq.Span);
        }

        return left;
    }

    private Expression ParsePrimaryExpression()
    {
        var token = Current();
        switch (token.Kind)
        {
            case TokenKind.NumberLiteral:
                Next(); return new LiteralExpression(token.Value, token.Span);
            case TokenKind.StringLiteral:
                Next(); return new LiteralExpression(token.Value, token.Span);
            case TokenKind.True:
                Next(); return new LiteralExpression(true, token.Span);
            case TokenKind.False:
                Next(); return new LiteralExpression(false, token.Span);
            case TokenKind.OpenParen:
                Next();
                var inside = ParseExpression();
                Expect(TokenKind.CloseParen, "PAR029", "Expected ')'.");
                return new ParenthesizedExpression(inside, token.Span);
            case TokenKind.Identifier:
                return ParseNameOrCallChain();
            default:
                diagnostics.Error(token.Span, "PAR030", $"Unexpected token '{token.Text}' in expression.");
                Next();
                return new LiteralExpression(null, token.Span);
        }
    }

    private Expression ParseNameOrCallChain()
    {
        Expression expr = new NameExpression(Expect(TokenKind.Identifier, "PAR031", "Expected identifier.").Text, Previous().Span);

        while (true)
        {
            if (Match(TokenKind.Dot))
            {
                var member = Expect(TokenKind.Identifier, "PAR032", "Expected member name after '.'.");
                expr = new MemberAccessExpression(expr, member.Text, member.Span);
                continue;
            }

            if (Current().Kind == TokenKind.OpenParen)
            {
                Next();
                var args = new List<Expression>();
                while (Current().Kind is not TokenKind.CloseParen and not TokenKind.EndOfFile)
                {
                    args.Add(ParseExpression());
                    if (!Match(TokenKind.Comma)) break;
                }
                Expect(TokenKind.CloseParen, "PAR033", "Expected ')'.");
                expr = new CallExpression(expr, args, expr.Span);
                continue;
            }

            break;
        }

        return expr;
    }

    private Token Current(int offset = 0) => _position + offset >= tokens.Count ? tokens[^1] : tokens[_position + offset];
    private Token Previous() => _position == 0 ? tokens[0] : tokens[_position - 1];
    private Token Next() => tokens[_position++];

    private Token Expect(TokenKind kind, string code, string message)
    {
        if (Current().Kind == kind) return Next();
        diagnostics.Error(Current().Span, code, message);
        return new Token(kind, string.Empty, null, Current().Span);
    }

    private bool Match(TokenKind kind)
    {
        if (Current().Kind != kind) return false;
        Next();
        return true;
    }

    private static int UnaryPrecedence(TokenKind kind) => kind switch
    {
        TokenKind.Plus or TokenKind.Minus or TokenKind.Bang => 7,
        _ => 0
    };

    private static int BinaryPrecedence(TokenKind kind) => kind switch
    {
        TokenKind.Star or TokenKind.Slash or TokenKind.Percent => 6,
        TokenKind.Plus or TokenKind.Minus => 5,
        TokenKind.Less or TokenKind.LessEquals or TokenKind.Greater or TokenKind.GreaterEquals => 4,
        TokenKind.DoubleEquals or TokenKind.BangEquals => 3,
        TokenKind.AmpersandAmpersand => 2,
        TokenKind.PipePipe => 1,
        _ => 0
    };
}
