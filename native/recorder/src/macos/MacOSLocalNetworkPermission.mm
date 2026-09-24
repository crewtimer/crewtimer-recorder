#include <Network/Network.h>
#include <dispatch/dispatch.h>

// Triggers the macOS Local Network Privacy permission dialog on first launch.
// Raw POSIX sockets (used by NDI SDK and mDNS code) do NOT trigger the dialog.
// NWBrowser (Network.framework) DOES trigger it.
extern "C" void triggerMacOSLocalNetworkPermission() {
  dispatch_queue_t queue = dispatch_queue_create(
      "net.entazza.localnetwork.trigger", DISPATCH_QUEUE_SERIAL);
  // A null domain asks Network.framework to browse the default local Bonjour
  // domain. "local" is not a fully-qualified DNS-SD domain and can cause the
  // browser to fail before macOS evaluates Local Network privacy.
  nw_browse_descriptor_t desc =
      nw_browse_descriptor_create_bonjour_service("_ndi._tcp", NULL);
  nw_browser_t browser = nw_browser_create(desc, NULL);
  nw_release(desc);
  nw_browser_set_queue(browser, queue);
  nw_browser_start(browser);
  // Keep the browser alive long enough for TCC to evaluate the request. This
  // function is called after Electron's app-ready event, so the prompt can be
  // attributed to the application and presented to the user.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), queue, ^{
    nw_browser_cancel(browser);
    nw_release(browser);
  });
}
