import { UseStoredDatum } from '../store/UseElectronDatum';

export const [useHello] = UseStoredDatum('hello', false);
export const [useWorld] = UseStoredDatum('world', false);
