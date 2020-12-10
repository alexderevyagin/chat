#include "chatserver.h"
#include "ui_chatserver.h"
#include "QTcpServer"

ChatServer::ChatServer(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ChatServer)
{
    ui->setupUi(this);
    db=QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("set.db");
    if (!db.open()){
        ui->textBrowser->append("Невозможно открыть базу данных!");
        ui->statusBar->showMessage("Невозможно открыть базу данных!");
    }
    checkSchema();
    Actions["reg"]=1;
    Actions["login"]=2;
    Actions["logout"]=3;
    Actions["msg-to"]=4;
    Actions["get_users"]=5;
    getUsers();
    server=new QTcpServer(this);
    connect(server,SIGNAL(newConnection()),this,SLOT(newConnection()));
    connect(this,SIGNAL(msgQueueUpd()),this,SLOT(msgQueueProcess()));
    ui->start->setEnabled(true);
    timer=new QTimer(this);
    timer->setInterval(30000);
    connect(timer,SIGNAL(timeout()),this,SIGNAL(msgQueueUpd()));
    startServer();
}

ChatServer::~ChatServer()
{
    delete ui;
}
void ChatServer::getUsers() //Обновление таблицы пользователей
{
    QSqlQuery query;
    QStringList blocked_status;
    int counter=0;
    query.exec("select username,isBlocked from users;");
    Users.clear();
    ui->tableWidget->clear();
    ui->tableWidget->setRowCount(0);
    QStringList headers;
    headers.insert(0,"Имя пользователя");
    headers.insert(1,"Статус блокировки");
    headers.insert(2,"Статус");
    ui->tableWidget->setHorizontalHeaderLabels(headers);
    while (query.next()){
        ui->tableWidget->insertRow(ui->tableWidget->rowCount());
        ui->tableWidget->setItem(counter,0,new QTableWidgetItem(query.value(0).toString()));
        ui->tableWidget->setItem(counter,1,new QTableWidgetItem(query.value(1).toString()));
        if (Sessions.contains(query.value(0).toString()))
            ui->tableWidget->setItem(counter,2,new QTableWidgetItem("В сети"));
        else
            ui->tableWidget->setItem(counter,2,new QTableWidgetItem("Не в сети"));
        counter++;
        Users.append(query.value(0).toString());
    }
    ui->tableWidget->resizeColumnsToContents();
    ui->lockButton->setEnabled(false);
    ui->changeUserPwButton->setEnabled(false);
    ui->deleteUserButton->setEnabled(false);
    ui->lockButton->setText("Заблокировать");
}


void ChatServer::newConnection() //Обработчик новых соединений
{
    while(server->hasPendingConnections()){
        QTcpSocket* currentSocket=server->nextPendingConnection();
        connect(currentSocket,SIGNAL(readyRead()),this,SLOT(readServer()));
        connect(currentSocket,SIGNAL(disconnected()),this,SLOT(clientDisconnected()));
        Connections[currentSocket->socketDescriptor()]=currentSocket;
    }
}


void ChatServer::readServer() //Чтение пришедших из сокета данных и передача их процедуре парсинга
{
    QByteArray msg;
    QTcpSocket* currentSocket=(QTcpSocket*)sender();
    msg=currentSocket->readAll();
    msgParse(currentSocket->socketDescriptor(),msg);
}
void ChatServer::msgQueueProcess()//Обработка очереди отправки сообщений
{
    if (!queueProcessing){
        queueProcessing=true;
        while (!msgQueue.isEmpty()){
            QStringList msg=msgQueue.dequeue();
            if (Sessions.contains(msg.at(1)))
                sendMsg(msg.at(0),msg.at(1),msg.at(2));
            else
                msg_toPending(msg.at(0),msg.at(1),msg.at(2));
        }
        queueProcessing=false;
    }
}
void ChatServer::msgParse(int descr, QByteArray msg)//Парсинг пришедшего сообщения в формате XML
{
    QXmlStreamReader xmlMsg(msg);
    bool isStart;
    int action;
    QMap <QString,QString>params;
    while(!xmlMsg.atEnd()){
        QXmlStreamReader::TokenType token=xmlMsg.readNext();
        if (token==QXmlStreamReader::StartDocument)
            continue;
        if (token==QXmlStreamReader::StartElement&&xmlMsg.name()=="chat"){
            isStart=true;
            continue;
        }
        if (isStart &&token==QXmlStreamReader::StartElement){
            QXmlStreamAttributes attrib=xmlMsg.attributes();
            if (xmlMsg.name()=="action"){
                if (Actions.contains(attrib.value("type").toString()))
                    action=Actions[attrib.value("type").toString()];
                else
                    action=-1;}
            if (xmlMsg.name()=="param")
                params[attrib.value("name").toString()]=xmlMsg.readElementText();
            continue;
        }

        if (isStart && token==QXmlStreamReader::EndDocument)
            break;
    }
    msgProc(descr,action,params);
}

void ChatServer::msgProc(int descr, int action, QMap<QString, QString> params)//Парсинг событий из сообщения и вызов их обработчиков
{
    switch (action){
    case 1 : ui->textBrowser->append("Попытка регистрации");
        reg(descr,params["username"],params["password"]);
        break;
    case 2 : ui->textBrowser->append("Попытка логина");
        login(descr,params["username"],params["password"]);
        break;
    case 3: ui->textBrowser->append("Выход");
        logout(descr,params["username"],params["session_id"].toInt());
        break;
    case 4: ui->textBrowser->append("Новое сообщение");
        msg_to(descr,params["to"],params["session_id"].toInt(),params["message"]);
        break;
    case 5:
        ui->textBrowser->append("Запрос списка пользователей");
        usersSend(descr,params["username"],params["session_id"].toInt());
        break;
    case -1:
        ui->textBrowser->setText("Ошибочное сообщение");
        break;
    }
}
void ChatServer::reg(int descr, QString username, QString password)//Обработчик события регистрации
{
    if (username!="" && password!=""){
        if (userChkUniq(username)){
            userAdd(username,password);
            sendStatus(descr,0,false,false);
            getUsers();
        }
        else sendStatus(descr,2,true,true);
    }else
        sendStatus(descr,1,true,true);
}
void ChatServer::sendStatus(int descr, int code,bool isError, bool close)//Процедура отправки статуса
{
    /* Коды статуса
  0 - Действие успешно выполнено
  1 - При выполнении запроса не указан один из параметров
  2 - Пользователь с таким логином существует
  3 - Неверные имя пользователя и пароль
  4 - Ошибка проверки подлинности (попытка выполнения команды от имени другого пользователя)
  5 - Пользователь заблокирован
  6 - Пользователь не существует

*/
    QTextStream os(Connections[descr]);
    QByteArray xmlMsg;
    QXmlStreamWriter statMsg(&xmlMsg);
    statMsg.writeStartDocument();
    statMsg.writeStartElement("status");
    if (!isError)
        statMsg.writeAttribute("type","OK");
    else
        statMsg.writeAttribute("type","Error");
    statMsg.writeCharacters(QString::number(code));
    statMsg.writeEndElement();
    statMsg.writeEndDocument();
    os<<xmlMsg;
    ui->textBrowser->append("Код завершения: "+QString::number(code));
    if (close)
        closeConn(descr);
}
void ChatServer::userAdd(QString username, QString password)//Процедура добавления пользователя
{
    QSqlQuery query;
    query.prepare("insert into users (username,password,isBlocked) values (:username,:password,0);");
    query.bindValue(":username",username);
    query.bindValue(":password",password);
    query.exec();
    ui->textBrowser->append("Создан пользователь. Логин : "+username);
}
bool ChatServer::userChkUniq(QString username)//Проверка уникальности имени пользователя
{
    QSqlQuery query;
    query.prepare("select count(*) from users where username=:username;");
    query.bindValue(":username",username);
    query.exec();
    query.next();
    if (query.value(0).toInt()==0)
        return true;
    else
        return false;
}
void ChatServer::closeConn(int descr)//Процедура закрытия соединения
{
    Connections[descr]->close();
    Connections.remove(descr);
}
void ChatServer::login(int descr, QString username, QString password)//Процедура аутентификации пользователя
{
    QSqlQuery query;
    query.prepare("select count(*) from users where username=:username and password=:password;");
    query.bindValue(":username",username);
    query.bindValue(":password",password);
    query.exec();
    query.next();
    if (query.value(0).toInt()==1){
        query.prepare("select isBlocked from users where username=:username");
        query.bindValue(":username",username);
        query.exec();
        query.next();
        if (query.value(0).toInt()==0){
            if (Sessions.contains(username) && Sessions[username]!=descr){
                ui->textBrowser->append("Попытка повторного входа! Соединение сброшено");
                closeConn(Sessions[username]);
            }
            Sessions[username]=descr;
            QTextStream os(Connections[descr]);
            QByteArray xmlMsg;
            QXmlStreamWriter statMsg(&xmlMsg);
            statMsg.writeStartDocument();
            statMsg.writeStartElement("session_id");
            statMsg.writeCharacters(QString::number(descr));
            statMsg.writeEndElement();
            statMsg.writeEndDocument();
            os<<xmlMsg;
            ui->tableWidget->item(Users.indexOf(username),2)->setText("В сети");
            sendBroadcast(username,1);
            ui->textBrowser->append("Код завершения: 0");
            if (msg_hasPenging(username))
                msg_fromPending(username);
        } else sendStatus(descr,5,true,true);
    }
    else
        sendStatus(descr,3,true,true);
}
void ChatServer::logout(int descr, QString username, int session_id)//Процедура выхода пользователя из системы
{
    if (Sessions[username]==descr){
        if (session_id==descr){
            Sessions.remove(username);
            sendStatus(descr,0,false,true);
            ui->tableWidget->item(Users.indexOf(username),2)->setText("Не в сети");
            sendBroadcast(username,0);
        } else sendStatus(descr,4,true,true);
    }else sendStatus(descr,4,true,true);
}
void ChatServer::clientDisconnected()//Обработчик события разрыва соединения
{
    QTcpSocket* currentSocket=(QTcpSocket*)sender();
    int descr=Connections.key(currentSocket);
    if (Sessions.key(descr)!=""){
        sendBroadcast(Sessions.key(descr),0);
        Sessions.remove(Sessions.key(descr));

    }
    Connections.remove(descr);
    getUsers();
}
void ChatServer::msg_to(int descr, QString to,int session_id,QString message)//Обработка сообщений (текстовых) от пользователей
{
    QString username;
    if (Sessions.key(descr)!=""){
        username=Sessions.key(descr);
        if (Sessions[username]==session_id){
            QStringList msg;
            msg.append(username);
            msg.append(to);
            msg.append(message);
            msgQueue.enqueue(msg);
            sendStatus(descr,0,false,false);
            emit msgQueueUpd();
        }else sendStatus(descr,4,true,true);
    } else sendStatus(descr,4,true,false);
}
void ChatServer::userChg(QString username, QString password, int lock_state)//Процедура изменения параметров пользователя
{
    QSqlQuery query;
    if (password==""){
        query.prepare("update users set isBlocked=:lock_state where username=:username");
        query.bindValue(":lock_state",lock_state);
    } else {
        query.prepare("update users set password=:password where username=:username");
        query.bindValue(":password",password);
    }
    query.bindValue(":username",username);
    query.exec();
    query.next();

    getUsers();
}
void ChatServer::usersSend(int descr, QString username,int session_id)//Отправка списка пользователей клиенту
{
    int i;
    QString from;
    if (Sessions.key(descr)!=""){
        from=Sessions.key(descr);
        if (Sessions[from]==session_id){
            QTextStream os(Connections[descr]);
            QMap <QString,QString> params;
            QByteArray msg;
            QXmlStreamWriter xmlMsg(&msg);
            xmlMsg.writeStartDocument();
            xmlMsg.writeStartElement("users");
            for (i=0;i<Users.count();i++){
                if (Users[i]!=from){
                    int status=0;
                    if (Sessions.keys().contains(Users[i]))
                        status=1;
                    xmlMsg.writeStartElement("user");
                    xmlMsg.writeAttribute("status",QString::number(status));
                    xmlMsg.writeCharacters(Users[i]);
                    xmlMsg.writeEndElement();}
            }
            xmlMsg.writeEndElement();
            xmlMsg.writeEndDocument();
            os<<msg;

        }else sendStatus(descr,4,true,true);
    }else sendStatus(descr,4,true,false);
}
void ChatServer::sendMsg(QString from, QString to, QString message)//Процедура отправки сообщения пользователю
{
    int descr=Sessions[to];
    int i;
    QTextStream os(Connections[descr]);
    QMap <QString,QString> params;
    params["from"]=from;
    params["message"]=message;
    QByteArray msg;
    QXmlStreamWriter xmlMsg(&msg);
    xmlMsg.writeStartDocument();
    xmlMsg.writeStartElement("action");
    xmlMsg.writeAttribute("type","msg-from");
    for (i=0;i<params.count();i++){
        xmlMsg.writeStartElement("param");
        xmlMsg.writeAttribute("name",params.keys()[i]);
        xmlMsg.writeCharacters(params.values()[i]);
        xmlMsg.writeEndElement();
    }
    xmlMsg.writeEndElement();
    xmlMsg.writeEndDocument();
    os<<msg;
}

void ChatServer::on_lockButton_clicked()//Обработчик нажатия на кнопку блокировки
{
    QString username=ui->tableWidget->selectedItems()[0]->text();
    int lock_state=0;
    if (ui->tableWidget->selectedItems()[1]->text()=="1"){
        lock_state=1;
    }
    if (lock_state==1)
        lock_state=0;
    else
        lock_state=1;
    userChg(username,"",lock_state);

}


void ChatServer::on_tableWidget_cellActivated(int row, int column)//Обработчик выбора пользователя
{
    ui->lockButton->setEnabled(true);
    ui->deleteUserButton->setEnabled(true);
    ui->changeUserPwButton->setEnabled(true);
    if (ui->tableWidget->selectedItems()[1]->text()=="0")
        ui->lockButton->setText("Заблокировать");
    else
        ui->lockButton->setText("Разблокировать");
}

void ChatServer::userDel(QString username)//Процедура удаления пользователя
{
    QSqlQuery query;
    query.prepare("delete from users where username=:username");
    query.bindValue(":username",username);
    query.exec();
    getUsers();
}
void ChatServer::on_deleteUserButton_clicked()//Обработчик нажатия на кнопку удаления
{
    userDel(ui->tableWidget->selectedItems()[0]->text());
}

void ChatServer::on_changeUserPwButton_clicked()//Обработчик нажатия на кнопку изменения пароля
{
    QString password=QInputDialog::getText(this,"Изменение пароля","Введите новый пароль",QLineEdit::PasswordEchoOnEdit,"");
    userChg(ui->tableWidget->selectedItems()[0]->text(),password,0);
}

void ChatServer::on_start_triggered()//Обработчик кнопки запуска сервера
{
    startServer();
}

void ChatServer::on_stop_triggered() //Обработчик кнопки остановки сервера
{
    for (int i=0;i<Connections.count();i++)
        closeConn(Connections.keys()[i]);
    server->close();
    if (!server->isListening()){
        ui->start->setEnabled(true);
        ui->stop->setEnabled(false);
        ui->statusBar->showMessage("Сервер остановлен");
        queueProcessing=false;
        timer->stop();
        getUsers();
    }
}
void ChatServer::on_exit_triggered() //Обработчик кнопки выхода
{
    for (int i=0;i<Connections.count();i++)
        closeConn(Connections.keys()[i]);
    server->close();
    this->close();
}

void ChatServer::on_about_triggered()//Обработчик кнопки "О программе"
{
    QMessageBox::information(this,"О программе","Сервер чата\n Версия 0.1a");
}

void ChatServer::msg_toPending(QString from, QString to, QString message) //Сохранение непрочитанных сообщений
{
    QSqlQuery query;
    query.prepare("insert into messages (timestamp,to_user,from_user,message) values (:timestamp,:to,:from,:message);");
    query.bindValue(":timestamp",QDateTime::currentDateTime().toMSecsSinceEpoch());
    query.bindValue(":to",to);
    query.bindValue(":from",from);
    query.bindValue(":message",message);
    query.exec();
}
bool ChatServer::msg_hasPenging(QString username) //Проверка наличия непрочитанных сообщений
{
    QSqlQuery query;
    query.prepare("select count(*) from messages where to_user=:username;");
    query.bindValue(":username",username);
    query.exec();
    query.next();
    if (query.value(0).toInt()==0)
        return false;
    else
        return true;
}
void ChatServer::msg_fromPending(QString username) //Отправка непрочитанных сообщений
{
    QSqlQuery query;
    query.prepare("select from_user,message from messages where to_user=:username order by timestamp;");
    query.bindValue(":username",username);
    query.exec();
    while (query.next()){
        QStringList msg;
        msg.append(query.value(0).toString());
        msg.append(username);
        msg.append(query.value(1).toString());
        msgQueue.enqueue(msg);
    }
    query.prepare("delete from messages where to_user=:username;");
    query.bindValue(":username",username);
    query.exec();

}

void ChatServer::on_ChatServer_destroyed() //Обработчик закрытия главного окна
{
    for (int i=0;i<Connections.count();i++)
        closeConn(Connections.keys()[i]);
    server->close();
}

void ChatServer::checkSchema()//Проверка и реинициализация схемы БД при необходимости
{
    QSqlQuery query;
    query.exec("select count(*) from sqlite_master where name='messages' or name='users';");
    query.next();
    if (query.value(0).toInt()<2){
        query.exec("CREATE TABLE messages (timestamp integer,to_user text,from_user text, message text);");
        query.exec("CREATE TABLE users (username TEXT, password Text,isBlocked integer);");
    }
}
void ChatServer::startServer()//Процедура запуска сервера
{
    if (server->listen(QHostAddress::AnyIPv4,12345)){
        ui->start->setEnabled(false);
        ui->stop->setEnabled(true);
        ui->statusBar->showMessage("Сервер запущен");
        queueProcessing=false;
        timer->start();
    }
}
void ChatServer::sendBroadcast(QString username, int status)//Процедура отправки широковещательного сообщения
{
    int i;
    int descr;
    QTextStream os;
    QMap <QString,QString> params;
    QByteArray msg;
    QXmlStreamWriter xmlMsg(&msg);
    params["username"]=username;
    params["status"]=QString::number(status);
    xmlMsg.writeStartDocument();
    xmlMsg.writeStartElement("action");
    xmlMsg.writeAttribute("type","broadcast");
    for (i=0;i<params.keys().count();i++){
        xmlMsg.writeStartElement("param");
        xmlMsg.writeAttribute("name",params.keys()[i]);
        xmlMsg.writeCharacters(params[params.keys()[i]]);
        xmlMsg.writeEndElement();
    }
    xmlMsg.writeEndElement();
    xmlMsg.writeEndDocument();
    for (i=0;i<Sessions.keys().count();i++){
        if (Sessions.keys()[i]!=username){
            descr=Sessions[Sessions.keys()[i]];
            os.setDevice(Connections[descr]);
            os<<msg;}
    }

}

