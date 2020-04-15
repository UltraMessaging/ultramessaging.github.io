/*file: MyListener.java
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
 * The MyListener class implements the MessageListener
 * interface by defining an onMessage method that displays
 * the contents of a TextMessage.
 *
 * This class acts as the listener for the SimpleTopicSubscriber
 * class.
 */
import javax.jms.*;

public class MyListener implements MessageListener {

    public void onMessage(Message message) {
        TextMessage msg = null;
        BytesMessage bmsg = null;

        try {
            if (message instanceof TextMessage) {
                msg = (TextMessage) message;
                System.out.println("Reading message: "
                        + msg.getText());
            } else if (message instanceof BytesMessage) {
                bmsg = (BytesMessage) message;
                System.out.println("BytesMessage " + bmsg.readInt() + ", " + bmsg.readInt());
            }
            else
            {
                 System.out.println("Message");
            }
        } catch (JMSException e) {
            System.out.println("JMSException in onMessage(): "
                    + e.toString());
        } catch (Throwable t) {
            System.out.println("Exception in onMessage():"
                    + t.getMessage());
        }
    }
}
