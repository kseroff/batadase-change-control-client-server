#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <iostream>
#include <thread>
#include <string>
#include <sstream>
#include <map>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <numeric>
#include "Database.h"

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

class ChatServer {
    atomic<bool> running;
    int port;
    WSADATA wsaData;
    SOCKET serverSocket;
    ClientDatabase clientDatabase;
    mutex clientsMutex;
    HANDLE serverEventHandle;
    map<SOCKET, HANDLE> clientEvents;

    struct ClientData {
        SOCKET clientSocket;
        bool isAuthorized = false;
        DatabaseManager databaseManager;

        ClientData(SOCKET clientSocket) : clientSocket(clientSocket) {}
    };

public:
    ChatServer(int port) : port(port), serverSocket(INVALID_SOCKET), running(false), serverEventHandle(NULL) {}

    ~ChatServer() {
        // Закрываем события клиентов
        for (auto& entry : clientEvents) {
            CloseHandle(entry.second);
        }
        if (serverEventHandle) {
            CloseHandle(serverEventHandle);
        }
        closesocket(serverSocket);
        WSACleanup();
    }

    void Start() {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            cerr << "Не удалось инициализировать Winsock." << endl;
            return;
        }

        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == INVALID_SOCKET) {
            cerr << "Не удалось создать сокет." << endl;
            WSACleanup();
            return;
        }

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr); // Используем IP 127.0.0.1
        serverAddr.sin_port = htons(port);  // Порт для прослушивания

        if (::bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            cerr << "Не удалось привязать сокет." << endl;
            closesocket(serverSocket);
            WSACleanup();
            return;
        }

        if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
            cerr << "Не удалось начать прослушивание." << endl;
            closesocket(serverSocket);
            WSACleanup();
            return;
        }

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &serverAddr.sin_addr, ipStr, sizeof(ipStr));
        cout << "Сервер запущен на IP: " << ipStr << " (port: 777)" << endl;


        // Используем WSAEventSelect для асинхронных уведомлений
        serverEventHandle = WSACreateEvent();
        if (serverEventHandle == NULL) {
            cerr << "Не удалось создать событие." << endl;
            closesocket(serverSocket);
            WSACleanup();
            return;
        }

        // Связываем событие с серверным сокетом
        if (WSAEventSelect(serverSocket, serverEventHandle, FD_ACCEPT | FD_CLOSE) == SOCKET_ERROR) {
            cerr << "Не удалось привязать событие к серверному сокету." << endl;
            closesocket(serverSocket);
            WSACleanup();
            return;
        }

        running = true;
        std::thread([this]() { ProcessEvents(); }).detach();
    }

    void Stop() {
        if (running) {
            running = false;

            if (serverSocket != INVALID_SOCKET) {
                closesocket(serverSocket);
                serverSocket = INVALID_SOCKET;
            }

            if (serverEventHandle) {
                CloseHandle(serverEventHandle);
                serverEventHandle = NULL;
            }

            WSACleanup();
            cout << "Сервер остановлен." << endl;
        }
    }

private:
    void AcceptConnections() {
        while (running) {
            sockaddr_in clientAddr;
            int addrLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);

            if (clientSocket != INVALID_SOCKET) {
                cout << "Новое подключение!" << endl;

                // Создаем событие для клиента
                HANDLE clientEventHandle = WSACreateEvent();
                if (clientEventHandle == NULL) {
                    cerr << "Не удалось создать событие для клиента." << endl;
                    closesocket(clientSocket);
                    continue;
                }

                // Устанавливаем событие для клиентского сокета
                if (WSAEventSelect(clientSocket, clientEventHandle, FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR) {
                    cerr << "Ошибка при установке события для клиентского сокета." << endl;
                    closesocket(clientSocket);
                    CloseHandle(clientEventHandle);
                    continue;
                }

                // Синхронизируем доступ к clientEvents
                {
                    lock_guard<mutex> lock(clientsMutex);  // Блокируем доступ к clientEvents
                    clientEvents[clientSocket] = clientEventHandle;  // Добавляем клиента в контейнер
                }

                // Создаем поток для обработки клиента
                thread([this, clientSocket]() { HandleClient(clientSocket); }).detach();
            }
        }
    }

    void HandleClient(SOCKET clientSocket) {
        char buffer[1024];
        WSABUF wsabuf;
        wsabuf.buf = buffer;
        wsabuf.len = sizeof(buffer);

        DWORD bytesReceived = 0;
        DWORD flags = 0;

        ClientData clientData(clientSocket);
        while (running) {
            int result = WSARecv(clientData.clientSocket, &wsabuf, 1, &bytesReceived, &flags, NULL, NULL);
            if (result == SOCKET_ERROR) {
                int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK) {
                    // Нет данных для чтения, продолжаем ожидание
                    continue;
                }
                cerr << "Пользователь "<< clientData.databaseManager.getUser() <<" отключился" << endl;

                break;
            }

            if (bytesReceived == 0) {
                cout << "Клиент отключился." << endl;
                break;
            }

            if (bytesReceived == 0) {
                cout << "Клиент отключился." << endl;
                break;
            }

            buffer[bytesReceived] = '\0';
            string receiveMessage(buffer);

            // Обрабатываем сообщение
            string response;
            HandleProtocol(clientData, receiveMessage, response);

        }

        // После завершения работы с клиентом удаляем его из списка
        {
            lock_guard<mutex> lock(clientsMutex);
            clientEvents.erase(clientSocket);
        }

        closesocket(clientData.clientSocket);
    }

    void HandleProtocol(ClientData& clientData, string receiveMessage, string& response) {
        istringstream iss(receiveMessage);
        string action;
        getline(iss, action, '\n');

        if (action == "LOGOUT") {
            response = "Goodbye!";
            SendAsync(clientData.clientSocket, response.c_str(), response.size());

            closesocket(clientData.clientSocket);
            clientData.isAuthorized = false;
            return; 
        }
        else if (clientData.isAuthorized) {
            if (action == "GET_ALL_TABLE") {
                vector<string> vec = clientData.databaseManager.getTables();
                response = accumulate(
                    vec.begin(), vec.end(), string(),
                    [](const string& a, const string& b) {
                        return a.empty() ? b : a + "|" + b;
                    }
                );
                SendAsync(clientData.clientSocket, response.c_str(), response.size());
            }
            else if (action == "GET_MY_TABLE") {
                vector<string> vec = clientData.databaseManager.getAccessibleTables();
                response = accumulate(
                    vec.begin(), vec.end(), string(),
                    [](const string& a, const string& b) {
                        return a.empty() ? b : a + "|" + b;
                    }
                );
                SendAsync(clientData.clientSocket, response.c_str(), response.size());
            }
            else if (action == "GET_DUMP") {
                const string& output_file = "C:\\Users\\Kseroff\\AppData\\Local\\Temp\\" + clientData.databaseManager.getUser() + "Dump.json";
                if (clientData.databaseManager.dumpTablesToJson(output_file)) {
                    SendFileAsync(clientData.clientSocket, output_file);
                }
                else {
                    response = "error";
                }
            }
            else if (action == "SET_DUMP") {
                string fileName;
                getline(iss, fileName, '\n');
                string response = "READY";
                send(clientData.clientSocket, response.c_str(), response.size(), 0);

                // Принимаем файл
                string filePath = ReceiveFile(clientData.clientSocket, fileName);

                if (!filePath.empty()) {
                    cout << "Файл успешно получен и сохранён: " << filePath << endl;
                    if (clientData.databaseManager.updateTablesFromJson(filePath)) {
                        cout << "Данные обновлены успешно" << endl;
                    }
                    else {
                        cout << "Данные не обновлены" << endl;
                    }
                }
                else {
                    cerr << "Ошибка при получении файла." << endl;
                }
            }
            else {
                response = "Invalid";
                SendAsync(clientData.clientSocket, response.c_str(), response.size());
            }
        }
        else {
            if (action == "LOGIN") {
                string username, password;
                getline(iss, username, '\n');
                getline(iss, password, '\n');

                if (clientDatabase.AuthenticateUser(username, password)) {
                    string role = clientDatabase.GetUserRole(username);
                    clientData.isAuthorized = true;
                    clientData.databaseManager.setUser(username);
                    clientData.databaseManager.setRole(role);
                    response = "true";
                }
                else {
                    response = "false";
                }
                SendAsync(clientData.clientSocket, response.c_str(), response.size());
            }
            else if (action == "REGISTER") {
                string username, password, name;
                getline(iss, username, '\n');
                getline(iss, password, '\n');
                getline(iss, name, '\n');

                string role = "user";
                if (clientDatabase.RegisterUser(username, password, name, role)) {
                    clientData.isAuthorized = true;
                    clientData.databaseManager.setUser(username);
                    clientData.databaseManager.setRole(role);
                    response = "true";
                }
                else {
                    response = "false";
                }
                SendAsync(clientData.clientSocket, response.c_str(), response.size());
            }
        }
    }

    void ProcessEvents() {
        // Обрабатываем события как для серверного сокета, так и для клиентских
        while (running) {
            DWORD dwWait = WSAWaitForMultipleEvents(1, &serverEventHandle, FALSE, WSA_INFINITE, FALSE);
            if (dwWait == WSA_WAIT_FAILED) {
                cerr << "Ошибка при ожидании событий." << endl;
                break;
            }

            if (dwWait == WSA_WAIT_EVENT_0) {
                WSANETWORKEVENTS networkEvents;
                if (WSAEnumNetworkEvents(serverSocket, serverEventHandle, &networkEvents) == SOCKET_ERROR) {
                    cerr << "Ошибка при получении сетевых событий." << endl;
                    break;
                }

                if (networkEvents.lNetworkEvents & FD_ACCEPT) {
                    AcceptConnections(); // Обработка нового подключения
                }

                if (networkEvents.lNetworkEvents & FD_CLOSE) {
                    break; // Закрытие соединений
                }
            }
        }
    }

    void SendAsync(SOCKET clientSocket, const char* message, size_t size) {
        // Проверка, доступен ли сокет для отправки данных
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(clientSocket, &writeSet);

        // Используем select для проверки доступности сокета
        timeval timeout = { 0, 0 };  // Не блокирующий режим
        int result = select(0, NULL, &writeSet, NULL, &timeout);

        if (result == SOCKET_ERROR) {
            cerr << "Ошибка при проверке сокета перед отправкой сообщения: " << WSAGetLastError() << endl;
            return;
        }

        if (result == 0) {
            cerr << "Сокет не доступен для отправки данных, возможно, он отключен." << endl;
            return;
        }

        WSABUF wsabuf;
        wsabuf.buf = const_cast<char*>(message);
        wsabuf.len = static_cast<ULONG>(size);

        DWORD bytesSent = 0;
        DWORD flags = 0;

        // Асинхронная отправка сообщения
        result = WSASend(clientSocket, &wsabuf, 1, &bytesSent, flags, NULL, NULL);
        if (result == SOCKET_ERROR) {
            cerr << "Ошибка при отправке сообщения: " << WSAGetLastError() << endl;
        }
    }

    bool SendFileAsync(SOCKET clientSocket, const string& filePath) {
        ifstream file(filePath, ios::binary);
        if (!file.is_open()) {
            cerr << "Не удалось открыть файл для отправки: " << filePath << endl;
            return false;
        }

        const size_t bufferSize = 4096; // Размер блока передачи (4 КБ)
        char buffer[bufferSize];

        while (file) {
            file.read(buffer, bufferSize);
            streamsize bytesRead = file.gcount();

            if (bytesRead > 0) {
                size_t totalBytesSent = 0;
                while (totalBytesSent < static_cast<size_t>(bytesRead)) {
                    WSABUF wsabuf;
                    wsabuf.buf = buffer + totalBytesSent; // Сдвигаем указатель на непросланные данные
                    wsabuf.len = static_cast<ULONG>(bytesRead - totalBytesSent);

                    DWORD bytesSent = 0;
                    DWORD flags = 0;

                    int result = WSASend(clientSocket, &wsabuf, 1, &bytesSent, flags, NULL, NULL);
                    if (result == SOCKET_ERROR) {
                        cerr << "Ошибка при отправке файла: " << WSAGetLastError() << endl;
                        file.close();
                        return false;
                    }

                    totalBytesSent += bytesSent; // Увеличиваем счётчик отправленных данных
                }
            }
        }

        const char* endSignal = "END";
        WSABUF endBuf;
        endBuf.buf = (char*)endSignal;
        endBuf.len = static_cast<ULONG>(strlen(endSignal));

        DWORD endBytesSent = 0;
        WSASend(clientSocket, &endBuf, 1, &endBytesSent, 0, NULL, NULL);

        file.close();
        return true;
    }

    string ReceiveFile(SOCKET clientSocket, const string& fileName) {
        try {
            // Формируем полный путь к файлу
            string filePath = std::filesystem::current_path().string() + "\\" + fileName + ".json";

            // Открываем файл для записи
            ofstream file(filePath, ios::binary);
            if (!file.is_open()) {
                cerr << "Не удалось создать файл для записи: " << filePath << endl;
                return "";
            }

            const size_t bufferSize = 4096; // Размер блока приёма (4 КБ)
            char buffer[bufferSize];


            WSABUF dataBuf;
            DWORD bytesReceived = 0;
            DWORD flags = 0;
            DWORD bytesSent = 0;
            WSAOVERLAPPED overlapped = { 0 };

            dataBuf.buf = buffer;
            dataBuf.len = bufferSize;

            cout << "Начало приёма файла: " << filePath << endl;

            while (running) {
                int result = WSARecv(clientSocket, &dataBuf, 1, &bytesReceived, &flags, &overlapped, NULL);
                if (result == SOCKET_ERROR) {
                    int error = WSAGetLastError();
                    if (error != WSAEWOULDBLOCK) {
                        cerr << "Ошибка при получении файла: " << error << endl;
                        break;
                    }
                    // Если WSAEWOULDBLOCK, то продолжаем попытки
                    continue;
                }

                if (bytesReceived == 0) {
                    cout << "Соединение закрыто. Завершаем прием." << endl;
                    break;
                }

                file.write(buffer, bytesReceived);
                cout << "Получено " << bytesReceived << " байт." << endl;
            }

            file.close();
            cout << "Файл успешно сохранён: " << filePath << endl;
            return filePath; // Возвращаем полный путь к сохранённому файлу
        }
        catch (const exception& ex) {
            cerr << "Ошибка при работе с файлом: " << ex.what() << endl;
            return "";
        }
    }
};

int main() {
    srand(time(NULL));
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    ChatServer server(777);
    server.Start();

    cin.get();
    cin.get();

    server.Stop();

    return 0;
}
