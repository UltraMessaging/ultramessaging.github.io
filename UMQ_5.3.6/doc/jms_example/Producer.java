/*file: Producer.java
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

/*file: Producer.java
 * The Producer class consists only of a main
 * method, which sends10 messages to a TOPIC.1. Run this program in conjunction with
 * AsyncConsumer and SyncConsumer.
 *
 */
import javax.jms.*;
import javax.naming.*;

public class Producer {

    public static void main(String[] args) {
        new Producer();
    }

    public Producer() {
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
        Connection connection = null;
        try {
            ConnectionFactory factory = (ConnectionFactory) jndiContext.lookup("uJMSConnectionFactory");
            connection = factory.createConnection();
            Session session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);
            connection.start();
            Destination destination = session.createTopic("TOPIC.1");
            MessageProducer producer = session.createProducer(destination);
            TextMessage message = session.createTextMessage();
            for (int i = 0; i < 10; i++) {
                ((TextMessage) message).setText("********* This is a test *********** ");
                producer.send(message);
                Thread.sleep(10);
                System.out.println("Sent " + message.getText());
            }
        } catch (Exception e) {
            System.out.println("JNDI API lookup failed: "
                    + e.toString());
            e.printStackTrace();
            System.exit(1);

        } finally {
            if (connection != null) {
                try {

                    connection.close();
                    System.exit(0);
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }
    }
}

