/*file: Request.java
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
 * Run this program in conjunction with Reply.
 */
import javax.jms.*;
import javax.naming.*;

public class Request {

    public static void main(String[] args) {
        Context jndiContext = null;
        ConnectionFactory connectionFactory = null;
        Connection connection = null;
        Session session = null;

        /*
         * Create a JNDI API InitialContext.
         */
        try {
            jndiContext = new InitialContext();
        } catch (NamingException e) {
            System.out.println("Could not create JNDI API "
                    + "context: " + e.toString());
            System.exit(1);
        }

        /*
         * Look up connection factory
         */
        try {
            connectionFactory = (ConnectionFactory) jndiContext.lookup("uJMSConnectionFactory");
        } catch (NamingException e) {
            System.out.println("JNDI API lookup failed: " + e.toString());
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
            connection = connectionFactory.createConnection();


            session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);

            // Create request and reply destinations
            Destination requestTopic = (Destination) jndiContext.lookup("RequestTopic");
            Destination replyTopic = (Destination) jndiContext.lookup("ReplyTopic");

            MessageProducer requestProducer = session.createProducer(requestTopic);
            MessageConsumer replyConsumer = session.createConsumer(replyTopic);

            // start the connection - This needs to be started after the replyConsumer is created.
            connection.start();

            TextMessage requestMessage = session.createTextMessage();
            requestMessage.setText("Hello world.");
            requestMessage.setJMSReplyTo(replyTopic);
            requestProducer.send(requestMessage);
            System.out.println("Sent Message " + requestMessage);

            // Wait for the reply
            TextMessage replyMessage = (TextMessage) replyConsumer.receive();
            System.err.println("Reply message: " + replyMessage.getText());

            session.close();
        } catch (JMSException e) {
            System.out.println("Exception occurred: "
                    + e.toString());
        } catch (Exception e) {
            System.out.println("Exception occurred: "
                    + e.toString());
        } finally {
            if (connection != null) {
                try {
                    connection.close();
                } catch (JMSException e) {
                }
            }
        }
    }
}


