#pragma once

#include <cstdint>

/** Install hooks on advapi32 EventRegister + EventWriteTransfer to count PCLStats ETW events
 *  (PCLStatsEvent, PCLStatsEventV2, PCLStatsEventV3) from any caller (game or Display Commander).
 */
bool InstallPCLStatsEtwHooks();

void UninstallPCLStatsEtwHooks();

/** Returns true if PCLStats ETW hooks are installed. */
bool ArePCLStatsEtwHooksInstalled();

/** Get call counts for PCLStats marker events (EventWriteTransfer with PCLStats provider + event name).
 *  All params nullable. */
void GetPCLStatsEtwCounts(uint64_t* out_pclstats_event,
                          uint64_t* out_pclstats_event_v2,
                          uint64_t* out_pclstats_event_v3);

/** Number of PCLStats marker types we track (0..kPCLStatsMarkerTypeCount-1 = SIMULATION_START .. LATE_WARP_SUBMIT_END). */
constexpr size_t kPCLStatsMarkerTypeCount = 20;

/** Get per-marker call counts. out_counts must hold at least kPCLStatsMarkerTypeCount elements; we write counts for marker 0..19. */
void GetPCLStatsEtwCountsByMarker(uint64_t* out_counts);

/** Display name for PCLStats marker index (0..19). */
const char* GetPCLStatsMarkerTypeName(size_t index);

/** Reset PCLStats ETW counts to zero (including per-marker). */
void ResetPCLStatsEtwCounts();
