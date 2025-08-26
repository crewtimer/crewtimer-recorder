import { UseStoredDatum } from '../store/UseElectronDatum';

export const [useLastInfoNag, setLastInfoNag, getLastInfoNag] = UseStoredDatum(
  'lastInfoNag',
  '',
);
