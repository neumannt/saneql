#ifndef H_saneql_ASTBase
#define H_saneql_ASTBase
//---------------------------------------------------------------------------
#include <cassert>
#include <cstdint>
#include <string>
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
/// Base class for all ast nodes
class ASTBase {
   private:
   /// The number of bits to use to represent the entry count
   static constexpr unsigned entryCountBits = 10;
   /// The number of bits to use for the sub type
   static constexpr unsigned subTypeBits = 10;

   /// The content
   std::string_view content;
   /// The descriptor
   uintptr_t descriptor;
   /// The hash value
   uint64_t hash;

   protected:
   /// Get the value of a token
   std::string getTokenValue(unsigned encoding) const;

   friend class ASTContainer;

   public:
   /// Constructor
   ASTBase(std::string_view content, uintptr_t descriptor) : content(content), descriptor(descriptor), hash(0) {}

   /// Build an descriptor
   template <class A, class B>
   static constexpr uintptr_t buildDescriptor(A type, B subType, uintptr_t entryCount) {
      return entryCount | (static_cast<uintptr_t>(subType) << entryCountBits) | (static_cast<uintptr_t>(type) << (entryCountBits + subTypeBits));
   }

   /// Compute the hash value. Is usually called automatically in the derived constructors, but is needed for manual node constructions
   void computeHash();

   /// Get the raw type
   unsigned getRawType() const { return descriptor >> (entryCountBits + subTypeBits); }
   /// Get the raw subtype
   unsigned getRawSubType() const { return (descriptor >> entryCountBits) & ((1u << subTypeBits) - 1); }
   /// Get the raw entry count
   unsigned getRawEntryCount() const { return descriptor & ((1u << entryCountBits) - 1); }
   /// Get the hash value
   uint64_t getHash() const { return hash; }

   /// Get the content
   std::string_view getContent() const { return content; }
   /// Get the content
   const char* begin() const { return content.begin(); }
   /// Get the content
   const char* end() const { return content.end(); }

   /// Check for equivalence
   bool isEquivalent(const ASTBase* other) const;
};
//---------------------------------------------------------------------------
/// A container for AST nodes
class ASTContainer {
   private:
   /// A memory chunk
   struct Chunk {
      /// The next chunk
      Chunk* next;
      /// The data
      char data[];
   };
   /// The chunks
   Chunk* chunks = nullptr;
   /// The free memory
   char *freeBegin = nullptr, *freeEnd = nullptr;
   /// The result
   ASTBase* result = nullptr;
   /// The last allocated size
   size_t lastSize = 0;
   /// The total allocated size
   size_t totalSize = 0;

   ASTContainer(const ASTContainer&) = delete;
   void operator=(const ASTContainer&) = delete;

   /// Allocate a new chunk
   void allocateNewChunk(size_t size);

   public:
   /// Constructor
   ASTContainer();
   /// Destructor
   ~ASTContainer();

   /// Set the result
   void setResult(ASTBase* ast) { result = ast; }
   /// Get the result
   template <class T>
   T* getResult() const { return static_cast<T*>(result); }

   /// Allocate memory
   void* allocateRaw(unsigned size);
   /// Allocate an AST node
   ASTBase* allocateAST(std::string_view view, unsigned descriptor, std::initializer_list<ASTBase*> entries);
   /// Allocate an AST node. C-style variadic function to reduce compile time
   ASTBase* allocateASTImpl(std::string_view view, unsigned descriptor, ...);
   /// Createa new list
   ASTBase* createList(std::string_view view, unsigned descriptor, ASTBase* head);
   /// Append a list result
   ASTBase* appendList(std::string_view view, unsigned descriptor, ASTBase* head, ASTBase* tail);
};
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
#endif
