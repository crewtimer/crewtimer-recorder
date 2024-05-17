# Support for combining electron-store with react-use-datum

## Project configuration

1. Add ```import './store/store';``` to [main.ts](../main.ts) for main handlers.
2. Add ```import './store/store-preload.ts';``` to [preload.ts](../preload.ts).

In main.ts, add a call to setMainWindow(mainWindow) when the mainWindow is created and a call to setMainWindow(null) when it is closed.
