#pragma once

// Config I/O worker (config_worker.cpp).
// Owns the freeze of live ini text, the 3 second deadline, and DisplayCommander.ini disk writes.
//
// Thread contract:
// - Started lazily on the first save_config / flush. Never from DllMain.
// - save_config and the flush requests are safe from the UI thread and the monitoring thread.
//   They only update queue state under config_mutex_ and wake this thread.
// - This thread is the only one that serializes the live ini for the queue and writes DisplayCommander.ini for it.
// - Flush callers wait until their generation completes. Exit flush then joins the thread.
//   Overlay-close flush waits and does not join.
