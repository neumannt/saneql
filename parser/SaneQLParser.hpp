#ifndef H_saneql_SaneQLParser
#define H_saneql_SaneQLParser
//---------------------------------------------------------------------------
#include <string_view>
//---------------------------------------------------------------------------
// SaneQL
// (c) 2023 Thomas Neumann
// SPDX-License-Identifier: BSD-3-Clause
//---------------------------------------------------------------------------
namespace saneql {
//---------------------------------------------------------------------------
class ASTContainer;
//---------------------------------------------------------------------------
namespace ast {
class AST;
}
//---------------------------------------------------------------------------
/// The interface to the generated SaneQL parser
class SaneQLParser {
   public:
   /// Parse the input
   static ast::AST* parse(ASTContainer& container, std::string_view input);
};
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
#endif
