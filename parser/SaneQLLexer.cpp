#include "parser/SaneQLLexer.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
//---------------------------------------------------------------------------
// SaneQL
// (c) 2023 Thomas Neumann
// SPDX-License-Identifier: BSD-3-Clause
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace saneql {
//---------------------------------------------------------------------------
namespace {
//---------------------------------------------------------------------------
/// A keyword category
enum class KeywordCategory : unsigned { RESERVED_KEYWORD,
                                        UNRESERVED_KEYWORD,
                                        TYPE_FUNC_NAME_KEYWORD,
                                        COL_NAME_KEYWORD,
                                        PSEUDO_KEYWORD };
//---------------------------------------------------------------------------
/// A keyword description
struct KeywordInfo {
   SaneQLLexer::Token token;
   KeywordCategory category;
};
//---------------------------------------------------------------------------
struct HashCaseInsensitive {
   uint64_t operator()(string_view v) const {
      uint64_t result = 0xcbf29ce484222325;
      for (unsigned c : v) {
         result ^= ((c >= 'A') && (c <= 'Z')) ? (c + 'a' - 'A') : c;
         result *= 0x100000001b3;
      }
      return result;
   }
};
//---------------------------------------------------------------------------
static auto getKeywordsTable()
// Build the entries for the keywords loookup table
{
   using enum KeywordCategory;
   constexpr unsigned numKeywords =
#define KEYWORD(A, B, C) +1
#include "parser/Keywords.hpp"
#undef KEYWORD
      ;

   struct RawKeyword {
      string_view keyword;
      SaneQLLexer::Token token;
      KeywordCategory category;
   };
   static RawKeyword rawKeywords[] = {
#define KEYWORD(A, B, C) {A##sv, SaneQLLexer::Token::B, C},
#include "parser/Keywords.hpp"
#undef KEYWORD
   };

   unordered_map<string_view, KeywordInfo, HashCaseInsensitive> res;
   for (unsigned reader = 0; reader != numKeywords; ++reader) {
      if (rawKeywords[reader].category == PSEUDO_KEYWORD)
         continue;
      res[rawKeywords[reader].keyword] = {rawKeywords[reader].token, rawKeywords[reader].category};
   }
   return res;
}
//---------------------------------------------------------------------------
static const auto keywordsHashTable = getKeywordsTable();
//---------------------------------------------------------------------------
static bool isWS(unsigned c)
// Recognize whitespace characters (including Unicode whitespace)
{
   switch (c) {
      case 0x09:
      case 0x0A:
      case 0x0B:
      case 0x0C:
      case 0x0D:
      case 0x20:
      case 0x00A0:
      case 0x2000:
      case 0x2001:
      case 0x2002:
      case 0x2003:
      case 0x2004:
      case 0x2005:
      case 0x2006:
      case 0x2007:
      case 0x2008:
      case 0x2009:
      case 0x200A:
      case 0x200B:
      case 0x200C:
      case 0x200D:
      case 0x200E:
      case 0x200F:
      case 0x3000:
      case 0x2028:
      case 0x2029:
      case 0xFEFF: return true;
   }
   return false;
}
//---------------------------------------------------------------------------
static const char* skipWS(const char* current, const char* end, bool* nl = nullptr)
// Skip whitespaces
{
   if (nl) *nl = false;
   while (current < end) {
      unsigned c = *(current++);
      switch (c) {
         case 0x09:
         case 0x0B:
         case 0x0C:
         case 0x20: continue;
         case 0x0A:
         case 0x0D:
            if (nl) *nl = true;
            continue;
         case '-':
            if (!((current < end) && ((*current) == '-')))
               return current - 1;
            ++current;
            while (true) {
               if (current >= end)
                  return end;
               char c = *(current++);
               if ((c == 0x0A) || (c == 0x0D))
                  break;
            }
            if (nl) *nl = true; // (SQL Standard 5.2, Syntax Rule 10, comments are supposed to be treated like newlines)
            continue;
         case '/':
            if (!((current < end) && ((*current) == '*')))
               return current - 1;
            ++current;
            while (true) {
               if (current >= end)
                  return end;
               if (*(current++) == '/')
                  if (current[-2] == '*')
                     break;
            }
            if (nl) *nl = true; // (SQL Standard 5.2, Syntax Rule 10, comments are supposed to be treated like newlines)
            continue;
         default:
            // TODO accept Unicode whitespaces
            return current - 1;
      }
   }
   return end;
}
//---------------------------------------------------------------------------
static const char* findNextLiteralFragment(const char* current, const char* end)
// SQL allows literals to continue if separated by newline. Comments are supposed to be treated as newlines
{
   bool nl = false;
   current = skipWS(current, end, &nl);

   // we can only continue if we had a newline in between (SQL Standard 5.3, Syntax Rule 7)
   if (nl && (current < end) && (current[0] == '\''))
      return current + 1;
   else
      return nullptr;
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
SaneQLLexer::SaneQLLexer(string_view input, Token forcedToken)
   : input(input), current(input.begin()), forcedToken(forcedToken)
// Constructor
{
}
//---------------------------------------------------------------------------
SaneQLLexer::Token SaneQLLexer::next(TokenInfo& info)
// Get the next token
{
   // Handle forced tokens
   if (forcedToken != Token::Eof) {
      info.content = ""sv;
      info.encoding = TokenInfo::Encoding::Raw;
      auto result = forcedToken;
      forcedToken = Token::Eof;
      return result;
   }

   auto result = nextImpl(info);
   return result;
}
//---------------------------------------------------------------------------
unsigned SaneQLLexer::nextChar()
// Get the next character
{
   // Check of of input
   if (current == input.end())
      return 0;

   // Handle ASCII
   unsigned c = (*current) & 0xFF;
   if (!(c & 0x80)) {
      current += !!c;
      return c;
   }

   // TODO unicode support
   current += !!c;
   return c;
}
//---------------------------------------------------------------------------
unsigned SaneQLLexer::peekChar()
// Retrieve the next character without consuming it. Does not interpret Unicode characters
{
   return (current != input.end()) ? (*current) : 0;
}
//---------------------------------------------------------------------------
SaneQLLexer::Token SaneQLLexer::nextImpl(TokenInfo& info)
// Get the next token
{
   while (true) {
      // Many tokens are single-character anyway, prepare for handling these directly
      auto old = current;
      unsigned c = nextChar();
      info.content = string_view(old, current - old);
      info.encoding = TokenInfo::Encoding::Raw;

      // Handle EOF
      if (!c)
         return (current == input.end()) ? Token::Eof : Token::Error;

      // Handle Unicode characters
      if (__builtin_expect(c >= 0x80, 0)) {
         // Recognize Unicode whitespace characters
         if (isWS(c))
            continue;
         return lexIdentifier(info);
      }

      // Handle ASCII characters
      switch (c) {
         case 0x09:
         case 0x0A:
         case 0x0B:
         case 0x0C:
         case 0x0D:
         case 0x20: continue;
         case '!':
         case '#': return lexOperator(info);
         case '$':
            c = peekChar();
            if ((c >= '0') && (c <= '9'))
               return lexParameter(info);
            [[fallthrough]];
         case '%':
         case '&': return lexOperator(info);
         case '(': return Token::LParen;
         case ')': return Token::RParen;
         case '*':
         case '+': return lexOperator(info);
         case ',': return Token::Comma;
         case '-':
            // -- start a single-line comment
            if (peekChar() == '-') {
               while (current != input.end()) {
                  c = *(current++);
                  if ((c == '\n') || (c == '\r'))
                     break;
               }
               continue;
            }
            return lexOperator(info);
         case '.':
            // Recognize .. as DotDot and .[0-9] as number
            c = peekChar();
            if (c == '.') {
               ++current;
               info.content = string_view(old, current - old);
               return Token::DotDot;
            }
            if ((c >= '0') && (c <= '9'))
               return lexNumber(info);
            return Token::Dot;
         case '/':
            // /* starts a multi-line comment
            if (peekChar() == '*') {
               ++current;
               unsigned c2 = 0;
               while (true) {
                  c = nextChar();
                  if (!c) {
                     ++current;
                     info.content = string_view(old, current - old);
                     return Token::UnterminatedMultilineComment;
                  }
                  if ((c2 == '*') && (c == '/'))
                     break;
                  c2 = c;
               }
               continue;
            }
            return lexOperator(info);
         case '0':
         case '1':
         case '2':
         case '3':
         case '4':
         case '5':
         case '6':
         case '7':
         case '8':
         case '9': return lexNumber(info);
         case ':':
            c = peekChar();
            if ((c == ':') || (c == '=')) {
               ++current;
               info.content = string_view(old, current - old);
               return (c == ':') ? Token::ColonColon : Token::ColonEquals;
            }
            return Token::Colon;
         case ';': return Token::SemiColon;
         case '<':
         case '=':
         case '>':
         case '?':
         case '@': return lexOperator(info);
         case '[': return Token::LSquare;
         case '\"': return lexIdentifierLiteral(info);
         case '\'': return lexStringLiteral(info);
         case '\\': return lexClientCommand(info);
         case ']': return Token::RSquare;
         case '^':
         case '`': return lexOperator(info);
         case '{': return Token::LCurly;
         case '|': return lexOperator(info);
         case '}': return Token::RCurly;
         case '~': return lexOperator(info);
         default:
            if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || (c == '_'))
               return lexIdentifier(info);
            return Token::Error;
      }
   }
}
//---------------------------------------------------------------------------
SaneQLLexer::Token SaneQLLexer::lexClientCommand(TokenInfo& info)
// Lex a client command
{
   unsigned c = *(current++);
   info.content = {info.content.begin(), 2};
   if (!(((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '?') || (c == '_')))
      return Token::Error;

   while (true) {
      c = peekChar();
      if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '?') || (c == '_')) {
         ++current;
         continue;
      }
      info.content = string_view(info.content.begin(), current - info.content.begin());
      return Token::ClientCommand;
   }
}
//---------------------------------------------------------------------------
SaneQLLexer::Token SaneQLLexer::lexIdentifier(TokenInfo& info)
// Lex an identifier
{
   // Consume the identifier body
   while (true) {
      // Accept literal characters
      unsigned c = peekChar();
      if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '_')) {
         ++current;
         continue;
      }

      // Every other non-Unicode character terminates the identifier
      if (!(c & 0x80))
         break;

      // Accept non-whitespace Unicode characters
      auto old = current;
      c = nextChar();
      if (isWS(c)) {
         current = old;
         break;
      }
   }
   info.content = string_view(info.content.begin(), current - info.content.begin());
   info.encoding = TokenInfo::Encoding::Identifier;

   // Our identifier could be a keyword, check that
   auto keywordInfo = keywordsHashTable.find(info.content);
   if (keywordInfo != keywordsHashTable.end())
      return keywordInfo->second.token;
   return Token::Identifier;
}
//---------------------------------------------------------------------------
SaneQLLexer::Token SaneQLLexer::lexIdentifierLiteral(TokenInfo& info)
// Lex an identifier literal
{
   for (auto limit = input.end(); current < limit;) {
      if ((*(current++)) == '\"') {
         // Recognize escaped quotes
         if ((current < limit) && ((*current) == '\"')) {
            ++current;
            continue;
         }

         info.content = string_view(info.content.begin(), current - info.content.begin());
         info.encoding = TokenInfo::Encoding::IdentifierLiteral;
         return Token::Identifier;
      }
   }
   return Token::UnterminatedLiteral;
}
//---------------------------------------------------------------------------
SaneQLLexer::Token SaneQLLexer::lexNumber(TokenInfo& info)
// Lex a number
{
   auto begin = info.content.begin();
   auto limit = input.end();

   // The integer part
   if (begin[0] != '.') {
      while (true) {
         if (current == limit) {
            info.content = string_view(begin, current - begin);
            return Token::Integer;
         }
         char c = *(current++);
         if ((c >= '0') && (c <= '9'))
            continue;
         if ((c == '.') || (c == 'E') || (c == 'e'))
            break;
         --current;
         info.content = string_view(begin, current - begin);
         return Token::Integer;
      }
   }

   // The fractional part
   if (current[-1] == '.') {
      while (true) {
         if (current == limit) {
            info.content = string_view(begin, current - begin);
            return Token::Float;
         }
         char c = *(current++);
         if ((c >= '0') && (c <= '9'))
            continue;
         if ((c == 'E') || (c == 'e'))
            break;
         --current;
         info.content = string_view(begin, current - begin);
         return Token::Float;
      }
   }

   // The exponent part
   auto beginExponent = current - 1;
   unsigned c = peekChar();
   if ((c == '+') || (c == '-')) {
      ++current;
      c = peekChar();
   }
   if (!((c >= '0') && (c <= '9'))) {
      current = beginExponent;
      info.content = string_view(begin, current - begin);
      return Token::Float;
   }
   while (true) {
      if (current == limit) {
         info.content = string_view(begin, current - begin);
         return Token::Float;
      }
      char c = *(current++);
      if ((c >= '0') && (c <= '9'))
         continue;
      --current;
      info.content = string_view(begin, current - begin);
      return Token::Float;
   }
}
//---------------------------------------------------------------------------
SaneQLLexer::Token SaneQLLexer::lexOperator(TokenInfo& info)
// Lex an operator
{
   // Find the end of the operator string
   bool specialChars = false;
   for (auto limit = input.end(); current < limit; ++current) {
      switch (*current) {
         case '*':
         case '+':
         case '<':
         case '=':
         case '>': continue;
         case '!':
         case '#':
         case '%':
         case '&':
         case '?':
         case '@':
         case '^':
         case '`':
         case '|':
         case '~': specialChars = true; continue;
         case '-':
            if (((current + 1) < limit) && (current[1] == '-')) break;
            continue; // Stop for "--" comments
         case '/':
            if (((current + 1) < limit) && (current[1] == '*')) break;
            continue; // Stop for "/*" comments
      }
      break;
   }

   // Drop trailing + and - for regular operators
   auto begin = info.content.begin();
   if (!specialChars) {
      while (true) {
         if (((current - begin) <= 1) || ((current[-1] != '+') && (current[-1] != '-')))
            break;
         --current;
      }
   }
   info.content = string_view(begin, current - begin);

   // Recognize some named operators
   if ((current - begin) == 1) {
      switch (begin[0]) {
         case '%': return Token::Percent;
         case '*': return Token::Asterisk;
         case '+': return Token::Plus;
         case '-': return Token::Minus;
         case '/': return Token::Solidus;
         case '<': return Token::Less;
         case '=': return Token::Equals;
         case '>': return Token::Greater;
         case '^': return Token::Circumflex;
         case '!': return Token::Exclamation;
      }
   } else if ((current - begin) == 2) {
      char c = begin[0];
      if (c == '<') {
         char c = begin[1];
         if (c == '>')
            return Token::NotEquals;
         if (c == '=')
            return Token::LessEquals;
      } else if (c == '>') {
         char c = begin[1];
         if (c == '=')
            return Token::GreaterEquals;
      } else if (c == '!') {
         char c = begin[1];
         if (c == '=')
            return Token::NotEquals;
      } else if (c == '=') {
         char c = begin[1];
         if (c == '>')
            return Token::EqualsGreater;
      } else if (c == '&') {
         char c = begin[1];
         if (c == '&')
            return Token::AND;
      } else if (c == '|') {
         char c = begin[1];
         if (c == '|')
            return Token::OR;
      }
   }
   return Token::Operator;
}
//---------------------------------------------------------------------------
SaneQLLexer::Token SaneQLLexer::lexParameter(TokenInfo& info)
// Lex a parameter
{
   while (true) {
      unsigned c = peekChar();
      if ((c >= '0') && (c <= '9'))
         ++current;
      else
         break;
   }

   info.content = string_view(info.content.begin(), current - info.content.begin());
   info.encoding = TokenInfo::Encoding::Parameter;
   return Token::Parameter;
}
//---------------------------------------------------------------------------
SaneQLLexer::Token SaneQLLexer::lexStringLiteral(TokenInfo& info)
// Lex a string literal
{
   for (auto limit = input.end(); current < limit;) {
      char c = *(current++);
      if (c == '\'') {
         // Escaped quote?
         if ((current < limit) && ((*current) == '\'')) {
            ++current;
            continue;
         }

         // End of literal?
         auto next = findNextLiteralFragment(current, limit);
         if (!next) {
            info.content = string_view(info.content.begin(), current - info.content.begin());
            info.encoding = TokenInfo::Encoding::StringLiteral;
            return Token::String;
         }
         current = next;
      }
   }
   return Token::UnterminatedLiteral;
}
//---------------------------------------------------------------------------
string SaneQLLexer::TokenInfo::asString() const
// Get the content converted into a regular string
{
   switch (encoding) {
      case Encoding::Raw:
         return string(content.begin(), content.end());
      case Encoding::Identifier: {
         string result;
         result.reserve(content.size());
         for (char c : content) {
            if ((c >= 'A') && (c <= 'Z'))
               c += 'a' - 'A';
            result += c;
         }
         return result;
      }
      case Encoding::IdentifierLiteral: {
         string result;
         for (auto iter = content.begin() + 1, limit = content.end() - 1; iter < limit; ++iter) {
            char c = *iter;
            // Handle quotes within the literal
            if (c == '\"')
               ++iter; // skip the double quote
            result += c;
         }
         return result;
      }
      case Encoding::Parameter:
         return string(content.begin() + 1, content.end());
      case Encoding::StringLiteral: {
         string result;
         for (auto iter = content.begin() + 1, limit = content.end() - 1; iter < limit; ++iter) {
            assert(iter);
            char c = *iter;
            // Handle quotes within the literal
            if (c == '\'') {
               if (iter[1] == '\'') {
                  ++iter;
               } else {
                  // Must have another fragment
                  iter = findNextLiteralFragment(iter + 1, limit) - 1;
                  continue;
               }
            }
            result += c;
         }
         return result;
      }
   }
   __builtin_unreachable();
}
//---------------------------------------------------------------------------
bool SaneQLLexer::isKeyword(string_view symbol)
// Check if a symbol is a keyword
{
   return keywordsHashTable.find(symbol) != keywordsHashTable.end();
}
//---------------------------------------------------------------------------
static void appendSymbol(string& message, const char* symbol)
// Append a symbol, stripping quotes if needed
{
   unsigned len = strlen(symbol);
   if (len && (symbol[len - 1] == '"') && (symbol[0] != '"')) --len;
   message.append(symbol, len);
}
//---------------------------------------------------------------------------
void SaneQLLexer::reportSyntaxError(unsigned column, const char* gotSymbol, const std::vector<const char*>& expectedSymbols)
// Report a syntax error in the current input
{
   // Bounds check the column
   if (column > 0) --column;
   if (column > input.size()) column = input.size();

   // Provide a query extract
   constexpr unsigned contextSize = 25;
   string message = "syntax error near '";
   if (column > contextSize) {
      message.append("...", 3);
      message.append(input.begin() + column - contextSize + 3, contextSize - 3);
   } else {
      message.append(input.begin(), column);
   }
   if (column)
      message.append(" ", 1);
   message.append("*error*");
   if (column < input.size())
      message.append(" ", 1);
   if ((input.size() - column) > contextSize) {
      message.append(input.begin() + column, contextSize - 3);
      message.append("...", 3);
   } else {
      message.append(input.begin() + column, input.size() - column);
   }
   message.append("'", 1);

   // Show the current symbol
   if (gotSymbol) {
      message.append(", got ", 6);
      appendSymbol(message, gotSymbol);
   }

   // Make suggestions
   if (!expectedSymbols.empty()) {
      message.append(", expected ", 11);
      bool first = true;
      for (auto s : expectedSymbols) {
         if (first)
            first = false;
         else
            message.append(" or ", 4);
         appendSymbol(message, s);
      }
   }
   message.append(".");

   // And throw
   throw runtime_error(move(message));
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
