#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include "common.h"
#include "wc.h"

#define HASHTABLE_INCREMENTSIZE 1073741824	// Initial growing size of the hashtable
#define HASHTABLE_LOADFACTOR 0.5			// Load factor of the hashtable
#define HASHTABLE_SCALEFACTOR 2				// scaling factor of the hashtable(used for growing)
#define HASHTABLE_MOVECOUNT 2				// Num of bucket rehashed during the reallocation

typedef enum eStructTypes{
    eType_uint32_t,
    eType_uint64_t,
    eType_string
} eStructTypes;

// Linked list

typedef struct Linker{
	void *m_pPointer;
	struct Linker* m_pNextLinker;
	struct Linker* m_pPriorLinker;
}Linker;

typedef struct Linkedlist{
    struct Linker* m_pStartLinker;
    struct Linker* m_pEndLinker;
    int m_iSize;
} Linkedlist;

// Simple Pair
typedef struct Pair{
	void* first;
	void* second;
}Pair;

// function for pair
Pair* Pair_makepair(void* _first, void* _second)
{
	Pair* newpair = (Pair*)malloc(sizeof(Pair));
	newpair->first = _first;
	newpair->second = _second;
	return newpair;
}

void Pair_free(Pair* _pair)
{
	free(_pair->first);
	free(_pair->second);
}

Linkedlist* Linkedlist_new()
{
    Linkedlist* nl = (struct Linkedlist*)malloc(sizeof(Linkedlist));
    nl->m_iSize = 0;
    nl->m_pStartLinker = NULL;
    nl->m_pEndLinker = NULL;
    return nl;
}

void* Linkedlist_front(Linkedlist* _ll)
{
	if (_ll->m_iSize == 0)
	{
		return NULL;
	}
	else
	{
		return _ll->m_pStartLinker->m_pPointer;
	}
}

void* Linkedlist_back(Linkedlist* _ll)
{
	if (_ll->m_iSize == 0)
	{
		return NULL;
	}
	else
	{
		return _ll->m_pEndLinker->m_pPointer;
	}
}

int Linkedlist_pushback(Linkedlist* _ll, void* _p)
{
    struct Linker* nl = (struct Linker*)malloc(sizeof(struct Linker));
    if(nl == NULL)
        return 0;
    else if(_ll->m_iSize == 0)
    {
        nl->m_pNextLinker = NULL;
		nl->m_pPriorLinker = NULL;
		nl->m_pPointer = _p;
		_ll->m_pStartLinker = nl;
		_ll->m_pEndLinker = nl;
		++_ll->m_iSize;
		return 1;
    }
    else
    {
        nl->m_pPointer = _p;
        nl->m_pNextLinker = NULL;
        nl->m_pPriorLinker = _ll->m_pEndLinker;
        _ll->m_pEndLinker->m_pNextLinker = nl;
        _ll->m_pEndLinker = nl;
        _ll->m_iSize++;
        return 1;
    }
}

int Linkedlist_popback(Linkedlist* _ll)
{
    if(_ll->m_iSize == 0)
		return 0;
	else if (_ll->m_iSize == 1)
	{
		free(_ll->m_pStartLinker->m_pPointer);
		free(_ll->m_pStartLinker);
		--_ll->m_iSize;
		_ll->m_pStartLinker = NULL;
		_ll->m_pEndLinker = NULL;
		return 1;
	}
	else
	{
		struct Linker* temp = _ll->m_pEndLinker;
		_ll->m_pEndLinker = temp->m_pPriorLinker;
		temp->m_pPriorLinker->m_pNextLinker = NULL;
		--_ll->m_iSize;
		free(temp->m_pPointer);
		free(temp);
		return 1;
	}
}

int Linkedlist_pushfront(Linkedlist* _ll, void* _p)
{
    struct Linker* nl = (struct Linker *)malloc(sizeof(struct Linker));
    if(nl == NULL)
        return 0;
    else if(_ll->m_iSize == 0)
    {
        nl->m_pNextLinker = NULL;
		nl->m_pPriorLinker = NULL;
		nl->m_pPointer = _p;
		_ll->m_pStartLinker = nl;
		_ll->m_pEndLinker = nl;
		++_ll->m_iSize;
		return 1;
    }
    else
    {
        nl->m_pPointer = _p;
        nl->m_pPriorLinker = NULL;
        nl->m_pNextLinker = _ll->m_pStartLinker;
        _ll->m_pStartLinker->m_pPriorLinker = nl;
        _ll->m_pStartLinker = nl;
        ++_ll->m_iSize;
        return 1;
    }
}
void Linkedlist_free(Linkedlist* _ll)
{
    if(_ll->m_iSize == 0)
    {
        free(_ll);
        return;
    }
	else
	{
		struct Linker* current_linker = _ll->m_pStartLinker;
		struct Linker* next_linker = current_linker->m_pNextLinker;
		for(;next_linker!=NULL;)
		{
			free(current_linker->m_pPointer);
			free(current_linker);
			current_linker = next_linker;
			next_linker = current_linker->m_pNextLinker;
		}
		free(_ll);
		return;
	}
}

int Linkedlist_popfront(Linkedlist* _ll)
{
    if(_ll->m_iSize == 0)
		return 0;
	else if (_ll->m_iSize == 1)
	{
		free(_ll->m_pStartLinker->m_pPointer);
		free(_ll->m_pStartLinker);
		_ll->m_pStartLinker = NULL;
		_ll->m_pEndLinker = NULL;
		_ll->m_iSize = 0;
		return 1;
	}
	else
	{
		struct Linker* temp = _ll->m_pStartLinker;
		_ll->m_pStartLinker = temp->m_pNextLinker;
		temp->m_pNextLinker->m_pPriorLinker = NULL;
		_ll->m_iSize--;
		free(temp->m_pPointer);
		free(temp);
		return 1;
	}
}

int Linkedlist_insertat(Linkedlist* _ll, int _pos, void* _p)
{
    if(_pos > _ll->m_iSize-1 || _pos < 0)
		return 0;
	else if(_pos == 0)
		return Linkedlist_pushfront(_ll,_p);
	else if(_pos == _ll->m_iSize-1)
		return Linkedlist_pushback(_ll,_p);
	else
	{
		int counter = 0;
		struct Linker* temp = _ll->m_pStartLinker;
		for(;counter < _pos; ++counter)
		{
			temp = temp->m_pNextLinker;
		}
		struct Linker* nl = (struct Linker*)malloc(sizeof(struct Linker));
		nl->m_pPriorLinker = temp->m_pPriorLinker;
		nl->m_pNextLinker = temp;
		nl->m_pPointer = _p;
		temp->m_pPriorLinker->m_pNextLinker = nl;
		temp->m_pPriorLinker = nl;
		++_ll->m_iSize;
		return 1;
	}

}

int Linkedlist_deleteat(Linkedlist* _ll, int _pos)
{
    if(_pos > _ll->m_iSize-1 || _pos < 0)
		return 0;
	else if(_pos == 0)
		return Linkedlist_popfront(_ll);
	else if(_pos == _ll->m_iSize-1)
		return Linkedlist_popback(_ll);
	else
	{
		int counter = 0;
		struct Linker* temp = _ll->m_pStartLinker;
		for(;counter < _pos; ++counter)
		{
			temp = temp->m_pNextLinker;
		}
		temp->m_pPriorLinker->m_pNextLinker = temp->m_pNextLinker;
		temp->m_pNextLinker->m_pPriorLinker = temp->m_pPriorLinker;
		--_ll->m_iSize;
		free(temp->m_pPointer);
		free(temp);
		return 1;
	}
}



void* Linkedlist_at(Linkedlist* _ll, int _pos)
{
	if (_pos < 0 || _pos >= _ll->m_iSize)
	{
		return NULL;
	}
	else
	{
		int i;
		Linker* start_linker;
		for (i = 0, start_linker = _ll->m_pStartLinker; i < _pos; i++)
			start_linker = start_linker->m_pNextLinker;
		return start_linker->m_pPointer;
	}
}

int Linkedlist_popfront_nofree(Linkedlist* _ll)
{
	if (_ll->m_iSize == 0)
		return 0;
	else if (_ll->m_iSize == 1)
	{
		free(_ll->m_pStartLinker);
		_ll->m_pStartLinker = NULL;
		_ll->m_pEndLinker = NULL;
		--_ll->m_iSize;
		return 1;
	}
	else
	{
		struct Linker* temp = _ll->m_pStartLinker;
		_ll->m_pStartLinker = temp->m_pNextLinker;
		temp->m_pNextLinker->m_pPriorLinker = NULL;
		--_ll->m_iSize;
		free(temp);
		return 1;
	}
}

int Linkedlist_popback_nofree(Linkedlist* _ll)
{
	if (_ll->m_iSize == 0)
		return 0;
	else if (_ll->m_iSize == 1)
	{
		free(_ll->m_pStartLinker);
		--_ll->m_iSize;
		_ll->m_pStartLinker = NULL;
		_ll->m_pEndLinker = NULL;
		return 1;
	}
	else
	{
		struct Linker* temp = _ll->m_pEndLinker;
		_ll->m_pEndLinker = temp->m_pPriorLinker;
		temp->m_pPriorLinker->m_pNextLinker = NULL;
		--_ll->m_iSize;
		free(temp);
		return 1;
	}
}

int Linkedlist_deleteat_nofree(Linkedlist* _ll, int _pos)
{
	if (_pos > _ll->m_iSize - 1 || _pos < 0)
		return 0;
	else if (_pos == 0)
		return Linkedlist_popfront(_ll);
	else if (_pos == _ll->m_iSize - 1)
		return Linkedlist_popback(_ll);
	else
	{
		int counter = 0;
		struct Linker* temp = _ll->m_pStartLinker;
		for (; counter < _pos; ++counter)
		{
			temp = temp->m_pNextLinker;
		}
		temp->m_pPriorLinker->m_pNextLinker = temp->m_pNextLinker;
		temp->m_pNextLinker->m_pPriorLinker = temp->m_pPriorLinker;
		--_ll->m_iSize;
		free(temp);
		return 1;
	}
}

int Linkedlist_concatenate(Linkedlist* _list1, Linkedlist* _list2)
{
	if(_list1==NULL || _list2==NULL)
	{
		return 0;
	}
	else
	{
		if(_list1->m_iSize == 0)
		{
			_list1->m_pStartLinker = _list2->m_pStartLinker;	
			_list1->m_pEndLinker = _list2->m_pEndLinker;
			_list1->m_iSize = _list2->m_iSize;
		}
		else
		{
			_list1->m_pEndLinker->m_pNextLinker = _list2->m_pStartLinker;
			_list2->m_pStartLinker->m_pPriorLinker = _list1->m_pEndLinker;
			_list1->m_pEndLinker = _list2->m_pEndLinker;
			_list1->m_iSize += _list2->m_iSize;
		}
		free(_list2);
		return 1;
	}
}



// BucketBuffer for hash table
typedef struct BucketBuffer{
	Linkedlist** m_pLinkedList;
	uint64_t m_iSize;
	uint64_t m_iBucketNumber;
	uint64_t m_iReallocateCursor;
}BucketBuffer;

// 
typedef struct HashTable{
	BucketBuffer* m_pMainBuffer;
    BucketBuffer* m_pSubBuffer;
    eStructTypes m_eKeyType;
	eStructTypes m_eBucketType;
} HashTable;


// Hash Function
uint64_t HashTable_strHash(char* _str, BucketBuffer* _table)
{
	uint64_t key = 4294979861;
	int i;
	for (i = 0; _str[i] != '\0'; i++)
	{
		key = (key << 5) + key + (uint64_t)_str[i];
	}
	return key & (_table->m_iSize-1);
}

// Bucket Function
BucketBuffer* BucketBuffer_new(uint64_t _size)
{
	BucketBuffer* newbuffer = (BucketBuffer*)malloc(sizeof(BucketBuffer));
	newbuffer->m_iSize = _size;
	newbuffer->m_iBucketNumber = 0;
	newbuffer->m_pLinkedList = (Linkedlist**)malloc(sizeof(Linkedlist*)*_size);
	newbuffer->m_iReallocateCursor = 0;
	int i;
	for (i = 0; i < _size; i++)
	{
		newbuffer->m_pLinkedList[i] = NULL;
		
	}
	return newbuffer;
}

void BucketBuffer_free(BucketBuffer* _buffer)
{
	if (_buffer == NULL)
		return;
	int i;
	for (i = 0; i < _buffer->m_iSize; i++)
	{
		Linkedlist* temp = _buffer->m_pLinkedList[i];
		for (; temp!=NULL && temp->m_iSize != 0;)
		{
			Pair* temp = (Pair*)Linkedlist_front(_buffer->m_pLinkedList[i]);
			Pair_free(temp);
			Linkedlist_popfront(_buffer->m_pLinkedList[i]);
		}
		free(_buffer->m_pLinkedList[i]);
	}
	free(_buffer->m_pLinkedList);
	free(_buffer);
}

void* BucketBuffer_find(BucketBuffer* _buffer, eStructTypes _etype, void* _key)
{
	if(_buffer == NULL)
		return NULL;
	switch (_etype)
	{
		case eType_string:
		{	
			uint64_t main_key = HashTable_strHash((char*)_key, _buffer);
			Linkedlist* main_temp_list = _buffer->m_pLinkedList[main_key];
			if (main_temp_list!=NULL && main_temp_list->m_iSize != 0)
			{
				Linker* start;
				
				for (start = main_temp_list->m_pStartLinker; start != NULL; start = start->m_pNextLinker)
				{
					char* _old_key = (char*)((Pair*)(start->m_pPointer))->first;
					int result = strcmp((char*)_key, _old_key);
					if (result == 0)
					{
						return (void*)((Pair*)(start->m_pPointer))->second;
					}
				}
				return NULL;
			}
			else
			{
				return NULL;
			}
			break;
		}
		default:
		{
			return NULL;
		}
	}
}

HashTable* HashTable_new(eStructTypes _KeyType, eStructTypes _BucketType)
{
    HashTable* newtable = (HashTable*)malloc(sizeof(HashTable));
	// Initialize buffer
	newtable->m_pMainBuffer = BucketBuffer_new(HASHTABLE_INCREMENTSIZE);
	newtable->m_pSubBuffer = NULL;
    newtable->m_eKeyType = _KeyType;
	newtable->m_eBucketType = _BucketType;
    return newtable;
}

void HashTable_free(HashTable* _table)
{
	BucketBuffer_free(_table->m_pMainBuffer);
	BucketBuffer_free(_table->m_pSubBuffer);
	free(_table);
}

int HashTable_reallocate(HashTable* _table)
{
	if (_table->m_pSubBuffer!=NULL )
	{
		int i;
		int count = 0;
		
		for (i = _table->m_pSubBuffer->m_iReallocateCursor; i < _table->m_pSubBuffer->m_iSize; i++)
		{
			//printf("Move count: %d Size: %d\n",i, _table->m_pSubBuffer->m_iSize);
			Linkedlist* sub_list = _table->m_pSubBuffer->m_pLinkedList[i];
			if (sub_list != NULL && sub_list->m_iSize != 0)
			{
				Pair* temp = Linkedlist_front(_table->m_pSubBuffer->m_pLinkedList[i]);
				switch (_table->m_eKeyType)
				{
					// Add more case for reallocatio#n in the future
					case eType_string:
					{
						char* _input = (char*)temp->first;
						uint64_t key = HashTable_strHash(_input, _table->m_pMainBuffer);
						if(_table->m_pMainBuffer->m_pLinkedList[key]==NULL)
						{
							_table->m_pMainBuffer->m_pLinkedList[key] = Linkedlist_new();
							Linkedlist_concatenate(_table->m_pMainBuffer->m_pLinkedList[key],sub_list);
							_table->m_pSubBuffer->m_pLinkedList[i] = NULL;
						}
						else
						{
							Linkedlist_concatenate(_table->m_pMainBuffer->m_pLinkedList[key],sub_list);
							_table->m_pSubBuffer->m_pLinkedList[i] = NULL;
						}
						_table->m_pMainBuffer->m_iBucketNumber++;
						_table->m_pSubBuffer->m_iBucketNumber--;
						count++;
						break;
						// Add more reallocation here
						
					}
					default:
					{
						return 0;
					}
				}
				if(count >= HASHTABLE_MOVECOUNT)
				{
					_table->m_pSubBuffer->m_iReallocateCursor = i;
					break;
				}
			}
				
		}
		if (_table->m_pSubBuffer->m_iSize==0)
		{
			BucketBuffer_free(_table->m_pSubBuffer);
			_table->m_pSubBuffer = NULL;
		}
		return 1;
	}
	return 0;	
}

int HashTable_insert(HashTable* _table, void* _key, void* _bucket)
{
	switch (_table->m_eKeyType)
	{
		// Hash string into the hash table
		case eType_string:
		{
			// Insert string into the main buffer
			char* input_str = (char *)_key;
			uint64_t key = HashTable_strHash(input_str, _table->m_pMainBuffer);
			//printf("%lu\n",key);
			Linkedlist* inputlist = _table->m_pMainBuffer->m_pLinkedList[key];
			if(inputlist == NULL)
			{
				_table->m_pMainBuffer->m_pLinkedList[key] = Linkedlist_new();
				Linkedlist_pushback(_table->m_pMainBuffer->m_pLinkedList[key], (void*)Pair_makepair(_key,_bucket));
			}
			else
				Linkedlist_pushback(_table->m_pMainBuffer->m_pLinkedList[key], (void*)Pair_makepair(_key,_bucket));
			_table->m_pMainBuffer->m_iBucketNumber++;
			// Move things from sub buffer to main buffer
			HashTable_reallocate(_table);
			break;
		}
		default:
		{
			return 0;
		}	
	}
	// Check for load factor of the main buffer
	if (_table->m_pMainBuffer->m_iSize* HASHTABLE_LOADFACTOR <=
		_table->m_pMainBuffer->m_iBucketNumber)
	{
		// switch the main buffer to sub buffer,
		// reallocate a new buffer as the new buffer
		if(_table->m_pSubBuffer!=NULL)
		{
			BucketBuffer_free(_table->m_pSubBuffer); 
		}
		_table->m_pSubBuffer = _table->m_pMainBuffer;
		printf("New Buffer Allocated. size: %lu",_table->m_pMainBuffer->m_iSize);
		_table->m_pMainBuffer = BucketBuffer_new(HASHTABLE_SCALEFACTOR * _table->m_pSubBuffer->m_iSize);
	}
	return 1;
}

void* HashTable_find(HashTable* _table, void* _key)
{
	
	switch (_table->m_eKeyType)
	{
		// Hashed String
		case eType_string:
		{
			
			void* return1 = BucketBuffer_find(_table->m_pMainBuffer, _table->m_eKeyType, _key);
			if (return1 != NULL)
				return return1;
			else
			{
				
				void* return2 = BucketBuffer_find(_table->m_pSubBuffer, _table->m_eKeyType, _key);
				if (return2 != NULL)
					return return2;
				else
				{
					return NULL;
				}
			}
		}
		default:
		{
			return NULL;
			break;
		}
	}
}





struct wc {
	HashTable* m_pHashTable;
	/* you can define this struct to have whatever fields you want. */
};

struct wc *
wc_init(char *word_array, long size)
{
	struct wc *wc;
	wc = (struct wc *)malloc(sizeof(struct wc));
	wc->m_pHashTable = HashTable_new(eType_string,eType_uint64_t);
	char* source_pointer = word_array;
	int word_size = 0;
	int counter;
	for(counter = 0; word_array[counter]!='\0'; counter++)
	{
		//printf("%d\n",counter);
		if(isspace((int)(word_array[counter])))
		{
			char* input_word = (char*)malloc(sizeof(char)*word_size+1);
			memcpy(input_word,source_pointer,word_size);
			input_word[word_size] = '\0';
			word_size = 0;
			for(source_pointer = word_array+counter+1;isspace((int)(*source_pointer));source_pointer++);
			int size = strlen(input_word);
			if(size!=0)
			{
				void* ret_value = HashTable_find(wc->m_pHashTable, input_word);
				if(ret_value == NULL)
				{
					// Insert the word into the hashtable
					int* word_count = (int*)malloc(sizeof(int));
					*(word_count) = 1;
					HashTable_insert(wc->m_pHashTable,input_word, word_count);
				}
				else
				{
					int* word_count = (int*)ret_value;
					(*word_count)++;
					free(input_word);
				}
			}
			else
			{
				free(input_word);
			}
		}
		else
		{
			word_size++;			
		}
	}
	assert(wc);

	return wc;
}

void
wc_output(struct wc *wc)
{
	BucketBuffer* mainbuffer = wc->m_pHashTable->m_pMainBuffer;
	BucketBuffer* subbuffer = wc->m_pHashTable->m_pSubBuffer;
	if(mainbuffer!=NULL)
	{
		int buffersize = mainbuffer->m_iSize;
		int i;
		for(i = 0; i < buffersize; i++)
		{
			Linkedlist* bufferlist = mainbuffer->m_pLinkedList[i];
			if(bufferlist != NULL && bufferlist->m_iSize!=0)
			{
				Linker* startlinker = mainbuffer->m_pLinkedList[i]->m_pStartLinker;
				for(;startlinker!=NULL;startlinker = startlinker->m_pNextLinker)
				{
					Pair* entry = startlinker->m_pPointer;
					char* output_word = (char*)(entry->first);
					int* output_count = (int*)(entry->second);
					printf("%s:%d\n",output_word,*output_count);
				}
			}
		}
	}
	if(subbuffer!=NULL)
	{
		int buffersize = subbuffer->m_iSize;
		int i;
		for(i = 0; i < buffersize; i++)
		{
			Linkedlist* bufferlist = subbuffer->m_pLinkedList[i];
			if(bufferlist != NULL && bufferlist->m_iSize!=0)
			{
				Linker* startlinker = subbuffer->m_pLinkedList[i]->m_pStartLinker;
				for(;startlinker!=NULL;startlinker = startlinker->m_pNextLinker)
				{
					Pair* entry = startlinker->m_pPointer;
					char* output_word = (char*)(entry->first);
					int* output_count = (int*)(entry->second);
					printf("%s:%d\n",output_word,*output_count);
				}
			}
		}
	}
}

void
wc_destroy(struct wc *wc)
{
	HashTable_free(wc->m_pHashTable);
	free(wc);
}
