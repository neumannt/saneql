#ifndef H_saneql_SaneQLLexer
#define H_saneql_SaneQLLexer
//---------------------------------------------------------------------------
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
// SaneQL
// (c) 2023 Thomas Neumann
// SPDX-License-Identifier: BSD-3-Clause
//---------------------------------------------------------------------------
namespace saneql {
//---------------------------------------------------------------------------
/// A lexer for SaneQL statements
class SaneQLLexer {
   public:
   /// A token
   enum class Token : unsigned {
      Eof,
      Error,
      Asterisk,
      Circumflex,
      ClientCommand,
      Colon,
      ColonColon,
      ColonEquals,
      Comma,
      Dot,
      DotDot,
      Equals,
      EqualsGreater,
      Exclamation,
      Float,
      Greater,
      GreaterEquals,
      Identifier,
      Integer,
      LCurly,
      LParen,
      LSquare,
      Less,
      LessEquals,
      Minus,
      NotEquals,
      Operator,
      Percent,
      Parameter,
      Plus,
      RCurly,
      RParen,
      RSquare,
      SemiColon,
      Solidus,
      String,
      UnterminatedLiteral,
      UnterminatedMultilineComment,
#define KEYWORD(A, B, C) B,
#include "parser/Keywords.hpp"
#undef KEYWORD
   };
   /// The content of a token
   struct TokenInfo {
      /// Possible token encodings
      enum class Encoding : unsigned {
         Raw,
         Identifier,
         IdentifierLiteral,
         Parameter,
         StringLiteral,
      };

      /// The content
      std::string_view content;
      /// The encoding
      Encoding encoding;

      /// Get the content converted into a regular string
      std::string asString() const;
   };

   private:
   /// Get the next character
   unsigned nextChar();
   /// Retrieve the next character without consuming it. Does not interpret unicode characters
   inline unsigned peekChar();

   /// Get the next token
   Token nextImpl(TokenInfo& info);

   /// Lex a client command
   Token lexClientCommand(TokenInfo& info);
   /// Lex an identifier
   Token lexIdentifier(TokenInfo& info);
   /// Lex an identifier literal
   Token lexIdentifierLiteral(TokenInfo& info);
   /// Lex a number
   Token lexNumber(TokenInfo& info);
   /// Lex an operator
   Token lexOperator(TokenInfo& info);
   /// Lex a parameter
   Token lexParameter(TokenInfo& info);
   /// Lex a string literal
   Token lexStringLiteral(TokenInfo& info);

   private:
   /// The input
   std::string_view input;
   /// The current position
   std::string_view::iterator current;
   /// A forced token (if any)
   Token forcedToken;

   public:
   /// Constructor
   explicit SaneQLLexer(std::string_view input, Token forcedToken = Token::Eof);

   /// Access the full text
   std::string_view getFullText() const { return input; }
   /// Get the current position
   std::string_view::iterator savePosition() const { return current; }
   /// Go back to a previously saved position
   void restorePosition(std::string_view::iterator p) { current = p; }

   /// Get the next token
   Token next(TokenInfo& info);

   /// Get the column of a certain position
   unsigned getColumnId(const char* pos) const { return pos - input.begin() + 1; }
   /// Get a content range from bounds
   std::string_view getRangeFromBounds(unsigned beginColumn, unsigned endColumn) const { return std::string_view{input.begin() + beginColumn - 1, endColumn - beginColumn}; }
   /// Check if a symbol is a keyword
   static bool isKeyword(std::string_view symbol);

   /// Report a syntax error in the current input
   void reportSyntaxError(unsigned column, const char* gotSymbol, const std::vector<const char*>& expectedSymbols);
};
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
#endif
