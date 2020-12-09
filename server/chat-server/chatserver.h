#ifndef CHATSERVER_H
#define CHATSERVER_H

#include <QMainWindow>
#include<QTcpServer>
#include <QtSql>
#include <QTcpSocket>
#include <QTextStream>
#include <QQueue>
#include <QXmlReader>
#include <QInputDialog>
#include<QMessageBox>
namespace Ui {
class ChatServer;
}

class ChatServer : public QMainWindow
{
    Q_OBJECT

public:
    explicit ChatServer(QWidget *parent = 0);
    ~ChatServer();
    QTcpServer *server;
    QMap<QString,int>Sessions;
    QMap<int,QTcpSocket*>Connections;
    QMap<QString,int>Actions;
    QSqlDatabase db;
    void getUsers();
    QQueue<QStringList> msgQueue;
    QStringList Users;
    QTimer* timer;
    int queueProcessing;
    void msgParse(int descr,QByteArray msg);
    void msgProc(int descr,int action,QMap<QString,QString>params);
    void userAdd(QString username,QString password);
    void userDel(QString username);
    void userChg(QString username,QString password,int lock_state);
    bool userChkUniq(QString username);
    void userChk(int descr,QString username,int session_id);
    void reg(int descr,QString username,QString password);
    void login(int descr,QString username,QString password);
    void sendStatus(int descr,int code,bool isError,bool close);
    void closeConn(int descr);
    void logout(int descr,QString username,int session_id);
    void msg_to(int descr,QString to,int session_id,QString message);
    void sendMsg(QString from,QString to,QString message);
    void msg_toPending(QString from,QString to,QString message);
    bool msg_hasPenging(QString username);
    void msg_fromPending(QString username);
    void checkSchema();
    void startServer();
private slots:
    void newConnection();
    void readServer();
    void msgQueueProcess();
    void clientDisconnected();
    void on_lockButton_clicked();
    void on_tableWidget_cellActivated(int row, int column);
    void on_deleteUserButton_clicked();
    void on_changeUserPwButton_clicked();
    void on_start_triggered();
    void on_stop_triggered();
    void on_exit_triggered();
    void on_about_triggered();
    void on_reinit_triggered();
    void on_ChatServer_destroyed();

signals:
    void msgQueueUpd();

private:
    Ui::ChatServer *ui;
};

#endif // CHATSERVER_H
