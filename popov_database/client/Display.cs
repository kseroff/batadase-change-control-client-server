using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Net.Sockets;
using Npgsql;
using System.IO;
using System.Text.Json;

namespace Client
{
    public partial class Display : Form
    {
        string connectionString = "Host=localhost;Port=5433;Username=postgres;Password=1111;Database=clientDB";
        string saveDirectory = "C:\\Users\\Kseroff\\source\\repos\\Completion Port\\Client\\bin\\Debug\\main.json";
        TcpClient tcpClientMenu;
		NetworkStream streamMenu;
		string username;
		List<string> allTableList;
		List<string> myTableList;

        Dictionary<string, object> jsonData;
        public Display()
        {
            InitializeComponent();
        }
        public Display(string senderName, TcpClient client, NetworkStream stream, bool online) : this()
        {
            this.username = senderName;
            this.tcpClientMenu = client;
            this.streamMenu = stream;

            label1.Text = username;

            if (!online)
            {
                button1.Visible = false;
                button4.Visible = false;
                checkBox1.Visible = false;
                label1.Text = "Offline";
            }

            using (var connection = new NpgsqlConnection(connectionString))
            {
                connection.Open();

                allTableList = GetAllTableNames(connection);
                myTableList = GetAllTableNames(connection);

                connection.Close();
            }

            ExportDatabaseToJson();
            string jsonContent = File.ReadAllText(saveDirectory);
            jsonData = JsonSerializer.Deserialize<Dictionary<string, object>>(jsonContent);

            tabControl1.TabPages.Clear();
            foreach (var item in myTableList)
            {
                TabPage tabPage = new TabPage(item);
                tabControl1.TabPages.Add(tabPage);
                FillTabWithJsonData(tabPage);
            }

            UpdateTabControl();
        }

        private void UpdateAll()
        {
            tabControl1.TabPages.Clear();
            GetJsonAllTable();
            string jsonContent = File.ReadAllText(saveDirectory);
            jsonData = JsonSerializer.Deserialize<Dictionary<string, object>>(jsonContent);

            SettingsTabPage();
            UpdateDatabaseWithJson();
        }

            private void SettingsTabPage()
        {
            try
            {
                // Отправляем запрос на сервер для получения информации о всех таблицах
                string credentialsMessage = "GET_ALL_TABLE\n";
                byte[] data = Encoding.UTF8.GetBytes(credentialsMessage);
                streamMenu.Write(data, 0, data.Length);
                streamMenu.Flush();

                byte[] responseBuffer = new byte[1024];
                int bytesRead = streamMenu.Read(responseBuffer, 0, responseBuffer.Length);

                if (bytesRead > 0)
                {
                    string response = Encoding.UTF8.GetString(responseBuffer, 0, bytesRead);
                    char[] separator = new char[] { '|' };
                    string[] parts = response.Split(separator, StringSplitOptions.RemoveEmptyEntries);
                    allTableList = new List<string>(parts);
                }

                // Запрашиваем таблицу для текущего пользователя
                credentialsMessage = "GET_MY_TABLE\n";
                data = Encoding.UTF8.GetBytes(credentialsMessage);
                streamMenu.Write(data, 0, data.Length);

                byte[] responseBuffer2 = new byte[1024];
                bytesRead = streamMenu.Read(responseBuffer2, 0, responseBuffer2.Length);

                if (bytesRead > 0)
                {
                    string response = Encoding.UTF8.GetString(responseBuffer2, 0, bytesRead);
                    char[] separator = new char[] { '|' };
                    string[] parts = response.Split(separator, StringSplitOptions.RemoveEmptyEntries);
                    myTableList = new List<string>(parts);
                }

                tabControl1.TabPages.Clear();
                foreach (var item in myTableList)
                {
                    TabPage tabPage = new TabPage(item);
                    tabControl1.TabPages.Add(tabPage);
                    FillTabWithJsonData(tabPage);

                }

                UpdateTabControl();
            }
            catch (Exception e)
            {
                Console.WriteLine("Error requesting user info: " + e.Message);
            }
        }

        // Обработчик изменения выбранной вкладки
        private void tabControl1_SelectedIndexChanged(object sender, EventArgs e)
        {
            TabPage selectedTabPage = tabControl1.SelectedTab;
            if (selectedTabPage != null)
            {
                DataGridView dataGrid = selectedTabPage.Controls[0] as DataGridView;
                if (dataGrid != null)
                {
                    bool isReadOnly = !(myTableList.Contains(selectedTabPage.Text));
                    dataGrid.ReadOnly = isReadOnly;

                    // Дополнительно: Установить ReadOnly для каждой ячейки
                    foreach (DataGridViewRow row in dataGrid.Rows)
                    {
                        foreach (DataGridViewCell cell in row.Cells)
                        {
                            cell.ReadOnly = isReadOnly;
                        }
                    }
                }
            }
        }


        private void GetJsonAllTable()
        {
            try
            {
                byte[] buffer = new byte[4096]; // Увеличили размер буфера
                string credentialsMessage = "GET_DUMP\n";
                byte[] data = Encoding.UTF8.GetBytes(credentialsMessage);
                streamMenu.Write(data, 0, data.Length);
                streamMenu.Flush();

                if (File.Exists(saveDirectory))
                {
                    File.Delete(saveDirectory);
                }

                // Принимаем файл
                using (FileStream fileStream = new FileStream(saveDirectory, FileMode.Create, FileAccess.Write))
                {
                    int bytesReadFromClient;
                    while ((bytesReadFromClient = streamMenu.Read(buffer, 0, buffer.Length)) > 0)
                    {
                        // Проверяем наличие конца передачи (например, "END")
                        string lastBytes = Encoding.UTF8.GetString(buffer, 0, bytesReadFromClient);
                        if (lastBytes.Contains("END"))
                        {
                            // Убираем "END" из буфера и записываем только данные
                            int endIndex = lastBytes.IndexOf("END");
                            if (endIndex > 0)
                            {
                                fileStream.Write(buffer, 0, endIndex);
                            }
                            break; // Выходим из цикла
                        }

                        // Пишем данные в файл
                        fileStream.Write(buffer, 0, bytesReadFromClient);
                    }
                }

                Console.WriteLine($"Файл успешно получен и сохранён по пути: {saveDirectory}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Ошибка при получении файла: {ex.Message}");
            }
        }

        // Обновление вкладок в TabControl
        private void UpdateTabControl()
        {
            if (checkBox1.Checked)
            {
                foreach (var item in allTableList)
                {
                    if (!myTableList.Contains(item))
                    {
                        foreach (TabPage tabPage in tabControl1.TabPages)
                        {
                            if (tabPage.Text == item)
                            {
                                tabControl1.TabPages.Remove(tabPage);
                                break;
                            }
                        }
                    }
                }
            }
            else
            {
                foreach (var item in allTableList)
                {
                    if (!myTableList.Contains(item))
                    {
                        TabPage tabPage = new TabPage(item);
                        tabControl1.TabPages.Add(tabPage);
                        FillTabWithJsonData(tabPage);
                    }
                }
            }
        }

        public void FillTabWithJsonData(TabPage tabPage)
        {
            try
            {
                // Проверяем наличие таблицы с именем вкладки
                if (!jsonData.ContainsKey(tabPage.Text))
                {
                    return;
                }

                // Получаем данные таблицы
                var tableData = JsonSerializer.Deserialize<List<Dictionary<string, string>>>(jsonData[tabPage.Text].ToString());

                // Создаем DataTable для отображения данных
                DataTable dataTable = new DataTable();

                // Если есть данные, добавляем колонки и строки
                if (tableData.Count > 0)
                {
                    // Добавляем колонки
                    foreach (var key in tableData[0].Keys)
                    {
                        dataTable.Columns.Add(key);
                    }

                    // Добавляем строки
                    foreach (var row in tableData)
                    {
                        DataRow dataRow = dataTable.NewRow();
                        foreach (var key in row.Keys)
                        {
                            dataRow[key] = row[key];
                        }
                        dataTable.Rows.Add(dataRow);
                    }
                }

                // Создаем DataGridView и заполняем его данными
                DataGridView dataGridView = new DataGridView
                {
                    Dock = DockStyle.Fill,
                    DataSource = dataTable,
                    AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill // Автоматическое растяжение колонок
                };

                // Добавляем DataGridView на вкладку
                tabPage.Controls.Add(dataGridView);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Произошла ошибка при заполнении таблицы: {ex.Message}", "Ошибка", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        //обновляем таблицы в базе данных по json
        public void UpdateDatabaseWithJson()
        {
            // Читаем и десериализуем JSON-файл
            string jsonContent = File.ReadAllText(saveDirectory);
            var jsonData = JsonSerializer.Deserialize<Dictionary<string, List<Dictionary<string, string>>>>(jsonContent);

            // Подключаемся к базе данных
            using (var connection = new NpgsqlConnection(connectionString))
            {
                connection.Open();

                foreach (var tableName in myTableList)
                {
                    if (!jsonData.ContainsKey(tableName))
                    {
                        Console.WriteLine($"Таблица '{tableName}' не найдена в JSON.");
                        continue;
                    }

                    // Получаем данные таблицы
                    var tableData = jsonData[tableName];
                    if (tableData.Count == 0)
                    {
                        Console.WriteLine($"Данные для таблицы '{tableName}' пусты.");
                        continue;
                    }

                    // Удаляем таблицу, если она существует
                    string dropTableQuery = $"DROP TABLE IF EXISTS {tableName};";
                    using (var dropCommand = new NpgsqlCommand(dropTableQuery, connection))
                    {
                        dropCommand.ExecuteNonQuery();
                    }

                    // Создаем таблицу заново
                    var createTableQuery = GenerateCreateTableQuery(tableName, tableData[0]);
                    using (var createCommand = new NpgsqlCommand(createTableQuery, connection))
                    {
                        createCommand.ExecuteNonQuery();
                    }

                    // Заполняем таблицу данными
                    foreach (var row in tableData)
                    {
                        var insertQuery = GenerateInsertQuery(tableName, row);
                        using (var insertCommand = new NpgsqlCommand(insertQuery, connection))
                        {
                            insertCommand.ExecuteNonQuery();
                        }
                    }

                    Console.WriteLine($"Таблица '{tableName}' успешно создана и заполнена.");
                }

                connection.Close();
            }
        }

        private string GenerateCreateTableQuery(string tableName, Dictionary<string, string> firstRow)
        {
            var columns = new List<string>();
            foreach (var column in firstRow.Keys)
            {
                columns.Add($"{column} TEXT"); // Используем тип TEXT для простоты. Можно адаптировать под нужные типы данных.
            }

            return $"CREATE TABLE {tableName} ({string.Join(", ", columns)});";
        }

        private string GenerateInsertQuery(string tableName, Dictionary<string, string> row)
        {
            var columns = string.Join(", ", row.Keys);
            var values = string.Join(", ", row.Values.Select(value => $"'{value.Replace("'", "''")}'")); // Экранируем кавычки
            return $"INSERT INTO {tableName} ({columns}) VALUES ({values});";
        }

        // получить json из базы
        public void ExportDatabaseToJson()
        {
            var databaseStructure = new Dictionary<string, List<Dictionary<string, object>>>();

            using (var connection = new NpgsqlConnection(connectionString))
            {
                connection.Open();

                // Получаем список всех таблиц в базе данных
                var tables = GetAllTableNames(connection);

                foreach (var tableName in tables)
                {
                    // Извлекаем данные таблицы
                    var tableData = GetTableData(connection, tableName);

                    // Добавляем данные таблицы в структуру
                    databaseStructure[tableName] = tableData;
                }

                connection.Close();
            }

            // Сохраняем данные в JSON
            string jsonContent = JsonSerializer.Serialize(databaseStructure, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(saveDirectory, jsonContent);

            Console.WriteLine($"Данные успешно экспортированы в файл: {saveDirectory}");
        }

        private List<string> GetAllTableNames(NpgsqlConnection connection)
        {
            var tableNames = new List<string>();
            string query = @"
            SELECT table_name
            FROM information_schema.tables
            WHERE table_schema = 'public' AND table_type = 'BASE TABLE';";

            using (var command = new NpgsqlCommand(query, connection))
            using (var reader = command.ExecuteReader())
            {
                while (reader.Read())
                {
                    tableNames.Add(reader.GetString(0));
                }
            }

            return tableNames;
        }

        private List<Dictionary<string, object>> GetTableData(NpgsqlConnection connection, string tableName)
        {
            var tableData = new List<Dictionary<string, object>>();

            string query = $"SELECT * FROM {tableName};";

            using (var command = new NpgsqlCommand(query, connection))
            using (var reader = command.ExecuteReader())
            {
                while (reader.Read())
                {
                    var row = new Dictionary<string, object>();

                    for (int i = 0; i < reader.FieldCount; i++)
                    {
                        row[reader.GetName(i)] = reader.IsDBNull(i) ? null : reader.GetValue(i);
                    }

                    tableData.Add(row);
                }
            }

            return tableData;
        }

        // json из таблиц в приложении
        public void GenerateJsonFromTabs()
        {
            try
            {
                // Создаем словарь для хранения данных таблиц
                var updatedJsonData = new Dictionary<string, object>();

                // Проходим по каждому имени таблицы в списке
                foreach (var tableName in myTableList)
                {
                    // Получаем TabPage по имени
                    TabPage tabPage = tabControl1.TabPages.Cast<TabPage>().FirstOrDefault(tp => tp.Text == tableName);

                    if (tabPage != null)
                    {
                        // Ищем DataGridView внутри вкладки
                        DataGridView dataGridView = tabPage.Controls.OfType<DataGridView>().FirstOrDefault();

                        if (dataGridView != null)
                        {
                            // Создаем список для хранения строк таблицы
                            var tableData = new List<Dictionary<string, string>>();

                            // Проходим по строкам DataGridView и добавляем их в список
                            foreach (DataGridViewRow row in dataGridView.Rows)
                            {
                                if (row.IsNewRow) continue;

                                var rowData = new Dictionary<string, string>();
                                foreach (DataGridViewCell cell in row.Cells)
                                {
                                    rowData[cell.OwningColumn.Name] = cell.Value?.ToString() ?? "";
                                }
                                tableData.Add(rowData);
                            }

                            // Добавляем данные таблицы в словарь
                            updatedJsonData[tableName] = tableData;
                        }
                    }
                }

                // Сериализуем словарь в JSON строку
                string updatedJson = JsonSerializer.Serialize(updatedJsonData, new JsonSerializerOptions { WriteIndented = true });

                // Обновляем JSON файл
                File.WriteAllText(saveDirectory, updatedJson, Encoding.UTF8);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Произошла ошибка при обновлении JSON файла: {ex.Message}", "Ошибка", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void checkBox1_CheckedChanged(object sender, EventArgs e)
        {
            UpdateTabControl();
        }

        private void button4_Click(object sender, EventArgs e)
        {
            UpdateAll();
        }

        private void button2_Click(object sender, EventArgs e)
        {
            GenerateJsonFromTabs();
            UpdateDatabaseWithJson();
        }

        private void button1_Click(object sender, EventArgs e)
        {
            GenerateJsonFromTabs();
            UpdateDatabaseWithJson();
            // Отправить файл
            try
            {
                // Получаем имя файла и его тип
                string fileName = "main";

                // Отправляем клиенту, что будет передаваться файл
                string message = $"SET_DUMP\n{fileName}\n";
                byte[] messageBytes = Encoding.UTF8.GetBytes(message);
                streamMenu.Write(messageBytes, 0, messageBytes.Length);
                streamMenu.Flush();

                // Ожидаем, что сервер подтвердит готовность
                byte[] responseBuffer = new byte[1024];
                int bytesRead = streamMenu.Read(responseBuffer, 0, responseBuffer.Length);
                string response = Encoding.UTF8.GetString(responseBuffer, 0, bytesRead);

                if (response == "READY")
                {
                    // Чтение файла и отправка данных
                    byte[] buffer = new byte[4096];
                    using (FileStream fileStream = new FileStream(saveDirectory, FileMode.Open, FileAccess.Read))
                    {
                        int bytesReadFromFile;
                        while ((bytesReadFromFile = fileStream.Read(buffer, 0, buffer.Length)) > 0)
                        {
                            streamMenu.Write(buffer, 0, bytesReadFromFile);
                            streamMenu.Flush();
                        }
                    }

                    Console.WriteLine("Файл успешно отправлен.");
                }
                else
                {
                    Console.WriteLine("Сервер не готов к приему файла.");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Ошибка при отправке файла: {ex.Message}");
            }
        }

        private void button3_Click(object sender, EventArgs e)
        {
            this.Close();
            Login login = new Login();
            login.Show();
        }
    }
}
