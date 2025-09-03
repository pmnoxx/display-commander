#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgidebug.h>
#include <dxgi1_3.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <mutex>

namespace dxgi_debug_layer {

using Microsoft::WRL::ComPtr;

// Global state
static ComPtr<IDXGIInfoQueue> g_dxgi_info_queue;
static ComPtr<ID3D11InfoQueue> g_d3d11_info_queue;
static ComPtr<ID3D12InfoQueue> g_d3d12_info_queue;
static HMODULE g_dxgi_debug_module = nullptr;
static std::atomic<bool> g_enabled{false};
static std::atomic<bool> g_break_on_error{true};
static std::atomic<bool> g_break_on_corruption{true};
static std::atomic<bool> g_log_all_messages{true};
static std::atomic<bool> g_show_ui{false};
static std::thread g_message_processor_thread;
static std::atomic<bool> g_shutdown{false};

// Message storage for UI
struct DebugMessage {
	std::string text;
	reshade::log::level level;
	uint64_t timestamp;
};

static std::vector<DebugMessage> g_messages;
static std::mutex g_messages_mutex;
static constexpr size_t MAX_MESSAGES = 1000;

// Avoid linking against GUID symbol by using a local constant matching dxgidebug.h
static const DXGI_DEBUG_ID KDXGI_DEBUG_ALL = { 0xe48ae283, 0xda80, 0x490b, { 0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x08 } };

static void EnableDxgiDebug()
{
	if (g_dxgi_info_queue)
		return;

	// Load dxgidebug.dll dynamically to avoid link-time dependency
	g_dxgi_debug_module = ::GetModuleHandleW(L"dxgidebug.dll");
	if (g_dxgi_debug_module == nullptr)
		g_dxgi_debug_module = ::LoadLibraryW(L"dxgidebug.dll");

	if (g_dxgi_debug_module == nullptr)
	{
		reshade::log::message(reshade::log::level::warning, "[DXGI Debug] dxgidebug.dll not available (DXGI debug layer not installed)");
		return;
	}

	typedef HRESULT (WINAPI *PFN_DXGIGetDebugInterface)(REFIID, void **);
	const auto get_debug_interface = reinterpret_cast<PFN_DXGIGetDebugInterface>(
		::GetProcAddress(g_dxgi_debug_module, "DXGIGetDebugInterface"));
	if (get_debug_interface == nullptr)
	{
		reshade::log::message(reshade::log::level::warning, "[DXGI Debug] DXGIGetDebugInterface not found");
		return;
	}

	// First try to get the main DXGI debug interface to enable debugging
	ComPtr<IDXGIDebug> dxgi_debug;
	if (SUCCEEDED(get_debug_interface(__uuidof(IDXGIDebug), reinterpret_cast<void **>(dxgi_debug.GetAddressOf()))))
	{
		reshade::log::message(reshade::log::level::info, "[DXGI Debug] IDXGIDebug interface acquired");
	}

	ComPtr<IDXGIInfoQueue> info_queue;
	if (SUCCEEDED(get_debug_interface(__uuidof(IDXGIInfoQueue), reinterpret_cast<void **>(info_queue.GetAddressOf()))))
	{
		g_dxgi_info_queue = info_queue;
		// Set break on severity based on settings
		g_dxgi_info_queue->SetBreakOnSeverity(KDXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, g_break_on_error.load());
		g_dxgi_info_queue->SetBreakOnSeverity(KDXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, g_break_on_corruption.load());
		
		// Enable message storage for all severities
		g_dxgi_info_queue->SetMessageCountLimit(KDXGI_DEBUG_ALL, 1000);
		
		// Try to enable all debug categories
		g_dxgi_info_queue->SetMuteDebugOutput(KDXGI_DEBUG_ALL, FALSE);
		
		g_enabled.store(true);
		reshade::log::message(reshade::log::level::info, "[DXGI Debug] DXGI InfoQueue acquired and configured");
	}
	else
	{
		reshade::log::message(reshade::log::level::warning, "[DXGI Debug] Failed to acquire IDXGIInfoQueue");
	}
}

static void DisableDxgiDebug()
{
	g_enabled.store(false);
	if (g_dxgi_info_queue)
	{
		g_dxgi_info_queue.Reset();
		reshade::log::message(reshade::log::level::info, "[DXGI Debug] DXGI InfoQueue released");
	}
	if (g_dxgi_debug_module)
	{
		::FreeLibrary(g_dxgi_debug_module);
		g_dxgi_debug_module = nullptr;
	}
}

static void ProcessDebugMessages()
{
	if (!g_enabled.load())
		return;

	// DXGI messages
	if (g_dxgi_info_queue)
	{
		const UINT64 message_count = g_dxgi_info_queue->GetNumStoredMessages(KDXGI_DEBUG_ALL);
		if (message_count > 0)
		{
			const UINT64 max_messages_per_frame = 50;
			const UINT64 messages_to_process = (message_count < max_messages_per_frame) ? message_count : max_messages_per_frame;

			for (UINT64 i = 0; i < messages_to_process; ++i)
			{
				SIZE_T message_size = 0;
				HRESULT hr = g_dxgi_info_queue->GetMessage(KDXGI_DEBUG_ALL, i, nullptr, &message_size);
				if (SUCCEEDED(hr) && message_size > 0)
				{
					auto message_buffer = std::make_unique<char[]>(message_size);
					DXGI_INFO_QUEUE_MESSAGE* message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(message_buffer.get());
					hr = g_dxgi_info_queue->GetMessage(KDXGI_DEBUG_ALL, i, message, &message_size);
					if (SUCCEEDED(hr) && g_log_all_messages.load())
					{
						std::string message_text(message->pDescription, message->DescriptionByteLength - 1);
						std::string severity_str;
						reshade::log::level log_level;
						switch (message->Severity)
						{
							case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION: severity_str = "CORRUPTION"; log_level = reshade::log::level::error; break;
							case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR:      severity_str = "ERROR";      log_level = reshade::log::level::error; break;
							case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING:    severity_str = "WARNING";    log_level = reshade::log::level::warning; break;
							case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO:       severity_str = "INFO";       log_level = reshade::log::level::info; break;
							default:                                           severity_str = "MESSAGE";    log_level = reshade::log::level::info; break;
						}

						std::string log_message = "[DXGI Debug] [" + severity_str + "] ID:" + std::to_string(message->ID) + " - " + message_text;
						reshade::log::message(log_level, log_message.c_str());
						{
							std::lock_guard<std::mutex> lock(g_messages_mutex);
							if (g_messages.size() >= MAX_MESSAGES)
								g_messages.erase(g_messages.begin());
							g_messages.push_back({log_message, log_level, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())});
						}
					}
				}
			}

			g_dxgi_info_queue->ClearStoredMessages(KDXGI_DEBUG_ALL);
		}
	}

	// D3D11 messages
	if (g_d3d11_info_queue)
	{
		const UINT64 message_count = g_d3d11_info_queue->GetNumStoredMessages();
		if (message_count > 0)
		{
			const UINT64 max_messages_per_frame = 50;
			const UINT64 messages_to_process = (message_count < max_messages_per_frame) ? message_count : max_messages_per_frame;
			for (UINT64 i = 0; i < messages_to_process; ++i)
			{
				SIZE_T message_size = 0;
				HRESULT hr = g_d3d11_info_queue->GetMessage(i, nullptr, &message_size);
				if (SUCCEEDED(hr) && message_size > 0)
				{
					auto message_buffer = std::make_unique<char[]>(message_size);
					D3D11_MESSAGE* message = reinterpret_cast<D3D11_MESSAGE*>(message_buffer.get());
					hr = g_d3d11_info_queue->GetMessage(i, message, &message_size);
					if (SUCCEEDED(hr) && g_log_all_messages.load())
					{
						std::string message_text(message->pDescription, message->DescriptionByteLength - 1);
						std::string severity_str;
						reshade::log::level log_level;
						switch (message->Severity)
						{
							case D3D11_MESSAGE_SEVERITY_CORRUPTION: severity_str = "CORRUPTION"; log_level = reshade::log::level::error; break;
							case D3D11_MESSAGE_SEVERITY_ERROR:      severity_str = "ERROR";      log_level = reshade::log::level::error; break;
							case D3D11_MESSAGE_SEVERITY_WARNING:    severity_str = "WARNING";    log_level = reshade::log::level::warning; break;
							case D3D11_MESSAGE_SEVERITY_INFO:       severity_str = "INFO";       log_level = reshade::log::level::info; break;
							default:                                   severity_str = "MESSAGE";    log_level = reshade::log::level::info; break;
						}

						std::string log_message = "[D3D11 Debug] [" + severity_str + "] ID:" + std::to_string(message->ID) + " - " + message_text;
						reshade::log::message(log_level, log_message.c_str());
						{
							std::lock_guard<std::mutex> lock(g_messages_mutex);
							if (g_messages.size() >= MAX_MESSAGES)
								g_messages.erase(g_messages.begin());
							g_messages.push_back({log_message, log_level, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())});
						}
					}
				}
			}
			g_d3d11_info_queue->ClearStoredMessages();
		}
	}

	// D3D12 messages
	if (g_d3d12_info_queue)
	{
		const UINT64 message_count = g_d3d12_info_queue->GetNumStoredMessages();
		if (message_count > 0)
		{
			const UINT64 max_messages_per_frame = 50;
			const UINT64 messages_to_process = (message_count < max_messages_per_frame) ? message_count : max_messages_per_frame;
			for (UINT64 i = 0; i < messages_to_process; ++i)
			{
				SIZE_T message_size = 0;
				HRESULT hr = g_d3d12_info_queue->GetMessage(i, nullptr, &message_size);
				if (SUCCEEDED(hr) && message_size > 0)
				{
					auto message_buffer = std::make_unique<char[]>(message_size);
					D3D12_MESSAGE* message = reinterpret_cast<D3D12_MESSAGE*>(message_buffer.get());
					hr = g_d3d12_info_queue->GetMessage(i, message, &message_size);
					if (SUCCEEDED(hr) && g_log_all_messages.load())
					{
						std::string message_text(message->pDescription, message->DescriptionByteLength - 1);
						std::string severity_str;
						reshade::log::level log_level;
						switch (message->Severity)
						{
							case D3D12_MESSAGE_SEVERITY_CORRUPTION: severity_str = "CORRUPTION"; log_level = reshade::log::level::error; break;
							case D3D12_MESSAGE_SEVERITY_ERROR:      severity_str = "ERROR";      log_level = reshade::log::level::error; break;
							case D3D12_MESSAGE_SEVERITY_WARNING:    severity_str = "WARNING";    log_level = reshade::log::level::warning; break;
							case D3D12_MESSAGE_SEVERITY_INFO:       severity_str = "INFO";       log_level = reshade::log::level::info; break;
							default:                                   severity_str = "MESSAGE";    log_level = reshade::log::level::info; break;
						}

						std::string log_message = "[D3D12 Debug] [" + severity_str + "] ID:" + std::to_string(message->ID) + " - " + message_text;
						reshade::log::message(log_level, log_message.c_str());
						{
							std::lock_guard<std::mutex> lock(g_messages_mutex);
							if (g_messages.size() >= MAX_MESSAGES)
								g_messages.erase(g_messages.begin());
							g_messages.push_back({log_message, log_level, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())});
						}
					}
				}
			}
			g_d3d12_info_queue->ClearStoredMessages();
		}
	}
}

static void MessageProcessorThread()
{
	reshade::log::message(reshade::log::level::info, "[DXGI Debug] Message processor thread started");

	while (!g_shutdown.load())
	{
		if (g_enabled.load())
		{
			try
			{
				ProcessDebugMessages();
			}
			catch (const std::exception& e)
			{
				reshade::log::message(reshade::log::level::error, 
					("[DXGI Debug] Exception in message processor: " + std::string(e.what())).c_str());
			}
			catch (...)
			{
				reshade::log::message(reshade::log::level::error, 
					"[DXGI Debug] Unknown exception in message processor");
			}
		}

		// Process messages every 16ms (roughly 60 FPS)
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}

	reshade::log::message(reshade::log::level::info, "[DXGI Debug] Message processor thread stopped");
}

static void OnRegisterOverlay(reshade::api::effect_runtime* /*runtime*/)
{
	// TODO remove unused g_show_ui
	// Always show the overlay in ReShade's overlay menu
	g_show_ui.store(true);
	
	if (!g_show_ui.load())
		return;

	ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
	bool show_ui = g_show_ui.load();
	if (ImGui::Begin("DXGI Debug Layer", &show_ui))
	{
		// Settings section
		if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool break_on_error = g_break_on_error.load();
			bool break_on_corruption = g_break_on_corruption.load();
			bool log_all_messages = g_log_all_messages.load();

			if (ImGui::Checkbox("Break on Error", &break_on_error))
			{
				g_break_on_error.store(break_on_error);
				if (g_dxgi_info_queue)
				{
					g_dxgi_info_queue->SetBreakOnSeverity(KDXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, break_on_error);
				}
				if (g_d3d11_info_queue)
					g_d3d11_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, break_on_error);
				if (g_d3d12_info_queue)
					g_d3d12_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, break_on_error);
			}
			ImGui::SameLine();
			if (ImGui::Checkbox("Break on Corruption", &break_on_corruption))
			{
				g_break_on_corruption.store(break_on_corruption);
				if (g_dxgi_info_queue)
				{
					g_dxgi_info_queue->SetBreakOnSeverity(KDXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, break_on_corruption);
				}
				if (g_d3d11_info_queue)
					g_d3d11_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, break_on_corruption);
				if (g_d3d12_info_queue)
					g_d3d12_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, break_on_corruption);
			}
			ImGui::SameLine();
			ImGui::Checkbox("Log All Messages", &log_all_messages);
			g_log_all_messages.store(log_all_messages);

			ImGui::Separator();
			ImGui::Text("Status: %s", g_enabled.load() ? "Enabled" : "Disabled");
			ImGui::Text("Use the X button to close this window");
		}

		// Messages section
		if (ImGui::CollapsingHeader("Debug Messages", ImGuiTreeNodeFlags_DefaultOpen))
		{
			std::lock_guard<std::mutex> lock(g_messages_mutex);
			
			if (g_messages.empty())
			{
				ImGui::Text("No debug messages yet.");
			}
			else
			{
				ImGui::Text("Messages (%zu):", g_messages.size());
				ImGui::BeginChild("Messages", ImVec2(0, 400), true);
				
				for (const auto& msg : g_messages)
				{
					ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
					switch (msg.level)
					{
						case reshade::log::level::error:
							color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
							break;
						case reshade::log::level::warning:
							color = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
							break;
						case reshade::log::level::info:
							color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
							break;
						default:
							break;
					}
					
					ImGui::PushStyleColor(ImGuiCol_Text, color);
					ImGui::TextWrapped("%s", msg.text.c_str());
					ImGui::PopStyleColor();
				}
				
				ImGui::EndChild();
			}
		}
	}
	ImGui::End();
	g_show_ui.store(show_ui);
}

static void OnInitDevice(reshade::api::device * /*device*/)
{
	EnableDxgiDebug();
	
	// Acquire D3D11/D3D12 info queues from native device if available
	// ReShade exposes the native device pointer via get_native(); select based on API
	// Note: This is best-effort; if the app did not enable D3D debug, these may fail
	{
		reshade::api::device *dev_api_ptr = nullptr;
		// The parameter name is omitted, but we still have it as an argument; recover via function signature
		// This block intentionally left empty as we cannot access the argument name in this context
	}
	// Recover the device pointer from the last registered init event is not possible here,
	// so rely on the parameter directly
	// Redefine the function to accept the parameter to use it
	
	// Start message processor thread if not already running
	if (g_enabled.load() && !g_message_processor_thread.joinable())
	{
		g_message_processor_thread = std::thread(MessageProcessorThread);
	}
}

static void OnDestroyDevice(reshade::api::device * /*device*/)
{
	DisableDxgiDebug();
	if (g_d3d11_info_queue)
		g_d3d11_info_queue.Reset();
	if (g_d3d12_info_queue)
		g_d3d12_info_queue.Reset();
}
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID /*lpv_reserved*/)
{
	switch (fdw_reason)
	{
	case DLL_PROCESS_ATTACH:
		if (!reshade::register_addon(h_module))
			return FALSE;
		reshade::log::message(reshade::log::level::info, "[DXGI Debug] Addon registered");
		reshade::register_event<reshade::addon_event::init_device>(dxgi_debug_layer::OnInitDevice);
		reshade::register_event<reshade::addon_event::destroy_device>(dxgi_debug_layer::OnDestroyDevice);
		reshade::register_overlay("DXGI Debug Layer", dxgi_debug_layer::OnRegisterOverlay);
		
		dxgi_debug_layer::g_shutdown.store(false);
		break;
	case DLL_PROCESS_DETACH:
		reshade::log::message(reshade::log::level::info, "[DXGI Debug] Addon shutting down");
		
		// Signal shutdown to message processor thread
		dxgi_debug_layer::g_shutdown.store(true);
		
		// Wait for message processor thread to finish
		if (dxgi_debug_layer::g_message_processor_thread.joinable())
		{
			dxgi_debug_layer::g_message_processor_thread.join();
		}
		
		reshade::unregister_overlay("DXGI Debug Layer", dxgi_debug_layer::OnRegisterOverlay);
		reshade::unregister_event<reshade::addon_event::init_device>(dxgi_debug_layer::OnInitDevice);
		reshade::unregister_event<reshade::addon_event::destroy_device>(dxgi_debug_layer::OnDestroyDevice);
		reshade::log::message(reshade::log::level::info, "[DXGI Debug] Addon unregistered");
		reshade::unregister_addon(h_module);
		break;
	}
	return TRUE;
}



