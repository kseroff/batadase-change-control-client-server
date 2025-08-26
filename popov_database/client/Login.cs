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

namespace Client
{
    public partial class Login : Form
    {
        private string action;
        private string username;
        private string password;
        private string name;
        private TcpClient tcpClient;
        private NetworkStream stream;

        public Login()
        {
            InitializeComponent();
        }

        private void registerButtonLabel_Click(object sender, EventArgs e)
        {
            ClientSize = new System.Drawing.Size(273, 176);
            MyLoginButton.Visible = false;
            label6.Visible = false;
            backButton.Visible = true;
            registerConfirmButton.Visible = true;
            firstNameTextBox.Visible = true;
            label3.Visible = true;
            backButton.Visible = true;
        }

        // Обработчик нажатия на кнопку "Назад"
        private void backButton_Click(object sender, EventArgs e)
        {
            ClientSize = new System.Drawing.Size(273, 142);
            MyLoginButton.Visible = true;
            label6.Visible = true;
            backButton.Visible = false;
            registerConfirmButton.Visible = false;
            firstNameTextBox.Visible = false;
            label3.Visible = false;
            backButton.Visible = false;
        }

        // Обработчик нажатия на кнопку "Вход"
        private void MyLoginButton_Click(object sender, EventArgs e)
        {
            action = "LOGIN";
            username = MyLoginTextBox.Text;
            password = passwordTextBox.Text;

            SendCredentialsToServer();
        }

        // Обработчик нажатия на кнопку "Регистрация"
        private void registerButton_Click(object sender, EventArgs e)
        {
            action = "REGISTER";

            username = MyLoginTextBox.Text;
            for (int i = 0; i < username.Length; i++)
            {
                if (username[i] == '|' || username[i] == '=')
                {
                    MessageBox.Show("Логин не может содержать специальные символы", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }
            }

            password = passwordTextBox.Text;
            if (string.IsNullOrEmpty(password))
            {
                MessageBox.Show("Заполните поле регистрации", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            name = firstNameTextBox.Text;
            if (string.IsNullOrEmpty(name))
            {
                MessageBox.Show("Заполните поле Имя", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            SendCredentialsToServer();
        }

        // Метод отправки сообщения на сервер
        private void sendMessage(string message)
        {
            try
            {
                byte[] data = Encoding.UTF8.GetBytes(message);
                stream.Write(data, 0, data.Length);
                stream.Flush();

                // Получение ответа от сервера
                byte[] responseBuffer = new byte[1024];
                int bytesRead = stream.Read(responseBuffer, 0, responseBuffer.Length);
                if (bytesRead > 0)
                {
                    string response = Encoding.UTF8.GetString(responseBuffer, 0, bytesRead);
                    if (response == "true")
                    {
                        this.Hide();
                        Display myMenu = new Display(username, tcpClient, stream, true);
                        myMenu.Show();
                    }
                    else
                    {
                        label5.Text = "Ошибка при авторизации";
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine("Ошибка при отправке сообщения: " + ex.Message);
            }
        }

        // Метод отправки данных на сервер
        private void SendCredentialsToServer()
        {
            try
            {
                // Создаем подключение к серверу
                tcpClient = new TcpClient("127.0.0.1", 777);  // IP-адрес и порт сервера
                stream = tcpClient.GetStream();

                // Проверяем состояние подключения
                if (tcpClient.Connected)
                {
                    // Отправляем данные на сервер
                    string credentialsMessage;
                    if (action == "LOGIN")
                    {
                        credentialsMessage = string.Format("{0}\n{1}\n{2}", action, username, password);
                    }
                    else
                    {
                        credentialsMessage = string.Format("{0}\n{1}\n{2}\n{3}", action, username, password, name);
                    }

                    sendMessage(credentialsMessage);
                }
                else
                {
                    MessageBox.Show("Сбой при подключении к серверу", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show("Ошибка: " + ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void button1_Click(object sender, EventArgs e)
        {
            this.Hide();
            Display myMenu = new Display(username, tcpClient, stream, false);
            myMenu.Show();
        }
    }
}
