using Jcsc.Compiler.Common;
using Jcsc.Compiler.Diagnostics;

namespace Jcsc.Compiler.Lexing;

/// <summary>
/// Converts source code into a token stream while tracking position and lexical diagnostics.
/// </summary>
public sealed class Lexer(string text, DiagnosticBag diagnostics)
{
    private static readonly Dictionary<string, TokenKind> Keywords = new(StringComparer.Ordinal)
    {
        ["class"] = TokenKind.Class,
        ["static"] = TokenKind.Static,
        ["void"] = TokenKind.Void,
        ["int"] = TokenKind.Int,
        ["string"] = TokenKind.String,
        ["bool"] = TokenKind.Bool,
        ["if"] = TokenKind.If,
        ["else"] = TokenKind.Else,
        ["while"] = TokenKind.While,
        ["for"] = TokenKind.For,
        ["return"] = TokenKind.Return,
        ["true"] = TokenKind.True,
        ["false"] = TokenKind.False,
        ["var"] = TokenKind.Var,
    };

    private int _position;
    private int _line = 1;
    private int _column = 1;

    public IEnumerable<Token> Lex()
    {
        while (true)
        {
            var token = NextToken();
            if (token.Kind != TokenKind.BadToken)
                yield return token;
            if (token.Kind == TokenKind.EndOfFile)
                yield break;
        }
    }

    private Token NextToken()
    {
        SkipWhitespaceAndComments();

        var start = _position;
        var line = _line;
        var col = _column;

        if (IsAtEnd())
            return new Token(TokenKind.EndOfFile, string.Empty, null, new TextSpan(start, 0, line, col));

        var c = Current();

        if (char.IsLetter(c) || c == '_') return ReadIdentifierOrKeyword(start, line, col);
        if (char.IsDigit(c)) return ReadNumber(start, line, col);
        if (c == '"') return ReadString(start, line, col);

        Advance();
        return c switch
        {
            '+' => Tok(TokenKind.Plus, "+", start, line, col),
            '-' => Tok(TokenKind.Minus, "-", start, line, col),
            '*' => Tok(TokenKind.Star, "*", start, line, col),
            '/' => Tok(TokenKind.Slash, "/", start, line, col),
            '%' => Tok(TokenKind.Percent, "%", start, line, col),
            '=' when Match('=') => Tok(TokenKind.DoubleEquals, "==", start, line, col),
            '=' => Tok(TokenKind.Equals, "=", start, line, col),
            '!' when Match('=') => Tok(TokenKind.BangEquals, "!=", start, line, col),
            '!' => Tok(TokenKind.Bang, "!", start, line, col),
            '<' when Match('=') => Tok(TokenKind.LessEquals, "<=", start, line, col),
            '<' => Tok(TokenKind.Less, "<", start, line, col),
            '>' when Match('=') => Tok(TokenKind.GreaterEquals, ">=", start, line, col),
            '>' => Tok(TokenKind.Greater, ">", start, line, col),
            '&' when Match('&') => Tok(TokenKind.AmpersandAmpersand, "&&", start, line, col),
            '|' when Match('|') => Tok(TokenKind.PipePipe, "||", start, line, col),
            '.' => Tok(TokenKind.Dot, ".", start, line, col),
            ',' => Tok(TokenKind.Comma, ",", start, line, col),
            ';' => Tok(TokenKind.Semicolon, ";", start, line, col),
            '(' => Tok(TokenKind.OpenParen, "(", start, line, col),
            ')' => Tok(TokenKind.CloseParen, ")", start, line, col),
            '{' => Tok(TokenKind.OpenBrace, "{", start, line, col),
            '}' => Tok(TokenKind.CloseBrace, "}", start, line, col),
            _ => Bad(c, start, line, col)
        };
    }

    private void SkipWhitespaceAndComments()
    {
        while (!IsAtEnd())
        {
            if (char.IsWhiteSpace(Current()))
            {
                Advance();
                continue;
            }

            if (Current() == '/' && Peek() == '/')
            {
                while (!IsAtEnd() && Current() != '\n') Advance();
                continue;
            }

            if (Current() == '/' && Peek() == '*')
            {
                Advance(); Advance();
                while (!IsAtEnd())
                {
                    if (Current() == '*' && Peek() == '/') { Advance(); Advance(); break; }
                    Advance();
                }
                continue;
            }

            break;
        }
    }

    private Token ReadIdentifierOrKeyword(int start, int line, int col)
    {
        while (!IsAtEnd() && (char.IsLetterOrDigit(Current()) || Current() == '_')) Advance();
        var text = textSlice(start, _position - start);
        var kind = Keywords.TryGetValue(text, out var k) ? k : TokenKind.Identifier;
        return new Token(kind, text, null, new TextSpan(start, text.Length, line, col));
    }

    private Token ReadNumber(int start, int line, int col)
    {
        while (!IsAtEnd() && char.IsDigit(Current())) Advance();
        var lexeme = textSlice(start, _position - start);
        if (!int.TryParse(lexeme, out var value))
            diagnostics.Error(new TextSpan(start, lexeme.Length, line, col), "LEX001", $"Invalid integer literal '{lexeme}'.");
        return new Token(TokenKind.NumberLiteral, lexeme, value, new TextSpan(start, lexeme.Length, line, col));
    }

    private Token ReadString(int start, int line, int col)
    {
        Advance();
        var contentStart = _position;
        while (!IsAtEnd() && Current() != '"') Advance();

        if (IsAtEnd())
        {
            diagnostics.Error(new TextSpan(start, _position - start, line, col), "LEX002", "Unterminated string literal.");
            return new Token(TokenKind.StringLiteral, textSlice(start, _position - start), string.Empty, new TextSpan(start, _position - start, line, col));
        }

        var value = textSlice(contentStart, _position - contentStart);
        Advance();
        var totalText = textSlice(start, _position - start);
        return new Token(TokenKind.StringLiteral, totalText, value, new TextSpan(start, totalText.Length, line, col));
    }

    private Token Bad(char c, int start, int line, int col)
    {
        diagnostics.Error(new TextSpan(start, 1, line, col), "LEX000", $"Unexpected character '{c}'.");
        return new Token(TokenKind.BadToken, c.ToString(), null, new TextSpan(start, 1, line, col));
    }

    private Token Tok(TokenKind kind, string txt, int start, int line, int col) => new(kind, txt, null, new TextSpan(start, txt.Length, line, col));

    private bool Match(char expected)
    {
        if (IsAtEnd() || Current() != expected) return false;
        Advance();
        return true;
    }

    private bool IsAtEnd() => _position >= text.Length;
    private char Current() => IsAtEnd() ? '\0' : text[_position];
    private char Peek() => _position + 1 >= text.Length ? '\0' : text[_position + 1];

    private void Advance()
    {
        if (IsAtEnd()) return;
        if (text[_position] == '\n')
        {
            _line++;
            _column = 1;
        }
        else _column++;
        _position++;
    }

    private string textSlice(int start, int len) => len <= 0 ? string.Empty : text.Substring(start, len);
}
