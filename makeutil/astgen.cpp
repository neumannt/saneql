#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
//---------------------------------------------------------------------------
// astgen
// (c) 2017 Thomas Neumann
// SPDX-License-Identifier: GPL-3.0-only
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
static string astBaseHeader = "ASTBase.hpp";
static string astBaseClass = "ASTBase";
static string astHeader = "AST.hpp";
static string targetNamespace = "ast";
//---------------------------------------------------------------------------
/// A lexer for grammars
class GrammarLexer {
   public:
   /// Possible tokens
   enum Token { Eof,
                Whitespace,
                Symbol,
                Colon,
                ColonColon,
                Pipe,
                Semicolon,
                LCurly,
                RCurly,
                PercentPercent };

   private:
   /// The input
   istream& in;
   /// The putback character
   char putBack = 0;

   /// Get the next character
   char get();
   /// Unget a character
   void unget(char c) { putBack = c; }

   public:
   /// Constructor
   explicit GrammarLexer(istream& in) : in(in) {}

   /// Get the next token
   Token next(string& value, bool skipWS = true);
};
//---------------------------------------------------------------------------
char GrammarLexer::get()
// Get the next character
{
   if (putBack) {
      char c = putBack;
      putBack = 0;
      return c;
   }
   char c;
   if (in.get(c))
      return c;
   else
      return 0;
}
//---------------------------------------------------------------------------
GrammarLexer::Token GrammarLexer::next(string& value, bool skipWS)
// Get the next token
{
   while (true) {
      value.clear();
      char c = get();
      if (!c)
         return Eof;

      value += c;
      switch (c) {
         case ' ':
         case '\t':
         case '\n':
         case '\r':
            if (skipWS) continue;
            return Whitespace;
         case ':':
            c = get();
            if (c == ':') {
               value += c;
               return ColonColon;
            }
            unget(c);
            return Colon;
         case '|': return Pipe;
         case ';': return Semicolon;
         case '{': return LCurly;
         case '}': return RCurly;
         case '\'':
            while (true) {
               c = get();
               if (!c) break;
               value += c;
               if (c == '\'') break;
            }
            return Symbol;
         case '\"':
            while (true) {
               c = get();
               if (!c) break;
               value += c;
               if (c == '\"') break;
            }
            return Symbol;
         case '/':
            c = get();
            if (c == '/') {
               value += c;
               while (true) {
                  c = get();
                  if ((!c) || (c == '\n') || (c == '\r')) {
                     unget(c);
                     break;
                  }
                  value += c;
               }
               if (skipWS) continue;
               return Whitespace;
            }
            if (c == '*') {
               value += c;
               char last = 0;
               while (true) {
                  c = get();
                  if (!c) {
                     cerr << "unterminated comment" << endl;
                     exit(1);
                  }
                  value += c;
                  if ((last == '*') && (c == '/'))
                     break;
                  last = c;
               }
               if (skipWS) continue;
               return Whitespace;
            }
            unget(c);
            goto doSymbol;
         case '%':
            c = get();
            if (c == '%') {
               value += c;
               return PercentPercent;
            }
            unget(c);
            goto doSymbol;
         default:
         doSymbol:
            while (true) {
               c = get();
               if ((!c) || (c == '\n') || (c == '\r') || (c == ' ') || (c == '\t') || (c == ':') || (c == '|') || (c == ';') || (c == '{') || (c == '}')) {
                  unget(c);
                  break;
               }
               value += c;
            }
            return Symbol;
      }
   }
}
//---------------------------------------------------------------------------
/// A rule case
struct RuleCase {
   /// The symbols
   vector<string> symbols;
   /// The action
   string action;
};
//---------------------------------------------------------------------------
/// A rule
struct Rule {
   /// The name of the rule
   string name;
   /// The cases
   vector<RuleCase> cases;
};
//---------------------------------------------------------------------------
static vector<Rule> readRules(const char* name) {
   ifstream in(name);
   GrammarLexer lexer(in);
   string v;
   while (true) {
      auto token = lexer.next(v);
      if (token == GrammarLexer::Eof) {
         cerr << "rules body not found" << endl;
         exit(1);
      }
      if (token == GrammarLexer::PercentPercent)
         break;
   }

   vector<Rule> result;
   while (true) {
      auto token = lexer.next(v);
      if (token == GrammarLexer::Eof) {
         cerr << "unterminated rule body" << endl;
         exit(1);
      }
      if (token == GrammarLexer::PercentPercent)
         break;
      if (token != GrammarLexer::Symbol) {
         cerr << "rule name expected, got '" << v << "'" << endl;
         exit(1);
      }
      Rule rule;
      rule.name = v;
      if (lexer.next(v) != GrammarLexer::Colon) {
         cerr << "':' expected in rule " << rule.name << endl;
         exit(1);
      }
      RuleCase current;
      while (true) {
         token = lexer.next(v);
         if (token == GrammarLexer::Pipe) {
            rule.cases.push_back(move(current));
            current = RuleCase();
            continue;
         }
         if (token == GrammarLexer::Semicolon)
            break;
         if (token == GrammarLexer::Symbol) {
            current.symbols.push_back(move(v));
            continue;
         }
         if (token == GrammarLexer::LCurly) {
            unsigned level = 1;
            while (true) {
               token = lexer.next(v, false);
               if ((token == GrammarLexer::Eof) || (token == GrammarLexer::PercentPercent)) {
                  cerr << "unterminated action for rule " << rule.name << endl;
                  exit(1);
               }
               if (token == GrammarLexer::LCurly)
                  ++level;
               if (token == GrammarLexer::RCurly)
                  if (!(--level))
                     break;
               current.action += v;
            }
            token = lexer.next(v);
            if (token == GrammarLexer::Semicolon)
               break;
            if (token == GrammarLexer::Pipe) {
               rule.cases.push_back(move(current));
               current = RuleCase();
               continue;
            }
            cerr << "unexpected rules after action in rule " << rule.name << endl;
            exit(1);
         }
         cerr << "malformed rule " << rule.name << endl;
         exit(1);
      }
      rule.cases.push_back(move(current));
      result.push_back(move(rule));
   }
   return result;
}
//---------------------------------------------------------------------------
struct AstNode {
   string name;
   vector<string> cases, input;
};
//---------------------------------------------------------------------------
static vector<AstNode> readAst(const char* file)
// Read the AST specification
{
   ifstream in(file);
   GrammarLexer lexer(in);
   string s;
   vector<AstNode> result;
   while (true) {
      auto token = lexer.next(s);
      if (token == GrammarLexer::Eof)
         break;
      if (token != GrammarLexer::Symbol) {
         cerr << "malformed AST rule" << endl;
         exit(1);
      }
      AstNode node;
      node.name = s;
      token = lexer.next(s);
      if (token == GrammarLexer::ColonColon) {
         set<string> cn;
         while (true) {
            token = lexer.next(s);
            if (token == GrammarLexer::Colon)
               break;
            if (token != GrammarLexer::Symbol) {
               cerr << "malformed AST cases " << node.name << endl;
               exit(1);
            }
            if (cn.count(s)) {
               cerr << "duplicate AST case in " << node.name << " " << s << endl;
               exit(1);
            }
            cn.insert(s);
            node.cases.push_back(s);
         }
         sort(node.cases.begin(), node.cases.end());
      }
      if (token != GrammarLexer::Colon) {
         cerr << "AST body expected " << node.name << endl;
         exit(1);
      }
      vector<string> input;
      set<string> in;
      while (true) {
         token = lexer.next(s);
         if (token == GrammarLexer::Semicolon)
            break;
         if (token != GrammarLexer::Symbol) {
            cerr << "malformed AST body " << node.name << endl;
            exit(1);
         }
         if (in.count(s)) {
            cerr << "duplicate AST input in " << node.name << " " << s << endl;
            exit(1);
         }
         in.insert(s);
         node.input.push_back(s);
      }
      result.push_back(node);
   }
   return result;
}
//---------------------------------------------------------------------------
static string buildGuard(string_view file)
// Build a suitable guard macro
{
   string guard = "H_";
   for (char c : file) {
      if (c == '.') break;
      guard += (c == '/') ? '_' : c;
   }
   return guard;
}
//---------------------------------------------------------------------------
static void writeASTHeader(const char* astFile, const char* outFile)
// Write the AST header
{
   auto ast = readAst(astFile);

   ofstream out(outFile);
   out << "#ifndef " << buildGuard(astHeader) << endl
       << "#define " << buildGuard(astHeader) << endl
       << "//---------------------------------------------------------------------------" << endl
       << "#include \"" << astBaseHeader << "\"" << endl
       << "#include <iosfwd>" << endl
       << "//---------------------------------------------------------------------------" << endl
       << "namespace " << targetNamespace << " {" << endl
       << "//---------------------------------------------------------------------------" << endl
       << "// LCOV_EXCL_START" << endl
       << "//---------------------------------------------------------------------------" << endl
       << "// An AST node. This is the generic base class, each specific node forms a subclass" << endl
       << "class AST : public " << astBaseClass << " {" << endl
       << "   public:" << endl
       << "   /// All known types" << endl
       << "   enum class Type : unsigned { Token";
   for (auto& a : ast)
      out << "," << a.name;
   out << " };" << endl
       << endl
       << "   /// Constructor" << endl
       << "   AST(std::string_view content,uintptr_t descriptor) : " << astBaseClass << "(content,descriptor) {}" << endl
       << endl
       << "   /// Get the node type" << endl
       << "   Type getType() const { return static_cast<Type>(getRawType()); }" << endl
       << "};" << endl
       << "//---------------------------------------------------------------------------" << endl
       << "std::ostream& operator<<(std::ostream& out,AST::Type type);" << endl
       << "//---------------------------------------------------------------------------" << endl
       << "/// A token terminal node" << endl
       << "class Token : public AST {" << endl
       << "   public:" << endl
       << "   /// Constructor" << endl
       << "   template <class T> Token(const T& token) : AST(token.content,AST::buildDescriptor(static_cast<unsigned>(AST::Type::Token),token.encoding,0)) { computeHash(); }" << endl
       << "   /// Get as string" << endl
       << "   std::string asString() const { return getTokenValue(getRawSubType()); }" << endl
       << "   /// A type safe cast" << endl
       << "   static inline const Token* cast(const AST* node) { assert((!node)||(node->AST::getType()==AST::Type::Token)); return static_cast<const Token*>(node); }" << endl
       << "   /// A type safe cast" << endl
       << "   static inline const Token& ref(const AST* node) { assert((!!node)&&(node->AST::getType()==AST::Type::Token)); return *static_cast<const Token*>(node); }" << endl
       << "};" << endl;
   for (auto& a : ast) {
      out << "//---------------------------------------------------------------------------" << endl
          << "/// An AST node" << endl
          << "class " << a.name << " : public AST {" << endl
          << "   public:" << endl;
      if (!a.cases.empty()) {
         out << "   /// The specific subtype" << endl
             << "   enum class SubType : unsigned { ";
         bool first = true;
         for (auto& c : a.cases) {
            if (first)
               first = false;
            else
               out << ", ";
            out << c;
         }
         out << " };" << endl
             << "   /// Get the subtype" << endl
             << "   SubType getSubType() const { return static_cast<SubType>(getRawSubType()); }" << endl;
      }
      for (auto& i : a.input)
         out << "   /// Subnode" << endl
             << "   AST* " << i << ";" << endl;
      out << endl
          << "   /// Constructor" << endl
          << "   " << a.name << "(std::string_view _content";
      if (!a.cases.empty()) {
         out << ",SubType _subType";
      }
      for (auto& i : a.input)
         out << ",AST* " << i;
      out << ") : AST(_content,AST::buildDescriptor(AST::Type::" << a.name << "," << (a.cases.empty() ? "0" : "_subType") << "," << a.input.size() << "))";
      for (auto& i : a.input)
         out << "," << i << "(" << i << ")";
      out << " { computeHash(); }" << endl
          << "   /// A type safe cast" << endl
          << "   static inline const " << a.name << "* cast(const AST* node) { assert((!node)||(node->AST::getType()==AST::Type::" << a.name << ")); return static_cast<const " << a.name << "*>(node); }" << endl
          << "   /// A type safe cast" << endl
          << "   static inline const " << a.name << "& ref(const AST* node) { assert((!!node)&&(node->AST::getType()==AST::Type::" << a.name << ")); return *static_cast<const " << a.name << "*>(node); }" << endl
          << "   /// A type safe cast" << endl
          << "   static inline const " << a.name << "& ref(const AST& node) { assert((node.AST::getType()==AST::Type::" << a.name << ")); return static_cast<const " << a.name << "&>(node); }" << endl
          << "};" << endl;
   }
   out << "//---------------------------------------------------------------------------" << endl
       << "}" << endl
       << "//---------------------------------------------------------------------------" << endl
       << "// LCOV_EXCL_STOP" << endl
       << "//---------------------------------------------------------------------------" << endl
       << "#endif" << endl;
}
//---------------------------------------------------------------------------
static void writeASTBody(const char* astFile, const char* outFile)
// Write the AST body
{
   auto ast = readAst(astFile);

   ofstream out(outFile);
   out << "#include \"" << astHeader << "\"" << endl
       << "#include <ostream>" << endl
       << "//---------------------------------------------------------------------------" << endl
       << "namespace " << targetNamespace << " {" << endl
       << "//---------------------------------------------------------------------------" << endl
       << "// LCOV_EXCL_START" << endl
       << "std::ostream& operator<<(std::ostream& out,AST::Type type) {" << endl
       << "   switch (type) {" << endl
       << "      case AST::Type::Token: out << \"Token\"; break;" << endl;
   for (auto& a : ast)
      out << "      case AST::Type::" << a.name << ": out << \"" << a.name << "\"; break;" << endl;
   out << "   }" << endl
       << "   return out;" << endl
       << "}" << endl
       << "//---------------------------------------------------------------------------" << endl
       << "}" << endl
       << "//---------------------------------------------------------------------------" << endl;
}
//---------------------------------------------------------------------------
static void splitKey(const string& s, string& key, unsigned& field, const string& name, unsigned inputCount)
// Split a key
{
   auto split = s.find('=');
   if (split == string::npos) {
      cerr << "invalid key '" << s << "' in rule " << name << endl;
      exit(1);
   }
   key = s.substr(0, split);
   try {
      field = stoi(s.substr(split + 1));
   } catch (...) { field = 0; }
   if ((field < 1) || (field > inputCount)) {
      cerr << "field reference out of bounds for key " << key << " in rule " << name << endl;
      exit(1);
   }
}
//---------------------------------------------------------------------------
static void writeExpandedGrammar(const char* astFile, const char* grammarFile, const char* outFile)
// Write an expanded grammar
{
   auto ast = readAst(astFile);
   map<string, AstNode*> astLookup;
   for (auto& a : ast)
      astLookup[a.name] = &a;

   ifstream in(grammarFile);
   stringstream out;
   GrammarLexer lexer(in);
   string v;
   while (true) {
      auto token = lexer.next(v, false);
      if (token == GrammarLexer::Eof) {
         cerr << "rules body not found" << endl;
         exit(1);
      }
      out << v;
      if (token == GrammarLexer::PercentPercent)
         break;
   }
   auto next = [&](string& s) -> GrammarLexer::Token {
      while (true) {
         auto token = lexer.next(s, false);
         out << s;
         if (token != GrammarLexer::Whitespace)
            return token;
      }
   };

   while (true) {
      auto token = next(v);
      if (token == GrammarLexer::Eof) {
         cerr << "unterminated rule body" << endl;
         exit(1);
      }
      if (token == GrammarLexer::PercentPercent)
         break;
      if (token != GrammarLexer::Symbol) {
         cerr << "rule name expected, got '" << v << "'" << endl;
         exit(1);
      }
      string name = v;
      if (next(v) != GrammarLexer::Colon) {
         cerr << "':' expected in rule " << name << endl;
         exit(1);
      }
      vector<string> current;
      while (true) {
         token = next(v);
         if (token == GrammarLexer::Pipe) {
            current.clear();
            continue;
         }
         if (token == GrammarLexer::Semicolon)
            break;
         if (token == GrammarLexer::Symbol) {
            current.push_back(move(v));
            continue;
         }
         if (token == GrammarLexer::LCurly) {
            for (size_t index = 0, limit = current.size(); index != limit; ++index)
               if (current[index].front() == '%') {
                  current.resize(index);
                  break;
               }
            if (lexer.next(v) != GrammarLexer::Symbol) {
               cerr << "ast operation required in rule " << name << endl;
               exit(1);
            }
            if (v == "raw") {
               while (true) {
                  token = next(v);
                  if (token == GrammarLexer::Eof) {
                     cerr << "unterminated AST rule in rule " << name << endl;
                     exit(1);
                  }
                  if (token == GrammarLexer::RCurly)
                     break;
               }
            } else if (v == "return") {
               if (lexer.next(v) != GrammarLexer::Symbol) {
                  cerr << "malformed return clause in rule " << name << endl;
                  exit(1);
               }
               unsigned id;
               try {
                  id = stoi(v);
               } catch (...) { id = 0; }
               if ((id < 1) || (id > current.size())) {
                  cerr << "return clause out of range in rule " << name << endl;
                  exit(1);
               }
               out << " $$ = $" << id << "; }";
               if (lexer.next(v) != GrammarLexer::RCurly) {
                  cerr << "malformed return clause in rule " << name << endl;
                  exit(1);
               }
            } else if (v == "ast") {
               if (lexer.next(v) != GrammarLexer::Symbol) {
                  cerr << "malformed ast clause in rule " << name << endl;
                  exit(1);
               }
               if (!astLookup.count(v)) {
                  cerr << "unknown AST type " << v << " in rule " << name << endl;
                  exit(1);
               }
               auto node = astLookup[v];
               out << " $$ = static_cast<" << targetNamespace << "::AST*>(container.allocateASTImpl(lexer.getRangeFromBounds(@$.begin.column,@$.end.column)," << astBaseClass << "::buildDescriptor(" << targetNamespace << "::AST::Type::" << v;
               if (!node->cases.empty()) {
                  if (lexer.next(v) != GrammarLexer::Symbol) {
                     cerr << "malformed ast clause in rule " << name << endl;
                     exit(1);
                  }
                  bool found = false;
                  for (auto& c : node->cases) {
                     if (c == v) {
                        found = true;
                        break;
                     }
                  }
                  if (!found) {
                     cerr << "unknown subtype " << v << " of AST node " << node->name << " in rule " << name << endl;
                     exit(1);
                  }
                  out << "," << targetNamespace << "::" << node->name << "::SubType::" << v;
               } else {
                  out << ",0";
               }
               out << "," << node->input.size() << ")";
               vector<string> args;
               args.resize(node->input.size());
               map<string, unsigned> argSlots;
               for (size_t index = 0, limit = node->input.size(); index != limit; ++index) {
                  argSlots[node->input[index]] = index;
               }
               while (true) {
                  token = lexer.next(v);
                  if (token == GrammarLexer::RCurly)
                     break;
                  if (token != GrammarLexer::Symbol) {
                     cerr << "malformed ast clause in rule " << name << endl;
                     exit(1);
                  }
                  string key;
                  unsigned field;
                  splitKey(v, key, field, name, current.size());
                  if (!argSlots.count(key)) {
                     cerr << "unknown field " << key << " in AST node " << node->name << " in rule " << name << endl;
                     exit(1);
                  }
                  if (!args[argSlots[key]].empty()) {
                     cerr << "multiple definitions for field " << key << " in rule " << name << endl;
                     exit(1);
                  }
                  args[argSlots[key]] = "$" + to_string(field);
               }
               for (auto& a : args) {
                  out << ",";
                  if (a.empty())
                     out << "nullptr";
                  else
                     out << a;
               }
               out << ")); }";
            } else if (v == "list") {
               if (lexer.next(v) != GrammarLexer::Symbol) {
                  cerr << "malformed list clause in rule " << name << endl;
                  exit(1);
               }
               if (!astLookup.count(v)) {
                  cerr << "unknown AST type " << v << " in rule " << name << endl;
                  exit(1);
               }
               auto node = astLookup[v];
               if ((!node->cases.empty()) || (node->input.size() != 2)) {
                  cerr << "malformed list clause in rule " << name << endl;
                  exit(1);
               }
               auto type = v;
               vector<string> args;
               args.resize(node->input.size());
               map<string, unsigned> argSlots;
               for (size_t index = 0, limit = node->input.size(); index != limit; ++index) {
                  argSlots[node->input[index]] = index;
               }
               while (true) {
                  token = lexer.next(v);
                  if (token == GrammarLexer::RCurly)
                     break;
                  if (token != GrammarLexer::Symbol) {
                     cerr << "malformed list clause in rule " << name << endl;
                     exit(1);
                  }
                  string key;
                  unsigned field;
                  splitKey(v, key, field, name, current.size());
                  if (!argSlots.count(key)) {
                     cerr << "unknown field " << key << " in rule " << name << endl;
                     exit(1);
                  }
                  if (!args[argSlots[key]].empty()) {
                     cerr << "multiple definitions for field " << key << " in rule " << name << endl;
                     exit(1);
                  }
                  args[argSlots[key]] = "$" + to_string(field);
               }
               if ((args.size() > 1) && args[1].empty()) {
                  out << " $$ = static_cast<" << targetNamespace << "::" << type << "*>(container.createList(lexer.getRangeFromBounds(@$.begin.column,@$.end.column), " << astBaseClass << "::buildDescriptor(" << targetNamespace << "::AST::Type::" << type << ",0,2)";
                  out << ",";
                  if (args[0].empty())
                     out << "nullptr";
                  else
                     out << args[0];
               } else {
                  out << " $$ = static_cast<" << targetNamespace << "::" << type << "*>(container.appendList(lexer.getRangeFromBounds(@$.begin.column,@$.end.column), " << astBaseClass << "::buildDescriptor(" << targetNamespace << "::AST::Type::" << type << ",0,2)";
                  for (auto& a : args) {
                     out << ",";
                     if (a.empty())
                        out << "nullptr";
                     else
                        out << a;
                  }
               }
               out << ")); }";
            } else if (v == "null") {
               out << " $$ = nullptr; }";
               if (lexer.next(v) != GrammarLexer::RCurly) {
                  cerr << "malformed null clause in rule " << name << endl;
                  exit(1);
               }
            } else {
               cerr << "unknown AST operation " << v << " in rule " << name << endl;
               exit(1);
            }
            token = next(v);
            if (token == GrammarLexer::Semicolon)
               break;
            if (token == GrammarLexer::Pipe) {
               current.clear();
               continue;
            }
            cerr << "unexpected rules after action in rule " << name << endl;
            exit(1);
         }
         cerr << "malformed rule " << name << endl;
         exit(1);
      }
   }
   while (next(v) != GrammarLexer::Eof) {}

   {
      ofstream target(outFile);
      target << out.str();
   }
}
//---------------------------------------------------------------------------
static void showPossibleTypes(const char* astFile, const char* grammarFile, const char* symbol)
// Show the possible types for a symbol
{
   auto ast = readAst(astFile);
   auto grammar = readRules(grammarFile);

   struct Symbols {
      set<string> types;
      set<string> nonTerminals;
      bool null = false;
   };
   map<string, Symbols> symbolMap;
   for (auto& r : grammar)
      symbolMap[r.name];
   for (auto& r : grammar) {
      auto& symbol = symbolMap[r.name];
      for (auto& r2 : r.cases) {
         auto reportError = [&]() { cerr << "malformed action rule " << r2.action << endl; exit(1); };
         if (r2.action.empty()) {
            if (r2.symbols.empty())
               reportError();
            if (r2.symbols.size() != 1)
               cerr << "warning: implicit return in multi-symbol rule " << r.name << endl;
            if (symbolMap.count(r2.symbols.front()))
               symbol.nonTerminals.insert(r2.symbols.front());
            else
               symbol.types.insert("Token");
            continue;
         }
         stringstream in(r2.action);
         GrammarLexer lexer(in);
         string v;
         if (lexer.next(v) != GrammarLexer::Symbol)
            reportError();
         if (v == "null") {
            symbol.null = true;
         } else if (v == "raw") {
         } else if ((v == "ast") || (v == "list")) {
            if (lexer.next(v) != GrammarLexer::Symbol)
               reportError();
            symbol.types.insert(v);
         } else if (v == "return") {
            if (lexer.next(v) != GrammarLexer::Symbol)
               reportError();
            unsigned id;
            try {
               id = stoi(v);
            } catch (...) { id = 0; }
            if ((id < 1) || (id > r2.symbols.size()))
               reportError();
            string next = r2.symbols[id - 1];
            if (symbolMap.count(next))
               symbol.nonTerminals.insert(next);
            else
               symbol.types.insert("Token");
         } else {
            reportError();
         }
      }
   }

   bool changed = true;
   while (changed) {
      changed = false;
      for (auto& s : symbolMap) {
         auto& symbol = s.second;
         for (auto& s2 : symbol.nonTerminals) {
            auto& symbol2 = symbolMap[s2];
            if (symbol2.null && (!symbol.null)) {
               symbol.null = true;
               changed = true;
            }
            for (auto& t : symbol2.types) {
               if (!symbol.types.count(t)) {
                  symbol.types.insert(t);
                  changed = true;
               }
            }
         }
      }
   }

   if (!symbolMap.count(symbol)) {
      cout << "unknown symbol " << symbol << endl;
      exit(1);
   }
   auto& info = symbolMap[symbol];
   if (info.null)
      cout << "   // can be null" << endl;
   for (auto& t : info.types)
      cout << "   case ast::AST::Type::" << t << ": " << endl;
}
//---------------------------------------------------------------------------
[[noreturn]] static void showHelp(const char* argv0)
// Show command line options
{
   cerr << "usage: " << argv0 << " [command] <args..>" << endl
        << " astheader ast outfile" << endl
        << " astbody ast outfile" << endl
        << " expandgrammar ast grammarfile outfile" << endl
        << " possibletypes ast grammarfile symbol" << endl;
   exit(1);
}
//---------------------------------------------------------------------------
static int interpretArgs(int argc, char** argv) {
   if (!argc) return argc;
   int result = 1;
   for (int index = 1; index < argc; ++index) {
      if (((index + 1) < argc) && (argv[index][0] == '-')) {
         string_view o = argv[index];
         if (o == "--astbaseheader")
            astBaseHeader = argv[++index];
         else if (o == "--astbaseclass")
            astBaseClass = argv[++index];
         else if (o == "--astheader")
            astHeader = argv[++index];
         else if (o == "--namespace")
            targetNamespace = argv[++index];
         else {
            cerr << "unsupported option " << o << endl;
            exit(1);
         }
      } else {
         argv[result++] = argv[index];
      }
   }
   argv[result] = nullptr;
   return result;
}
//---------------------------------------------------------------------------
int main(int argc, char** argv) {
   argc = interpretArgs(argc, argv);
   if (argc < 2)
      showHelp(argv[0]);

   string command = argv[1];
   if (command == "astheader") {
      if (argc < 4)
         showHelp(argv[0]);
      writeASTHeader(argv[2], argv[3]);
      return 0;
   } else if (command == "astbody") {
      if (argc < 4)
         showHelp(argv[0]);
      writeASTBody(argv[2], argv[3]);
      return 0;
   } else if (command == "expandgrammar") {
      if (argc < 5)
         showHelp(argv[0]);
      writeExpandedGrammar(argv[2], argv[3], argv[4]);
      return 0;
   } else if (command == "possibletypes") {
      if (argc < 5)
         showHelp(argv[0]);
      showPossibleTypes(argv[2], argv[3], argv[4]);
      return 0;
   }
   showHelp(argv[0]);
}
//---------------------------------------------------------------------------
