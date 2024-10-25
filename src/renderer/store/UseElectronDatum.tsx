import { IpcRendererEvent } from 'electron';
import { UseDatum } from 'react-usedatum';
import { notifyChange } from './StoreUtil';

type DatumType = ReturnType<typeof UseDatum>;
const datumMap = new Map<string, DatumType>();
window.store.onStoredDatumUpdate(
  (_event: IpcRendererEvent, key: string, value: unknown) => {
    const datum = datumMap.get(key);
    if (datum) {
      datum[1](value);
    }
  },
);

window.mem.onDatumUpdate(
  (_event: IpcRendererEvent, key: string, value: unknown) => {
    const datum = datumMap.get(key);
    if (datum) {
      datum[1](value);
    }
  },
);

function isKeyMap(ref: any): ref is Record<string, unknown> {
  return ref !== null && typeof ref === 'object' && !Array.isArray(ref);
}

export function UseStoredDatum<T>(
  key: string,
  initialValue: T,
  onChange?: (current: T, prior: T) => void,
) {
  const datum = UseDatum<T>(initialValue, async (newValue, priorValue) => {
    notifyChange(key);
    if (newValue === undefined) {
      await window.store.delete(key);
    } else {
      await window.store.set(key, newValue);
    }
    if (onChange) {
      onChange(newValue, priorValue);
    }
  });

  datumMap.set(key, datum as unknown as DatumType);

  // Query initial value
  setTimeout(async () => {
    let value: T = (await window.store.get(key, initialValue)) as T;
    if (isKeyMap(initialValue) && isKeyMap(value)) {
      // Merge initial value with persisted value
      value = { ...initialValue, ...value };
    }
    datum[1](value);
  }, 10);
  return datum;
}

export function UseMemDatum<T>(
  key: string,
  initialValue: T,
  onChange?: (current: T, prior: T) => void,
) {
  const datum = UseDatum<T>(initialValue, async (newValue, priorValue) => {
    notifyChange(key);
    if (newValue === undefined) {
      await window.mem.delete(key);
    } else {
      await window.mem.set(key, newValue);
    }
    if (onChange) {
      onChange(newValue, priorValue);
    }
  });

  datumMap.set(key, datum as unknown as DatumType);

  // Query initial value
  setTimeout(async () => {
    let value: T = (await window.mem.get(key, initialValue)) as T;
    if (isKeyMap(initialValue) && isKeyMap(value)) {
      // Merge initial value with persisted value
      value = { ...initialValue, ...value };
    }
    datum[1](value);
  }, 10);
  return datum;
}
