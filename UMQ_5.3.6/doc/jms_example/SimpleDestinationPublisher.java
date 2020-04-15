/*file:  SimpleDestinationPublisher.java
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

 /* The SimpleDestinationPublisher class consists only of a main
 * method, which sends one or more messages from a topic.
 * Run this program in conjunction with SimpleDestinationSubscriber.java
 *
 * Specify a count, message, delay on the command line.
 */
import javax.jms.*;
import javax.naming.*;
import java.io.*;

public class SimpleDestinationPublisher {

    /**
     * Main method.
     *
     * @param args     the topic used by the example
     */
    public static void main(String[] args) {
        String topicName = null;
        Context jndiContext = null;
        TopicConnectionFactory topicConnectionFactory = null;
        TopicConnection topicConnection = null;
        TopicSession topicSession = null;
        Topic topic = null;
        TopicSubscriber topicSubscriber = null;
        MessageListener topicListener = null;
        TextMessage textMessage = null;
        InputStreamReader inputStreamReader = null;
        char answer = '\0';

        /*
         * Read topic name from command line and display it.
         */
        if (args.length != 3) {
            System.out.println("Usage: java "
                    + "SimpleDestinationPublisher Count Message sleep");
            System.exit(1);
        }
      
        int cnt = Integer.parseInt(args[0]);
        String text = args[1];
        int sleepTime = Integer.parseInt(args[2]);

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
            Destination source = (Destination) jndiContext.lookup("DestTopic");
            connection = factory.createConnection();
            connection.start();
            Session session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);

            MessageProducer producer = session.createProducer(null);

            BytesMessage message = session.createBytesMessage();

            message.writeUTF(text);

            for (int i = 0; i < cnt; i++) {
                producer.send(source, message);
                if (sleepTime > 0) {
                    Thread.sleep(sleepTime);
                }
            }
            producer.send(source, session.createMessage());
            connection.close();

        } catch (Exception e) {
            e.printStackTrace();
            System.exit(1);

        } finally {
            if (connection != null) {
                try {
//                    connection.close();
                    System.exit(0);
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }
    }
}


