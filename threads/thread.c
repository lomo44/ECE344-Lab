#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

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

typedef enum eThreadStatus{
    eThreadNew,
	eThreadRun,
	eThreadIdle,
	eThreadExit,
    eThreadSleep
}eThreadStatus;

typedef enum eMutexStatus{
    eMutexLocked,
    eMutexFree
}eMutexStatus;

// Queue Function
Queue* 		Queue_new							();
void   		Queue_free							(Queue* _queue);
int    		Queue_push							(Queue* _queue, void* _p);
int			Queue_pop							(Queue* _queue);
void*		Queue_top							(Queue* _queue);
int			Queue_movetop						(Queue* _queue, int pos);


/* This is the thread control block */
struct thread {
	int m_iID;
	ucontext_t* m_pContext;
	void* m_pStack;
	eThreadStatus m_eThreadStatus;
};

typedef struct thread thread;


// Global Ready queue for threads, top thread of the ready queue
// is the thread that is running
Queue* thread_ReadyQueue = NULL;
// Global exit queue for threads
Queue* thread_ExitQueue = NULL;


thread* thread_ReadyQueue_index[THREAD_MAX_THREADS] = {NULL};

Tid thread_exit(Tid tid);
Tid thread_yield(Tid tid);	

void
thread_init(void)
{
	// Initialize the ready queue
	thread_ReadyQueue = Queue_new();
	thread_ExitQueue = Queue_new();
	// Add current thread to the ready queue
	thread* newthread = (thread*)malloc(sizeof(thread));
	ucontext_t* newcontext = (ucontext_t*)malloc(sizeof(ucontext_t));
	newthread->m_iID = thread_ReadyQueue->m_iSize;
	newthread->m_pContext = newcontext;
	newthread->m_eThreadStatus = eThreadRun;
	newthread->m_pStack = NULL;
	thread_ReadyQueue_index[newthread->m_iID] = newthread;
	Queue_push(thread_ReadyQueue,newthread);
	getcontext(newcontext);
    // Enable interrupt handler
	return;
}

Tid
thread_id()
{
	if(thread_ReadyQueue == NULL)
	{
		return THREAD_INVALID;
	}
	else
	{
		return ((thread*)Queue_top(thread_ReadyQueue))->m_iID;
	}
}

void thread_stub(void (*fn) (void*), void* parg)
{
    interrupts_set(1);
	Tid ret;
	//printf("Enter Stub\n");
	fn(parg);
	ret = thread_exit(THREAD_SELF);
        assert(ret == THREAD_NONE);
	exit(0);
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
    //printf("called creat\n");
    int interrupt_enabled = interrupts_set(0);
	ucontext_t* new_context = (ucontext_t*)malloc(sizeof(ucontext_t));
	thread* new_thread = (thread*)malloc(sizeof(thread));
	if(new_thread == NULL || new_context == NULL)
	{
		free(new_context);
		free(new_thread);
        interrupts_set(interrupt_enabled);
        return THREAD_NOMEMORY;
	}
	new_thread->m_pContext = new_context;
	getcontext(new_context);
	int counter;
	for(counter = 1; counter < THREAD_MAX_THREADS; counter++)
	{
		if (thread_ReadyQueue_index[counter]==NULL)
		{
			new_thread->m_iID = counter;
			new_thread->m_eThreadStatus = eThreadNew;
			thread_ReadyQueue_index[counter] = new_thread;
			void* new_stack = malloc(3 * THREAD_MIN_STACK+16);
            new_stack =(void*)(((unsigned long) new_stack+16) &(unsigned long)(~0xF));
            new_stack = (void*)((unsigned long)new_stack - 8);
			if(new_stack == NULL)
			{
			  free(new_stack);
			  free(new_context);
			  free(new_thread);
              interrupts_set(interrupt_enabled);
			  return THREAD_NOMEMORY;
			}
			// Assign value to new context
			new_thread->m_pStack = new_stack;
			new_context->uc_stack.ss_sp = new_stack;
			new_context->uc_stack.ss_flags = 0;
			new_context->uc_stack.ss_size = 2 * THREAD_MIN_STACK;
			unsigned long int sp1 =(unsigned long int)(new_context->uc_stack.ss_sp);
			unsigned long sp = sp1 + new_context->uc_stack.ss_size;
			new_context->uc_mcontext.gregs[REG_RIP] = (long int)thread_stub;
			new_context->uc_mcontext.gregs[REG_RSP] = sp;
			new_context->uc_mcontext.gregs[REG_RDI] = (long int)fn;
			new_context->uc_mcontext.gregs[REG_RSI] = (long int)parg;
			// Allocating new stack for the thread
			Queue_push(thread_ReadyQueue,new_thread);
            interrupts_set(interrupt_enabled);
			return new_thread->m_iID;
		}
	}
	if(counter == THREAD_MAX_THREADS)	{
        interrupts_set(interrupt_enabled);
		return THREAD_NOMORE;
	}
    interrupts_set(interrupt_enabled);
	return new_thread->m_iID;
}

int thread_stackrealloc(thread* _thread)
{
    unsigned long current_sp = _thread->m_pContext->uc_mcontext.gregs[REG_RSP];
	unsigned long base_sp = (unsigned long)(_thread->m_pContext->uc_stack.ss_sp);
	unsigned long distance = current_sp-base_sp;
	// check if the stack is running out
	if(current_sp - base_sp  <= 512)
	{
		// Reallocating stack;
		unsigned long new_size = _thread->m_pContext->uc_stack.ss_size*2;
		void* new_stack = malloc(new_size);
        void* new_stack_p = (void*)(((unsigned long)new_stack + (unsigned long)(new_size >> 1)));
		memcpy(new_stack_p,_thread->m_pContext->uc_stack.ss_sp,new_size*0.5);
		current_sp = (unsigned long)(new_stack)+new_size*0.5+distance;
		_thread->m_pContext->uc_mcontext.gregs[REG_RSP] = current_sp; 
		_thread->m_pContext->uc_stack.ss_sp = new_stack;
		_thread->m_pContext->uc_stack.ss_size = new_size;
		return 1;
	}
	else
	{
		return 0;
	}
}

Tid
thread_yield(Tid want_tid)
{
    int interrupt_enabled = interrupts_set(0);
    //printf("Called Yield\n");
    assert(thread_ReadyQueue->m_pEndLinker->m_pNextLinker==NULL);
  	if(want_tid < THREAD_SELF)	
	{
        interrupts_set(interrupt_enabled);
		return THREAD_INVALID;
	}
	else
	{
		// Yield myself
		if(want_tid == THREAD_SELF)
		{
            interrupts_set(interrupt_enabled);
			return ((thread*)Queue_top(thread_ReadyQueue))->m_iID;
		}
		// Yield to anybody
		else if(want_tid == THREAD_ANY)
		{
			if(thread_ReadyQueue->m_iSize == 1)
			{
                interrupts_set(interrupt_enabled);
				return THREAD_NONE;
			}
			getcontext(((thread*)Queue_top(thread_ReadyQueue))->m_pContext);
			// Remember to update the current thread;
			thread* current_thread = (thread*)Queue_top(thread_ReadyQueue);
			if(current_thread->m_eThreadStatus == eThreadRun ||
                current_thread->m_eThreadStatus == eThreadNew)
			{
			    thread* current_thread = (thread*)Queue_top(thread_ReadyQueue);
				current_thread->m_eThreadStatus = eThreadIdle;
				Queue_pop(thread_ReadyQueue);
				Queue_push(thread_ReadyQueue,current_thread);
				thread* next_thread = (thread*)Queue_top(thread_ReadyQueue);
				int thread_ID = next_thread->m_iID;
                //next_thread->m_eThreadStatus = eThreadRun;
                //printf("Yield from %d to %d\n",current_thread->m_iID,next_thread->m_iID);
               	setcontext(next_thread->m_pContext);
                interrupts_set(interrupt_enabled);
				return thread_ID;
			}
			else
			{
				current_thread->m_eThreadStatus = eThreadRun;
                interrupts_set(interrupt_enabled);
				return current_thread->m_iID;
			} 
		}
		// Yield to specific ID
		else
		{	
			// TODO: Fix this funciton
			if(want_tid == ((thread*)Queue_top(thread_ReadyQueue))->m_iID)
			{
                interrupts_set(interrupt_enabled);
			    return want_tid;
			}
			getcontext(((thread*)Queue_top(thread_ReadyQueue))->m_pContext);
            assert(!interrupts_enabled());
			thread* current_thread = (thread*)Queue_top(thread_ReadyQueue);
			if(current_thread->m_eThreadStatus == eThreadRun ||
                current_thread->m_eThreadStatus == eThreadNew)
			{
				// Need to find the right context
				Single_Linker* temp;
				int position = 0;
				thread* want_thread = NULL;
				//printf("NewSearch\n");
				for(temp = thread_ReadyQueue->m_pStartLinker; temp!=NULL; temp = temp->m_pNextLinker)
				{
				  //printf("ID: %d\n",((thread*)temp->m_pPointer)->m_iID);
					if(((thread*)(temp->m_pPointer))->m_iID == want_tid)
					{
						want_thread = temp->m_pPointer;
						break;
					}
					position++;
				}	     
				if(want_thread!=NULL)
				{
				    thread* current_thread = Queue_top(thread_ReadyQueue);
					current_thread->m_eThreadStatus = eThreadIdle;
					Queue_pop(thread_ReadyQueue);
					Queue_movetop(thread_ReadyQueue,position-1);
					assert(thread_ReadyQueue->m_pEndLinker->m_pNextLinker==NULL);
					Queue_push(thread_ReadyQueue,current_thread);
                    //want_thread-> m_eThreadStatus = eThreadRun;
					setcontext(want_thread->m_pContext);
                    interrupts_set(interrupt_enabled);
					return want_thread->m_iID;
				}
				else
				{
                    interrupts_set(interrupt_enabled);
					return THREAD_INVALID;
				}
								
			}
			else
			{
				current_thread->m_eThreadStatus = eThreadRun;
                interrupts_set(interrupt_enabled);
				return want_tid;
			}
		}
	}
}


Tid
thread_exit(Tid tid)
{
    int interrupt_enabled = interrupts_set(0);
	if(tid>=0)
	{
	      thread* current_thread = (thread*)Queue_top(thread_ReadyQueue);
	      int return_id;
		// TODO
		// Find the corresponding thread and exit it
		Single_Linker* prior_linker = NULL;
		Single_Linker* current_linker = thread_ReadyQueue->m_pStartLinker;
		thread* exit_thread = NULL;
		for(;current_linker!=NULL;)
		{
			thread* temp_thread = (thread*)(current_linker->m_pPointer);
			if(temp_thread->m_iID == tid)
			{
				exit_thread = temp_thread;
				break;
			}
			prior_linker = current_linker;
			current_linker = current_linker->m_pNextLinker;
		}
		if(exit_thread == NULL)
		{
            interrupts_set(interrupt_enabled);
		    return THREAD_INVALID;
		}
		if(current_linker == thread_ReadyQueue->m_pStartLinker)
		{
			thread_ReadyQueue->m_pStartLinker = current_linker->m_pNextLinker;
			
		}
		else if(current_linker == thread_ReadyQueue->m_pEndLinker)
		{
			thread_ReadyQueue->m_pEndLinker = prior_linker;
			prior_linker->m_pNextLinker = NULL;
		}
		else
		{
			prior_linker->m_pNextLinker = current_linker->m_pNextLinker;
		}
		free(current_linker);
		thread_ReadyQueue_index[exit_thread->m_iID]=NULL;
		thread_ReadyQueue->m_iSize--;
		// if thread is the current thread, push it to the exit queue
		if(exit_thread->m_iID == current_thread->m_iID)
		{
		  return_id = exit_thread->m_iID;
			Queue_push(thread_ExitQueue,exit_thread);
		}
		else
		{
		  return_id = exit_thread->m_iID;
			free(exit_thread->m_pStack);
		    free(exit_thread->m_pContext);
			free(exit_thread);
		}
        interrupts_set(interrupt_enabled);
		return return_id;
	}
	else
	{
		
		// put the last thread into the exit queue
		if(tid == THREAD_ANY)
		{
		  if(thread_ReadyQueue->m_iSize == 1)
          {
              interrupts_set(interrupt_enabled);
		      return THREAD_NONE;
          }
			Single_Linker* prior_linker = NULL;
			Single_Linker* current_linker = thread_ReadyQueue->m_pStartLinker;
			for(;current_linker->m_pNextLinker!=NULL;)
			{
				prior_linker = current_linker;
				current_linker = current_linker->m_pNextLinker;
			}
			// remove the thread in the ready queue
			prior_linker->m_pNextLinker = NULL;
			thread_ReadyQueue->m_pEndLinker = prior_linker;
			thread_ReadyQueue->m_iSize--;
			thread* exit_thread = (thread*)(current_linker->m_pPointer);
			thread_ReadyQueue_index[exit_thread->m_iID] = NULL; 
			Queue_push(thread_ExitQueue, current_linker->m_pPointer);
			free(current_linker);
            interrupts_set(interrupt_enabled);
		    return exit_thread->m_iID;
		}
		// exit itself
		if(tid == THREAD_SELF)
		{
			thread* current_thread = Queue_top(thread_ReadyQueue);
			// final main release
			if(thread_ReadyQueue->m_iSize == 1 && current_thread->m_iID == 0)
			{
				//clear up the exit queue
				for(;thread_ReadyQueue->m_iSize!=0;)
				{
					thread* current_thread = Queue_top(thread_ExitQueue);
					free(current_thread->m_pStack);
					free(current_thread->m_pContext);
					free(current_thread);
					Queue_pop(thread_ExitQueue);
				}
				Queue_free(thread_ExitQueue);
			}
			else if(thread_ReadyQueue->m_iSize ==1)
			{
			  thread* current_thread = Queue_top(thread_ReadyQueue);
			  Queue_free(thread_ExitQueue);
			  free(current_thread->m_pContext);
			  free(current_thread->m_pStack);
			  free(current_thread);
              interrupts_set(interrupt_enabled);
			  return THREAD_NONE;
			}
			else
			{	
				thread* current_thread = Queue_top(thread_ReadyQueue);
				int destroyed_id = current_thread->m_iID;
				thread_ReadyQueue_index[current_thread->m_iID] = NULL; 
				Queue_push(thread_ExitQueue,current_thread);
				Queue_pop(thread_ReadyQueue);	
				current_thread = Queue_top(thread_ReadyQueue);
				setcontext(current_thread->m_pContext);
                interrupts_set(interrupt_enabled);
				return destroyed_id;
			}
			
		}
		else
		{
            interrupts_set(interrupt_enabled);
			return THREAD_INVALID;
		}
	}
    interrupts_set(interrupt_enabled);
	return THREAD_FAILED;
}

int thread_free(thread* _thread)
{
    if(_thread == NULL)
        return 0;
    free(_thread->m_pContext);
    if(_thread->m_pStack != NULL)
        free(_thread->m_pStack);
    free(_thread);
    return 1;
}
/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* This is the wait queue structure */
/* use queue as a underlying structure */
struct wait_queue {
    Queue* _wait_queue;
};

typedef struct wait_queue wait_queue;

struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;
	wq = malloc(sizeof(struct wait_queue));
    wq->_wait_queue = Queue_new();
	assert(wq);
	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	thread* top;
    for(;wq->_wait_queue->m_iSize!=0;)
    {
        top =(thread*)(Queue_top(wq->_wait_queue));
        thread_free(top);
        Queue_pop(wq->_wait_queue);
    }
    free(wq->_wait_queue);
	free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
  // printf("called sleep\n");
    int interrupt_enabled = interrupts_set(0);
    if(queue == NULL)
    {
        interrupts_set(interrupt_enabled);
        return THREAD_INVALID;
    }
    if(thread_ReadyQueue->m_iSize == 1)
    {
        interrupts_set(interrupt_enabled);
        return THREAD_NONE;
    }
    assert(queue);
    Single_Linker* tl = thread_ReadyQueue->m_pStartLinker->m_pNextLinker;
    thread* control_thread = (thread*)(tl->m_pPointer);
    int control_id = control_thread->m_iID;
    thread* current_thread = (thread*)(Queue_top(thread_ReadyQueue));
    //unintr_printf("control: %d Current thread: %d ",control_id,current_thread->m_iID);
    getcontext(((thread*)(Queue_top(thread_ReadyQueue)))->m_pContext);
    current_thread = (thread*)(Queue_top(thread_ReadyQueue));
    if(current_thread->m_eThreadStatus == eThreadRun ||
        current_thread->m_eThreadStatus == eThreadNew)
    {
        Queue_pop(thread_ReadyQueue);
        current_thread -> m_eThreadStatus = eThreadSleep;
        Queue_push(queue->_wait_queue,current_thread);
        thread* next_thread = (thread*)(Queue_top(thread_ReadyQueue));
        //next_thread -> m_eThreadStatus = eThreadRun;
        // unintr_printf("Next thread: %d\n",next_thread->m_iID);
        setcontext(next_thread->m_pContext);
    }
    else
    {
        if(current_thread -> m_eThreadStatus == eThreadIdle)
            current_thread -> m_eThreadStatus = eThreadRun;
    }
    interrupts_set(interrupt_enabled);
    return control_id;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
    int interrupt_enabled = interrupts_set(0);
    if(queue == NULL || queue->_wait_queue->m_iSize ==0)
    {
        interrupts_set(interrupt_enabled);
        return 0;
    }
    if(all == 1)
    {
        Queue* __wait_queue = queue->_wait_queue;
        int size = __wait_queue->m_iSize;
        for(;__wait_queue->m_iSize!=0;)
        {
            thread* _thread = (thread*)(Queue_top(__wait_queue));
            _thread->m_eThreadStatus = eThreadIdle;
            Queue_pop(__wait_queue);
            Queue_push(thread_ReadyQueue,_thread);
        }
        interrupts_set(interrupt_enabled);
        //printf("Lol\n");
        return size;
    }
    else
    {
        Queue* __wait_queue = queue->_wait_queue;
        thread* _thread = (thread*)(Queue_top(__wait_queue));
        _thread->m_eThreadStatus = eThreadIdle;
        Queue_pop(__wait_queue);
        Queue_push(thread_ReadyQueue,_thread);
        //printf("lOL\n");
        interrupts_set(interrupt_enabled);
        return 1;
    }
}

struct lock {
    thread* m_pLockedThread;
    eMutexStatus m_eStatus;
    wait_queue* m_pQueue;
};

struct lock *
lock_create()
{
    int interrupt_enabled = interrupts_set(0);
	struct lock *lock;
	lock = malloc(sizeof(struct lock));
    lock->m_pQueue = wait_queue_create();
    lock->m_eStatus = eMutexFree;
    lock->m_pLockedThread = NULL;
	assert(lock);
    interrupts_set(interrupt_enabled);
	return lock;
}

void
lock_destroy(struct lock *lock)
{
    int interrupt_enabled = interrupts_set(0);
	wait_queue_destroy(lock->m_pQueue);
	free(lock);
    interrupts_set(interrupt_enabled);
}

void
lock_acquire(struct lock *lock)
{
	int interrupt_enabled = interrupts_set(0);
    for(;lock->m_eStatus!= eMutexFree;)
    {
        thread_sleep(lock->m_pQueue);
    }
    lock-> m_pLockedThread = (thread*)(Queue_top(thread_ReadyQueue));
    lock-> m_eStatus = eMutexLocked;
    interrupts_set(interrupt_enabled);
}

void
lock_release(struct lock *lock)
{
	int interrupt_enabled = interrupts_set(0);
    lock->m_eStatus = eMutexFree;
    thread_wakeup(lock->m_pQueue,1);
    lock->m_pLockedThread = NULL;
    interrupts_set(interrupt_enabled);
}

struct cv {
    thread* m_pCurrentThread;
    wait_queue* m_pQueue;
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
    cv->m_pQueue = wait_queue_create();
	assert(cv);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);
    wait_queue_destroy(cv->m_pQueue);
	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
    int interrupt_enabled = interrupts_set(0);
	assert(cv != NULL);
	assert(lock != NULL);
    thread* current_thread = (thread*)(Queue_top(thread_ReadyQueue));
    if(lock->m_pLockedThread == current_thread&& lock->m_eStatus == eMutexLocked)
    {
        lock_release(lock);
        thread_sleep(cv->m_pQueue);
        lock_acquire(lock);
    }
    interrupts_set(interrupt_enabled);
    return;
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
    int interrupt_enabled = interrupts_set(0);
	assert(cv != NULL);
	assert(lock != NULL);
    thread* current_thread = (thread*)(Queue_top(thread_ReadyQueue));
    if(lock->m_pLockedThread == current_thread && lock->m_eStatus == eMutexLocked)
        thread_wakeup(cv->m_pQueue,0);
    interrupts_set(interrupt_enabled);
    return;
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
    int interrupt_enabled = interrupts_set(0);
	assert(cv != NULL);
	assert(lock != NULL);
    thread* current_thread = (thread*)(Queue_top(thread_ReadyQueue));
    if(lock->m_pLockedThread == current_thread && lock->m_eStatus == eMutexLocked)
        thread_wakeup(cv->m_pQueue,1);
    interrupts_set(interrupt_enabled);
    return;
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

