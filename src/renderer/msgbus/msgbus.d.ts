declare global {
  interface Window {
    msgbus: {
      sendMessage<T, R>(dest: string, message: T): Promise<R>;
    };
  }
}

export {};
