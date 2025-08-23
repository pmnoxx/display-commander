#include "display_restore.hpp"
#include "display_cache.hpp"
#include "addon.hpp"
#include <map>
#include <set>
#include <mutex>

namespace renodx::display_restore {

namespace {

struct OriginalMode {
	int width = 0;
	int height = 0;
	UINT32 refresh_num = 0;
	UINT32 refresh_den = 1;
};

std::mutex s_mutex;
std::map<std::wstring, OriginalMode> s_device_to_original; // device name -> original mode
std::set<std::wstring> s_devices_changed; // devices we modified

bool GetCurrentForDevice(const std::wstring &device_name, OriginalMode &out) {
	// Walk display cache for this device
	const auto &cache = renodx::display_cache::g_displayCache;
	for (size_t i = 0; i < cache.GetDisplayCount(); ++i) {
		const auto *disp = cache.GetDisplay(i);
		if (disp == nullptr) continue;
		if (disp->device_name == device_name) {
			out.width = disp->current_width;
			out.height = disp->current_height;
			out.refresh_num = disp->current_refresh_rate.numerator;
			out.refresh_den = disp->current_refresh_rate.denominator == 0 ? 1 : disp->current_refresh_rate.denominator;
			return true;
		}
	}
	return false;
}

bool GetDeviceNameForMonitor(HMONITOR monitor, std::wstring &out_name) {
	MONITORINFOEXW mi; mi.cbSize = sizeof(mi);
	if (GetMonitorInfoW(monitor, &mi) == FALSE) return false;
	out_name = mi.szDevice;
	return true;
}

bool ApplyModeForDevice(const std::wstring &device_name, const OriginalMode &mode) {
	DEVMODEW dm{}; dm.dmSize = sizeof(dm);
	dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
	dm.dmPelsWidth = static_cast<DWORD>(mode.width);
	dm.dmPelsHeight = static_cast<DWORD>(mode.height);
	// Round if rational
	double hz = mode.refresh_den == 0 ? 0.0 : static_cast<double>(mode.refresh_num) / static_cast<double>(mode.refresh_den);
	if (hz <= 0.0) {
		// Fallback to current registry frequency; leave frequency field unset
		dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
	} else {
		dm.dmDisplayFrequency = static_cast<DWORD>(hz + 0.5);
	}
	LONG res = ChangeDisplaySettingsExW(device_name.c_str(), &dm, nullptr, 0, nullptr);
	if (res == DISP_CHANGE_SUCCESSFUL) return true;
	// Try with CDS_UPDATEREGISTRY as fallback
	res = ChangeDisplaySettingsExW(device_name.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
	return res == DISP_CHANGE_SUCCESSFUL;
}

} // anonymous namespace

// use ::s_auto_restore_resolution_on_close from globals.cpp via addon.hpp include

void MarkOriginalForMonitor(HMONITOR monitor) {
	std::wstring device;
	if (!GetDeviceNameForMonitor(monitor, device)) return;
	MarkOriginalForDeviceName(device);
}

void MarkOriginalForDeviceName(const std::wstring &device_name) {
	std::scoped_lock lock(s_mutex);
	if (s_device_to_original.find(device_name) != s_device_to_original.end()) return;
	OriginalMode mode{};
	if (GetCurrentForDevice(device_name, mode)) {
		s_device_to_original.emplace(device_name, mode);
	}
}

void MarkOriginalForDisplayIndex(int display_index) {
	if (display_index < 0) return;
	const auto *disp = renodx::display_cache::g_displayCache.GetDisplay(static_cast<size_t>(display_index));
	if (disp == nullptr) return;
	MarkOriginalForDeviceName(disp->device_name);
}

void MarkDeviceChangedByDisplayIndex(int display_index) {
	if (display_index < 0) return;
	const auto *disp = renodx::display_cache::g_displayCache.GetDisplay(static_cast<size_t>(display_index));
	if (disp == nullptr) return;
	MarkDeviceChangedByDeviceName(disp->device_name);
}

void MarkDeviceChangedByDeviceName(const std::wstring &device_name) {
	std::scoped_lock lock(s_mutex);
	// Ensure we have original captured first
	if (s_device_to_original.find(device_name) == s_device_to_original.end()) {
		OriginalMode mode{};
		if (GetCurrentForDevice(device_name, mode)) {
			s_device_to_original.emplace(device_name, mode);
		}
	}
	s_devices_changed.insert(device_name);
}

// (ApplyModeForDevice is in anonymous namespace above)

void RestoreAll() {
	std::set<std::wstring> devices;
	{
		std::scoped_lock lock(s_mutex);
		devices = s_devices_changed; // copy to avoid holding lock during API calls
	}
	if (devices.empty()) return;

	for (const auto &device : devices) {
		OriginalMode orig{};
		{
			std::scoped_lock lock(s_mutex);
			auto it = s_device_to_original.find(device);
			if (it == s_device_to_original.end()) continue;
			orig = it->second;
		}
		ApplyModeForDevice(device, orig);
	}
}

void RestoreAllIfEnabled() {
	if (!::s_auto_restore_resolution_on_close) return;
	RestoreAll();
}

void Clear() {
	std::scoped_lock lock(s_mutex);
	s_device_to_original.clear();
	s_devices_changed.clear();
}

bool HasAnyChanges() {
	std::scoped_lock lock(s_mutex);
	return !s_devices_changed.empty();
}

bool RestoreDisplayByDeviceName(const std::wstring &device_name) {
	OriginalMode orig{};
	{
		std::scoped_lock lock(s_mutex);
		auto it = s_device_to_original.find(device_name);
		if (it == s_device_to_original.end()) return false;
		orig = it->second;
	}
	return ApplyModeForDevice(device_name, orig);
}

bool RestoreDisplayByIndex(int display_index) {
	if (display_index < 0) return false;
	const auto *disp = renodx::display_cache::g_displayCache.GetDisplay(static_cast<size_t>(display_index));
	if (disp == nullptr) return false;
	return RestoreDisplayByDeviceName(disp->device_name);
}

} // namespace renodx::display_restore


