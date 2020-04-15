/*file: ume-example-rcv-3.c - UME example receiver program.
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
 -           -L$HOME/lbm/LBM_<VERS>/<PLATFORM>/lib -llbm -lm -o ume-example-rcv-3 ume-example-rcv-3.c
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

/* Structure to be used to hold information about a receivers RegID information */
typedef struct rcv_info_t_stct
{
    int existing_regid;       /* flag to indicate an existing RegID is being used */
    char store[80];           /* the store in use */
    unsigned int src_regid;   /* RegID of the source */
    unsigned int rcv_regid;   /* RegID for this receiver for the src_regid RegID of the source */
} rcv_info_t;

/* The filename we will use to store the RegID information */
#define RCV_REGID_SAVE_FILENAME "UME-example-rcv-RegID"

/*
 * A global variable is used to communicate from the receiver callback to
 * the main application thread.
 */
int msgs_rcvd = 0;

/*callout: save RegID to file
 * We will be saving the RegID information to the given filename. We want the format
 * of the file to be easy to use, so we will make it easy to parse and contain the
 * Store info (IP and Port), sources RegID and receiver RegID. RegIDs are per topic
 * per source. So, this operation only supports a single source at this time.
 */
int save_rcv_regid_to_file(const char *filename, lbm_msg_ume_registration_ex_t *reg)
{
    FILE *fp;   /* FILE pointer for the saved file */

    /* Open the file for writing. We will be overwriting any existing info */
    if ((fp = fopen(filename, "w")) == NULL)
        return -1;
    /* Write the information in Store-SrcRegID-RcvRegID form (Store expands to IP:port) */
    fprintf(fp, "%s-%u-%u", reg->store, reg->src_registration_id, reg->rcv_registration_id);
    /* Printf some information to stdout so that we can see what was done */
    printf("saving RegID info to \"%s\" - %s:%u:%u\n", filename, reg->store, reg->src_registration_id, reg->rcv_registration_id);
    /* Flush the file so that it is on disk */
    fflush(fp);
    /* Close the file */
    fclose(fp);
    return 0;
} /* save_rcv_regid_to_file */

/*callout: read RegID from file
 * The information is saved in the format that the save used. This is Store-SrcRegID-RcvRegID.
 * This function reads in the store saving it, the source RegID and the receiver RegID.
 */
int read_rcv_regid_from_file(const char *filename, rcv_info_t *rcvinfo)
{
    FILE *fp;           /* FILE pointer for the saved file */

    /* Open the file for reading */
    if ((fp = fopen(filename, "r")) == NULL)
        return -1;
    /* Read in the line and parse it into store, source RegID, and receiver RegID.
     * If the line is malformed, we will return an error and set receiver RegID to 0 */
    if (fscanf(fp, "%[0-9.:]-%u-%u", rcvinfo->store, &(rcvinfo->src_regid), &(rcvinfo->rcv_regid)) != 3)
    {
        rcvinfo->rcv_regid = 0;       /* make sure to set this to 0 because it will be used by the caller */
        return -1;
    }
    /* Close the file */
    fclose(fp);
    /* Printf some information to stdout so that we can see what was done */
    printf("read in saved RegID info from \"%s\" - %s RegIDs source %u, receiver %u\n", filename,
           rcvinfo->store, rcvinfo->src_regid, rcvinfo->rcv_regid);
    return 0;
} /* read_rcv_regid_from_file */

/*callout: remove RegID file
 * We want to remove the file from the filesystem as we are done with it.
 */
int remove_saved_rcv_regid(const char *filename)
{
    /* Printf some information that we are removing the file */
    printf("removing saved RegID file \"%s\"\n", filename);
    /* Actually remove the file */
    return unlink(filename);
} /* remove_saved_rcv_regid */

/*callout: RegID callback
 * UME will call this callback when it needs to know the RegID to use for a source.
 * As with any callback in LBM, This callback function has some severe limitations
 * placed upon it.  It must execute very quickly; any potentially blocking calls
 * it might make will interfere with the proper execution of the LBM context thread.
 * Some printf calls are used in this callback as well as a function. Normally,
 * this would be a bad idea for a callback. However, for this minimal application,
 * this should only be called once per source created.
 */
unsigned int app_rcv_regid_callback(lbm_ume_rcv_regid_ex_func_info_t *info, void *clientd)
{
    rcv_info_t *rcvinfo = (rcv_info_t *)clientd;    /* the passed in receiver information */
    unsigned int regid = 0;                         /* the returned RegID */

    /* Check to see if the source that is passed in is actually for the source we are
     * expecting, if so, then return the saved receiver RegID for us to use. If not, then
     * return 0 to instruct UME to use a store assigned RegID. We could check the store also
     * to make sure it is the same store that we want, however, for this minimal application,
     * we should be fine just checking the RegID. Notice that we don't check for the
     * existing_regid flag. That is because we do not really care. If we have existing
     * RegID info passed in, either from a saved RegID file or set by the application, we
     * want to use it. */
    if (rcvinfo->src_regid == info->src_registration_id)
    {
        regid = rcvinfo->rcv_regid;
    }
    /* Printf some information to see what is being done */
    printf("UME Store %u: %s [%s][%u] Requesting RegID: %u\n", info->store_index,
           info->store, info->source, info->src_registration_id, regid);
    return regid;
} /* app_rcv_regid_callback */

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
    rcv_info_t *rcvinfo = (rcv_info_t *)clientd;    /* the passed in receiver information */
    int err;                                        /* return status of functions (true=error) */

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
        printf("Received %d bytes on topic %s (sequence number %u) '%.*s'\n",
               msg->len, msg->topic_name, msg->sequence_number, msg->len, msg->data);

        /* Tell main thread that we've received our message. */
        ++ msgs_rcvd;
        /* Check to see if we received the last message, if so, then even if
         * the numbers are not correct, then we go ahead and signal that we have gotten everything.
         * We can do this based on the semantics we wanted for the receiver and the fact
         * that the receiver may have been restarted and picked up where it left off. */
        if (msg->sequence_number == 19)
            msgs_rcvd = 20;
        break;
    case LBM_MSG_UME_REGISTRATION_ERROR:    /* UME store registration had an error */
        /* Printf the error */
        printf("[%s][%s] UME registration error: %s\n", msg->topic_name, msg->source, msg->data);
        /* Exit the application if the receiver can not register with the store */
        exit(0);
        break;
    case LBM_MSG_UME_REGISTRATION_SUCCESS_EX:   /* UME store registered with success */
    {
        /* the event data (registration information structure) */
        lbm_msg_ume_registration_ex_t *reg = (lbm_msg_ume_registration_ex_t *)(msg->data);

        /* If not using a saved RegID from a file, then save our information */
        if (!rcvinfo->existing_regid)
        {
            /* Save the information using the function */
            err = save_rcv_regid_to_file(RCV_REGID_SAVE_FILENAME, reg);
            if (err)
            {
                printf("line %d: could not save RegID to file\n", __LINE__);
                exit(1);
            }
            /* Set the source and receiver RegID so that they can be used right away if needed
             * by the RegID callback */
            rcvinfo->rcv_regid = reg->rcv_registration_id;
            rcvinfo->src_regid = reg->src_registration_id;
        }
    }
    break;

    default:    /* unexpected receiver event */
        printf("Received lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
        break;
    }  /* switch msg->type */

    return 0;
}  /* app_rcv_callback */


main()
{
    lbm_context_t *ctx;             /* pointer to context object */
    lbm_topic_t *topic;             /* pointer to topic object */
    lbm_rcv_t *rcv;                 /* pointer to receiver object */
    int err;                        /* return status of lbm functions (true=error) */
    lbm_rcv_topic_attr_t * attr;        /* attribute structure for the receiver */
    lbm_ume_rcv_regid_ex_func_t id; /* structure to hold registration function information */
    rcv_info_t rcvinfo;             /* structure to hold receiver information */

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

    /* Initialize the receiver information structure. Source and receiver RegIDs are
     * set to 0 and existing regid flag is initially off (0) */
    rcvinfo.existing_regid = 0;
    rcvinfo.src_regid = 0;
    rcvinfo.rcv_regid = 0;

    /*callout: read RegID
     * Attempt to read in an exsiting RegID from the file that it would have
     * been saved to. If an error is encountered or the file is not found, we
     * treat it the same way and simple use the initial values set in the receiver
     * information (RegID of 0). If we do read in a RegID information, we will use
     * that information and set the flag in the receiver information so that we
     * can determine that a saved RegID is in use.
     */
    err = read_rcv_regid_from_file(RCV_REGID_SAVE_FILENAME, &rcvinfo);
    if (!err)
    {
        rcvinfo.existing_regid = 1;
    }
    /* else, then go ahead and use default settings (no RegID) */

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
     * Set the callback function to be called when a RegID is desired for UME.
     * We desire UME to call app_rcv_regid_callback and pass a pointer to
     * the receiver information as the clientd argument.
     */
    id.func = app_rcv_regid_callback;   /* the callback function to call */
    id.clientd = &rcvinfo;              /* the value to pass in the clientd to the function */
    err = lbm_rcv_topic_attr_setopt(attr, "ume_registration_extended_function",
                                    &id, sizeof(id));
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
     * in an attribute structure, it will set the attributes to what is passed.
     * The string "UME Example" is the topic string.
     */
    err = lbm_rcv_topic_lookup(&topic, ctx, "UME Example", attr);
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
    err = lbm_rcv_create(&rcv, ctx, topic, app_rcv_callback, &rcvinfo, NULL);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /* Wait until we have received 20 messages, then continue */
    while (msgs_rcvd < 20)
        SLEEP(1);

    /*callout: remove RegID
     * The RegID is of no more use and the file can be removed from the filesystem
     * and cleaned up.
     */
    remove_saved_rcv_regid(RCV_REGID_SAVE_FILENAME);

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
