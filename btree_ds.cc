#include <new>
#include <iostream>
#include <assert.h>
#include <string.h>

#include "btree_ds.h"
#include "buffercache.h"

#include "btree.h"

//an important thing to grok here is that there are two notions of pointers.   
//The pointers within a node on the disk are disk block numbers.   This is not the same thing as an in-memory pointer.
 

using namespace std;


SIZE_T NodeMetadata::GetNumDataBytes() const
{
  SIZE_T n=blocksize-sizeof(*this); ////reveals the number of free bytes minus the node metadata structure
  return n;
}

//size of T for the pointer because its an array of keys
SIZE_T NodeMetadata::GetNumSlotsAsInterior() const
{
  return (GetNumDataBytes()-sizeof(SIZE_T))/(keysize+sizeof(SIZE_T));  // floor intended
}

SIZE_T NodeMetadata::GetNumSlotsAsLeaf() const
{
  return (GetNumDataBytes()-sizeof(SIZE_T))/(keysize+valuesize);  // floor intended
}



ostream & NodeMetadata::Print(ostream &os) const 
{
  os << "NodeMetaData(nodetype="<<(nodetype==BTREE_UNALLOCATED_BLOCK ? "UNALLOCATED_BLOCK" :
				   nodetype==BTREE_SUPERBLOCK ? "SUPERBLOCK" :
				   nodetype==BTREE_ROOT_NODE ? "ROOT_NODE" :
				   nodetype==BTREE_INTERIOR_NODE ? "INTERIOR_NODE" :
				   nodetype==BTREE_LEAF_NODE ? "LEAF_NODE" : "UNKNOWN_TYPE")
     << ", keysize="<<keysize<<", valuesize="<<valuesize<<", blocksize="<<blocksize
     << ", rootnode="<<rootnode<<", freelist="<<freelist<<", numkeys="<<numkeys<<")";
  return os;
}

////////////////////////////////////////////////////////////////////////////////
////BTreeNode
//??????????????????

BTreeNode::BTreeNode() 
{
  info.nodetype=BTREE_UNALLOCATED_BLOCK; //upon declaring this will be an unallocated node
  data=0;
}

BTreeNode::~BTreeNode()
{
  if (data) { 
    delete [] data; //delete any data thats currently in there
  }
  data=0;
  info.nodetype=BTREE_UNALLOCATED_BLOCK;
}


BTreeNode::BTreeNode(int node_type, SIZE_T key_size, SIZE_T value_size, SIZE_T block_size)
{
  info.nodetype=node_type;
  info.keysize=key_size;
  info.valuesize=value_size;
  info.blocksize=block_size;
  info.rootnode=0;
  info.freelist=0;
  info.numkeys=0;				       
  data=0;

  if (info.nodetype!=BTREE_UNALLOCATED_BLOCK && info.nodetype!=BTREE_SUPERBLOCK) {
    data = new char [info.GetNumDataBytes()]; //new char syntax for a character array
    memset(data,0,info.GetNumDataBytes());
	//void * memset ( void * ptr, int value, size_t num) //data is the pointer to area in memory, 0 is the value that will be set
  }
}

//creates a copy of the node that was passed into this member function
BTreeNode::BTreeNode(const BTreeNode &rhs) //parameter is address to actual node calling this function
{
  info.nodetype=rhs.info.nodetype;
  info.keysize=rhs.info.keysize;
  info.valuesize=rhs.info.valuesize;
  info.blocksize=rhs.info.blocksize;
  info.rootnode=rhs.info.rootnode;
  info.freelist=rhs.info.freelist;
  info.numkeys=rhs.info.numkeys;				       
  data=0;
  if (rhs.data) { 
   data=new char [info.GetNumDataBytes()];
    memcpy(data,rhs.data,info.GetNumDataBytes());
  }
}


BTreeNode & BTreeNode::operator=(const BTreeNode &rhs) 
{
  return *(new (this) BTreeNode(rhs));
}

//writes block to memory/buffer
ERROR_T BTreeNode::Serialize(BufferCache *b, const SIZE_T blocknum) const
{
  assert((unsigned)info.blocksize==b->GetBlockSize()); //will terminate the serialize there are different block sizes

  Block block(sizeof(info)+info.GetNumDataBytes()); //creates a new temporary block here

  memcpy(block.data,&info,sizeof(info)); //puts all of the metadate inside of this created block

  if (info.nodetype!=BTREE_UNALLOCATED_BLOCK && info.nodetype!=BTREE_SUPERBLOCK) { //for a normal node
    memcpy(block.data+sizeof(info),data,info.GetNumDataBytes()); //copies this node data into the block, (will never be 0's cause the block cannot be unallocated)
  }

  return b->WriteBlock(blocknum,block); //write this newly created temprorary block into the buffer, specifying the exact block number

}

//reads specific block from memory/buffer TO the block that is calling this member function
ERROR_T  BTreeNode::Unserialize(BufferCache *b, const SIZE_T blocknum)
{
  Block block;

  ERROR_T rc;

  //note that there is a direct memory refernence to input block &
  rc=b->ReadBlock(blocknum,block); //will read block from memory if it is not in the cache already
  //this specific buffer cache 'b' will be used in reading

  if (rc!=ERROR_NOERROR) {
    return rc;
  }

  memcpy(&info,block.data,sizeof(info));
  
  if (data) { 
    delete [] data;
    data=0;
  }

  assert(b->GetBlockSize()==(unsigned)info.blocksize);

  if (info.nodetype!=BTREE_UNALLOCATED_BLOCK && info.nodetype!=BTREE_SUPERBLOCK) {
    data = new char [info.GetNumDataBytes()];
    memcpy(data,block.data+sizeof(info),info.GetNumDataBytes());
  }
  
  return ERROR_NOERROR;
}


//returns memory pointer to the specific offset or key
// Gives a pointer to the ith key  (interior or leaf)

char * BTreeNode::ResolveKey(const SIZE_T offset) const
{
  switch (info.nodetype) { 
  case BTREE_INTERIOR_NODE:
  case BTREE_ROOT_NODE: //apparently root nodes are interior nodes!!!
    assert(offset<info.numkeys); // extra sizeofT because its the key, and there are key value pairs stored side by side in memory
    return data+sizeof(SIZE_T)+offset*(sizeof(SIZE_T)+info.keysize);
    break;
  case BTREE_LEAF_NODE:
    assert(offset<info.numkeys);
    return data+sizeof(SIZE_T)+offset*(info.keysize+info.valuesize);
    break;
  default:
    return 0;
  }
}


// Gives a pointer to the ith pointer (interior)
char * BTreeNode::ResolvePtr(const SIZE_T offset) const
{
  switch (info.nodetype) { 
  case BTREE_INTERIOR_NODE:
  case BTREE_ROOT_NODE:
    assert(offset<=info.numkeys);
    return data+offset*(sizeof(SIZE_T)+info.keysize);
    break;
  case BTREE_LEAF_NODE:
    assert(offset==0);
    return data;
    break;
  default:
    return 0;
  }
}



char * BTreeNode::ResolveVal(const SIZE_T offset) const
{
  switch (info.nodetype) { 
  case BTREE_LEAF_NODE:
    assert(offset<info.numkeys);
    return data+sizeof(SIZE_T)+offset*(info.keysize+info.valuesize)+info.keysize;
    break;
  default:
    return 0;
  }
}


char * BTreeNode::ResolveKeyVal(const SIZE_T offset) const
{
  return ResolveKey(offset);
}





//note a key, same iwth a vlaue and apinter is a block
ERROR_T BTreeNode::GetKey(const SIZE_T offset, KEY_T &k) const
{
  char *p=ResolveKey(offset); //returns a pointer to this offset within the current TreeNode

  if (p==0) { 
    return ERROR_NOMEM;
  }
  
  k.Resize(info.keysize,false); //must resize Key block to be standard
  memcpy(k.data,p,info.keysize); //Copies the characters from the btree node into the Key's block
  return ERROR_NOERROR;
}

ERROR_T BTreeNode::GetPtr(const SIZE_T offset, SIZE_T &ptr) const
{
  char *p=ResolvePtr(offset);

  if (p==0) { 
    return ERROR_NOMEM;
  }
  
  memcpy(&ptr,p,sizeof(SIZE_T));
  return ERROR_NOERROR;
}

ERROR_T BTreeNode::GetVal(const SIZE_T offset, VALUE_T &v) const
{
  char *p=ResolveVal(offset);

  if (p==0) { 
    return ERROR_NOMEM;
  }
  
  v.Resize(info.valuesize,false);
  memcpy(v.data,p,info.valuesize);
  return ERROR_NOERROR;
}


ERROR_T BTreeNode::GetKeyVal(const SIZE_T offset, KeyValuePair &p) const
{
  ERROR_T rc= GetKey(offset,p.key);

  if (rc!=ERROR_NOERROR) { 
    return rc; 
  } else {
    return GetVal(offset,p.value);
  }
}


ERROR_T BTreeNode::SetKey(const SIZE_T offset, const KEY_T &k)
{
  char *p=ResolveKey(offset);

  if (p==0) { 
    return ERROR_NOMEM;
  }

  memcpy(p,k.data,info.keysize);

  return ERROR_NOERROR;
}


ERROR_T BTreeNode::SetPtr(const SIZE_T offset, const SIZE_T &ptr)
{
  char *p=ResolvePtr(offset);

  if (p==0) { 
    return ERROR_NOMEM;
  }

  memcpy(p,&ptr,sizeof(SIZE_T));

  return ERROR_NOERROR;
}



ERROR_T BTreeNode::SetVal(const SIZE_T offset, const VALUE_T &v)
{
  char *p=ResolveVal(offset);
  
  if (p==0) { 
    return ERROR_NOMEM;
  }
  
  memcpy(p,v.data,info.valuesize);
  
  return ERROR_NOERROR;
}


ERROR_T BTreeNode::SetKeyVal(const SIZE_T offset, const KeyValuePair &p)
{
  ERROR_T rc=SetKey(offset,p.key);

  if (rc!=ERROR_NOERROR) { 
    return rc;
  } else {
    return SetVal(offset,p.value);
  }
}



//prints the values of the tree out
ostream & BTreeNode::Print(ostream &os) const 
{
  os << "BTreeNode(info="<<info;
  if (info.nodetype!=BTREE_UNALLOCATED_BLOCK && info.nodetype!=BTREE_SUPERBLOCK) { 
    os <<", ";
    if (info.nodetype==BTREE_INTERIOR_NODE || info.nodetype==BTREE_ROOT_NODE) {
      SIZE_T ptr;
      KEY_T key;
      os << "pointers_and_values=(";
      if (info.numkeys>0) { // ==0 implies an empty root node
	for (SIZE_T i=0;i<info.numkeys;i++) {
	  GetPtr(i,ptr);
	  os<<ptr<<", ";
	  GetKey(i,key);
	  os<<key<<", ";
	}
	GetPtr(info.numkeys,ptr);
	os <<ptr;
      } 
      os << ")";
	
    }
    if (info.nodetype==BTREE_LEAF_NODE) { 
      KEY_T key;
      VALUE_T val;
      os << "keys_and_values=(";
      for (SIZE_T i=0;i<info.numkeys;i++) {
	if (i>0) { 
	  os<<", ";
	}
	GetKey(i,key);
	os<<key<<", ";
	GetVal(i,val);
	os<<val;
      }
      os <<")";
    }
  }
  os <<")";
  return os;
}
