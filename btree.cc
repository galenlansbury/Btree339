#include <assert.h>
#include "btree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//  To start, walk through how a BtreeIndex::Lookup() works and strive to understand it

KeyValuePair::KeyValuePair()
{}


KeyValuePair::KeyValuePair(const KEY_T &k, const VALUE_T &v) :
key(k), value(v)
{}


KeyValuePair::KeyValuePair(const KeyValuePair &rhs) :
key(rhs.key), value(rhs.value)
{}


KeyValuePair::~KeyValuePair()
{}


KeyValuePair & KeyValuePair::operator=(const KeyValuePair &rhs)
{
	return *(new (this) KeyValuePair(rhs));
}

BTreeIndex::BTreeIndex(SIZE_T keysize,
	SIZE_T valuesize,
	BufferCache *cache,
	bool unique)
{
	superblock.info.keysize = keysize;
	superblock.info.valuesize = valuesize;
	buffercache = cache; //
	// note: ignoring unique now
}

BTreeIndex::BTreeIndex()
{
	// shouldn't have to do anything
}


//
// Note, will not attach!
//
BTreeIndex::BTreeIndex(const BTreeIndex &rhs)
{
	buffercache = rhs.buffercache;
	superblock_index = rhs.superblock_index;
	superblock = rhs.superblock;
}

BTreeIndex::~BTreeIndex()
{
	// shouldn't have to do anything
	//everything stays on the temporary file we created using makedisk once we go out of scope or end the program
}


BTreeIndex & BTreeIndex::operator=(const BTreeIndex &rhs)
{
	return *(new(this)BTreeIndex(rhs));
}


//NOTE: the actual value of N gets changed to the number of the free block
ERROR_T BTreeIndex::AllocateNode(SIZE_T &n)
{
	n = superblock.info.freelist; //finds a free block for us to unserialize

	if (n == 0) {
		return ERROR_NOSPACE;
	}

	BTreeNode node;
	node.Unserialize(buffercache, n); //read this particular block numberfrom the buffer cahce, (and memory if its not there)
	//reads the block number to node

	assert(node.info.nodetype == BTREE_UNALLOCATED_BLOCK); //will break the operation if this is already an unallocated block

	superblock.info.freelist = node.info.freelist; //As this block becomes ready to allocate, the next free block gets put into superblock

	superblock.Serialize(buffercache, superblock_index); //now we write it back to memory

	buffercache->NotifyAllocateBlock(n); //allocates block n on the buffer

	return ERROR_NOERROR;
}


ERROR_T BTreeIndex::DeallocateNode(const SIZE_T &n)
{
	BTreeNode node;

	node.Unserialize(buffercache, n);

	assert(node.info.nodetype != BTREE_UNALLOCATED_BLOCK);

	node.info.nodetype = BTREE_UNALLOCATED_BLOCK;

	node.info.freelist = superblock.info.freelist; //this points to the next free block now

	node.Serialize(buffercache, n);

	superblock.info.freelist = n; //super node points to this specific node

	superblock.Serialize(buffercache, superblock_index);

	buffercache->NotifyDeallocateBlock(n);

	return ERROR_NOERROR;

}

ERROR_T BTreeIndex::Attach(const SIZE_T initblock, const bool create)
{
	ERROR_T rc;

	superblock_index = initblock;
	assert(superblock_index == 0);

	if (create) {
		// build a super block, root node, and a free space list
		//
		// Superblock at superblock_index
		// root node at superblock_index+1
		// free space list for rest
		BTreeNode newsuperblock(BTREE_SUPERBLOCK,
			superblock.info.keysize,
			superblock.info.valuesize,
			buffercache->GetBlockSize());
		newsuperblock.info.rootnode = superblock_index + 1;
		newsuperblock.info.freelist = superblock_index + 2;
		newsuperblock.info.numkeys = 0;

		buffercache->NotifyAllocateBlock(superblock_index);

		rc = newsuperblock.Serialize(buffercache, superblock_index);

		if (rc) {
			return rc;
		}

		BTreeNode newrootnode(BTREE_ROOT_NODE,
			superblock.info.keysize,
			superblock.info.valuesize,
			buffercache->GetBlockSize());
		newrootnode.info.rootnode = superblock_index + 1;
		newrootnode.info.freelist = superblock_index + 2;
		newrootnode.info.numkeys = 0;

		buffercache->NotifyAllocateBlock(superblock_index + 1);

		rc = newrootnode.Serialize(buffercache, superblock_index + 1);

		if (rc) {
			return rc;
		}

		for (SIZE_T i = superblock_index + 2; i < buffercache->GetNumBlocks(); i++) {
			BTreeNode newfreenode(BTREE_UNALLOCATED_BLOCK,
				superblock.info.keysize,
				superblock.info.valuesize,
				buffercache->GetBlockSize());
			newfreenode.info.rootnode = superblock_index + 1;
			newfreenode.info.freelist = ((i + 1) == buffercache->GetNumBlocks()) ? 0 : i + 1;

			rc = newfreenode.Serialize(buffercache, i);

			if (rc) {
				return rc;
			}

		}
	}

	// OK, now, mounting the btree is simply a matter of reading the superblock 

	return superblock.Unserialize(buffercache, initblock);
}


ERROR_T BTreeIndex::Detach(SIZE_T &initblock)
{
	return superblock.Serialize(buffercache, superblock_index);
}


// Interior node:
//
// PTR KEY PTR KEY PTR KEY PTR
//
// Leaf:
//
// PTR* KEY VALUE KEY VALUE KEY VALUE
//


ERROR_T BTreeIndex::LookupOrUpdateInternal(const SIZE_T &node,
	const BTreeOp op,
	const KEY_T &key,
	VALUE_T &value)
{
	//&node is the root btreenode index for first recursion, so probably 1
	BTreeNode b;
	ERROR_T rc;
	SIZE_T offset;
	KEY_T testkey;
	SIZE_T ptr;

	rc = b.Unserialize(buffercache, node); //read the root node in the case of the first function call to the temporary node

	if (rc != ERROR_NOERROR) {
		return rc;
	}

	switch (b.info.nodetype) {
	case BTREE_ROOT_NODE:
	case BTREE_INTERIOR_NODE:
		// Scan through key/ptr pairs
		//and recurse if possible
		for (offset = 0; offset < b.info.numkeys; offset++) {
			rc = b.GetKey(offset, testkey); //current offset asigns the key to the temporary key
			if (rc) { return rc; } //this means that there was no key to find

			if (key < testkey) {
				// OK, so we now have the first key that's larger
				// so we ned to recurse on the ptr value associated with this key 
				// this one, if it exists
				rc = b.GetPtr(offset, ptr); //copies the pointer at said index into the integer pointer
				if (rc) { return rc; }
				return LookupOrUpdateInternal(ptr, op, key, value); //ptr is the number of the next btree node containing our value
			}
		}
		// if we got here, we need to go to the pointer (which is a node number) associated with the largest value, if it exists
		if (b.info.numkeys > 0) {
			rc = b.GetPtr(b.info.numkeys, ptr);
			if (rc) { return rc; }
			return LookupOrUpdateInternal(ptr, op, key, value);
		}
		else {
			// There are no keys at all on this node, so nowhere to go
			return ERROR_NONEXISTENT;
		}
		break;
	case BTREE_LEAF_NODE:
		// Scan through keys looking for matching value
		for (offset = 0; offset<b.info.numkeys; offset++) {
			rc = b.GetKey(offset, testkey);
			if (rc) { return rc; }
			if (testkey == key) {
				if (op == BTREE_OP_LOOKUP) {
					return b.GetVal(offset, value);
				}
				else {
					rc = b.SetVal(offset, value);
					if (rc) { return rc; }
					return b.Serialize(buffercache, node);
				}
			}
		}
		return ERROR_NONEXISTENT;
		break;
	default:
		// We can't be looking at anything other than a root, internal, or leaf
		return ERROR_INSANE;
		break;
	}

	return ERROR_INSANE;
}


static ERROR_T PrintNode(ostream &os, SIZE_T nodenum, BTreeNode &b, BTreeDisplayType dt)
{
	KEY_T key;
	VALUE_T value;
	SIZE_T ptr;
	SIZE_T offset;
	ERROR_T rc;
	unsigned i;

	if (dt == BTREE_DEPTH_DOT) {
		os << nodenum << " [ label=\"" << nodenum << ": ";
	}
	else if (dt == BTREE_DEPTH) {
		os << nodenum << ": ";
	}
	else {
	}

	switch (b.info.nodetype) {
	case BTREE_ROOT_NODE:
	case BTREE_INTERIOR_NODE:
		if (dt == BTREE_SORTED_KEYVAL) {
		}
		else {
			if (dt == BTREE_DEPTH_DOT) {
			}
			else {
				os << "Interior: ";
			}
			for (offset = 0; offset <= b.info.numkeys; offset++) {
				rc = b.GetPtr(offset, ptr);
				if (rc) { return rc; }
				os << "*" << ptr << " ";
				// Last pointer
				if (offset == b.info.numkeys) break;
				rc = b.GetKey(offset, key);
				if (rc) { return rc; }
				for (i = 0; i<b.info.keysize; i++) {
					os << key.data[i];
				}
				os << " ";
			}
		}
		break;
	case BTREE_LEAF_NODE:
		if (dt == BTREE_DEPTH_DOT || dt == BTREE_SORTED_KEYVAL) {
		}
		else {
			os << "Leaf: ";
		}
		for (offset = 0; offset<b.info.numkeys; offset++) {
			if (offset == 0) {
				// special case for first pointer
				rc = b.GetPtr(offset, ptr);
				if (rc) { return rc; }
				if (dt != BTREE_SORTED_KEYVAL) {
					os << "*" << ptr << " ";
				}
			}
			if (dt == BTREE_SORTED_KEYVAL) {
				os << "(";
			}
			rc = b.GetKey(offset, key);
			if (rc) { return rc; }
			for (i = 0; i<b.info.keysize; i++) {
				os << key.data[i];
			}
			if (dt == BTREE_SORTED_KEYVAL) {
				os << ",";
			}
			else {
				os << " ";
			}
			rc = b.GetVal(offset, value);
			if (rc) { return rc; }
			for (i = 0; i<b.info.valuesize; i++) {
				os << value.data[i];
			}
			if (dt == BTREE_SORTED_KEYVAL) {
				os << ")\n";
			}
			else {
				os << " ";
			}
		}
		break;
	default:
		if (dt == BTREE_DEPTH_DOT) {
			os << "Unknown(" << b.info.nodetype << ")";
		}
		else {
			os << "PrintNode: Unsupported Node Type " << b.info.nodetype;
		}
	}
	if (dt == BTREE_DEPTH_DOT) {
		os << "\" ]";
	}
	return ERROR_NOERROR;
}


ERROR_T BTreeIndex::Lookup(const KEY_T &key, VALUE_T &value)
{
	return LookupOrUpdateInternal(superblock.info.rootnode, BTREE_OP_LOOKUP, key, value);
}

////////////////////////////////////////////////insert functions

//this is called recursively in the case of an interior node split. NOTE: this is handled in the leaf node insert member function
ERROR_T BTreeIndex::InteriorNodeCase(list<SIZE_T> Hvector, const KEY_T &key, const SIZE_T &ptr)
{
	//basically the same as iterate and leaf
	BTreeNode b;
	ERROR_T rc;
	SIZE_T offset;
	KEY_T testkey;

	//moddeled off of leaf insert
	SIZE_T x1; //used to keep track of current iteration
	KEY_T temp_key;
	KEY_T& temp_key_ref = temp_key;
	SIZE_T temp_ptr;
	SIZE_T& temp_ptr_ref = temp_ptr;


	if (Hvector.empty()) { return ERROR_INSANE; }


	const SIZE_T& node = Hvector.front();

	rc = b.Unserialize(buffercache, node);
	if (rc) { return rc; }

	if (b.info.nodetype != BTREE_INTERIOR_NODE && b.info.nodetype != BTREE_ROOT_NODE) 
	{
		return ERROR_BADNODETYPE;
	}

	if (b.info.numkeys == 0) {
		return ERROR_INSANE;
	}

	for (offset = 0; offset<b.info.numkeys; offset++) {
		// basically the same old thing as with before
		rc = b.GetKey(offset, testkey);
		if (rc) { return rc; }
		if (key == testkey) { return ERROR_CONFLICT; }
		if (key<testkey) { break; }
	}

	b.info.numkeys++;

	for (x1 = b.info.numkeys - 2; x1 >= offset; x1--) {


		rc = b.GetKey(x1, temp_key_ref);
		if (rc) { return rc; }
		rc = b.SetKey(x1 + 1, temp_key_ref);
		if (rc) { return rc; }


		rc = b.GetPtr(x1 + 1, temp_ptr_ref);
		if (rc) { return rc; }
		rc = b.SetPtr(x1 + 2, temp_ptr_ref);
		if (rc) { return rc; }

		if (x1 == offset) { break; }
	}


	rc = b.SetKey(offset, key);
	if (rc) { return rc; }

	
	rc = b.SetPtr(offset + 1, ptr);
	if (rc) { return rc; }

	
	rc = b.Serialize(buffercache, node);
	if (rc) { return rc; }

	if (b.info.numkeys >= b.info.GetNumSlotsAsInterior()) {
		rc = Split(Hvector);
		if (rc) { return rc; }
	}

	return ERROR_NOERROR;
}

//if the leaf node is overflowing then we are going to split
ERROR_T BTreeIndex::Split(list<SIZE_T> Hvector){

	SIZE_T OGblock_loc; //must keep track of the origian location of the first block
	ERROR_T rc;

	// because we have a list, we simply pop the first node on the path to leaf node.
	if (Hvector.empty()) { return ERROR_INSANE; }
	OGblock_loc = Hvector.front();
	SIZE_T& OGblock_ref = OGblock_loc;
	Hvector.pop_front();

	BTreeNode orig_node;
	rc = orig_node.Unserialize(buffercache, OGblock_ref);
	if (rc) { return rc; }


	SIZE_T blk2; //this is the second block
	SIZE_T blk1;

	// Pointer to new block location and reference to it and new node
	SIZE_T new_block_loc;
	SIZE_T& new_block_ref = new_block_loc;
	BTreeNode new_node;

	//modelled off of leaf insert
	SIZE_T x1; //used to keep track of current iteration
	KEY_T temp_key;
	KEY_T& temp_key_ref = temp_key;
	SIZE_T temp_ptr;
	SIZE_T& temp_ptr_ref = temp_ptr;
	VALUE_T temp_val;
	VALUE_T& temp_val_ref = temp_val;

	unsigned int i;
	string null_key_str;
	for (i = 0; i < superblock.info.keysize; i++) {
		null_key_str = "0" + null_key_str;
	}

	string null_val_str;

	SIZE_T null_ptr = 0;
	SIZE_T& null_ptr_ref = null_ptr;


	switch (orig_node.info.nodetype) {
	case BTREE_ROOT_NODE:
	case BTREE_INTERIOR_NODE:

		if (orig_node.info.numkeys < orig_node.info.GetNumSlotsAsInterior()) { return ERROR_INSANE; } //to prevent ugainst unessesary errors

		// must keep track of number of keys we are going to get from split
		blk1 = orig_node.info.numkeys / 2;
		blk2 = orig_node.info.numkeys - blk1 - 1;


		rc = AllocateNode(new_block_ref);
		if (rc) { cout << rc << endl; return rc; }
		rc = new_node.Unserialize(buffercache, new_block_ref);
		if (rc) { return rc; }
		new_node.info.nodetype = BTREE_INTERIOR_NODE;
		new_node.info.numkeys = blk2;
		new_node.data = new char[new_node.info.GetNumDataBytes()];

		memset(new_node.data, 0, new_node.info.GetNumDataBytes());
		
		//going through to find the right location
		for (x1 = blk1 + 1; x1<orig_node.info.numkeys; x1++) {
			rc = orig_node.GetKey(x1, temp_key_ref);
			if (rc) { return rc; }
			rc = new_node.SetKey(x1 - (blk1 + 1), temp_key_ref);
			if (rc) { return rc; }
			rc = orig_node.SetKey(x1, KEY_T(null_key_str.c_str()));
			if (rc) { return rc; }
			rc = orig_node.GetPtr(x1, temp_ptr_ref);
			if (rc) { return rc; }
			rc = new_node.SetPtr(x1 - (blk1 + 1), temp_ptr_ref);
			if (rc) { return rc; }
			rc = orig_node.SetPtr(x1, SIZE_T(null_ptr_ref));
			if (rc) { return rc; }
		}

		rc = orig_node.SetKey(blk1, KEY_T(null_key_str.c_str()));
		if (rc) { return rc; }

		rc = orig_node.GetPtr(orig_node.info.numkeys, temp_ptr_ref);
		if(rc) { return rc; }
		rc = new_node.SetPtr(blk1, temp_ptr_ref);
		rc = orig_node.SetPtr(orig_node.info.numkeys, null_ptr_ref);
		if (rc) { return rc; }

		orig_node.info.numkeys = blk1; //now we need to reset original amount nof keys

		//note that we will have a different ending depending on whether or not we are dealing with a root node
		if (orig_node.info.nodetype == BTREE_INTERIOR_NODE) {
			//must write back to memory the changes
			rc = orig_node.Serialize(buffercache, OGblock_ref);
			if (rc) { return rc; }
			rc = new_node.Serialize(buffercache, new_block_ref);
			if (rc) { return rc; }
			rc = new_node.GetKey(0, temp_key_ref);
			if (rc) { return rc; }
			rc = InteriorNodeCase(Hvector, temp_key, new_block_ref);
			if (rc) { return rc; }

			return ERROR_NOERROR;
		}
		else {
			orig_node.info.nodetype = BTREE_INTERIOR_NODE;
			SIZE_T TempRoot_loc;
			SIZE_T& TempRoot_ref = TempRoot_loc;
			BTreeNode TempRoot;
			rc = AllocateNode(TempRoot_ref);
			if (rc) { cout << rc << endl; return rc; }
			rc = TempRoot.Unserialize(buffercache, TempRoot_ref);
			if (rc) { return rc; }

			// in case of a root split everything is manual
			TempRoot.info.nodetype = BTREE_ROOT_NODE;
			TempRoot.info.numkeys = 1;
			TempRoot.data = new char[TempRoot.info.GetNumDataBytes()];
			memset(TempRoot.data, 0, TempRoot.info.GetNumDataBytes());

			// Set superblock to point to TempRoot
			superblock.info.rootnode = TempRoot_loc;

			// Serialize the new nodes back into memoruy
			rc = orig_node.Serialize(buffercache, OGblock_ref);
			if (rc) { return rc; }
			rc = new_node.Serialize(buffercache, new_block_ref);
			if (rc) { return rc; }

			// got to inser the temporary root incase of a root split
			rc = new_node.GetKey(0, temp_key_ref);
			if (rc) { return rc; }
			rc = TempRoot.SetKey(0, temp_key_ref);
			if (rc) { return rc; }
			rc = TempRoot.SetPtr(0, OGblock_ref);
			if (rc) { return rc; }
			rc = TempRoot.SetPtr(1, new_block_ref);
			if (rc) { return rc; }
			rc = TempRoot.Serialize(buffercache, TempRoot_ref);
			if (rc) { return rc; }

			return ERROR_NOERROR;
		}
		return ERROR_INSANE;
		break;

	case BTREE_LEAF_NODE:
		for (i = 0; i < superblock.info.valuesize; i++) {
			null_val_str = "0" + null_val_str;
		}

		if (orig_node.info.numkeys < orig_node.info.GetNumSlotsAsLeaf()) { return ERROR_INSANE; }

		blk2 = orig_node.info.numkeys / 2;
		blk1 = orig_node.info.numkeys - blk2;

		rc = AllocateNode(new_block_ref);
		if (rc) { cout << rc << endl; return rc; }
		rc = new_node.Unserialize(buffercache, new_block_ref);
		if (rc) { return rc; }

		
		new_node.info.nodetype = BTREE_LEAF_NODE;
		new_node.info.numkeys = blk2;
	
		new_node.data = new char[new_node.info.GetNumDataBytes()];
		memset(new_node.data, 0, new_node.info.GetNumDataBytes());

		for (x1 = blk1; x1<orig_node.info.numkeys; x1++) {

			
			rc = orig_node.GetKey(x1, temp_key_ref);
			if (rc) { return rc; }
			rc = new_node.SetKey(x1 - blk1, temp_key_ref);
			if (rc) { return rc; }
			
			rc = orig_node.SetKey(x1, KEY_T(null_key_str.c_str()));
			if (rc) { return rc; }

			
			rc = orig_node.GetVal(x1, temp_val_ref);
			if (rc) { return rc; }
			rc = new_node.SetVal(x1 - blk1, temp_val_ref);
			if (rc) { return rc; }
		
			rc = orig_node.SetVal(x1, VALUE_T(null_val_str.c_str()));
			if (rc) { return rc; }
		}

	
		orig_node.info.numkeys = blk1;


		rc = orig_node.Serialize(buffercache, OGblock_ref);
		if (rc) { return rc; }
		rc = new_node.Serialize(buffercache, new_block_ref);
		if (rc) { return rc; }

	
		rc = new_node.GetKey(0, temp_key_ref);
		if (rc) { return rc; }

		rc = InteriorNodeCase(Hvector, temp_key, new_block_ref);
		if (rc) { return rc; }

		return ERROR_NOERROR;
		break;


	default:
		return ERROR_INSANE;
		break;
	}
	return ERROR_INSANE;
}

//this is if we need to insert a key value into leaf node
ERROR_T BTreeIndex::LeafNodeInsert(list<SIZE_T> Hvector, const SIZE_T &node, BTreeNode &b, const KEY_T &key, const VALUE_T &value)
{
	

	ERROR_T rc;
	SIZE_T offset;
	KEY_T testkey;

	SIZE_T x1; //used to keep track of current iteration
	KEY_T temp_key;
	KEY_T& temp_key_ref = temp_key;
	VALUE_T temp_val;
	VALUE_T& temp_val_ref = temp_val;

	if (b.info.nodetype != BTREE_LEAF_NODE) {
		return ERROR_BADNODETYPE;
	}

	//if the leaf node is empty we just add the key and value and exit
	if (b.info.numkeys == 0) {
		b.info.numkeys++;
		rc = b.SetKey(0, key);
		if (rc) { return rc; }
		rc = b.SetVal(0, value); //because of the infunction addition within setval, we need not worry about the leaf structure 
		//PTR* KEY VALUE KEY VALUE KEY VALUE
		if (rc) { return rc; }

		rc = b.Serialize(buffercache, node);
		if (rc) { return rc; }

		return ERROR_NOERROR;
	}


	for (offset = 0; offset<b.info.numkeys; offset++) {
		rc = b.GetKey(offset, testkey);
		if (rc) { return rc; }
		if (key == testkey) { return ERROR_CONFLICT; } // can't insert a value if its already there, maybe doe this for eC
		// Otherwise, break loop and continue
		if (key<testkey) { break; } //moment we find a larger key, we break out of loop
	}



	b.info.numkeys++;
	for (x1 = (b.info.numkeys - 2); x1 >= offset; x1--) {

		rc = b.GetKey(x1, temp_key_ref);
		if (rc) { return rc; }
		rc = b.SetKey(x1 + 1, temp_key_ref);
		if (rc) { return rc; }
		rc = b.GetVal(x1, temp_val_ref);
		if (rc) { return rc; }
		rc = b.SetVal(x1 + 1, temp_val_ref);
		if (rc) { return rc; }
		if (x1 == offset) { break; }
	}

	//once we find the right offset, we insert key into its proper place
	rc = b.SetKey(offset, key);
	if (rc) { return rc; }
	rc = b.SetVal(offset, value);
	if (rc) { return rc; }

	// then we serialize it back into memroy
	rc = b.Serialize(buffercache, node);
	if (rc) { return rc; }

	//should never be greater than, but if its equal to slot limit we need to split. we are lazy and do not split frequently
	//only time we split is when the leaf node is full
	if (b.info.numkeys >= b.info.GetNumSlotsAsLeaf()) {
		rc = Split(Hvector);
		if (rc) { return rc; }
	}

	return ERROR_NOERROR;
}


//basically lookup or instert except with added parameter for when we need to recurse and add another node to btree
//NOTE: only time we are splitting is when the leaf node is full
ERROR_T BTreeIndex::Inserter(list<SIZE_T> Hvector, const SIZE_T &node, const KEY_T &key, const VALUE_T &value)
{
	BTreeNode b;
	BTreeNode& b_ref = b;
	ERROR_T rc;
	SIZE_T offset;
	KEY_T testkey;
	SIZE_T ptr;

	Hvector.push_front(node); //must use a vector to keep track of nodes we are recuring through with insert in case we need to make a new father node

	rc = b.Unserialize(buffercache, node);
	if (rc) { return rc; }
	switch (b.info.nodetype) {

	case BTREE_ROOT_NODE: //we have to do a bunch of initialization of leaf nodes
		if (b.info.numkeys == 0) //if the number of keys is 0 at root node then we must insert a new one
		{
			//now we're creating nodes to store the key value pair
			SIZE_T LBAdress;
			SIZE_T& LB_ref = LBAdress; //setting LB_ref ADDRESS to LBAdress's
			SIZE_T RBAdress;
			SIZE_T& RB_ref = RBAdress;

			//we must allocate a new node because there is nothing in root
			rc = AllocateNode(LB_ref); //puts the node into memory via unserialize in allocate function
			//LB_ref now becomes the number of a free block within memory, (it is loaded into buffer by the function allocate)
			if (rc) { 
				cout << rc << endl; 
				return rc;  //if we have an error
			}

			// Unserialize from block offset into left_node
			BTreeNode left_node;
			rc = left_node.Unserialize(buffercache, LBAdress); //LBAddress = node number& leftnode
			if (rc) { return rc; }

			//must do all initializing of the new leaf node right here
			left_node.info.nodetype = BTREE_LEAF_NODE;
			left_node.data = new char[left_node.info.GetNumDataBytes()];
			memset(left_node.data, 0, left_node.info.GetNumDataBytes()); //not putting anything in it because its the left node
			left_node.info.numkeys = 0;
			rc = left_node.Serialize(buffercache, LBAdress); //now we serialize it back ibnot the buffer
			if (rc) { return rc; }

			//same thing for the right node
			rc = AllocateNode(RB_ref);
			if (rc) { cout << rc << endl; return rc; }
			BTreeNode right_node;
			rc = right_node.Unserialize(buffercache, RBAdress);
			if (rc) { return rc; }
			right_node.info.nodetype = BTREE_LEAF_NODE;
			right_node.data = new char[right_node.info.GetNumDataBytes()];
			memset(right_node.data, 0, right_node.info.GetNumDataBytes());
			//rc = right_node.Serialize(buffercache, RBAdress);
			right_node.info.numkeys = 1;
			rc = right_node.SetKey(0, key); //instert the given value into the leaf node
			if (rc) { return rc; }
			rc = right_node.SetVal(0, value);
			if (rc) { return rc; }
			rc = right_node.Serialize(buffercache, RBAdress); //now we put it back in the buffer
			if (rc) { return rc; }


			//after creating leaf nodes from fresh root node, we need to tidy it up a little
			//so that the tree retains its integrity (establishing connection from root to leafs)
			b.info.numkeys = 1;
			rc = b.SetKey(0, key); //NB: in root node we are not parsing by key==test key, only if it is greater than will we go into the last node
			//rc = b.SetKey(1, key); //must set it up this way for the root node
			if (rc) { return rc; }
			rc = b.SetPtr(0, LB_ref); //in this case LB_ref, its the node number of the left one. Its probably going to be like 3
			if (rc) { return rc; }
			rc = b.SetPtr(1, RB_ref); //the right node is the one with the value we putin 
			if (rc) { return rc; }
			//now we got to save the changes made to root node back into memory and buffer
			rc = b.Serialize(buffercache, node);
			if (rc) { return rc; }

			return ERROR_NOERROR; //if succesful then this returns success, otherwise it fails during RC return
			break;
		}
		//All other cases, ie when we need more keys in root node, are covered in the split function


		//Basicually if we land on an interior node we are going to recurse all the way down to the bottom leaf node
		//But we are storing the path in Hvector "history vector" in case we want to split
	case BTREE_INTERIOR_NODE:
		for (offset = 0; offset<b.info.numkeys; offset++) {
			rc = b.GetKey(offset, testkey);
			if (rc) { return rc; }
			if (key<testkey) { // are use key==testkey in our implimentation
				// OK, so we now have the first key that's larger
				// so we ned to recurse on the ptr immediately previous to 
				// this one, if it exists
				rc = b.GetPtr(offset, ptr);
				if (rc) { return rc; }
				return Inserter(Hvector, ptr, key, value);
			}
		}

		if (b.info.numkeys>0) {
			rc = b.GetPtr(b.info.numkeys, ptr);
			if (rc) { return rc; }
			return Inserter(Hvector, ptr, key, value);
		}
		else {
			return ERROR_NONEXISTENT;
		}
		break;

		//now we finally have hit the leaf node
	case BTREE_LEAF_NODE:
		return LeafNodeInsert(Hvector, node, b_ref, key, value); //we have a record of all the nodes we have recursed and their 
		//orders in Hvector in case of split

	default:
		return ERROR_INSANE;
		break;
	}

	return ERROR_INSANE;
}


ERROR_T BTreeIndex::Insert(const KEY_T &key, const VALUE_T &value)
{
	// WRITE ME
	//return ERROR_UNIMPL;
	list<SIZE_T> Hvector;
	return Inserter(Hvector, superblock.info.rootnode, key, value);
}

ERROR_T BTreeIndex::Update(const KEY_T &key, const VALUE_T &value)
{
	VALUE_T value1 = value; //we must do this otherwise the actual input value address with get changed!
	return LookupOrUpdateInternal(superblock.info.rootnode, BTREE_OP_UPDATE, key, value1);
}


ERROR_T BTreeIndex::Delete(const KEY_T &key)
{
	// This is optional extra credit 
	//
	// 
	return ERROR_UNIMPL;
}


//
//
// DEPTH first traversal
// DOT is Depth + DOT format
//

ERROR_T BTreeIndex::DisplayInternal(const SIZE_T &node,
	ostream &o,
	BTreeDisplayType display_type) const
{
	KEY_T testkey;
	SIZE_T ptr;
	BTreeNode b;
	ERROR_T rc;
	SIZE_T offset;

	rc = b.Unserialize(buffercache, node);

	if (rc != ERROR_NOERROR) {
		return rc;
	}

	rc = PrintNode(o, node, b, display_type);

	if (rc) { return rc; }

	if (display_type == BTREE_DEPTH_DOT) {
		o << ";";
	}

	if (display_type != BTREE_SORTED_KEYVAL) {
		o << endl;
	}

	switch (b.info.nodetype) {
	case BTREE_ROOT_NODE:
	case BTREE_INTERIOR_NODE:
		if (b.info.numkeys > 0) {
			for (offset = 0; offset <= b.info.numkeys; offset++) {
				rc = b.GetPtr(offset, ptr);
				if (rc) { return rc; }
				if (display_type == BTREE_DEPTH_DOT) {
					o << node << " -> " << ptr << ";\n";
				}
				rc = DisplayInternal(ptr, o, display_type);
				if (rc) { return rc; }
			}
		}
		return ERROR_NOERROR;
		break;
	case BTREE_LEAF_NODE:
		return ERROR_NOERROR;
		break;
	default:
		if (display_type == BTREE_DEPTH_DOT) {
		}
		else {
			o << "Unsupported Node Type " << b.info.nodetype;
		}
		return ERROR_INSANE;
	}

	return ERROR_NOERROR;
}


ERROR_T BTreeIndex::Display(ostream &o, BTreeDisplayType display_type) const
{
	ERROR_T rc;
	if (display_type == BTREE_DEPTH_DOT) {
		o << "digraph tree { \n";
	}
	rc = DisplayInternal(superblock.info.rootnode, o, display_type);
	if (display_type == BTREE_DEPTH_DOT) {
		o << "}\n";
	}
	return ERROR_NOERROR;
}

//trees cannot have cycles, so we iterate through all nodes and edges to see if we come across repeats
ERROR_T BTreeIndex::TreeChecker(set<SIZE_T> SeenBefore, const SIZE_T &node) const
{
	BTreeNode b;
	ERROR_T rc;
	SIZE_T ptr;
	SIZE_T& ptr_ref = ptr;
	SIZE_T offset;

	if (SeenBefore.count(node)) {
		return ERROR_INSANE; //if the node is something we've seen. Then we got a faulty tree
	}
	else {
		SeenBefore.insert(node);
	}


	rc = b.Unserialize(buffercache, node);
	if (rc) { return rc; }

	switch (b.info.nodetype){
	case BTREE_ROOT_NODE:
	case BTREE_INTERIOR_NODE:

		if (b.info.numkeys >= b.info.GetNumSlotsAsInterior()) {
			return ERROR_INSANE;
		}

		for (offset = 0; offset <= b.info.numkeys; offset++){
			rc = b.GetPtr(offset, ptr_ref);
			if (rc) { return rc; }
			rc = TreeChecker(SeenBefore, ptr_ref);
			if (rc) { return rc; }
		}
		return ERROR_NOERROR;
		break;
	case BTREE_LEAF_NODE:
		if (b.info.numkeys >= b.info.GetNumSlotsAsLeaf()) {
			return ERROR_INSANE;
		}
		return ERROR_NOERROR;
		break;
	default:
		return ERROR_INSANE;
		break;
	}
	return ERROR_INSANE;
}


ERROR_T BTreeIndex::SanityCheck() const
{
	set<SIZE_T> SeenBefore; //we keep track of SeenBefore notes because trees cannot have loops
	SIZE_T root = superblock.info.rootnode;
	return TreeChecker(SeenBefore, root); //returns insanse if we have a cycle
}



ostream & BTreeIndex::Print(ostream &os) const
{
	// WRITE ME
	return os;
}




