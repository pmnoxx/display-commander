#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Testing Debug Output Hooks..." << std::endl;
    std::cout << "This program will call OutputDebugStringA and OutputDebugStringW multiple times." << std::endl;
    std::cout << "Check ReShade.log for captured debug output." << std::endl;
    std::cout << "Press Enter to start...";
    std::cin.get();

    // Test OutputDebugStringA calls
    for (int i = 0; i < 5; i++) {
        std::string message = "Test OutputDebugStringA message " + std::to_string(i + 1);
        OutputDebugStringA(message.c_str());
        std::cout << "Sent: " << message << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Test OutputDebugStringW calls
    for (int i = 0; i < 5; i++) {
        std::wstring message = L"Test OutputDebugStringW message " + std::to_wstring(i + 1);
        OutputDebugStringW(message.c_str());
        std::wcout << L"Sent: " << message << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Test with nullptr (should be logged as nullptr)
    OutputDebugStringA(nullptr);
    OutputDebugStringW(nullptr);
    std::cout << "Sent nullptr to both functions" << std::endl;

    std::cout << "Test completed. Check ReShade.log for results." << std::endl;
    std::cout << "Press Enter to exit...";
    std::cin.get();

    return 0;
}
