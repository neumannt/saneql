#include "parser/ASTBase.hpp"
#include "parser/SaneQLLexer.hpp"
#include <algorithm>
#include <cstdarg>
#include <vector>
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
struct NodeTemplate : ASTBase {
   ASTBase* entries[];
   NodeTemplate(string_view content, unsigned descriptor) : ASTBase(content, descriptor) {}
};
struct ListTemplate : ASTBase {
   ASTBase *head, *tail, *end;
   ListTemplate(string_view content, unsigned descriptor, ASTBase* head, ASTBase* tail, ASTBase* end) : ASTBase(content, descriptor), head(head), tail(tail), end(end) {}
};
struct Hash {
   static uint64_t hash(const void* data, uint64_t len, uint64_t seed);
};
uint64_t Hash::hash(const void* data, uint64_t len, uint64_t seed) {
   uint64_t result = 0xcbf29ce484222325 + seed;
   for (auto iter = static_cast<const byte*>(data), limit = iter + len; iter != limit; ++iter) {
      result ^= static_cast<unsigned>(*iter);
      result *= 0x100000001b3;
   }
   return result;
}
}
//---------------------------------------------------------------------------
void ASTBase::computeHash()
// Compute the hash value
{
   if (!getRawType()) {
      hash = Hash::hash(content.data(), content.size(), 0);
   } else {
      unsigned count = getRawEntryCount();
      vector<uint64_t> hashes(count + 1);
      hashes[0] = descriptor;
      for (unsigned index = 0; index != count; ++index) {
         auto e = static_cast<NodeTemplate*>(this)->entries[index];
         hashes[index + 1] = e ? e->getHash() : 0;
      }
      hash = Hash::hash(hashes.data(), (count + 1) * sizeof(uint64_t), 0);
   }
}
//---------------------------------------------------------------------------
string ASTBase::getTokenValue(unsigned encoding) const
// Get the value of a token
{
   return SaneQLLexer::TokenInfo{content, static_cast<SaneQLLexer::TokenInfo::Encoding>(encoding)}.asString();
}
//---------------------------------------------------------------------------
bool ASTBase::isEquivalent(const ASTBase* other) const
// Check for equivalence
{
   // Trivial checks first
   if (this == other) return true;
   if ((!other) || (descriptor != other->descriptor) || (hash != other->hash)) return false;

   // A token?
   if (!getRawType())
      return content == other->content;

   // Check the whole tree
   vector<pair<const ASTBase*, const ASTBase*>> todo;
   todo.emplace_back(this, other);
   while (!todo.empty()) {
      auto current = todo.back();
      todo.pop_back();

      // Trivial checks
      if (current.first == current.second) continue;
      if ((!current.first) || (!current.second) || (current.first->descriptor != current.second->descriptor) || (current.first->hash != current.second->hash)) return false;

      // A token?
      if (!current.first->getRawType()) {
         if (current.first->content != current.second->content) return false;
         continue;
      }

      // Recurse
      unsigned entryCount = current.first->descriptor & ((1u << entryCountBits) - 1);
      auto a = static_cast<const NodeTemplate*>(current.first), b = static_cast<const NodeTemplate*>(current.second);
      for (unsigned index = 0; index != entryCount; ++index)
         todo.emplace_back(a->entries[index], b->entries[index]);
   }
   return true;
}
//---------------------------------------------------------------------------
ASTContainer::ASTContainer()
// Constructor
{
}
//---------------------------------------------------------------------------
ASTContainer::~ASTContainer()
// Destructor
{
   while (chunks) {
      auto next = chunks->next;
      delete[] reinterpret_cast<char*>(chunks);
      chunks = next;
   }
}
//---------------------------------------------------------------------------
void ASTContainer::allocateNewChunk(size_t size)
// Allocate a new chunk
{
   unsigned newSize = sizeof(Chunk) + size;
   auto* newChunk = reinterpret_cast<Chunk*>(new char[newSize]);
   lastSize = newSize;
   totalSize += newSize;
   newChunk->next = chunks;
   freeBegin = newChunk->data;
   freeEnd = freeBegin + size;
   chunks = newChunk;
}
//---------------------------------------------------------------------------
void* ASTContainer::allocateRaw(unsigned size)
// Allocate a node
{
   // Do we need more space?
   if ((freeEnd - freeBegin) < size) [[unlikely]] {
      auto targetSize = max<size_t>(size, max(lastSize, totalSize / 4));
      auto lowerBound = max<size_t>(size, 1024);
      auto upperBound = max<size_t>(size, (10u << 20) - sizeof(Chunk));
      allocateNewChunk(clamp(targetSize, lowerBound, upperBound));
   }

   auto result = freeBegin;
   freeBegin += size;
   return result;
}
//---------------------------------------------------------------------------
ASTBase* ASTContainer::allocateAST(string_view view, unsigned descriptor, initializer_list<ASTBase*> entries)
// Allocate an AST node
{
   unsigned size = sizeof(NodeTemplate) + entries.size() * sizeof(ASTBase*);
   auto result = new (allocateRaw(size)) NodeTemplate(view, descriptor);
   unsigned slot = 0;
   for (auto i : entries)
      result->entries[slot++] = i;
   result->computeHash();
   return result;
}
//---------------------------------------------------------------------------
ASTBase* ASTContainer::allocateASTImpl(string_view view, unsigned descriptor, ...)
// Allocate an AST node. C-style interface to reduce compile time
{
   va_list ap;
   va_start(ap, descriptor);
   unsigned count = descriptor & ((1u << ASTBase::entryCountBits) - 1);
   unsigned size = sizeof(NodeTemplate) + count * sizeof(ASTBase*);
   auto result = new (allocateRaw(size)) NodeTemplate(view, descriptor);
   for (unsigned slot = 0; slot != count; ++slot)
      result->entries[slot] = va_arg(ap, ASTBase*);
   va_end(ap);
   result->computeHash();
   return result;
}
//---------------------------------------------------------------------------
ASTBase* ASTContainer::createList(string_view view, unsigned descriptor, ASTBase* head)
// Create a new list
{
   auto res = new (allocateRaw(sizeof(ListTemplate))) ListTemplate(view, descriptor, head, nullptr, nullptr);
   res->computeHash();
   return res;
}
//---------------------------------------------------------------------------
ASTBase* ASTContainer::appendList(string_view view, unsigned descriptor, ASTBase* head, ASTBase* tail)
// Append a list result
{
   // Append to an empty list?
   if (!head) {
      auto res = new (allocateRaw(sizeof(ListTemplate))) ListTemplate(view, descriptor, tail, nullptr, nullptr);
      res->computeHash();
      return res;
   }

   // No, examine the list
   auto list = static_cast<ListTemplate*>(head);
   auto existingTail = list;
   uint64_t hash = list->getHash();
   if (!existingTail->end) {
      while (existingTail->tail) {
         existingTail = static_cast<ListTemplate*>(existingTail->tail);
         uint64_t hashPart = existingTail->head ? existingTail->head->getHash() : 0;
         hash = Hash::hash(&hashPart, sizeof(uint64_t), hash);
      }
   } else {
      existingTail = static_cast<ListTemplate*>(existingTail->end);
   }

   // And append
   auto append = new (allocateRaw(sizeof(ListTemplate))) ListTemplate(view, descriptor, tail, nullptr, nullptr);
   existingTail->tail = append;
   uint64_t hashPart = tail ? tail->getHash() : 0;
   list->hash = Hash::hash(&hashPart, sizeof(uint64_t), hash);
   list->end = append;
   return list;
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
