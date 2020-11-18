#include <iostream>
#include <string>
#include <Windows.h>
#include "../RunInSandbox/Sandboxing.hpp"
#include "../Testcontrol/Socket.hpp"



static void TryOpenFile (std::string path) {
    HandleWrap handle;
    handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, /*no sharing*/0, /*no security*/NULL, OPEN_EXISTING, /*no overlapped*/0, NULL);
    if (!handle)
        throw std::runtime_error("Unable to open file");

    // attempt to write to device
    char buffer[] = "X";
    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(handle, buffer, sizeof(buffer), &bytesWritten, /*no overlapped*/NULL);
    if (!ok || (bytesWritten == 0))
        throw std::runtime_error("Write failed");
}

static void TryNetworkConnection (const std::string& host, std::string port) {
    SocketWrap sock;
    if (!sock.TryToConnect(host, stoi(port)))
        throw std::runtime_error("unable to connect");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Device path argument mising\n";
        return -1;
    }

    try {
        if (argc == 2) {
            TryOpenFile(argv[1]); // e.g. "COM3";
            std::cout << "File open and write succeeded\n";
        } else if (argc == 3) {
            TryNetworkConnection(argv[1], argv[2]);
            std::cout << "Network connection succeeded\n";
        }
    } catch (const std::exception & e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
