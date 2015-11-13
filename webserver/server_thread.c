#include "request.h"
#include "server_thread.h"
#include "common.h"
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#define HASHTABLE_INCREMENTSIZE 4096     	// Initial growing size of the hashtable
#define HASHTABLE_LOADFACTOR 0.5		// Load factor of the hashtable
#define HASHTABLE_SCALEFACTOR 2			// scaling factor of the hashtable(used for growing)
#define HASHTABLE_MOVECOUNT 2			// Num of bucket rehashed during the reallocation
////////////////////////////////////////////////
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t cond_t;
///////////////////////////////////////////////
// Single Linker, used for single-linked linked list
typedef struct Linker{
	void *m_pPointer;
	struct Linker* m_pNextLinker;
    struct Linker* m_pPrevLinker;
}Linker;

typedef struct List{
	struct Linker* m_pStartLinker;
	struct Linker* m_pEndLinker;
	int m_iSize;
}List;

// List Function //
List* 		List_new							();
List*       List_concatenate                    (List* _list1, List* _list2);
void   		List_free							(List* _list);
int         List_put                            (List* _list, void* _p);
int    		List_push							(List* _list, void* _p);
int			List_pop							(List* _list);
int         List_pull                           (List* _list);
int         List_delete                         (List* _list, int pos);
void*		List_top							(List* _list);
void*       List_bot                            (List* _list);
int			List_movetop						(List* _list, int pos);
int         List_pmovetop                        (List* _list, Linker* _linker);
////////////////////////////////////////////////
// Pair
typedef struct Pair{
    void* first;
    void* second;
}Pair;

Pair* Pair_new(void* _first, void* _second);
void  Pair_free(Pair* _p);
/////////////////////// HASH TABLE /////////////////////////
typedef struct BucketBuffer{
    List** m_pList;
    uint64_t m_iSize;
    uint64_t m_iBucketNumber;
    uint64_t m_iReallocateCursor;
}BucketBuffer;

typedef struct HashTable{
    BucketBuffer* m_pMainBuffer;
    BucketBuffer* m_pSubBuffer;
}HashTable;

HashTable*	HashTable_new                      ();
void		HashTable_free                     (HashTable* _table);
int			HashTable_insert                   (HashTable* _table, void* _key, void* _bucket);
void*		HashTable_find                     (HashTable* _table, void* _key);
int         HashTable_delete                   (HashTable* _table, void* _key);
int			HashTable_reallocate               (HashTable* _table);
void			BucketBuffer_free              (BucketBuffer* _buffer);
BucketBuffer*	BucketBuffer_new               (uint64_t _size);
void*			BucketBuffer_find              (BucketBuffer* _buffer,void* _key);
int             BucketBuffer_delete            (BucketBuffer* _buffer, void* _key);
uint64_t    HashTable_strHash                  (char* _str, BucketBuffer* _table);

////////////////////////////////////////////////
//////////////////////CACHE//////////////////////////
typedef struct Cache_block{
    int m_nPinned;
    Linker* m_pLinkPos;
    struct file_data* m_pData;
}Cache_block;

typedef struct Cache{
    mutex_t m_lFileTableLock;
    HashTable* m_pFileTable;
    List* m_pNameList;
    int m_iTotalSize;
    int m_iCurrentSize;
    int m_nPinned;
    int m_iEvictableSize;
}Cache;
// Happens in critical section, no lock needed.
int Cache_evict(Cache* _cache, int _ammount){
    if(_ammount > _cache->m_iTotalSize || _ammount <= 0)
        return 0;
    else{
        for(;_ammount>0;){
            //printf("Evicting\n");
            Cache_block* _blk = (Cache_block*)(List_bot(_cache->m_pNameList));
            if(_blk->m_nPinned > 0){
                //printf("%d,%d\n",pinned,_cache->m_pNameList->m_iSize);
                 List_pmovetop(_cache->m_pNameList,_blk->m_pLinkPos);
                 if(_cache->m_nPinned == _cache->m_pNameList->m_iSize){
                     return 0;
                 }
            }
            else{
                //printf("wowow00");
                struct file_data* _file  = _blk->m_pData;
                _ammount -= _file ->file_size;
                _cache->m_iCurrentSize -= _file->file_size;
                List_pull(_cache->m_pNameList);
                HashTable_delete(_cache->m_pFileTable, _file->file_name);
            }
        }
        //printf("EDD");
        return 1;
    }
}

Cache* Cache_init(int _size){
    Cache* _ret = (Cache*)Malloc(sizeof(Cache));
    pthread_mutex_init(&(_ret->m_lFileTableLock),NULL);
    _ret->m_pFileTable = HashTable_new();
    _ret->m_pNameList = List_new();
    _ret->m_iTotalSize = _size;
    _ret->m_iCurrentSize = 0;
    _ret->m_nPinned = 0;
    _ret->m_iEvictableSize = 0;
    return _ret;
}
int Cache_lookup(Cache* _cache, struct file_data* _data){
    pthread_mutex_lock(&(_cache->m_lFileTableLock));
    if(_cache == NULL || _data == NULL){
        pthread_mutex_unlock(&(_cache->m_lFileTableLock));
        return 0;
    }
    else{
        if(_data->file_size >= _cache ->m_iTotalSize >> 1){
            pthread_mutex_unlock(&(_cache->m_lFileTableLock));
            return 0;
        }
        else{
            void* _ret = HashTable_find(_cache->m_pFileTable, _data->file_name);
            if(_ret == NULL){
                pthread_mutex_unlock(&(_cache->m_lFileTableLock));
                return 0;
            }
            else{
                Cache_block* _blk = (Cache_block*)_ret;
                if(_blk->m_nPinned == 0){
                    _cache->m_nPinned++;
                }
                _blk->m_nPinned++;
                Linker* _linker = _blk->m_pLinkPos;
                List_pmovetop(_cache->m_pNameList,_linker);
                _data->file_buf = _blk->m_pData->file_buf;
                _data->file_size = _blk->m_pData->file_size;
                pthread_mutex_unlock(&(_cache->m_lFileTableLock));
                return 1;
            }
        }
    }
}
int Cache_insert(Cache* _cache, struct file_data* _data){
    //printf("Called Insert\n");
    pthread_mutex_lock(&(_cache->m_lFileTableLock));
    if(_cache == NULL || _data == NULL){
        pthread_mutex_unlock(&(_cache->m_lFileTableLock));
        return 0;
    }
    else{
        if(_data->file_size >= _cache->m_iTotalSize >> 1){
            //printf("File Not cached, Size: %d\n",_data->file_size);
            pthread_mutex_unlock(&(_cache->m_lFileTableLock));
            return 0;
        }
        void* _ret = HashTable_find(_cache->m_pFileTable,_data->file_name);
        if(_ret==NULL){
            if(_cache->m_iTotalSize - _cache->m_iCurrentSize < _data->file_size){
                //printf("Need Evict\n");
                int ret =  Cache_evict(_cache,_data->file_size);
                if(ret == 0){
                    pthread_mutex_unlock(&(_cache->m_lFileTableLock));
                    return 0;
                }
            }
            Cache_block* _new = (Cache_block*)malloc(sizeof(Cache_block));
            _new->m_pData = _data;
            _new->m_nPinned = 0;
            HashTable_insert(_cache->m_pFileTable,_data->file_name, _new);
            List_put(_cache->m_pNameList,_new);
            _new->m_pLinkPos = _cache->m_pNameList->m_pStartLinker;
            _cache->m_iCurrentSize += _data->file_size;
            pthread_mutex_unlock(&(_cache->m_lFileTableLock));
            return 1;
        }
        else{
            Cache_block* _blk = (Cache_block*)_ret;
            _blk->m_nPinned--;
            if(_blk->m_nPinned == 0)
                _cache->m_nPinned--;
            pthread_mutex_unlock(&(_cache->m_lFileTableLock));
            return 1;
        }
    }
}

////////////////////////////////////////////////
void *request_stub(void* _sv);
void *request_stub_nocache(void* _sv);
struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
    mutex_t* m_pRequestLock;
    cond_t* m_pCVRequest;
    cond_t* m_pCVProduce;
    List* m_pRequestPool; // type int
    Cache* m_pCache;
	/* add any other parameters you need */
};

/* static functions */

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
    //printf("Request Recieved, ");
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fills data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
    //printf("Find in Cache : ");
    int ret2 = Cache_lookup(sv->m_pCache,data);
    if(ret2!=0)
    {
        //printf("Cached Hit\n");
        request_sendfile(rq);
    }
    else{
        //printf("Cached miss\n");
        ret = request_readfile(rq);
        if(!ret){
            // printf("Read file invalid\n");
          request_destroy(rq);
          file_data_free(data);
          return;
        }
        else{
            //printf("File read, sending, size : %d", data->file_size);
            request_sendfile(rq);
        }
    }
    Cache_insert(sv->m_pCache,data);
    request_destroy(rq);
}

static void
do_server_request_nocache(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();
    rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
    ret = request_readfile(rq);
    if(!ret){
     // printf("Read file invalid\n");
        request_destroy(rq);
        file_data_free(data);
        return;
    }
    request_sendfile(rq);
    request_destroy(rq);
    file_data_free(data);
}

/////////////////////////////////////////////////////
/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests;
	sv->max_cache_size = max_cache_size;
    
	if (nr_threads > 0 || max_requests > 0) {
        sv->m_pRequestPool = List_new();
        sv->m_pRequestLock = (mutex_t*)malloc(sizeof(mutex_t));
        sv->m_pCVRequest = (cond_t*)malloc(sizeof(cond_t));
        sv->m_pCVProduce = (cond_t*)malloc(sizeof(cond_t));
        sv->m_pCache = Cache_init(max_cache_size);
        pthread_mutex_init(sv->m_pRequestLock,NULL);
        pthread_cond_init(sv->m_pCVRequest,NULL);
        pthread_cond_init(sv->m_pCVProduce,NULL);
        int i;
        // Creat N thread
        pthread_t* thread_list = (pthread_t*)malloc(nr_threads*sizeof(pthread_t));
        if(max_cache_size > 0)
            for(i = 0; i<nr_threads; i++)
                {
                    pthread_create(thread_list+i,NULL,request_stub,(void*)sv);
                }
        else
            for(i = 0; i<nr_threads; i++)
                {
                    pthread_create(thread_list+i,NULL,request_stub_nocache,(void*)sv);
                }
	}

	/* Lab 4: create queue of max_request size when max_requests > 0 */

	/* Lab 5: init server cache and limit its size to max_cache_size */

	/* Lab 4: create worker threads when nr_threads > 0 */

	return sv;
}

void *request_stub(void* _sv)
{
    struct server* sv = (struct server*)_sv;
start:
    pthread_mutex_lock(sv->m_pRequestLock);
    // if pool is empty
    for(;sv->m_pRequestPool->m_iSize == 0;)
    {
        pthread_cond_wait(sv->m_pCVRequest,sv->m_pRequestLock);
    }
    // if is not empty
    int connfd = *(int*)List_top(sv->m_pRequestPool);
    List_pop(sv->m_pRequestPool);
    pthread_cond_signal(sv->m_pCVProduce);
    pthread_mutex_unlock(sv->m_pRequestLock);
    
    do_server_request(sv,connfd);
    goto start;
}

void *request_stub_nocache(void* _sv)
{
    struct server* sv = (struct server*)_sv;
start:
    pthread_mutex_lock(sv->m_pRequestLock);
    // if pool is empty
    for(;sv->m_pRequestPool->m_iSize == 0;)
    {
        pthread_cond_wait(sv->m_pCVRequest,sv->m_pRequestLock);
    }
    // if is not empty
    int connfd = *(int*)List_top(sv->m_pRequestPool);
    List_pop(sv->m_pRequestPool);
    pthread_cond_signal(sv->m_pCVProduce);
    pthread_mutex_unlock(sv->m_pRequestLock);
    
    do_server_request_nocache(sv,connfd);
    goto start;
}


void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
        int* _connfd = (int*)malloc(sizeof(int));
        *_connfd = connfd;
        pthread_mutex_lock(sv->m_pRequestLock);
        // Buffer Full
        for(;sv->m_pRequestPool->m_iSize == sv->max_requests;)
          pthread_cond_wait(sv->m_pCVProduce,sv->m_pRequestLock);
        // Buffer not full
        List_push(sv->m_pRequestPool,_connfd);

        if(sv->m_pRequestPool->m_iSize > 0)
            pthread_cond_signal(sv->m_pCVRequest);

        pthread_mutex_unlock(sv->m_pRequestLock);
	}
}


//////////////////////////////////////////////////////
// 						List						//
//////////////////////////////////////////////////////


List* List_new()
{
	List* ret = (List *)malloc(sizeof(List));
	ret->m_pStartLinker = NULL;
	ret->m_pEndLinker = NULL;
	ret->m_iSize = 0;
	return ret;
}

void List_free(List* _list)
{
	if(_list == NULL)
	{
		return;
	}
	else
    {
        Linker* start_linker = _list->m_pStartLinker;
		Linker* prior_linker = NULL;
		for(;start_linker != NULL;)
		{
			prior_linker = start_linker;
			start_linker = start_linker->m_pNextLinker;
			free(prior_linker);
		}
		free(_list);
		return;
	}
}

int List_push(List* _list, void* _p)
{
	Linker* newlinker = (Linker*)malloc(sizeof(Linker));
	newlinker->m_pPointer = _p;
	if(newlinker == NULL)
		return 0;
	if(_list->m_iSize == 0)
	{
		_list->m_pStartLinker = newlinker;
		_list->m_pEndLinker = newlinker;
		_list->m_pEndLinker->m_pNextLinker = NULL;
        _list->m_pEndLinker->m_pPrevLinker = NULL;
		_list->m_iSize += 1;
		return 1;
	}
	else
	{
        newlinker->m_pPrevLinker = _list->m_pEndLinker;
		_list->m_pEndLinker->m_pNextLinker = newlinker;
		_list->m_pEndLinker = newlinker;
		_list->m_pEndLinker ->m_pNextLinker = NULL;
		_list->m_iSize += 1;
		return 1;
	}
}

int List_put(List* _list, void* _p)
{
    Linker* newlinker = (Linker*)malloc(sizeof(Linker));
    if(newlinker == NULL)
        return 0;
    newlinker->m_pPointer = _p;
    if(_list->m_iSize == 0){
        _list->m_pStartLinker = newlinker;
		_list->m_pEndLinker = newlinker;
		_list->m_pEndLinker->m_pNextLinker = NULL;
        _list->m_pEndLinker->m_pPrevLinker = NULL;
		_list->m_iSize += 1;
		return 1;
    }
    else
	{
        newlinker->m_pNextLinker = _list->m_pStartLinker;
		_list->m_pStartLinker->m_pPrevLinker = newlinker;
		_list->m_pStartLinker = newlinker;
		_list->m_pStartLinker ->m_pPrevLinker = NULL;
		_list->m_iSize += 1;
		return 1;
	}
}

int List_pop(List* _list)
{
	if(_list == NULL)
		return 0;
    if(_list -> m_iSize == 1)
    {
        free(_list->m_pStartLinker);
        _list->m_pStartLinker = NULL;
        _list->m_pEndLinker = NULL;
        _list->m_iSize -= 1;
        return 1;
    }
	else
	{
		Linker* temp = _list->m_pStartLinker;
		_list->m_pStartLinker = _list->m_pStartLinker->m_pNextLinker;
        _list->m_pStartLinker->m_pPrevLinker = NULL;
		free(temp);
		_list->m_iSize -= 1;
		return 1;
	}
}

int List_pull(List* _list)
{
    if(_list == NULL)
		return 0;
    if(_list -> m_iSize == 1)
    {
        free(_list->m_pStartLinker);
        _list->m_pStartLinker = NULL;
        _list->m_pEndLinker = NULL;
        _list->m_iSize -= 1;
        return 1;
    }
	else
	{
		Linker* temp = _list->m_pEndLinker;
		_list->m_pEndLinker = _list->m_pEndLinker->m_pPrevLinker;
        _list->m_pEndLinker->m_pNextLinker = NULL;
		free(temp);
		_list->m_iSize -= 1;
		return 1;
	}
}

int List_delete(List* _list, int pos){
    if(pos == 0){
        return List_pop(_list);
    }
    else if(pos == _list->m_iSize -1)
        return List_pull(_list);
    else{
        if(pos > 0 && pos < _list->m_iSize -1){
            Linker* temp = _list->m_pStartLinker;
            int count;
            for(count = 0;count != pos;count++)
                temp = temp->m_pNextLinker;
            temp->m_pPrevLinker->m_pNextLinker = temp->m_pNextLinker;
            temp->m_pNextLinker->m_pPrevLinker = temp->m_pPrevLinker;
            _list->m_iSize--;
            free(temp);
            return 1;
        }
        else{
            return 0;
        }
    }
}

void* List_top(List* _list)
{
	if(_list == NULL)
	{
		return NULL;
	}
	else	
	{
		return _list->m_pStartLinker->m_pPointer;
	}
}

void* List_bot(List* _list)
{
    if(_list == NULL)
    {
        return NULL;
    }
    else{
        return _list->m_pEndLinker->m_pPointer;
    }
}

int List_movetop(List* _list, int pos)
{
	if(pos < 0)
	{
		return 0;
	}
	else if(pos == 0)
	{
		return 1;
	}
	else if(_list->m_iSize == 1)
	{
	        return 1;
	}
	else if(pos >= _list->m_iSize)
	{
	  return 0;
	}
	else
	{
		Linker* previous_linker = _list->m_pStartLinker;
		Linker* current_linker = _list->m_pStartLinker->m_pNextLinker;
		int counter;
		// Find the linker at that position
		for(counter = 1;counter < pos;counter++)
		{
			previous_linker = current_linker;
			current_linker = current_linker -> m_pNextLinker;
		}
		if(current_linker == _list->m_pEndLinker)
		{
		       _list->m_pEndLinker = previous_linker;
		}
		previous_linker->m_pNextLinker = current_linker->m_pNextLinker;
        previous_linker->m_pNextLinker->m_pPrevLinker = previous_linker;
		current_linker->m_pNextLinker = _list->m_pStartLinker;
        current_linker->m_pPrevLinker = NULL;
        _list->m_pStartLinker->m_pPrevLinker = current_linker;
		_list->m_pStartLinker = current_linker;
		return 1;
	}
}
// Assume linker pos is in the list
int List_pmovetop(List* _list, Linker* pos){
    // printf("size: %d\n",_list->m_iSize);
    if(_list == NULL || pos == NULL){
        return 0;
    }
    else{
        if(_list->m_iSize == 1){
            return 1;
        }
        else if(pos == _list ->m_pStartLinker){
            return 1;
        }
        else if(pos == _list ->m_pEndLinker){
            pos->m_pPrevLinker->m_pNextLinker = NULL;
            _list->m_pEndLinker = pos->m_pPrevLinker;
        }
        else{
            pos->m_pPrevLinker->m_pNextLinker = pos->m_pNextLinker;
            pos->m_pNextLinker->m_pPrevLinker = pos->m_pPrevLinker;
        }
        pos->m_pPrevLinker = NULL;
        pos->m_pNextLinker = _list->m_pStartLinker;
        _list->m_pStartLinker->m_pPrevLinker = pos;
        _list->m_pStartLinker = pos;
        return 1;
    }
}
List* List_concatenate(List* _list1, List* _list2)
{
    if(_list1 == NULL || _list2 == NULL){
        return NULL;
    }
    else{
        if(_list1->m_iSize == 0){
            _list1->m_pStartLinker = _list2->m_pStartLinker;
            _list1->m_pEndLinker = _list2->m_pEndLinker;
            _list1->m_iSize = _list2->m_iSize;
        }
        else{
            _list1->m_pEndLinker->m_pNextLinker = _list2->m_pStartLinker;
            _list2->m_pStartLinker->m_pPrevLinker = _list1->m_pEndLinker;
            _list1->m_pEndLinker = _list2->m_pEndLinker;
            _list1->m_iSize += _list2->m_iSize;
        }
        free(_list2);
        return _list1;
    }
}
////////////////////////////////////////////////////////////
Pair* Pair_new(void* _first, void* _second){
    Pair* _ret = (Pair*)malloc(sizeof(Pair));
    _ret->first = _first;
    _ret->second = _second;
    return _ret;
}

void Pair_free(Pair* _p){
    free(_p);
}
//////////////////// Buffer /////////////////////////////////
// Bucket Function
BucketBuffer* BucketBuffer_new(uint64_t _size)
{
	BucketBuffer* newbuffer = (BucketBuffer*)malloc(sizeof(BucketBuffer));
	newbuffer->m_iSize = _size;
	newbuffer->m_iBucketNumber = 0;
	newbuffer->m_pList = (List**)malloc(sizeof(List*)*_size);
	newbuffer->m_iReallocateCursor = 0;
	int i;
	for (i = 0; i < _size; i++)
		newbuffer->m_pList[i] = NULL;
	return newbuffer;
}

void BucketBuffer_free(BucketBuffer* _buffer)
{
	if (_buffer == NULL)
		return;
	int i;
	for (i = 0; i < _buffer->m_iSize; i++)
	{
		List* temp = _buffer->m_pList[i];
		for (; temp!=NULL && temp->m_iSize != 0;)
		{
			Pair* temp = (Pair*)List_top(_buffer->m_pList[i]);
			Pair_free(temp);
			List_pop(_buffer->m_pList[i]);
		}
		free(_buffer->m_pList[i]);
	}
	free(_buffer->m_pList);
	free(_buffer);
}


void* BucketBuffer_find(BucketBuffer* _buffer, void* _key)
{
	if(_buffer == NULL)
		return NULL;
	uint64_t main_key = HashTable_strHash((char*)_key, _buffer);
	List* main_temp_list = _buffer->m_pList[main_key];
	if (main_temp_list!=NULL && main_temp_list->m_iSize != 0)
	{
		Linker* start;
		for (start = main_temp_list->m_pStartLinker; start != NULL; start = start->m_pNextLinker)
		{
			char* _old_key = (char*)((Pair*)(start->m_pPointer))->first;
			if (!strcmp((char*)_key, _old_key))
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
}

int BucketBuffer_delete(BucketBuffer* _buffer, void* _key){
    if(_buffer == NULL)
		return 0;
	uint64_t main_key = HashTable_strHash((char*)_key, _buffer);
	List* main_temp_list = _buffer->m_pList[main_key];
	if (main_temp_list!=NULL && main_temp_list->m_iSize != 0)
	{
		Linker* start;
        int count = 0;
		for (start = main_temp_list->m_pStartLinker; start != NULL; start = start->m_pNextLinker)
		{
          // printf("how");
            Pair* _pair = (Pair*)(start->m_pPointer);
            char* _old_key = (char*)((_pair)->first);
			if(!strcmp((char*)_key, _old_key)){
                if(count == 0)
                    List_pop(main_temp_list);
                else if(count == main_temp_list->m_iSize -1)
                    List_pull(main_temp_list);
                else{
                    start->m_pPrevLinker->m_pNextLinker = start->m_pNextLinker;
                    start->m_pNextLinker->m_pPrevLinker = start->m_pPrevLinker;
                    main_temp_list->m_iSize--;
                    free(start);
                }
                free(_pair->first);
                free(_pair->second);
                free(_pair);
                return 1;
            }
            count++;
		}
		return 0;
	}
	else
	{
		return 0;
	}
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
			List* sub_list = _table->m_pSubBuffer->m_pList[i];
			if (sub_list != NULL && sub_list->m_iSize != 0)
			{
				Pair* temp = List_top(_table->m_pSubBuffer->m_pList[i]);
				uint64_t key = HashTable_strHash(temp->first, _table->m_pMainBuffer);
				if(_table->m_pMainBuffer->m_pList[key]==NULL)
				{
					_table->m_pMainBuffer->m_pList[key] = List_new();
                    _table->m_pMainBuffer->m_iBucketNumber += sub_list->m_iSize;
                    _table->m_pSubBuffer->m_iBucketNumber -= sub_list->m_iSize;
					List_concatenate(_table->m_pMainBuffer->m_pList[key],sub_list);
					_table->m_pSubBuffer->m_pList[i] = NULL;
				}
				else
				{
                    _table->m_pMainBuffer->m_iBucketNumber += sub_list->m_iSize;
                    _table->m_pSubBuffer->m_iBucketNumber -= sub_list->m_iSize;
                    List_concatenate(_table->m_pMainBuffer->m_pList[key],sub_list);
					_table->m_pSubBuffer->m_pList[i] = NULL;
				}
				count++;
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

//////////////////////////////////////////////////////////////
// Hash table
HashTable* HashTable_new()
{
    HashTable* newtable = (HashTable*)malloc(sizeof(HashTable));
	// Initialize buffer
	newtable->m_pMainBuffer = BucketBuffer_new(HASHTABLE_INCREMENTSIZE);
	newtable->m_pSubBuffer = NULL;
    return newtable;
}

void HashTable_free(HashTable* _table)
{
	BucketBuffer_free(_table->m_pMainBuffer);
	BucketBuffer_free(_table->m_pSubBuffer);
	free(_table);
}

int HashTable_insert(HashTable* _table, void* _key, void* _bucket)
{
	char* input_str = (char *)_key;
	uint64_t key = HashTable_strHash(input_str, _table->m_pMainBuffer);
	List* inputlist = _table->m_pMainBuffer->m_pList[key];
	if(inputlist == NULL)
	{
		_table->m_pMainBuffer->m_pList[key] = List_new();
		List_push(_table->m_pMainBuffer->m_pList[key], (void*)Pair_new(_key,_bucket));
	}
	else
	{
		List_push(_table->m_pMainBuffer->m_pList[key], (void*)Pair_new(_key,_bucket));
	}
	_table->m_pMainBuffer->m_iBucketNumber++;
	HashTable_reallocate(_table);
	if (_table->m_pMainBuffer->m_iSize* HASHTABLE_LOADFACTOR <=
		_table->m_pMainBuffer->m_iBucketNumber)
	{
		if(_table->m_pSubBuffer!=NULL)
		{
			BucketBuffer_free(_table->m_pSubBuffer); 
		}
		_table->m_pSubBuffer = _table->m_pMainBuffer;
		_table->m_pMainBuffer = BucketBuffer_new(HASHTABLE_SCALEFACTOR * _table->m_pSubBuffer->m_iSize);
	}
	return 1;
}


void* HashTable_find(HashTable* _table, void* _key)
{
    void* return1 = BucketBuffer_find(_table->m_pMainBuffer, _key);
	if (return1 != NULL)
        return return1;
	else{
            void* return2 = BucketBuffer_find(_table->m_pSubBuffer, _key);
			if (return2 != NULL)
				return return2;
			else
                return NULL;
	}
}

int HashTable_delete(HashTable* _table, void* _key){
    int ret = BucketBuffer_delete(_table->m_pMainBuffer,_key);
    if(ret != 0)
        return ret;
    else{
        ret = BucketBuffer_delete(_table->m_pSubBuffer,_key);
        return ret;
    }
}

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
