namespace Jcsc.Compiler.Lexing;

/// <summary>
/// Enumerates all lexical categories recognized by the compiler front-end.
/// </summary>
public enum TokenKind
{
    EndOfFile,
    BadToken,

    Identifier,
    NumberLiteral,
    StringLiteral,

    // Keywords
    Class,
    Static,
    Void,
    Int,
    String,
    Bool,
    If,
    Else,
    While,
    For,
    Return,
    True,
    False,
    Var,

    // Operators / punctuation
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Equals,
    DoubleEquals,
    Bang,
    BangEquals,
    Less,
    LessEquals,
    Greater,
    GreaterEquals,
    AmpersandAmpersand,
    PipePipe,
    Dot,
    Comma,
    Semicolon,
    OpenParen,
    CloseParen,
    OpenBrace,
    CloseBrace,
}
