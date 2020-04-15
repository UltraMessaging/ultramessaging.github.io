/*file: q-example-rcv.c - UME Queue example receiver program.
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
 -
 - As you will see, error handling in this program is primitive.  A production
 - program would want to have better error handling, but for the purposes of
 - a minimal example, it would just be a distraction.  Also, a production
 - program would want to support a configuration file to override default
 - values on options.
 -
 - Build/run notes - WINDOWS
 -   1. Make sure the preprocessor variable "WIN32" is defined. From VC8:
 -        Select your project in the Solution Explorer
 -        Right click on it to bring up its Property Page dialog
 -        Under the Configuration field, select "All Configurations"
 -        Select the C/C++ category and pick the Preprocessor options
 -        Preprocessor Definitions:
 -   2. Add "C:\Program Files\29West\LBM_<VERS>\Win2k-i386\include" to
 -      additional include directories (where <VERS> is the appropriate
 -      version identifier). From VC8:
 -        Select your project in the Solution Explorer
 -        Right click on it to bring up its Property Page dialog
 -        Under the Configuration field, select "All Configurations"
 -        Select the C/C++ category and pick the General options
 -        Additional Include Directories:
 -   3. Add lbm.lib to Additional Dependencies. From VC8:
 -        Select your project in the Solution Explorer
 -        Right click on it to bring up its Property Page dialog
 -        Under the Configuration field, select "All Configurations"
 -        Select the Linker category and pick the Input options
 -        Additional Dependencies:
 -   4. Add "C:\Program Files\29West\LBM_<VERS>\Win2k-i386\lib" to
 -      additional library path (where <VERS> is the appropriate
 -      version identifier).  From VC8:
 -        Select your project in the Solution Explorer
 -        Right click on it to bring up its Property Page dialog
 -        Under the Configuration field, select "All Configurations"
 -        Select the Linker category and pick the General options
 -        Additional Library Path:
 -   5. The install procedure should already have added the LBM "bin" directory
 -      to the Windows PATH.  This is necessary so that "lbm.dll" can be found
 -      when a program is run.  To modify the Windows PATH from Win-XP:
 -        right-click "My Computer"->properties; Advanced;
 -        Environment variables; System variables:Path; Edit
 -
 -   Using the Visual Studio command prompt:
 -      cl /I..\include\lbm /Fd.\ /Z7 myapp.c /link ..\lib\lbm.lib Ws2_32.lib /DEBUG
 -      with the .c file in the lbm/bin directory
 -
 - Build/run notes - UNIX
 -   1. Here is a sample build command:
 -        cc -I$HOME/lbm/LBM_<VERS>/<PLATFORM>/include
 -           -L$HOME/lbm/LBM_<VERS>/<PLATFORM>/lib -llbm -lm -o q-example-rcv q-example-rcv.c
 -   2. The appropriate library search path should be updated to include the
 -      LBM "bin" directory.  For example, on Linux:
 -        LD_LIBRARY_PATH="$HOME/lbm/LBM_<VERS>/<PLATFORM>/bin:$LD_LIBRARY_PATH"
 -        export LD_LIBRARY_PATH
 -      Alternatively, the shared library can be copied from the LBM "bin"
 -      directory to a directory which is already in the library search path.
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

/*
 * A global variable is used to communicate from the receiver callback to
 * the main application thread.
 */
int msgs_rcvd = 0;

/*callout: callback
 * LBM passes received messages to the application by means of a callback.
 * I.e. the LBM context thread reads the network socket, performs its
 * higher-level protocol functions, and then calls an application-level
 * function that was set up during initialization.  This callback function
 * has some severe limitations placed upon it.  It must execute very quickly;
 * any potentially blocking calls it might make will interfere with the proper
 * execution of the LBM context thread.  One common desire is for the receive
 * function to send an LBM message (via lbm_src_send), however this has the
 * potential to produce a deadlock condition.  If it is desired for the
 * receive callback function to call LBM or other potentially blocking
 * functions, it is strongly advised to make use of an event queue, which
 * causes the callback to be executed from an application thread.  See the
 * example tool lbmrcvq.c for an example of using a receiver event queue.
 */
int app_rcv_callback(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
    /* There are several different events that can cause the receiver callback
     * to be called.  Decode the event that caused this.  */
    switch (msg->type)
    {
    case LBM_MSG_DATA:    /* a received message */
        /*callout: printf
         * Note - printf can block, which is normally a bad idea for a
         * callback (unless an event queue is being used).  However, for
         * this minimal application, only a few messages are expected.
         */
        printf("Received %d bytes on topic %s: '%.*s'\n",
               msg->len, msg->topic_name, msg->len, msg->data);

        /* Tell main thread that we've received our message. */
        ++ msgs_rcvd;
        break;

    default:    /* unexpected receiver event */
        printf("Received lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
        break;
    }  /* switch msg->type */

    return 0;
}  /* app_rcv_callback */


main()
{
    lbm_context_t *ctx;         /* pointer to context object */
    lbm_topic_t *topic;         /* pointer to topic object */
    lbm_rcv_t *rcv;             /* pointer to receiver object */
    int err;                    /* return status of lbm functions (true=error) */
    lbm_rcv_topic_attr_t * attr;    /* attribute structure for the receiver */

#if defined(_MSC_VER)
    /* windows-specific code */
    WSADATA wsadata;
    int wsStat = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (wsStat != 0)
    {
        printf("line %d: wsStat=%d\n",__LINE__,wsStat);
        exit(1);
    }
#endif

    /*callout: context
     * Create a context object.  A context is an environment in which LBM
     * functions.  Note that the first parameter is a pointer to a pointer
     * variable; lbm_context_create writes the pointer to the context
     * object into "ctx".  Also, by passing NULL to the context attribute
     * parameter, the default option values are used. For most applications
     * only a single context is required regardless of how many sources and
     * receivers are created.
     */
    err = lbm_context_create(&ctx, NULL, NULL, NULL);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /*callout: attribute init
     * Initialize the attribute structure to the default values.
     */
    err = lbm_rcv_topic_attr_create(&attr);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /*callout: attribute setopt
     * Set the Receiver Type ID for the receiver to use.
     */
    err = lbm_rcv_topic_attr_str_setopt(attr, "umq_receiver_type_id", "100");
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /*callout: topic
     * Lookup a topic object.  A topic object is little more than a string
     * (the topic name).  During operation, LBM keeps some state information
     * in the topic object as well.  The topic is bound to the containing
     * context, and will also be bound to a receiver object.  Note that the
     * first parameter is a pointer to a pointer variable; lbm_rcv_topic_lookup
     * writes the pointer to the topic object into "topic".  Also, by passing
     * NULL to the source topic attribute, the default option values are used.
     * The string "UME Queue Example" is the topic string.
     */
    err = lbm_rcv_topic_lookup(&topic, ctx, "UME Queue Example", attr);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /*callout: rcv
     * Create the receiver object and bind it to a topic.  Note that the first
     * parameter is a pointer to a pointer variable; lbm_rcv_create writes the
     * pointer to the source object to into "rcv".  The second and third
     * parameters are the function and application data pointers.  When a
     * message is received, the function is called with the data pointer passed
     * in as its last parameter.  The last parameter is an optional event
     * queue (not used in this example).
     */
    err = lbm_rcv_create(&rcv, ctx, topic, app_rcv_callback, NULL, NULL);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /* Wait until we have received 20 messages, then continue */
    while (msgs_rcvd < 20)
        SLEEP(1);

    /* Finished all receiving from this topic, delete the receiver object. */
    lbm_rcv_delete(rcv);

    /* Do not need to delete the topic object - LBM keeps track of topic
     * objects and deletes them as-needed.  */

    /* Finished with all LBM functions, delete the context object. */
    lbm_context_delete(ctx);

#if defined(_MSC_VER)
    WSACleanup();
#endif
}  /* main */
