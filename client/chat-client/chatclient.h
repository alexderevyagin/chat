#ifndef CHATCLIENT_H
#define CHATCLIENT_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QXmlStreamWriter>
#include <QMessageBox>
#include <QInputDialog>
#include <QListWidgetItem>
#include <QDateTime>
#include <QtSql>
namespace Ui {
class ChatClient;
}

class ChatClient : public QMainWindow
{
    Q_OBJECT

public:
    explicit ChatClient(QWidget *parent = 0);
    ~ChatClient();
    QTcpSocket* client;
    QMap <QString,int> Actions;
    QMap<QString,QStringList> Dialogs;
    QMap<QString,int> DialogStatus;
    QMap<int,QString>errorCode;
    QString currentDialog;
    QSqlDatabase db;
    int state;
    int session_id;
    void sendLogin();
    void completeLogin(int id);
    void parseStatus(int status);
    void sendXml(QString action,QMap <QString,QString> params);
    void parseXml(QByteArray msg);
    void addDialog(QString addUsername);
    void updateDialogList();
    void showDialog();
    void msg_from(QMap<QString,QString> params);
    void sendReg();
    void completeReg();
    void logout();
    void saveDialog(QStringList msg,QString username);
    void loadDialogs();
    void checkSchema();
    void delDialog(QString username);
    void getUsers();
    void showUsers(QMap<QString,int> users);
private slots:
    void onConnect();

    void onReadyRead();

    void onDisconnected();

    void onSocketError();

    void on_loginButton_clicked();

    void on_addDialog_clicked();

    void on_listWidget_itemDoubleClicked(QListWidgetItem *item);

    void on_sendButton_clicked();

    void on_logoutButton_clicked();

    void on_delDialog_clicked();

    void on_userAdd_clicked();

    void on_usersCancel_clicked();

private:
    Ui::ChatClient *ui;
};

#endif // CHATCLIENT_H
