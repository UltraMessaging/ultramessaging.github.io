/**	\file umeblocksrc.h
	\brief UME Blocking API

	The Ultra Messaging Enterprise (UME) API Description.
*/
#ifndef LBMAUX_H
#define LBMAUX_H

#if defined(_WIN32)
	#define SLEEP_SEC(x) Sleep((x)*1000)
	#define SLEEP_MSEC(x) Sleep(x)
#else
#   define SLEEP_SEC(x) sleep(x)
#   define SLEEP_MSEC(x) \
		do{ \
			if ((x) >= 1000){ \
				sleep((x) / 1000); \
				usleep((x) % 1000 * 1000); \
			} \
			else{ \
				usleep((x)*1000); \
			} \
		}while (0)
#endif /* _WIN32 */

#define UME_BLOCK_DEBUG 0
#define UME_BLOCK_DEBUG_PRINT(t, ...) do { \
	if(UME_BLOCK_DEBUG) { \
		fprintf(stderr, t, ##__VA_ARGS__); \
		fprintf(stderr, "\n"); \
	} \
} while(0)

#define UME_BLOCK_PRINT_ERROR(t, ...) do { \
	fprintf(stderr, t, ##__VA_ARGS__); \
	fprintf(stderr, "\n"); \
	} while(0)
	

/* Semaphore Helper macros */
#if defined(_WIN32) /* Win32*/
	#define UME_BLOCKING_TYPE "Win32 Semaphore"
	typedef struct ume_sem_t_stct
	{
		HANDLE hSem;
		int max;
		int state;
	} ume_sem_t;

	#define UME_SEM_INIT(sem,len,ret) \
		do { \
			ret=0; \
			sem.hSem  = CreateSemaphore(NULL, len, len, NULL); \
			sem.max   = len; \
			sem.state = len; \
			if(!sem.hSem) { ret = -1; } \
		} while(0)

	#define UME_SEM_DESTROY(sem)    CloseHandle(sem.hSem)
	#define UME_SEM_POST(sem) \
		do { \
			ReleaseSemaphore(sem.hSem, 1, NULL); \
			InterlockedIncrement(&(sem.state)); \
		} while(0)
		
	#define UME_SEM_WAIT(sem) \
	do { \
		WaitForSingleObject(sem.hSem, INFINITE); \
		InterlockedDecrement(&(sem.state)); \
	} while(0)

	#define UME_SEM_GETVALUE(sem,val)            val=sem.state;

	#define UME_SEM_TIMEDWAIT(sem,time,ret) \
		do { \
			ret=WaitForSingleObject(sem.hSem, time); \
			if(ret!=WAIT_TIMEOUT && ret==WAIT_OBJECT_0) { InterlockedDecrement(&(sem.state)); } \
		} while(0)
		
	#define UME_SEM_TIMEDOUT(v)           v == WAIT_TIMEOUT
	#define UME_SEM_TIMEDOK(v)            v == WAIT_OBJECT_0

/* #elif defined(__linux) */ /* POSIX Linux Systems */
#elif defined(__linux)   /* POSIX Linux Systems */
	#define UME_BLOCKING_TYPE "Posix Semaphore"
	typedef sem_t ume_sem_t;

	#define UME_SEM_INIT(sem, len, ret)   ret = sem_init(&sem, 0, 0)
	#define UME_SEM_POST(sem)             sem_post(&sem)
	#define UME_SEM_WAIT(sem) \
		do { \
			while(sem_wait(&sem) < 0) {} \
		} while(0)
	
	#define UME_SEM_GETVALUE(sem, value)     sem_getvalue(&(sem), &(value))
	#define UME_SEM_DESTROY(sem)             sem_destroy(&sem)

	/* Trick: Both timespec and timeval are 16 bytes long, use timespec for
	   gettimeofday and multiply timespec.tv_nsec by 1000 to convert to ns. */
	#define UME_SEM_TIMEDWAIT(sem,mstime,ret) \
		do { \
			struct timespec ts; \
			gettimeofday((struct timeval*) &ts, NULL); \
			ts.tv_sec  += (mstime/1000); \
			ts.tv_nsec  = (ts.tv_nsec*1000) + ((mstime%1000)*1000000); \
			ret = sem_timedwait(&sem, &ts); \
		} while(0)

	#define UME_SEM_TIMEDOUT(v)              v == ETIMEDOUT
	#define UME_SEM_TIMEDOK(v)               v == 0

#else /* Unix type systems without sem_timedwait */
	#define UME_BLOCKING_TYPE "Posix pthread_cond"
	typedef struct ume_sem_t_stct
	{
		pthread_mutex_t mtx;
		pthread_cond_t cond;
		int max;
		int state;
	} ume_sem_t;

	/* TODO: Error out if unable to gain mutex init or cond init*/
	#define UME_SEM_INIT(sem,len,ret) \
		do { \
			pthread_mutex_init(&(sem.mtx), NULL); \
			pthread_cond_init(&(sem.cond), NULL); \
			sem.max = len; \
			sem.state = len; \
		} while(0)

	#define UME_SEM_POST(sem) \
		do { \
			pthread_mutex_lock(&(sem.mtx)); \
			if(sem.state < sem.max) { sem.state++; } \
			pthread_cond_broadcast(&(sem.cond)); \
			pthread_mutex_unlock(&(sem.mtx)); \
		} while(0) 
		
	#define UME_SEM_WAIT(sem) \
		do { \
			pthread_mutex_lock(&(sem.mtx)); \
			while(sem.state == 0) { \
				pthread_cond_wait(&(sem.cond), &(sem.mtx)); } \
			sem.state--; \
			pthread_mutex_unlock(&(sem.mtx)); \
		} while(0)

	#define UME_SEM_GETVALUE(sem, value)  value = sem.state
	
	#define UME_SEM_DESTROY(sem) \
		do { \
			pthread_cond_destroy(&(sem.cond)); \
			pthread_mutex_destroy(&(sem.mtx)); \
		} while (0)

	/* Trick: Both timespec and timeval are 16 bytes long, use timespec for
	   gettimeofday and multiply timespec.tv_nsec by 1000 to convert to ns. */
	#define UME_SEM_TIMEDWAIT(sem, mstime, ret) \
	do { \
		struct timespec ts; \
		gettimeofday((struct timeval*) &ts, NULL); \
		ts.tv_sec  += (mstime/1000); \
		ts.tv_nsec  = (ts.tv_nsec*1000) + ((mstime%1000)*1000000); \
		pthread_mutex_lock(&(sem.mtx)); \
		ret = pthread_cond_timedwait(&(sem.cond), &(sem.mtx), &ts); \
		if(ret == 0) { sem.state--; } \
		pthread_mutex_unlock(&(sem.mtx)); \
	} while (0)

	#define UME_SEM_TIMEDOUT(v)           0
	#define UME_SEM_TIMEDOK(v)            0
	#define UME_TIMESPEC_MSSET(t,s,n)     0
#endif

#define UME_MALLOC_RETURN(e,s,r) do { \
	if((e = malloc(s)) == NULL) { \
		return r; \
	} \
} while(0)

#define UME_TIME_OUT 5000
#define UME_RETRY_COUNT 10

/*! \brief Structure that holds a bitmap for sequence numbers (opaque). */
struct ume_block_bitmap_t_stct;
typedef struct ume_block_bitmap_t_stct ume_block_bitmap_t;

/*! \brief Structure used to designate an UME Block source. */
typedef struct ume_block_src_t_stct
{
	lbm_src_t *src;                      /* Pointer to the actual LBM Source */
	lbm_src_cb_proc appproc;             /* Callback function for event callbacks */
	ume_sem_t stablelock;                /* Locking mechanism for blocking */
	ume_block_bitmap_t *bitmap;          /* Stability check mechanism */
	void *clientd;                       /* Client provided object */
	int maxretentionsz;                  /* Maximum retention buffer size */
	/* int seq; */
	int err;                             /* Error holder */
	unsigned int last;                   /* Last sequence number */
	unsigned int first;                  /* First sequence number */
} ume_block_src_t;


/*! \brief Delete an UMEBlock Source object
    \param asrc Pointero to an UMEBlock Source object to delete.
    \return 0 for Success and -1 for Failure.
*/
int ume_block_src_delete(ume_block_src_t *asrc);

/*! \brief Create an UMEBlock Source that will send messages to a given topic.
    \param srcp A pointer to a pointer to a UMEBlock source object. Will be filled
		            in by this function to point to a newly created ume_block_src_t object.
		\param ctx Pointer to the LBM context object associated with the sender.
		\param topic Pointer to the LBM topic object associated with the destination
		            of messages sent by the source.
    \param tattr Pointer to an LBM topic attribute object. The passed object CANNOT be NULL.
		\param proc Pointer to a function to call when events occur related to the source.
                If NULL, then events are not delivered to the source.
    \param clientd Pointer to tclient data that is passed when \a proc is called.
		\param evq Optional Event Queue to place events on when they occur.
		            If NULL causes \a proc to be called from context thread.
    \return 0 for Success and -1 for Failure.
*/
int ume_block_src_create(ume_block_src_t **srcp, lbm_context_t *ctx, lbm_topic_t *topic, lbm_src_topic_attr_t *tattr, lbm_src_cb_proc proc, void *clientd, lbm_event_queue_t *evq);

/*! \brief Extended send of a message to the topic associated with an UMBlock source.
    \param asrc Pointer to the UMBlock source to send from.
		\param msg Pointer to the data to send in this message.
		\param len Length (in bytes) of the data to send in this message.
		\param info Pointer to lbm_src_send_ex_info_t options.
    \return 0 for Success and -1 for Failure.
*/
int ume_block_src_send_ex(ume_block_src_t *asrc, const char *msg, size_t len, int flags, lbm_src_send_ex_info_t *info);
#endif

