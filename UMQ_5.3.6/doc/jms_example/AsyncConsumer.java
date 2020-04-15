/*file: AsyncConsumer.java
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
 * The AsyncConsumer class consists only of a main
 * method, which receives one or more messages from a topic using
 * asynchronous message delivery.  It uses the message listener
 * TextListener.  Run this program in conjunction with
 * SimpleProducer.
 *
To end the program, enter Q or q on the command line.
 */

import javax.jms.*;
import javax.naming.*;

public class AsyncConsumer implements MessageListener, ExceptionListener {

    public static void main(String[] args) {
        new AsyncConsumer();
        while (true) {
            try {
                Thread.sleep(1000000);
            } catch (Exception ex) {
                ex.printStackTrace();
            }
        }
    }

    public AsyncConsumer() {
        Context jndiContext = null;

        /*
         * Create a JNDI API InitialContext object if none exists
         * yet.
         */
        try {
            jndiContext = new InitialContext();
        } catch (NamingException e) {
            System.out.println("Could not create JNDI API "
                    + "context: " + e.toString());
            e.printStackTrace();
            System.exit(1);
        }

        /*
         * Look up connection factory and topic.  If either does
         * not exist, exit.
         */
        try {
            ConnectionFactory factory = (ConnectionFactory) jndiContext.lookup("uJMSConnectionFactory");
            Connection connection = factory.createConnection();

            // Create a Session
            Session session = connection.createSession(false,
                    javax.jms.Session.AUTO_ACKNOWLEDGE);

            // set the exception listener callback
            connection.setExceptionListener(this);

            // Create a topic destination
            Destination destination = session.createTopic("TOPIC.1");
            // create the consumer
            MessageConsumer msgConsumer = session.createConsumer(destination);

            // set the message listener callback
            msgConsumer.setMessageListener(this);
            // start the connection
            connection.start();
        } catch (Exception ex) {
            ex.printStackTrace();
        }
    }

// The exception listener
    public void onException(JMSException e) {
        // print the connection exception status
        e.printStackTrace();
    }

// The message listener callback
    public void onMessage(Message msg) {
        try {
            System.err.println("Received message: " + msg);
        } catch (Exception e) {
            System.err.println("Exception occurred: " + e.getMessage());
            System.exit(-1);
        }
    }
}
