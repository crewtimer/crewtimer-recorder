export type OnDatumChangeHandler = (name: string) => void;

const changeHandlers = new Map<string, OnDatumChangeHandler>();
export function notifyChange(name: string) {
  const handler = changeHandlers.get(name);
  if (handler) {
    handler(name);
  }
}

export const onPropertyChange = (
  name: string,
  handler: OnDatumChangeHandler,
) => {
  changeHandlers.set(name, handler);
};
