/*file: MinRcv.java - minimal receiver program.
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
 - As you will see, there is no error handling in this example program.  A
 - production program would want to have better error handling, but for the
 - purposes of a minimal example, it would just be a distraction.  Also, a
 - production program would want to support a configuration file to override
 - default values on options.
 -
 - Build/run notes - Windows
 -   1. Make sure "java" and "javac" are both in your current path.
 -   2. Make sure C:\Program Files\29West\LBM_<VERS>\Win2k-i386\bin
 -      is also in your path.
 -   3. Compile using a command like:
 -          javac -classpath C:\path\to\lbmjarfile.jar MinRcv.java
 -   4. Run using a command like:
 -          java -classpath .;C:\path\to\lbmjarfile.jar MinRcv
 -
 - Build/run notes - UNIX
 -   1. Make sure "java" and "javac" are both in your current path.
 -   2. The appropriate library search path should be updated to include the
 -      LBM "bin" directory.  For example, on Linux:
 -        LD_LIBRARY_PATH="$HOME/lbm/LBM_<VERS>/<PLATFORM>/bin:$LD_LIBRARY_PATH"
 -        export LD_LIBRARY_PATH
 -      Alternatively, the shared library can be copied from the LBM "bin"
 -      directory to a directory which is already in the library search path.
 -   3. Compile using a command like:
 -          javac -classpath /path/to/lbmjarfile.jar MinRcv.java
 -   4. Run using a command like:
 -          java -classpath .:/path/to/lbmjarfile.jar MinRcv
 */

/*
 * Java's print functions assume byte arrays are encoded with UTF-8 unless
 * you tell them otherwise.  In this example program, we'll explicitly
 * convert a byte array to a String using the ISO-8859-1 code page.  On
 * the off chance the system doesn't have that code page, we're going to
 * need to handle an UnsupportedEncodingException.
 */
import java.io.UnsupportedEncodingException;

/*
 * Import everything from the LBM jar file.  In a real application, of course,
 * you should always import only those classes that you actually use.
 */
import com.latencybusters.lbm.*;

public class MinRcv {

    /*
     * A class variable is used to communicate from the receiver callback to the
     * other class methods. It keeps track of the number of data messages that
     * have been received.
     */
    private static int messagesReceived = 0;

    public static void main(String[] args) throws LBMException{

        /*
         * First, create a set of attributes for an LBM context.  Attributes
         * allow you to control programmatically most of the same LBM options
         * you can set in a configuration file.  In fact, there are a few
         * options that _must_ be set programmatically in this way. (Usually
         * options that set particular callbacks.)
         */
        LBMContextAttributes myAttributes = new LBMContextAttributes();

        /*
         * Now we'll create a context object.  A context is an environment in
         * which LBM functions.  Each context can have its own attributes and
         * configuration settings, which can be specified in an
         * LBMContextAttributes object like the one we just created.
         */
        LBMContext myContext = new LBMContext(myAttributes);

        /*
         * Lookup a topic object.  A topic object is little more than a string
         * (the topic name).  During operation, LBM keeps some state information
         * in the topic object as well.  The topic is bound to the containing
         * context, and will also be bound to a receiver object.  Topics can
         * have a set of attributes; these are specified using either an
         * LBMReceiverAttributes object or an LBMSourceAttributes object.  The
         * simplest LBMTopic constructor makes a Receiver topic with the default
         * set of attributes.  Note that * "Greeting" is the topic string. */
        LBMTopic myTopic = new LBMTopic(myContext, "Greeting");
		
		/*
         * Add a callback function to our new receiver.  This function is
         * called each time our receiver gets a message.  First, we create an
         * instance of our receiver callback class, MinRcvReceiverCallback.
		 * Then we pass it into the constructor for our LBMReceiver object.
         */
        MinRcvReceiverCallback myReceiverCallback = new MinRcvReceiverCallback();
        
	/*
         * Create the receiver object and bind it to a topic.  Receivers must be
         * associated with a context.
         */
	LBMReceiver myReceiver = new LBMReceiver(myContext, myTopic, myReceiverCallback, null);
		
        /*
         * Wait until we've received some messages. While the main thread of the
         * example program sleeps, our LBM will be busy receiving messages and
         * calling the "onReceive" method of our MinRcvReceiverCallback class.
         * In this example program, the number of messages that have been
         * received when the main thread wakes up and continues is
         * unpredictable for an unknown source.  If the source is the minimal
         * source program, MinSrc.java or minsrc.c, then we'll get exactly one
         * message.
         */
        while (messagesReceived == 0) {
            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {
            }
        }

        /*
         * We've received and processed some messages.  Now let's close
         * things down.
         */

        /*
         * Close the receiver, which means we quit listening for
         * messages on our receiver's topic.
         */
        myReceiver.close();

        /*
         * Notice that we don't "close" the topic.  LBM keeps track of
         * topics for you.
         */


        /*
         * Now, we close the context itself.  Always close a context that is no
         * longer in use and won't be used again.
         */
        myContext.close();
    }  /* main */


    /*
     * LBM passes received messages to the application by means of a callback.
     * I.e. the LBM context thread reads the network socket, performs its
     * higher-level protocol functions, and then calls an application-level
     * function that was set up during initialization.  This callback function
     * has some limitations placed upon it.  It must execute very quickly;
     * any potentially blocking calls it might make will interfere with the
     *  proper execution of the LBM context thread.  One common desire is for
     * the receive function to send an LBM message (via LBMSource.send()),
     *  however this has the potential to produce a deadlock condition.  If
     * it is desired for the receive callback function to call LBM or other
     *  potentially blocking functions, it is recommended to make use of an
     *  event queue, which causes the callback to be executed from an
     *  application thread.  See the example tool lbmrcvq.java for an example
     *  of using a receiver event queue.
     *
     * LBM receiver callbacks in Java are merely classes that implement the
     * LBMReceiverCallback interface, like the example one below:
     */
    private static class MinRcvReceiverCallback implements LBMReceiverCallback {

        public int onReceive(Object cbArgs, LBMMessage theMessage) {
            /* There are several different events that can cause the receiver callback
             * to be called.  Decode the event that caused this.  */
            switch (theMessage.type()) {
            case LBM.MSG_DATA:
                try {
                    /* NOTE:  Normally it would be a bad idea to do something as
                     * slow as a print statement in the callback function.
                     * In this example, we'll probably only receive one message,
                     * so it doesn't matter.
                     */
                    System.out
                            .println("Received "
                                    + theMessage.dataLength()
                                    + " bytes on topic "
                                    + theMessage.topicName()
                                    + ": '"
                                    + new String(theMessage.data(),
                                            "ISO-8859-1") + "'");
                } catch (UnsupportedEncodingException e) {
                    System.out
                            .println("This system doesn't support the ISO-8859-1 code page.");
                    System.exit(1);
                }

                /* Increment the number of messages we've received.
                 */
                messagesReceived++;
                break;

	    case LBM.MSG_BOS:
		System.out.println("[" + theMessage.topicName() + "][" + theMessage.source() + "], Beginning of Transport Session");
		break;

	    case LBM.MSG_EOS:
		System.out.println("[" + theMessage.topicName() + "][" + theMessage.source() + "], End of Transport Session");
		break;

            default:
            	break;
	    }
			theMessage.dispose();
            /*
             * Return 0 if there were no errors. Returning a non-zero value will
             * cause LBM to log a generic error message.
             */
            return 0;
        }  /* onReceive */
    }  /* class MinRcvReceiverCallback */
}  /*  class MinRcv */
