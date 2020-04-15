/*file: QueueSender.java
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

 /* The QueueSender class consists only of a main method,
 * which sends several messages to a queue.
 *
 * Run this program in conjunction with QueueReceiver.
 */
import javax.jms.*;
import javax.naming.*;

public class QueueSender {

    /**
     * Main method.
     *
     * @param args     the queue used by the example and,
     *                 optionally, the number of messages to send
     */
    public static void main(String[] args) {
        String queueName = null;
        Context jndiContext = null;
        QueueConnectionFactory queueConnectionFactory = null;
        QueueConnection queueConnection = null;
        QueueSession queueSession = null;
        Queue queue = null;
        javax.jms.QueueSender queueSender = null;
        TextMessage message = null;
        final int NUM_MSGS;


        if (args.length == 2) {
            NUM_MSGS = (new Integer(args[1])).intValue();
        } else {
            NUM_MSGS = 15;
        }

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
            //  ((LBMQueue) queue).setName("WildMike");
        } catch (Exception e) {
            System.out.println("JNDI API lookup failed: "
                    + e.toString());
            System.exit(1);
        }

        /*
         * Create connection.
         * Create session from connection; false means session is
         * not transacted.
         * Create sender and text message.
         * Send messages, varying text slightly.
         * Send end-of-messages message.
         * Finally, close connection.
         */
        try {
            queueConnection =
                    queueConnectionFactory.createQueueConnection();
            queueConnection.start();

            queueSession =
                    queueConnection.createQueueSession(false,
                    Session.AUTO_ACKNOWLEDGE);
            queueSender = queueSession.createSender(queue);
            message = queueSession.createTextMessage();
            for (int i = 0; i < NUM_MSGS; i++) {
                message.setText("This is message " + (i + 1));
                System.out.println("Sending message: " + message.getText());
                queueSender.send(message);
                Thread.sleep(5);
            }

            /*
             * Send a non-text control message indicating end of
             * messages.
             */
            queueSender.send(queueSession.createMessage());

            queueSender.close();
            queueSession.close();
        } catch (JMSException e) {
            System.out.println("Exception occurred: "
                    + e.toString());
        } catch (Exception e) {
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


