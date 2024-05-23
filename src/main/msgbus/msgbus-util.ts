import { HandlerResponse } from '../../renderer/msgbus/MsgbusTypes';

type MessageHandler<T, R extends HandlerResponse> = (
  dest: string,
  msg: T,
) => Promise<R>;

class MessageBus {
  private subscribers: { [destination: string]: MessageHandler<any, any> } = {};

  // Adds a subscriber with a generic message type and return type
  addSubscriber<T, R extends HandlerResponse>(
    dest: string,
    handler: MessageHandler<T, R>,
  ): void {
    // Only one subscriber per destination allowed
    this.subscribers[dest] = handler as MessageHandler<any, any>;
  }

  // Sends a message to a specific destination with a generic message type
  async sendMessage<T, R extends HandlerResponse>(
    dest: string,
    msg: T,
  ): Promise<R | undefined> {
    const handler = this.subscribers[dest];
    if (handler) {
      const result = await handler(dest, msg as any);
      return result;
    }
    console.log(`No subscriber for destination: ${dest}`);
    return { status: 'Fail', error: 'No subscriber' } as R;
  }
}

// Example usage:
export const msgbus = new MessageBus();

// Subscribing with a specific extended return type
msgbus.addSubscriber<string, { status: string; processedAt?: Date }>(
  'channel1',
  async (dest: string, msg: string) => {
    console.log(`Received message on channel1: ${msg}`);
    return { status: 'Processed', processedAt: new Date() };
  },
);

// Sending a message and handling the response
msgbus
  .sendMessage<string, { status: string; processedAt?: Date }>(
    'channel1',
    'Hello, World!',
  )
  .then((response) => {
    if (response) {
      console.log(
        `Response received with status: ${response.status} and processed at: ${response.processedAt}`,
      );
    }
    return '';
  })
  .catch((err) => {
    console.error(err);
  });
