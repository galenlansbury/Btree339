#include <new>
#include <string.h>

#include "block.h"

//default constructor
Block::Block() : data(0), length(0), lastaccessed(-1), dirty(false)
{}


Block::Block(const SIZE_T s) : data(0), length(0), lastaccessed(-1), dirty(false)
{
  Resize(s); //will allocate memory for bytes
}



Block::Block(const Block &rhs) : data(0), length(0), lastaccessed(rhs.lastaccessed), dirty(rhs.dirty)
{
  if (Resize(rhs.length)!=ERROR_NOERROR) { 
    throw GenericException();
  }
  memcpy(data,rhs.data,rhs.length);
}

//called when serializing or writing data to block
Block::Block(const char * str) : data(0), length(0), lastaccessed(-1), dirty(false)
{
  if (Resize(strlen(str))!=ERROR_NOERROR) { 
    throw GenericException();
  }
  memcpy(data,str,strlen(str));  //void * memcpy ( void * destination, const void * source, size_t num );
  //data is the unique data per block
  //str is the input data
}

Block::~Block() 
{ 
  if (data) { delete [] data; data=0; } //if there are characters in the prexisting block erase them
  length=0;
  lastaccessed=-1;
  dirty=false;
}

Block & Block::operator=(const Block &rhs)
{
  return *(new (this) Block(rhs));
}

#define MIN(x,y) ((x)<(y) ? (x) : (y))


//erases/copies old data into new data of array[newlen] size
ERROR_T Block::Resize(const SIZE_T newlen, const bool copy)
{
  BYTE_T *d;
  
  try {
    d = new BYTE_T [newlen]; //creates a new array of size newlen
  }
  catch (...) {
    return ERROR_NOMEM;
  }

  if (copy) { 
    memcpy(d,data,MIN(newlen,length)); // copies old data into new array created
	//void * memcpy ( void * destination, const void * source, size_t num );
  }
  
  if (data) { delete [] data; }
  data = d;

  length=newlen;

  return ERROR_NOERROR;
}


static char high2hex(BYTE_T x)
{
  x>>=4;
  x&=0xf;
  return (x<10 ? '0'+x : 'a'+(x-10));
}

static char low2hex(BYTE_T x)
{
  x&=0xf;
  return (x<10 ? '0'+x : 'a'+(x-10));
}



#define MAX(x,y) ((x)>(y) ? (x) : (y))

bool Block::operator<(const Block &rhs) const
{
  return memcmp(data,rhs.data,MAX(length,rhs.length))<0;
}


bool Block::operator==(const Block &rhs) const
{
  return memcmp(data,rhs.data,MAX(length,rhs.length))==0;
}

ostream & Block::Print(ostream &os) const
{
  os << "Block(length="<<length<<", data=0x";
  for (SIZE_T i=0;i<length;i++) { 
    os << high2hex(data[i]) << low2hex(data[i]);
  }
  os << ", lastaccessed="<<lastaccessed<<", dirty="<<dirty<<")";
  return os;
}

