/*file: QueueReceiver.java
 *
 * Copyright (c) 2005-2014 Informatica Corporation  Permission is granted to licensees to use
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
 */

package examples;

/**
 * The QueueReceiver class consists only of a main method,
 * which fetches one or more messages from a queue using
 * synchronous message delivery. Run this program in conjunction
 * with QueueSender.
 */
import java.util.Hashtable;
import javax.jms.*;
import javax.naming.*;

public class QueueReceiver {

    /**
     * Main method.
     *
     * @param args     the queue used by the example
     */
    public static void main(String[] args) {
        Context jndiContext = null;
        QueueConnectionFactory queueConnectionFactory = null;
        QueueConnection queueConnection = null;
        QueueSession queueSession = null;
        Queue queue = null;
        javax.jms.QueueReceiver queueReceiver = null;
        TextMessage message = null;

        /*
         * Read queue name from command line and display it.
         */

        /*
         * Create a JNDI API InitialContext object if none exists
         * yet.
         */
        try {
            jndiContext = new InitialContext();
        } catch (NamingException e) {
            System.out.println("Could not create JNDI API "
                    + "context: " + e.toString());
            System.exit(1);
        }

        /*
         * Look up connection factory and queue.  If either does
         * not exist, exit.
         */
        try {
            queueConnectionFactory = (QueueConnectionFactory) jndiContext.lookup("uJMSConnectionFactory");
            queue = (Queue) jndiContext.lookup("TempQueue");

        } catch (Exception e) {
            System.out.println("JNDI API lookup failed: "
                    + e.toString());
            System.exit(1);
        }

        /*
         * Create connection.
         * Create session from connection; false means session is
         * not transacted.
         * Create receiver, then start message delivery.
         * Receive all text messages from queue until
         * a non-text message is received indicating end of
         * message stream.
         * Close connection.
         */
        try {
            queueConnection = queueConnectionFactory.createQueueConnection();
            queueSession = queueConnection.createQueueSession(false,
                    Session.AUTO_ACKNOWLEDGE);

            queueReceiver = queueSession.createReceiver(queue);
            queueConnection.start();
            while (true) {
                Message m = queueReceiver.receive();

                if (m instanceof TextMessage) {
                    message = (TextMessage) m;
                    System.out.println("Reading message: " + message.getText());
                } else {
                    break;
                }
            }
        } catch (JMSException e) {
            System.out.println("Exception occurred: "
                    + e.toString());
        } finally {
            if (queueConnection != null) {
                try {
                    queueConnection.close();
                } catch (JMSException e) {
                }
            }
        }
    }
}


