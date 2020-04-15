#ifdef __VOS__
#define _POSIX_C_SOURCE 200112L
#include <sys/time.h>
#include <pthread.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <sys/timeb.h>
	#define strcasecmp stricmp
#else
	#include <unistd.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <signal.h>
	#include <sys/time.h>
	#include <netdb.h>
	#include <errno.h>
	#if defined(__TANDEM)
		#include <sys/sem.h>
		#include <strings.h>
		#if defined(HAVE_TANDEM_SPT)
			#include <ktdmtyp.h>
			#include <spthread.h>
		#else
			#include <pthread.h>
		#endif
	#else
		#include <pthread.h>
		#if defined(__VMS)
			#include ppl$routines
		#else
			#include <semaphore.h>
		#endif
	#endif
#endif

#include <lbm/lbm.h>
#include "umeblocksrc.h"

/* Sequence # defines */
#define SEQ_SQN_LT(a,b) ((int)((a)-(b)) < 0)
#define SEQ_SQN_LTE(a,b) ((int)((a)-(b)) <= 0)
#define SEQ_SQN_GT(a,b) ((int)((b)-(a)) < 0)
#define SEQ_SQN_GTE(a,b) ((int)((b)-(a)) <= 0)
/* is a<=b<=c */
#define SEQ_SQN_BETWEEN(a,b,c) ((int)((c)-(b))>=(int)((a)-(b)))
/* which is min between 2 sqns */
#define SEQ_SQN_MIN(a,b) (SEQ_SQN_LT((a),(b)) ? (a) : (b))
#define SEQ_SQN_MAX(a,b) (SEQ_SQN_GT((a),(b)) ? (a) : (b))
#define SEQ_WRAP_DIFF(a,b) ((UINT_MAX - a) + b)
#define UME_DIFF_SEQ(s) ((SEQ_SQN_LT(s->last,s->first))? (SEQ_WRAP_DIFF(s->last,s->first)) : (s->last - s->first))

#define UME_NORMALIZE_MAP(a,s) (s - (a->first))

#define UME_CHAR_BITSIZE (sizeof(char)<<3)
#define UME_COUNT_WORDS(s) (((int)(s-1) >= 0)? ((s-1)/UME_CHAR_BITSIZE) + 1 : 0)

struct ume_block_bitmap_t_stct
{
	char *map;
	int words;
};

/**** Bitmap code ****/
void ume_block_src_bitmap_init(ume_block_bitmap_t *bitmap, unsigned int words)
{
	/* Dont allocate */
	if(!bitmap || bitmap->words >= words)
		return;
	
	if(!(bitmap->map))
	{
		bitmap->map   = (char*) malloc(sizeof(char) * words);
		bitmap->words = words;
	}
	else
	{
		bitmap->map   = (char*) realloc((void*) bitmap->map, sizeof(char) * words);
		bitmap->words = words;
	}
}

void ume_block_src_bitmap_free(ume_block_bitmap_t *bitmap)
{
	free(bitmap->map);
	bitmap->map   = NULL;
	bitmap->words = -1;
}

int ume_block_src_bitmap_check(ume_block_bitmap_t *bitmap, unsigned int check)
/* We do zero based comparisons */
{
	int words;
	int i;

	/* Initialize some temporary variables */
	words = ((check-1) / UME_CHAR_BITSIZE) + 1;

	for(i=0; i<words; i++)
	{
		if(bitmap->map[i] != 0)
			return 0;
	}

	return 1;
}

void ume_block_src_bitmap_clear(ume_block_bitmap_t *bitmap, unsigned int comparable)
{
	const char on  = ~0;
	const char map = on;
	int wordsize;
	int idx;
	int i;

	/* Set all comparable bits to 1 */
	for(i=0; i<bitmap->words; i++)
		bitmap->map[i] = on;
	
	/* Set non comparable bits to 0 */
	wordsize = UME_CHAR_BITSIZE;
	idx = comparable/wordsize;
	i   = ((idx)? comparable % wordsize : comparable);
	
	if(i)
	{
		i = (wordsize - i);

		bitmap->map[idx] &= ~(map<<i);
		bitmap->map[idx] = ~(bitmap->map[idx]);
	}
}

void ume_block_src_bitmap_set(ume_block_bitmap_t *bitmap, unsigned int val)
{
	unsigned int wordsize;
	unsigned int pos;
	unsigned int map;
	unsigned int mod;

	if(!bitmap)
		return;
	
	/* Initialize the variable */
	wordsize = UME_CHAR_BITSIZE;
	pos  = val/wordsize;
	map  = 0x1;

	if(pos > bitmap->words)
		return;

	mod = (val % wordsize);
	map = ((mod > 0)? (map<<(wordsize-mod-1)) : (map<<(wordsize-1)));

	/* Invert bits */
	map = ~map;

	bitmap->map[pos] &= map;
}

void ume_block_src_bitmap_print(ume_block_bitmap_t *bitmap)
{
	int i;
	
	printf("bitmap dump:\n\t");

	for(i=0; i<bitmap->words; i++)
		printf("%#04hx ", bitmap->map[i]);
	
	printf("\nend bitmap\n");
}
/**** End Bitmap Code ****/

int ume_block_src_callback(lbm_src_t *src, int event, void *ed, void *cd)
{
	ume_block_src_t *asrc;
	asrc = (ume_block_src_t*) cd;

	switch(event)
	{
		case LBM_SRC_EVENT_UME_REGISTRATION_ERROR:
		{
			const char *errstr = (const char *)ed;
			
			printf("Error registering source with UME store: %s\n", errstr);
		}
			break;
		case LBM_SRC_EVENT_UME_MESSAGE_STABLE:
		{
			lbm_src_event_ume_ack_info_t *sinfo = (lbm_src_event_ume_ack_info_t *) ed;

			ume_block_src_bitmap_set(asrc->bitmap, UME_NORMALIZE_MAP(asrc, sinfo->sequence_number));
			if(ume_block_src_bitmap_check(asrc->bitmap, (UME_DIFF_SEQ(asrc))+1))
			{
				UME_SEM_POST(asrc->stablelock);
			}

			break;
		}

		case LBM_SRC_EVENT_UME_MESSAGE_STABLE_EX:
		{
			lbm_src_event_ume_ack_ex_info_t *info = (lbm_src_event_ume_ack_ex_info_t*) ed;

			/* Check if this is a stable event message */
			if(info->flags & LBM_SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STABLE)
			{
				ume_block_src_bitmap_set(asrc->bitmap, UME_NORMALIZE_MAP(asrc, info->sequence_number));
				if(ume_block_src_bitmap_check(asrc->bitmap, (UME_DIFF_SEQ(asrc))+1))
				{
					UME_SEM_POST(asrc->stablelock);
				}
			}

			break;
		}

		case LBM_SRC_EVENT_SEQUENCE_NUMBER_INFO:
		{
			lbm_src_event_sequence_number_info_t *info = (lbm_src_event_sequence_number_info_t*) ed;
			unsigned int seq=0;

			asrc->last  = info->last_sequence_number;
			asrc->first = info->first_sequence_number;
			seq = UME_DIFF_SEQ(asrc)+1;

			ume_block_src_bitmap_init(asrc->bitmap, UME_COUNT_WORDS(seq));      /* Resize bitmap if needed */
			ume_block_src_bitmap_clear(asrc->bitmap, seq);                      /* Clear the bitmap */

			break;
		}
		
		case LBM_SRC_EVENT_UME_STORE_UNRESPONSIVE:
		{
			int semval=0;
			
			UME_SEM_GETVALUE(asrc->stablelock, semval);
			
			if(semval == 0)
			{
				/* Just log it and continue waiting */
				UME_BLOCK_PRINT_ERROR("Warning: store unresponsive while waiting for stability event%c", '.');
			}

			break;
		}

		case LBM_SRC_EVENT_UME_MESSAGE_RECLAIMED:
			/* Dont handle it */
			break;
		default: 		/* Send default msgs to main application */
			break;
	}

	if(asrc->appproc)
		asrc->appproc(src, event, ed, asrc->clientd);
	
	return 0;
}

int ume_block_src_create(ume_block_src_t **srcp, lbm_context_t *ctx, lbm_topic_t *topic, lbm_src_topic_attr_t *tattr, lbm_src_cb_proc proc, void *clientd, lbm_event_queue_t *evq)
{
	size_t optlen = sizeof(size_t);
	ume_block_src_t *src;
	int ret = 0;

	/* Set the application event callback function */
	(*srcp) = NULL;

	/* Create src_aux mutex */
	UME_MALLOC_RETURN(src, sizeof(ume_block_src_t), -1);
	UME_SEM_INIT(src->stablelock, 1, ret);

	src->src     = NULL;
	src->appproc = proc;
	src->clientd = clientd;
	src->err     = LBM_OK;
	src->first   = 0;
	src->last    = 0;

	/* Create bitmap and memset to null */
	src->bitmap = (ume_block_bitmap_t*) malloc(sizeof(ume_block_bitmap_t));
	memset((void*) (src->bitmap), 0, sizeof(ume_block_bitmap_t));

	/* Set retention size */
	lbm_src_topic_attr_getopt(tattr, "retransmit_retention_size_limit", &(src->maxretentionsz), &optlen);
	(*srcp)  = src;

	ret = lbm_src_create(&((*srcp)->src), ctx, topic, ume_block_src_callback, (void*) src, evq);

	return ret;
}

int ume_block_src_delete(ume_block_src_t *asrc)
{

	if((lbm_src_delete(asrc->src)) < 0)
		return -1;

	UME_SEM_DESTROY(asrc->stablelock);

	free(asrc);
	
	return 0;
}

int ume_block_src_send_ex(ume_block_src_t *asrc, const char *msg, size_t len, int flags, lbm_src_send_ex_info_t *info)
{
	lbm_src_send_ex_info_t tinfo;
	int retry=0;
	int ret=0;
	int err=0;

	if(asrc == NULL)
	{
		return LBM_FAILURE;
	}

	/* Make sure the message to be sent is not bigger than retention_buffer_size*/
	if(len > asrc->maxretentionsz)
	{
		return LBM_FAILURE;
	}

	asrc->err = LBM_OK;
	
	/* Set callback flags */
	memset(&tinfo, 0, sizeof(tinfo));
	tinfo.flags |= LBM_SRC_SEND_EX_FLAG_SEQUENCE_NUMBER_INFO | LBM_SRC_SEND_EX_FLAG_UME_CLIENTD;

	if(info)
	{
		tinfo.flags |= info->flags;
		tinfo.ume_msg_clientd = info->ume_msg_clientd; 
	}

	/* Clear out LBM_SRC_NONBLOCK */
	flags &= ~(LBM_SRC_NONBLOCK);

	/* Send the message */
	while((ret = lbm_src_send_ex(asrc->src, msg, len, LBM_SRC_BLOCK | LBM_MSG_FLUSH | flags, &tinfo)) == LBM_FAILURE)
	{
		err = lbm_errnum();
		switch(err)
		{
			case LBM_EUMENOREG: /* Not registered, wait a few and retry sending the message */
				SLEEP_MSEC(100);
				retry++;
				
				if(retry == UME_RETRY_COUNT)
				{
					UME_BLOCK_PRINT_ERROR("Tried registering to a store %d times. Giving up block.", UME_RETRY_COUNT);
					return LBM_FAILURE;
				}
				break;
			
			default:
				fprintf(stderr, "hit default: %d\n", err);
				/* UME_SEM_POST(asrc->stablelock); */
				return ret;
		}
	}

	/* Gain a lock on stable */
	UME_SEM_TIMEDWAIT(asrc->stablelock, UME_TIME_OUT, ret);
	while(!UME_SEM_TIMEDOK(ret) && UME_SEM_TIMEDOUT(errno))
	{
		UME_BLOCK_PRINT_ERROR("Warning: Waited %d seconds. Timeout hit.", (UME_TIME_OUT/1000));
		UME_SEM_TIMEDWAIT(asrc->stablelock, UME_TIME_OUT, ret);
	}

	/* Assign return value */
	if(!ret && asrc->err)
		ret = asrc->err;

	return ret;
}

