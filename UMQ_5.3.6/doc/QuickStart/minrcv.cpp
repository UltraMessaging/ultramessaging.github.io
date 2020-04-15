/*file: minrcv.cpp - minimal receiver program for C++
 *
 * Copyright (c) 2005-2014 Informatica Corporation. All Rights Reserved.
 * Permission is granted to licensees to use
 * or alter this software for any purpose, including commercial applications,
 * according to the terms laid out in the Software License Agreement.
 -
 - This source code example is provided by Informatica for educational
 - and evaluation purposes only.
 -
 - THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL WARRANTIES 
 - EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY IMPLIED WARRANTIES OF 
 - NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR 
 - PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE OF THE SOFTWARE WILL BE 
 - UNINTERRUPTED OR ERROR-FREE.  INFORMATICA SHALL NOT, UNDER ANY CIRCUMSTANCES, BE 
 - LIABLE TO LICENSEE FOR LOST PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR 
 - INDIRECT DAMAGES ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE 
 - TRANSACTIONS CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF 
 - THE LIKELIHOOD OF SUCH DAMAGES.
 *
 * This source code is offered in addition to the "minrcv.c" program to
 * demonstrate how LBM callbacks work in an object-oriented C++ environment.
 * The reader is assumed to have basic knowledge of LBM usage.  See the
 * "minrcv.c" program for introductory information on LBM usage.
 */

#include <stdio.h>

#if defined(_MSC_VER)
/* Windows-only includes */
#include <winsock2.h>
#define SLEEP(s) Sleep((s)*1000)
#else
/* Unix-only includes */
#include <stdlib.h>
#include <unistd.h>
#define SLEEP(s) sleep(s)
#endif

#include <lbm/lbm.h>

/* The class definition for "Minrcv" would normally be contained in an include
 * file.  It is here for simplicity.  Notice that "rcv_cb()" is declared
 * static.  This is necessary (as explained below).  */
class Minrcv {
public:
    Minrcv();
    ~Minrcv();
    static int rcv_cb(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd);
    int rcv_method(lbm_rcv_t *rcv, lbm_msg_t *msg);
};


/*
 * A global variable is used to communicate from the receiver callback to
 * the main application thread.
 */
int msgs_rcvd = 0;


/* constructor */
Minrcv::Minrcv()
{}


/* destructor */
Minrcv::~Minrcv()
{}


/* This is a static member function, callable from standard C code.  The caller
 * must supply an instance of the class as the client data pointer.  Note that
 * the "static" must be in the class definition (above).  This function's
 * parameters are specified in the API document (search for lbm_rcv_cb_proc).
 * The purpose of this function is simply to call the normal (non-static)
 * message receive method. */
int Minrcv::rcv_cb(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
    Minrcv *my_obj = (Minrcv *)clientd;  /* must cast clientd back to class */

    /* This function does not have access to instance-specific data.  Call
     * the normal (non-static) member function to do the "real" work. */

    return my_obj->rcv_method(rcv, msg);
}  /* rcv_cb */


/* This is a normal (non-static) member funciton.  It cannot be directly
 * called from standard C code.  It does the "real" work.  */
int Minrcv::rcv_method(lbm_rcv_t *rcv, lbm_msg_t *msg)
{
    switch (msg->type) {
        case LBM_MSG_DATA:    /* a received message */
            printf("Received %d bytes on topic %s: '%.*s'\n",
                   msg->len, msg->topic_name, msg->len, msg->data);

            /* Tell main thread that we've received our message. */
            ++ msgs_rcvd;
            break;

	case LBM_MSG_BOS:
	    printf("[%s][%s], Beginning of Trnansport Session\n", msg->topic_name, msg->source);
 	    break;

	case LBM_MSG_EOS:
	    printf("[%s][%s], End of Trnansport Session\n", msg->topic_name, msg->source);
 	    break;
	    
        default:    /* unexpected receiver event */
            printf("line %d: unexpected event: %d\n", __LINE__, msg->type);
            exit(1);
    }  /* switch msg->type */

    return 0;
}  /* rcv_method */


int main(int argc, char **argv)
{
    lbm_context_t *ctx;    /* pointer to context object */
    lbm_topic_t *topic;    /* pointer to topic object */
    lbm_rcv_t *rcv;        /* pointer to receiver object */
    int ret;               /* return status of lbm functions (less than zero = error) */

    Minrcv rcv_obj;        /* create instance of receiver class */

#if defined(_MSC_VER)
    /* windows-specific code */
    WSADATA wsadata;
    int wsStat = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (wsStat != 0) {printf("line %d: wsStat=%d\n",__LINE__,wsStat);exit(1);}
#endif

    ret = lbm_context_create(&ctx, NULL, NULL, NULL);
    if (ret) {printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);}

    ret = lbm_rcv_topic_lookup(&topic, ctx, "Greeting", NULL);
    if (ret) {printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);}

    /* Create receiver, using static member function as callback. */
    ret = lbm_rcv_create(&rcv, ctx, topic, &Minrcv::rcv_cb, &rcv_obj, NULL);
    if (ret) {printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);}

    while (msgs_rcvd == 0)
        SLEEP(1);

    lbm_rcv_delete(rcv);

    lbm_context_delete(ctx);

#if defined(_MSC_VER)
    WSACleanup();
#endif
}  /* main */
