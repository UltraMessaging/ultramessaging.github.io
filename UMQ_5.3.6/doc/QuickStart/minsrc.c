/*file: minsrc.c - minimal source (sender) program.
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
 -           -L$HOME/lbm/LBM_<VERS>/<PLATFORM>/lib -llbm -lm -o min_src min_src.c
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

main()
{
    lbm_context_t *ctx;    /* pointer to context object */
    lbm_topic_t *topic;    /* pointer to topic object */
    lbm_src_t *src;        /* pointer to source (sender) object */
    int err;               /* return status of lbm functions (true=error) */

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

    /*callout: topic
     * Allocate a topic object.  A topic object is little more than a string
     * (the topic name).  During operation, LBM keeps some state information
     * in the topic object as well.  The topic is bound to the containing
     * context, and will also be bound to a source object.  Note that the
     * first parameter is a pointer to a pointer variable; lbm_src_topic_alloc
     * writes the pointer to the topic object into "topic".  Also, by passing
     * NULL to the source topic attribute, the default option values are used.
     * The string "Greeting" is the topic string.
     */
    err = lbm_src_topic_alloc(&topic, ctx, "Greeting", NULL);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /*callout: src
     * Create the source object.  A source object is used to send messages.
     * It must be bound to a topic.  Note that the first parameter is a pointer
     * to a pointer variable; lbm_src_create writes the pointer to the source
     * object to into "src".  Use of the third and fourth parameters is
     * optional but recommended in a production program - some source events
     * can be important to the application.  The last parameter is an optional
     * event queue (not used in this example).
     */
    err = lbm_src_create(&src, ctx, topic, NULL, NULL, NULL);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /*callout: sleep1
     * Need to wait for receivers to find us before first send.  There are
     * other ways to accomplish this, but sleep is easy.  See https://communities.informatica.com/infakb/faq/5/Pages/80061.aspx
     * for details.
     */
    SLEEP(3);

    /*callout: send
     * Send a message to the "Greeting" topic.  The flags make sure the
     * call to lbm_src_send doesn't return until the message is sent.
     */
    err = lbm_src_send(src, "Hello!", 6, LBM_MSG_FLUSH | LBM_SRC_BLOCK);
    if (err)
    {
        printf("line %d: %s\n", __LINE__, lbm_errmsg());
        exit(1);
    }

    /* Finished all sending to this topic, delete the source object. */
    /*callout: sleep2
     * For some transport types (mostly UDP-based), a short delay before
     * deleting the source is advisable.  Even though the message is sent,
     * there may have been packet loss, and some transports need a bit of
     * time to request re-transmission.  Also, if the above lbm_src_send call
     * didn't include the flush, some time might also be needed to empty the
     * batching buffer.
     */
    SLEEP(2);
    lbm_src_delete(src);

    /* Do not need to delete the topic object - LBM keeps track of topic
     * objects and deletes them as-needed.  */

    /* Finished with all LBM functions, delete the context object. */
    lbm_context_delete(ctx);

#if defined(_MSC_VER)
    WSACleanup();
#endif
}  /* main */
