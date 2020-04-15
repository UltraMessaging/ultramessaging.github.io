/*file: ume-example-src-3.c - UME example source program.
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
 -           -L$HOME/lbm/LBM_<VERS>/<PLATFORM>/lib -llbm -lm -o ume-example-src-3 ume-example-src-3.c
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

/* Structure to be used to hold information about a source */
typedef struct src_info_t_stct
{
    int existing_regid;       /* flag to indicate an existing RegID (from a file) is being used */
    int message_num;          /* number of the current message to send */
} src_info_t;

/* The filename we will use to store the RegID information */
#define SRC_REGID_SAVE_FILENAME "UME-example-src-RegID"

/*callout: save RegID to file
 * We will be saving the RegID information to the given filename. We want the format
 * of the file to be easy to use, so we will make it the exact same information and
 * format as the ume_store configuration variable will expect. This allows us to read
 * in the file and use it directly.
 */
int save_src_regid_to_file(const char *filename, lbm_src_event_ume_registration_ex_t *reg)
{
    FILE *fp;           /* FILE pointer for the saved file */

    /* Open the file for writing. We will be overwriting any existing info */
    if ((fp = fopen(filename, "w")) == NULL)
        return -1;
    /* Write the information in Store:RegID form (expands to IP:port:RegID) */
    fprintf(fp, "%s:%u", reg->store, reg->registration_id);
    /* Printf some information to stdout so that we can see what was done */
    printf("saving RegID info to \"%s\" - %s:%u\n", filename, reg->store, reg->registration_id);
    /* Flush the file so that it is on disk */
    fflush(fp);
    /* Close the file */
    fclose(fp);
    return 0;
} /* save_src_regid_to_file */

/*callout: read RegID from file
 * The information is saved in the format that the ume_store configuration variable
 * expects, so all we need to do is read it into the string and return.
 */
int read_src_regid_from_file(const char *filename, char *store_spec, int size)
{
    FILE *fp;           /* FILE pointer for the saved file */

    /* Open the file for reading */
    if ((fp = fopen(filename, "r")) == NULL)
        return -1;
    /* Read in the first line and save it */
    if (fgets(store_spec, size, fp) == NULL)
        return -1;
    /* Close the file */
    fclose(fp);
    /* Printf some information to stdout so that we can see what was done */
    printf("read in saved RegID info from \"%s\" - %s\n", filename, store_spec);
    return 0;
} /* read_src_regid_from_file */

/*callout: remove RegID file
 * We want to remove the file from the filesystem as we are done with it.
 */
int remove_saved_src_regid(const char *filename)
{
    /* Printf some information that we are removing the file */
    printf("removing saved RegID file \"%s\"\n", filename);
    /* Actually remove the file */
    return unlink(filename);
} /* remove_saved_src_regid */

/*callout: callback
 * LBM passes events to a source when specific events occur related to UME and
 * other LBM features. We will catch these events and handle them as appropriate.
 * As with any callback in LBM, This callback function has some severe limitations
 * placed upon it.  It must execute very quickly; any potentially blocking calls
 * it might make will interfere with the proper execution of the LBM context thread.
 * If it is desired for the this callback function to call other potentially blocking
 * functions, it is strongly advised to make use of an event queue, which
 * causes the callback to be executed from an application thread. Some printf calls
 * are used in this callback as well as a function that will write to a file. Normally,
 * this would be a bad idea for a callback (unless an event queue is being used).
 * However, for this minimal application, only a few events are expected.
 */
int app_src_callback(lbm_src_t *src, int event, void *eventd, void *clientd)
{
    src_info_t *srcinfo = (src_info_t *)clientd;    /* the passed in source information */
    int err;                                        /* return status of functions (true=error) */

    /* There are several different events that can cause the source event callback
     * to be called.  Decode the event that caused this.  */
    switch (event)
    {
    case LBM_SRC_EVENT_UME_REGISTRATION_ERROR:  /* UME store registration had an error */
    {
        const char *errstr = (const char *)eventd;  /* the event data (string) */

        /* Printf the error */
        printf("Error registering source with UME store: %s\n", errstr);
        /* Exit the application if the source can not register with the store */
        exit(0);
    }
    break;
    case LBM_SRC_EVENT_UME_STORE_UNRESPONSIVE:  /* UME store unresponsive */
    {
        const char *infostr = (const char *)eventd; /* the event data (string) */

        /* Printf that the store is unresponsive and the information from UME.
         * UME will continue to try to contact the store, so we will let it go */
        printf("Store unresponsive: %s\n", infostr);
    }
    break;
    case LBM_SRC_EVENT_UME_REGISTRATION_SUCCESS_EX: /* UME store registered with succes */
    {
        /* the event data (registration information structure) */
        lbm_src_event_ume_registration_ex_t *reg = (lbm_src_event_ume_registration_ex_t *)eventd;

        /* If not using a saved RegID from a file, then save our information */
        if (!srcinfo->existing_regid)
        {
            /* Save the information using the function */
            err = save_src_regid_to_file(SRC_REGID_SAVE_FILENAME, reg);
            if (err)
            {
                printf("line %d: could not save RegID to file\n", __LINE__);
                exit(1);
            }
        }
    }
    break;
    case LBM_SRC_EVENT_UME_REGISTRATION_COMPLETE_EX: /* UME store registration complete */
    {
        /* the event data (registration complete information structure) */
        lbm_src_event_ume_registration_complete_ex_t *reg = (lbm_src_event_ume_registration_complete_ex_t *)eventd;

        /* If using a saved RegID from a file, the set the starting message to be
         * what we would expect to be seen from the source. We use the LBM/UME
         * sequence number to give us a clue. We started with 1 and LBM/UME started
         * with 0. So, take the sequence number and add 1 to it. */
        if (srcinfo->existing_regid)
        {
            /* Set the message number we will use */
            srcinfo->message_num = reg->sequence_number + 1;
            /* Printf what message number we will start with */
            printf("will start with message number %d\n", srcinfo->message_num);
        }
    }
    break;
    default:    /* unexpected event - we ignore for now */
        break;
    } /* switch event */
    return 0;
} /* app_src_callback */

main()
{
    lbm_context_t *ctx;         /* pointer to context object */
    lbm_topic_t *topic;         /* pointer to topic object */
    lbm_src_t *src;             /* pointer to source (sender) object */
    int err;                    /* return status of lbm functions (true=error) */
    char message[64];           /* buffer to hold message that will be sent */
    char store_info[64] =       /* string used to set ume_store configuration variable */
        "127.0.0.1:14567";      /*   default ume_store value (minus RegID) */
    lbm_src_topic_attr_t * attr;    /* attribute structure for the source */
    src_info_t srcinfo;     /* structure to hold source information */

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

    /* Initialize the source information structure. Message number starts at
     * 1 and existing regid flag is initially off (0) */
    srcinfo.message_num = 1;
    srcinfo.existing_regid = 0;

    /*callout: read RegID
     * Attempt to read in an exsiting RegID from the file that it would have
     * been saved to. If an error is encountered or the file is not found, we
     * treat it the same way and simple use the default ume_store information
     * (which is store sans a set RegID so that the store will assign us one).
     * If we do read in a RegID information, we will use that information in
     * the ume_store configuration variable and set the flag in the source
     * information so that we can determine that a saved RegID is in use.
     */
    err = read_src_regid_from_file(SRC_REGID_SAVE_FILENAME, store_info, sizeof(store_info));
    if (!err)
    {
        srcinfo.existing_regid = 1;
    }
    /* else, then go ahead and use default settings (i.e. no RegID) */

    /*callout: attribute init
     * Initialize the attribute structure to the default values.
     */
    err = lbm_src_topic_attr_create(&attr);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /*callout: attribute setopt (string)
     * Set the ume_store attribute to use the UME store from information from RegID saved file
     * or from the default info set above, which has no RegID information.
     */
    err = lbm_src_topic_attr_str_setopt(attr, "ume_store", store_info);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /*callout: topic
     * Allocate a topic object.  A topic object is little more than a string
     * (the topic name).  During operation, LBM keeps some state information
     * in the topic object as well.  The topic is bound to the containing
     * context, and will also be bound to a source object.  Note that the
     * first parameter is a pointer to a pointer variable; lbm_src_topic_alloc
     * writes the pointer to the topic object into "topic".  Also, by passing
     * in an attribute structure, it will set the attributes to what is passed.
     * The string "UME Example" is the topic string.
     */
    err = lbm_src_topic_alloc(&topic, ctx, "UME Example", attr);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /*callout: src
     * Create the source object.  A source object is used to send messages.
     * It must be bound to a topic.  Note that the first parameter is a pointer
     * to a pointer variable; lbm_src_create writes the pointer to the source
     * object to into "src". Have the source use app_src_callback for source
     * events and pass in a pointer to srcinfo when source events occur.
     * The last parameter is an optional event queue (not used in this example).
     */
    err = lbm_src_create(&src, ctx, topic, app_src_callback, &srcinfo, NULL);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /*callout: sleep1
     * Need to wait for receivers to find us.  See https://communities.informatica.com/infakb/faq/5/Pages/80061.aspx
     * for details. In addition, for UME sources, we have to give a source
     * time to register with stores. We could check source events if we
     * desired to know when we could send or not. But here, we will just pause
     */
    SLEEP(3);

    /* Send 20 messages with a 1 second pause between them */
    for (; srcinfo.message_num <= 20; srcinfo.message_num++)
    {
        /* Create a unique message for this iteration */
        sprintf(message, "UME Message %02d", srcinfo.message_num);

        /*callout: send
         * Send a message to the "UME Example" topic.  The flags make sure the
         * call to lbm_src_send doesn't return until the message is sent.
         */
        err = lbm_src_send(src, message, 15, LBM_MSG_FLUSH | LBM_SRC_BLOCK);
        if (err)
        {
            printf("line %d: %s\n", __LINE__, lbm_errmsg());
            exit(1);
        }

        /*callout: sleep2
         * Wait 1 second before sending next message.
         */
        SLEEP(1);
    }

    /*callout: sleep2
     * Even though the message is sent, some transports may need a bit of
     * time to request re-transmission.  If the above lbm_src_send call
     * didn't include the flags, some time might also be needed to empty
     * the batching buffer.
     */
    SLEEP(2);

    /*callout: remove RegID
     * The RegID is of no more use and the file can be removed from the filesystem
     * and cleaned up.
     */
    remove_saved_src_regid(SRC_REGID_SAVE_FILENAME);

    /* Finished all sending to this topic, delete the source object. */
    lbm_src_delete(src);

    /* Do not need to delete the topic object - LBM keeps track of topic
     * objects and deletes them as-needed.  */

    /* Finished with all LBM functions, delete the context object. */
    lbm_context_delete(ctx);

#if defined(_MSC_VER)
    WSACleanup();
#endif
}  /* main */
