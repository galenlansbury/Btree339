#ifndef _btree_ds
#define _btree_ds

#include <iostream>
#include "global.h"
#include "block.h"

using namespace std;

// Types of nodes
#define BTREE_UNALLOCATED_BLOCK 0
#define BTREE_SUPERBLOCK 1
#define BTREE_ROOT_NODE 2
#define BTREE_INTERIOR_NODE 3
#define BTREE_LEAF_NODE 4


typedef Block Buffer; //block = buffer = KeyOrValue
typedef Buffer KeyOrValue;

typedef KeyOrValue KEY_T;
typedef KeyOrValue VALUE_T;


class BufferCache; //already defined in buffercache.h with the following members
// private:
//DiskSystem *disk;
//SIZE_T cachesize;
//map<SIZE_T, Block, cache_compare_lessthan> blockmap;
//double curtime;
//SIZE_T allocs, deallocs, reads, writes, diskreads, diskwrites;

struct KeyValuePair;

struct NodeMetadata {
  int nodetype;
  SIZE_T keysize; 
  SIZE_T valuesize; //should remain constant but can be changed for extra credit\

  SIZE_T blocksize; //note this is to be declared in the construction of BTreeNode
  SIZE_T rootnode; //meaningful only for superblock
  SIZE_T freelist; //meaningful only for superblock or a free block
  SIZE_T numkeys;

  SIZE_T GetNumDataBytes() const;
  SIZE_T GetNumSlotsAsInterior() const; //returns number of available slots for keyPTR pairs within a specific node
  SIZE_T GetNumSlotsAsLeaf() const;

  ostream &Print(ostream &rhs) const;
			  
};

inline ostream & operator<< (ostream &os, const NodeMetadata &node) { return node.Print(os); }



//
// Interior node:
//
// PTR KEY PTR KEY PTR KEY PTR
//
// Leaf:
//
// PTR* KEY VALUE KEY VALUE KEY VALUE
//
// *Here this pointer is not used

//each node with have a certain amout of key values (blocks) mapped into its memory or *data in this case)
struct BTreeNode {
  NodeMetadata  info; //each tree node has this information appended to it
  char         *data; //A pointer to the actual bytes associated with it
  //
  // unallocated or superblock => blank
  // interior => array of keys
  // leaf => array of key/value pairs

 
  BTreeNode();//constructor called deuring BTreeNode declaration phase
  
  //
  // Note: This destructor is INTENTIONALLY left non-virtual
  //       This class must NOT have a vtable pointer
  //         because we will serialize it directly to disk
  //
  ~BTreeNode(); //is executed whenever an object of it's class goes out of scope (program closes)
				//or whenever the delete expression is applied to a pointer to the object of that class

  BTreeNode(int node_type, SIZE_T key_size, SIZE_T value_size, SIZE_T block_size);
  BTreeNode(const BTreeNode &rhs);
  BTreeNode & operator=(const BTreeNode &rhs);
  
  ERROR_T Serialize(BufferCache *b, const SIZE_T block) const;
  ERROR_T Unserialize(BufferCache *b, const SIZE_T block);

  // NOTE To simplify our lives, we will just treat a Key or Value as being the same as a block
  //these function will be called from a target block
  char *ResolveKey(const SIZE_T offset) const; // Gives a pointer to the ith key  (interior or leaf)
  char *ResolvePtr(const SIZE_T offset) const; // Gives a pointer to the ith pointer (interior)
  char *ResolveVal(const SIZE_T offset) const; // Gives a pointer to the ith value (leaf)
  char *ResolveKeyVal(const SIZE_T offset) const ; // Gives a pointer to the ith keyvalue pair (leaf)



  //Copies the characters from the btree node into the input Key's block
  ERROR_T GetKey(const SIZE_T offset, KEY_T &k) const ; // Gives the ith key  (interior or leaf)
  ERROR_T GetPtr(const SIZE_T offset, SIZE_T &p) const ;   // Gives the ith pointer (interior)
  ERROR_T GetVal(const SIZE_T offset, VALUE_T &v) const ; // Gives  the ith value (leaf)
  ERROR_T GetKeyVal(const SIZE_T offset, KeyValuePair &p) const; // Gives  the ith key value pair (leaf)

  //copies characters from input key into Treenode offset
  ERROR_T SetKey(const SIZE_T offset, const KEY_T &k); // Writesthe ith key  (interior or leaf)
  ERROR_T SetPtr(const SIZE_T offset, const SIZE_T &p);   // Writes the ith pointer (interior)
  ERROR_T SetVal(const SIZE_T offset, const VALUE_T &v); // Writes the ith value (leaf)
  ERROR_T SetKeyVal(const SIZE_T offset, const KeyValuePair &p); // Writes the ith key value pair (leaf)

  ostream &Print(ostream &rhs) const;
};


inline ostream & operator<<(ostream &os, const BTreeNode &node) { return node.Print(os); }






#endif