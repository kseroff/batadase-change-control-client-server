#include <iostream>
#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <cstdlib>
#include <stdexcept>
#include <memory>
#include <functional>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

class Database {
protected:
    string host = "localhost";
    string port = "5432";
    string user = "postgres";
    string password = "1111";
    string dbname;

    pqxx::connection conn;

    void reconnectToDatabase(const string& dbname) {
        try {
            conn = pqxx::connection("host=" + host + " port=" + port + " user=" + user + " password=" + password + " dbname=" + dbname);
            if (conn.is_open()) {
                cout << "Переподключение выполнено к базе данных: " << conn.dbname() << endl;
            }
        }
        catch (const exception& e) {
            cerr << "Ошибка при переподключении к базе данных " << dbname << ": " << e.what() << endl;
            throw;
        }
    }

public:

    Database(const string& dbname)
        : conn("host=" + host + " port=" + port + " user=" + user + " password=" + password + " dbname=" + dbname), dbname(dbname) {
        try {
            if (conn.is_open()) {
                cout << "Соединение установлено с базой данных: " << conn.dbname() << endl;
            }
            else {
                throw runtime_error("Не удалось установить соединение с центральной базой данных.");
            }
        }
        catch (const exception& e) {
            cerr << "Ошибка при переподключении к базе данных " << dbname << ": " << e.what() << endl;
            throw;
        }
    }

    void setUser(const string& user) {
        this->user = user;
    }

    string getUser() {
        return this->user;
    }

};

class ClientDatabase : public Database {

    string GenerateSalt() {
        // Генерация случайной соли с использованием hash
        hash<string> hasher;
        string salt = to_string(hasher(to_string(rand())));

        return salt;
    }

    string HashPasswordWithSalt(const string& password, const string& salt) {
        // Хэширование пароля с солью
        string combined = password + salt;
        hash<string> hasher;
        string hashedPassword = to_string(hasher(combined));

        return hashedPassword;
    }

public:

    // Конструктор: подключается к центральной базе данных
    ClientDatabase() : Database("serverDB") {}

    // Деструктор
    ~ClientDatabase() {
        if (conn.is_open()) {
            cout << "Соединение с центральной базой данных закрыто." << endl;
        }
    }

    bool RegisterUser(const string& username, const string& password, const string& name, const string& role) {
        try {
            pqxx::work txn(conn);

            // Проверка, существует ли пользователь
            pqxx::result res = txn.exec_params(
                "SELECT password FROM client WHERE username = $1",
                username
            );

            if (!res.empty()) {
                cout << "Регистрация для пользователя " << username << " отменена: пользователь уже существует." << endl;
                return false;
            }

            // Генерация соли и хэширование пароля
            string salt = GenerateSalt();
            string hashedPassword = HashPasswordWithSalt(password, salt);

            // Вставка нового пользователя в базу данных
            txn.exec_params(
                "INSERT INTO client (username, password, salt, name, role) VALUES ($1, $2, $3, $4, $5)",
                username, hashedPassword, salt, name, role
            );

            string createUserQuery = "CREATE ROLE " + txn.quote_name(username) +
                " WITH LOGIN PASSWORD " + txn.quote(password) + ";";
            txn.exec(createUserQuery);

            // Установка минимальных привилегий для пользователя
            string revokePrivilegesQuery = "REVOKE ALL PRIVILEGES ON DATABASE " +
                txn.quote_name(conn.dbname()) +
                " FROM " + txn.quote_name(username) + ";";
            txn.exec(revokePrivilegesQuery);

            txn.commit();

            cout << "Пользователь " << username << " успешно зарегистрирован!" << endl;
            return true;
        }
        catch (const exception& ex) {
            cerr << "Ошибка при регистрации: " << ex.what() << endl;
            return false;
        }
    }

    // Функция для проверки аутентификации пользователя
    bool AuthenticateUser(const string& username, const string& password) {
        try {
            pqxx::work txn(conn);

            // Проверка соли пользователя
            pqxx::result res = txn.exec_params(
                "SELECT salt FROM client WHERE username = $1",
                username
            );

            if (res.empty()) {
                cout << "Пользователь " << username << " не найден." << endl;
                return false;
            }

            string salt = res[0]["salt"].c_str();

            // Хэширование пароля с полученной солью
            string hashedPassword = HashPasswordWithSalt(password, salt);

            // Проверка пары логин-пароль
            res = txn.exec_params(
                "SELECT username FROM client WHERE username = $1 AND password = $2",
                username, hashedPassword
            );

            if (!res.empty()) {
                cout << "Пользователь " << username << " успешно аутентифицирован!" << endl;
                return true;
            }
            else {
                cout << "Ошибка аутентификации для пользователя " << username << "." << endl;
                return false;
            }
        }
        catch (const exception& ex) {
            cerr << "Ошибка при аутентификации: " << ex.what() << endl;
            return false;
        }
    }

    string GetUserRole(const string& username) {
        try {
            pqxx::work txn(conn);

            // Выполняем запрос для получения роли
            pqxx::result res = txn.exec_params(
                "SELECT role FROM client WHERE username = $1",
                username
            );

            // Если пользователь не найден
            if (res.empty()) {
                cout << "Пользователь " << username << " не найден." << endl;
                return ""; // Возвращаем пустую строку, если пользователь не найден
            }

            // Извлечение роли
            string role = res[0]["role"].c_str();

            cout << "Роль пользователя " << username << ": " << role << endl;
            return role;
        }
        catch (const exception& ex) {
            cerr << "Ошибка при получении роли: " << ex.what() << endl;
            return ""; // Возвращаем пустую строку в случае ошибки
        }
    }


};

class DatabaseManager : public Database {
private:
    string role;

protected:

    // проверка наличия роли
    bool roleExists(const string& role_name) {
        try {
            pqxx::work txn(conn);
            string query = "SELECT 1 FROM pg_roles WHERE rolname = " + txn.quote(role_name) + ";";
            pqxx::result res = txn.exec(query);
            txn.commit();
            return !res.empty();
        }
        catch (const exception& e) {
            cerr << "Ошибка при проверке роли " << role_name << ": " << e.what() << endl;
            throw;
        }
    }

    // Создание роли, если она не существует
    void createRoleIfNotExists(const string& role_name) {
        if (!roleExists(role_name)) {
            try {
                pqxx::work txn(conn);
                string query = "CREATE ROLE " + txn.quote_name(role_name) + " WITH NOLOGIN;";
                txn.exec(query);
                txn.commit();
                cout << "Роль " << role_name << " успешно создана." << endl;
            }
            catch (const exception& e) {
                cerr << "Ошибка при создании роли " << role_name << ": " << e.what() << endl;
                throw;
            }
        }
    }

public:

    DatabaseManager() : Database("clientDB") {}

    DatabaseManager(const string& user, const string& role) : DatabaseManager() {
        this->role = role;
        this->user = user;
    }

    void setRole(const string& role) {
        this->role = role;
    }

    string getRole() {
        return this->role;
    }

    // Возвращает список всех таблиц в базе данных
    vector<string> getTables() {
        try {
            pqxx::work txn(conn);
            string query = R"(
                SELECT tablename
                FROM pg_catalog.pg_tables
                WHERE schemaname != 'pg_catalog' AND schemaname != 'information_schema';
            )";
            pqxx::result res = txn.exec(query);
            txn.commit();

            vector<string> tables;
            for (const auto& row : res) {
                tables.push_back(row[0].c_str());
            }

            return tables;
        }
        catch (const exception& e) {
            cerr << "Ошибка при получении списка таблиц: " << e.what() << endl;
            throw;
        }
    }

    // Возвращает список таблиц, доступных текущему пользователю или роли
    vector<string> getAccessibleTables() {
        try {
            pqxx::work txn(conn);
            string query = R"(
                SELECT tablename
                FROM pg_catalog.pg_tables
                WHERE (tableowner = )" + txn.quote(user) + R"( 
                OR tablename IN (
                    SELECT relname
                    FROM pg_class
                    JOIN pg_roles ON pg_roles.oid = pg_class.relowner
                    WHERE pg_roles.rolname = )" + txn.quote(role) + R"(
                ))
                AND schemaname != 'pg_catalog' AND schemaname != 'information_schema';
            )";

            pqxx::result res = txn.exec(query);
            txn.commit();

            vector<string> tables;
            for (const auto& row : res) {
                tables.push_back(row[0].c_str());
            }

            if (std::find(tables.begin(), tables.end(), "orders") == tables.end()) {
                tables.push_back("orders");
            }

            return tables;
        }
        catch (const exception& e) {
            cerr << "Ошибка при получении списка доступных таблиц: " << e.what() << endl;
            throw;
        }
    }

    // Создаёт новую таблицу с заданным именем и устанавливает владельца
    void createTable(const string& table_name) {
        try {
            pqxx::work txn(conn);
            string query = "CREATE TABLE " + txn.quote_name(table_name) + " (id SERIAL PRIMARY KEY, name TEXT NOT NULL) OWNER TO " + txn.quote_name(user) + ";";
            txn.exec(query);
            txn.commit();

            cout << "Таблица " << table_name << " успешно создана с владельцем: " << user << endl;

            // Автоматически добавить права роли администратора
            grantTableAccess(table_name, { "admin" });
        }
        catch (const exception& e) {
            cerr << "Ошибка при создании таблицы " << table_name << ": " << e.what() << endl;
            throw;
        }
    }

    // Удаляет таблицу с указанным именем
    void dropTable(const string& table_name) {
        try {
            pqxx::work txn(conn);
            string query = "DROP TABLE IF EXISTS " + txn.quote_name(table_name) + " CASCADE;";
            txn.exec(query);
            txn.commit();
            cout << "Таблица " << table_name << " успешно удалена." << endl;
        }
        catch (const exception& e) {
            cerr << "Ошибка при удалении таблицы " << table_name << ": " << e.what() << endl;
            throw;
        }
    }

    // Предоставляет доступ к таблице для списка ролей
    void grantTableAccess(const string& table_name, const vector<string>& roles) {
        try {
            for (const string& role_name : roles) {
                createRoleIfNotExists(role_name);

                pqxx::work txn(conn);
                string query = "GRANT SELECT, INSERT, UPDATE, DELETE ON " + txn.quote_name(table_name) + " TO " + txn.quote_name(role_name) + ";";
                txn.exec(query);
                txn.commit();
                cout << "Роли " << role_name << " предоставлены права на таблицу " << table_name << "." << endl;
            }
        }
        catch (const exception& e) {
            cerr << "Ошибка при предоставлении прав для таблицы " << table_name << ": " << e.what() << endl;
            throw;
        }
    }

    // Генерирует JSON с данными указанных таблиц
    bool dumpTablesToJson(const string& output_file) {
        const vector < string> tables = getTables();
        if (tables.empty()) {
            cerr << "Список таблиц пуст. Нечего экспортировать." << endl;
            return false;
        }

        json result_json;

        try {
            pqxx::work txn(conn);

            // Проходим по всем таблицам и извлекаем их данные
            for (const auto& table : tables) {
                string query = "SELECT * FROM " + txn.quote_name(table) + ";";
                pqxx::result res = txn.exec(query);

                // Создаем массив для данных таблицы
                json table_data;
                for (const auto& row : res) {
                    json row_data;
                    for (size_t i = 0; i < row.size(); ++i) {
                        row_data[res.column_name(i)] = row[i].c_str();  // Преобразуем значения в строку
                    }
                    table_data.push_back(row_data);  // Добавляем строку данных в массив
                }

                result_json[table] = table_data;  // Добавляем данные таблицы в JSON
            }

            txn.commit();

            // Сохраняем результат в файл
            ofstream output(output_file);
            cout << endl << endl << result_json.dump() << endl << endl;
            if (output.is_open()) {
                output << result_json.dump(4);  // Запись в файл с отступами для читабельности
                output.close();
                cout << "Данные успешно сохранены в JSON файл: " << output_file << endl;
                return true;
            }
            else {
                cerr << "Ошибка при открытии файла для записи." << endl;
                return false;
            }
        }
        catch (const exception& e) {
            cerr << "Ошибка при выполнении запроса: " << e.what() << endl;
            return false;
        }
    }

    bool updateTablesFromJson(const string& json_file) {
        // Читаем JSON файл
        json input_json;
        ifstream file(json_file);
        if (!file.is_open()) {
            cerr << "Не удалось открыть JSON файл: " << json_file << endl;
            return false;
        }
        file >> input_json;
        file.close();

        try {
            pqxx::work txn(conn);

            // Получаем все таблицы в базе данных
            pqxx::result res = txn.exec("SELECT table_name FROM information_schema.tables WHERE table_schema = 'public';");

            // Список всех таблиц в базе данных
            vector<string> existing_tables;
            for (const auto& row : res) {
                existing_tables.push_back(row[0].as<string>());
            }

            // 1. Создаем или заменяем таблицы
            for (auto& item : input_json.items()) {
                string table_name = item.key();

                if (find(existing_tables.begin(), existing_tables.end(), table_name) != existing_tables.end()) {
                    // Очищаем таблицу
                    string truncate_query = "TRUNCATE TABLE " + txn.quote_name(table_name) + " RESTART IDENTITY CASCADE;";
                    txn.exec(truncate_query);
                }
                else {
                    // Если таблицы нет, создаём её
                    string create_table_query = "CREATE TABLE " + txn.quote_name(table_name) + " (";
                    json table_data = item.value();

                    // Добавляем столбцы
                    if (!table_data.empty()) {
                        for (auto& row : table_data[0].items()) {
                            create_table_query += txn.quote_name(row.key()) + " TEXT, ";
                        }
                        create_table_query.pop_back(); // Убираем последнюю запятую
                        create_table_query.pop_back(); // Убираем пробел
                    }

                    create_table_query += ");";
                    txn.exec(create_table_query);
                }

                // Вставляем данные из JSON в таблицу
                json table_data = item.value();
                for (auto& row : table_data) {
                    string insert_query = "INSERT INTO " + txn.quote_name(table_name) + " (";
                    string values = "";
                    bool first = true;

                    // Формируем запрос на вставку данных
                    for (auto& col : row.items()) {
                        if (!first) {
                            insert_query += ", ";
                            values += ", ";
                        }
                        insert_query += txn.quote_name(col.key());
                        values += txn.quote(col.value().get<string>());
                        first = false;
                    }

                    insert_query += ") VALUES (" + values + ");";
                    txn.exec(insert_query);
                }
            }

            txn.commit();
            cout << "Обновление таблиц успешно завершено." << endl;
            return true;
        }
        catch (const exception& e) {
            cerr << "Ошибка при обновлении таблиц: " << e.what() << endl;
            return false;
        }
    }

};
