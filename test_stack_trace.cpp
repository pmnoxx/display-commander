// Test program to demonstrate stack trace functionality
// This can be used to test the stack trace printing to DbgView

#include <windows.h>
#include <iostream>

// Simple function to create a stack trace
void TestFunction3() {
    OutputDebugStringA("=== TEST STACK TRACE FROM TestFunction3 ===\n");
    // This would normally call the stack trace function
    // For now, just print a message
    OutputDebugStringA("Stack trace would be generated here\n");
    OutputDebugStringA("=== END TEST STACK TRACE ===\n");
}

void TestFunction2() {
    OutputDebugStringA("Calling TestFunction3...\n");
    TestFunction3();
}

void TestFunction1() {
    OutputDebugStringA("Calling TestFunction2...\n");
    TestFunction2();
}

int main() {
    std::cout << "Stack Trace Test Program" << std::endl;
    std::cout << "Make sure DebugView is running to see the output!" << std::endl;
    std::cout << "Press Enter to generate stack trace..." << std::endl;
    std::cin.get();

    OutputDebugStringA("=== STARTING STACK TRACE TEST ===\n");
    TestFunction1();
    OutputDebugStringA("=== STACK TRACE TEST COMPLETE ===\n");

    std::cout << "Stack trace test complete. Check DebugView for output." << std::endl;
    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
