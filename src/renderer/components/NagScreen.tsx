import { getLastInfoNag, setLastInfoNag } from '../util/UseSettings';
import { setDialogConfig } from './ConfirmDialog';
import NagMarkdown from '../doc/WhatsNew.md';
import Markdown from './Markdown';

const Nag = () => <Markdown md={NagMarkdown} />;

export const openNagScreen = (forceOpen = false) => {
  if (forceOpen || window.platform.appVersion !== getLastInfoNag()) {
    setDialogConfig({
      title: `What's New`,
      body: <Nag />,
      button: "Don't show again",
      showCancel: true,
      handleConfirm: () => {
        setLastInfoNag(window.platform.appVersion);
      },
    });
  }
};
