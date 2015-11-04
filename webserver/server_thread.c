#include "request.h"
#include "server_thread.h"
#include "common.h"
#include <pthread.h>
#include <unistd.h>

///////////////////////////////////////////////
// Single Linker, used for single-linked linked list
typedef struct Single_Linker{
	void *m_pPointer;
	struct Single_Linker* m_pNextLinker;
}Single_Linker;

// Queue
typedef struct Queue{
	struct Single_Linker* m_pStartLinker;
	struct Single_Linker* m_pEndLinker;
	int m_iSize;
}Queue;

// Queue Function
Queue* 		Queue_new							();
void   		Queue_free							(Queue* _queue);
int    		Queue_push							(Queue* _queue, void* _p);
int			Queue_pop							(Queue* _queue);
void*		Queue_top							(Queue* _queue);
int			Queue_movetop						(Queue* _queue, int pos);

typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t cond_t;
///////////////////////////////////////////////
void *request_stub(void* _sv);
struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
    int m_inIdleThread;
    mutex_t* m_pRequestLock;
    cond_t* m_pCVRequest;
    cond_t* m_pCVProduce;
    Queue* m_pRequestPool; // type int
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
	/* reads file, 
	 * fills data->file_buf with the file contents,
	 * data->file_size with file size. */
	ret = request_readfile(rq);
	if (!ret)
		goto out;
	/* sends file to client */
	request_sendfile(rq);
out:
	request_destroy(rq);
	file_data_free(data);
}

/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests;
	sv->max_cache_size = max_cache_size;
    sv->m_inIdleThread = 0;
    
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
        sv->m_pRequestPool = Queue_new();
        sv->m_pRequestLock = (mutex_t*)malloc(sizeof(mutex_t));
        sv->m_pCVRequest = (cond_t*)malloc(sizeof(cond_t));
        sv->m_pCVProduce = (cond_t*)malloc(sizeof(cond_t));
        pthread_mutex_init(sv->m_pRequestLock,NULL);
        pthread_cond_init(sv->m_pCVRequest,NULL);
        int i;
        pthread_t* thread_list = (pthread_t*)malloc(nr_threads*sizeof(pthread_t));
        for(i = 0; i<nr_threads; i++)
        {
            pthread_create(thread_list+i,NULL,request_stub,(void*)sv);
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
    for(;sv->m_pRequestPool->m_iSize == 0;)
    {
      // sv->m_inIdleThread++;
        pthread_cond_wait(sv->m_pCVRequest,sv->m_pRequestLock);
        //sv->m_inIdleThread--;
    }
    int connfd = *(int*)Queue_top(sv->m_pRequestPool);
    Queue_pop(sv->m_pRequestPool);
    pthread_cond_signal(sv->m_pCVProduce);
    pthread_mutex_unlock(sv->m_pRequestLock);
    do_server_request(sv,connfd);
    goto start;
}

void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
        // need to push the connfd into request queue
        int* _connfd = (int*)malloc(sizeof(int));
        *_connfd = connfd;
        //printf("Request Received\n");
        pthread_mutex_lock(sv->m_pRequestLock);

        for(;sv->m_pRequestPool->m_iSize == sv->max_requests;)
          pthread_cond_wait(sv->m_pCVProduce,sv->m_pRequestLock);

        Queue_push(sv->m_pRequestPool,_connfd);

        if(sv->m_pRequestPool->m_iSize > 0)
            pthread_cond_signal(sv->m_pCVRequest);

        pthread_mutex_unlock(sv->m_pRequestLock);
	}
}


//////////////////////////////////////////////////////
// 						Queue						//
//////////////////////////////////////////////////////


Queue* Queue_new()
{
	Queue* ret = (Queue *)malloc(sizeof(Queue));
	ret->m_pStartLinker = NULL;
	ret->m_pEndLinker = NULL;
	ret->m_iSize = 0;
	return ret;
}

void Queue_free(Queue* _queue)
{
	if(_queue == NULL)
	{
		return;
	}
	else
	{
		Single_Linker* start_linker = _queue->m_pStartLinker;
		Single_Linker* prior_linker = NULL;
		for(;start_linker != NULL;)
		{
			prior_linker = start_linker;
			start_linker = start_linker->m_pNextLinker;
			free(prior_linker);
		}
		free(_queue);
		return;
	}
}

int Queue_push(Queue* _queue, void* _p)
{
	Single_Linker* newlinker = (Single_Linker*)malloc(sizeof(Single_Linker));
	newlinker->m_pPointer = _p;
	if(newlinker == NULL)
		return 0;
	if(_queue->m_iSize == 0)
	{
		_queue->m_pStartLinker = newlinker;
		_queue->m_pEndLinker = newlinker;
		_queue->m_pEndLinker->m_pNextLinker = NULL;
		_queue->m_iSize += 1;
		return 1;
	}
	else
	{
		_queue->m_pEndLinker->m_pNextLinker = newlinker;
		_queue->m_pEndLinker = newlinker;
		_queue->m_pEndLinker ->m_pNextLinker = NULL;
		_queue->m_iSize += 1;
		return 1;
	}
}

int Queue_pop(Queue* _queue)
{
	if(_queue == NULL)
		return 0;
    if(_queue -> m_iSize == 1)
    {
        free(_queue->m_pStartLinker);
        _queue->m_pStartLinker = NULL;
        _queue->m_pEndLinker = NULL;
        _queue->m_iSize -= 1;
        return 1;
    }
	else
	{
		Single_Linker* temp = _queue->m_pStartLinker;
		_queue->m_pStartLinker = _queue->m_pStartLinker->m_pNextLinker;
		free(temp);
		_queue->m_iSize -= 1;
		return 1;
	}
}

void* Queue_top(Queue* _queue)
{
	if(_queue == NULL)
	{
		return NULL;
	}
	else	
	{
		return _queue->m_pStartLinker->m_pPointer;
	}
}

int Queue_movetop(Queue* _queue, int pos)
{
	if(pos < 0)
	{
		return 0;
	}
	else if(pos == 0)
	{
		return 1;
	}
	else if(_queue->m_iSize == 1)
	{
	        return 1;
	}
	else if(pos >= _queue->m_iSize)
	{
	  return 0;
	}
	else
	{
		Single_Linker* previous_linker = _queue->m_pStartLinker;
		Single_Linker* current_linker = _queue->m_pStartLinker->m_pNextLinker;
		int counter;
		// Find the linker at that position
		for(counter = 1;counter < pos;counter++)
		{
			previous_linker = current_linker;
			current_linker = current_linker -> m_pNextLinker;
		}
		if(current_linker == _queue->m_pEndLinker)
		{
		       _queue->m_pEndLinker = previous_linker;
		}
		previous_linker->m_pNextLinker = current_linker->m_pNextLinker;
		current_linker->m_pNextLinker = _queue->m_pStartLinker;
		_queue->m_pStartLinker = current_linker;
		return 1;
	}
}
