/*file: SimpleBytePublisher.java
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

 /* The SimpleBytePublisher class consists only of a main
 * method, which sends 1000000 BytesMessages.
 * Run this program in conjunction with
 * SimpleBytesSubscriber.
 *
 * Specify a delay, in milliseconds, at the command line when you run the
 * program.
 */
import javax.jms.*;
import javax.naming.*;
import java.io.*;

public class SimpleBytePublisher {

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
        if (args.length != 1) {
            System.out.println("Usage: java "
                    + "delay");
            System.exit(1);
        }

        int delay = Integer.parseInt(args[0]);

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
            Destination source = (Destination) jndiContext.lookup("BytesTopic");
            connection = factory.createConnection();
            connection.start();
            Session session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);

            MessageProducer producer = session.createProducer(null);
            BytesMessage message = session.createBytesMessage();

            byte bytes[] = new byte[1024];
            for (int i = 0; i < 1024; i++) {
                bytes[i] = (byte) (i % 255);
            }
            message.writeBytes(bytes);

            for (int i = 0; i < 1000000; i++) {
                producer.send(source, message);
                if (delay > 0) {
                    Thread.sleep(delay);
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
                    System.exit(0);
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }
    }
}


