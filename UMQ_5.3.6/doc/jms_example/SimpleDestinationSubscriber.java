/*file: SimpleDestinationSubscriber.java
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

 /* The SimpleDestinationSubscriber class consists only of a main
 * method, which receives one or more messages from a topic (DestTopic) using
 * synchronous message delivery.
 * Run this program in conjunction with SimpleDestinationPublisher.
 *
 */
import javax.jms.*;
import javax.naming.*;

public class SimpleDestinationSubscriber {

    /**
     * Main method.
     *
     * @param args     the topic used by the example
     */
    public static void main(String[] args) {
        Context jndiContext = null;

        /*
         * Read topic name from command line and display it.
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
            e.printStackTrace();
            System.exit(1);
        }

        /*
         * Look up connection factory and topic.  If either does
         * not exist, exit.
         */
        try {
            ConnectionFactory factory = (ConnectionFactory) jndiContext.lookup("uJMSConnectionFactory");
            Topic source = (Topic) jndiContext.lookup("DestTopic");
            Connection connection = factory.createConnection();

            Session session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);
            MessageConsumer consumer = session.createConsumer(source);

            connection.start();
            long time = System.currentTimeMillis();
            long msgCnt = 0;
            while (true) {
                Message message = consumer.receive();
                if (message instanceof BytesMessage == false) {
                    break;
                }
                if(System.currentTimeMillis() - time > 1000)
                {
                    System.out.println("Msg Cnt  = "+msgCnt+", "+((BytesMessage)message).readUTF());
                    time = System.currentTimeMillis();
                    msgCnt = 0;
                }
                msgCnt++;
            }

            connection.close();
            jndiContext.close();
            System.exit(0);
        } catch (Exception ex) {
            ex.printStackTrace();
            System.exit(1);
        }
    }
}
