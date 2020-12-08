#include "chatclient.h"
#include "ui_chatclient.h"

ChatClient::ChatClient(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ChatClient)
{
    ui->setupUi(this);
    client=new QTcpSocket(this);

    db=QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("set.db");
    if (!db.open()){
        QMessageBox::warning(this,"Ошибка","Невозможно открыть базу данных! Диалоги не могут быть загружены!");
    }
    checkSchema();
    connect(client,SIGNAL(connected()),this,SLOT(onConnect()));
    connect(client,SIGNAL(readyRead()),this,SLOT(onReadyRead()));
    connect (client,SIGNAL(disconnected()),this,SLOT(onDisconnected()));
    connect (client,SIGNAL(error(QAbstractSocket::SocketError)),this,SLOT(onSocketError()));
    Actions["msg-from"]=0;
    errorCode[0]="Не ошибка!";
    errorCode[1]="Ошибка в параметрах запроса!";
    errorCode[2]="Пользователь с таким именем уже существует!";
    errorCode[3]="Неверное имя пользователя или пароль!";
    errorCode[4]="Ошибка аутентификации";
    errorCode[5]="Пользователь заблокирован! Свяжитесь с администратором сервера";
    errorCode[6]="Пользователь не существует!";

}
/*Коды состояния:
 * 0 - Процедура входа
 * 1 - Главная процедура
 * 2 - Отправлено сообщение
 * 3 - Процедура регистрации
 * 4 - Получено сообщение
 * 5 - Процедура добавления диалога
 * 6 - Процедура выхода
 * 7 - Процедура повторного соединения
 *
 */
ChatClient::~ChatClient()
{
    delete ui;
}
void ChatClient::onConnect()//Обработчик соединия с сервером
{
    switch (state){
    case 0:
        sendLogin();
        break;
    case 3:
        sendReg();
        break;
    case 6:
        logout();
        break;
    }
}
void ChatClient::onReadyRead()//Обработчик нового служебного сообщения
{
    QByteArray msg;
    int i;
    msg=client->readAll();
    QString tmp;
    QString tmpmsg;
    tmp=QString::fromUtf8(msg);
    QStringList msgList=tmp.split("\n");

    for (i=0;i<msgList.count()-1;i++)
        parseXml(msgList[i].toUtf8());
}

void ChatClient::on_loginButton_clicked()//Обработчик кнопки входа
{
    state=0;
    if (ui->isReg->isChecked())
        state=3;
    if (client->state()!=client->ConnectedState)
        client->connectToHost(ui->ipEdit->text(),12345,QIODevice::ReadWrite);
    else
        sendLogin();
}
void ChatClient::sendXml(QString action, QMap <QString,QString> params)//Процедура формирования и отправки служебного сообщения
{
    int i;
    if (client->state()==client->ConnectedState){
        QByteArray msg;
        QXmlStreamWriter xmlMsg(&msg);
        xmlMsg.writeStartDocument();
        xmlMsg.writeStartElement("chat");
        xmlMsg.writeStartElement("action");
        xmlMsg.writeAttribute("type",action);
        for(i=0;i<params.count();i++){
            xmlMsg.writeStartElement("param");
            xmlMsg.writeAttribute("name",params.keys()[i]);
            xmlMsg.writeCharacters(params.values()[i]);
            xmlMsg.writeEndElement();
        }
        xmlMsg.writeEndElement();
        xmlMsg.writeEndElement();
        xmlMsg.writeEndDocument();
        QTextStream os(client);
        os<<msg;
    }else
        client->connectToHost(ui->ipEdit->text(),12345,QIODevice::ReadWrite);
}
void ChatClient::sendLogin()//Процедура отправки авторизационных данных
{
    QMap <QString,QString> params;
    params["username"]=ui->loginEdit->text();
    params["password"]=ui->passwordEdit->text();
    sendXml("login",params);
}
void ChatClient::parseXml(QByteArray msg)//Процедура парсинга служебных сообщений
{
    QXmlStreamReader xmlMsg(msg);
    bool isStart=true;
    int action=-1;
    int status=-1;
    int id=-1;
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

            if (xmlMsg.name()=="status")
                status=xmlMsg.readElementText().toInt();


            if (xmlMsg.name()=="session_id")
                id=xmlMsg.readElementText().toInt();

            continue;
        }

        if (isStart && token==QXmlStreamReader::EndDocument)
            break;
    }
    if (action!=-1){
        if (action==0)
            if (state==1){
                state=4;
                msg_from(params);
            }
    }

    if (id!=-1){
        completeLogin(id);
    }
    if (status!=-1){
        parseStatus(status);
    }
}
void ChatClient::completeLogin(int id)//Процедура завершения авторизации
{
    if (state==0)
        ui->loginFrame->setVisible(false);
    session_id=id;
    state=1;
    ui->userName->setText(ui->loginEdit->text());
    loadDialogs();
}
void ChatClient::parseStatus(int status)//Парсинг сообщений статуса и вызов соответствующих обработчиков
{
    if (status==0){
        switch (state){
        case 5:
            addDialog();
            break;
        case 2:
            showDialog();
            break;
        case 3:
            completeReg();
            break;
        case 6:
            logout();
            break;
        }
    }
    else {
        if (state==5)
            state=1;
        QMessageBox::warning(this,"Ошибка!",errorCode[status]);
    }
}

void ChatClient::on_addDialog_clicked()//Обработчик кнопки "+" (Добавить диалог)
{
    QMap <QString,QString> params;
    state=5;
    params["username"]=QInputDialog::getText(this,"Добавление диалога","Введите имя пользователя",QLineEdit::Normal,"");
    params["session_id"]=QString::number(session_id);
    addUsername=params["username"];
    sendXml("check_user",params);
}
void ChatClient::addDialog()//Процедура добавления диалога
{
    QSqlQuery query;
    QStringList none;
    Dialogs[addUsername]=none;
    query.prepare("insert or ignore into dialogs (login_name,username) VALUES (:login_name,:username);");
    query.bindValue(":username",addUsername);
    query.bindValue(":login_name",ui->loginEdit->text());
    query.exec();
    updateDialogList();
}
void ChatClient::updateDialogList()//Процедура обновления списка диалогов
{
    ui->listWidget->clear();
    for (int i=0;i<Dialogs.count();i++){
        ui->listWidget->addItem(Dialogs.keys()[i]);
    }
    state=1;

}
void ChatClient::showDialog()//Процедура отображения диалога
{
    QSqlQuery query;
    ui->textBrowser->clear();
    for (int i=0;i<Dialogs[currentDialog].count();i++){
        ui->textBrowser->append(Dialogs[currentDialog][i]);
    }
    state=1;
}
void ChatClient::on_listWidget_itemDoubleClicked(QListWidgetItem *item)//Обработчик события выбора диалога
{
    QSqlQuery query;
    currentDialog=item->text();
    showDialog();
    ui->sendButton->setEnabled(true);
}

void ChatClient::on_sendButton_clicked()//Обработчик кнопки "Отправить"
{
    QSqlQuery query;
    if (currentDialog!=""){
        QMap<QString,QString> params;
        params["session_id"]=QString::number(session_id);
        params["to"]=currentDialog;
        params["message"]=ui->lineEdit->text();
        state=2;
        sendXml("msg-to",params);
        QStringList msg;
        Dialogs[currentDialog].append("\t-"+ui->loginEdit->text()+"-");
        Dialogs[currentDialog].append(ui->lineEdit->text());
        msg.append("\t-"+ui->loginEdit->text()+"-");
        msg.append(ui->lineEdit->text());
        saveDialog(msg,currentDialog);
        ui->lineEdit->clear();
    }
}
void ChatClient::sendReg()//Процедура запроса регистрации
{
    QMap <QString,QString> params;
    params["username"]=ui->loginEdit->text();
    params["password"]=ui->passwordEdit->text();
    sendXml("reg",params);
}

void ChatClient::completeReg()//Процедура завершения регистрации
{
    ui->isReg->setChecked(false);
    QMessageBox::information(this,"Информация","Регистрация успешно завершена!");
    state=0;

}
void ChatClient::onDisconnected()//Обработчик события разрыва соединения
{
    if (state!=0&&state!=6){
        state=7;
        client->connectToHost(ui->ipEdit->text(),12345,QIODevice::ReadWrite);
    }
}
void ChatClient::onSocketError()//Обработчик ошибок  соединения
{
    ui->statusBar->showMessage("Ошибка сети! "+client->errorString());

}
void ChatClient::msg_from(QMap<QString, QString> params)//Обработчик новых сообщений
{
    QSqlQuery query;
    QString from=params["from"];
    QString message=params["message"];
    if (!Dialogs.keys().contains(from)){
        addUsername=from;
        addDialog();
    }
    QStringList msg;
    msg.append("\t-"+from+"-");
    msg.append(message);
    Dialogs[from].append(msg);
    state=1;
    showDialog();
    saveDialog(msg,from);
}
void ChatClient::logout()//Обработчик события выхода
{
    session_id=-1;
    ui->loginFrame->setVisible(true);
    client->close();
    state=0;
    Dialogs.clear();
    ui->textBrowser->clear();
}


void ChatClient::on_logoutButton_clicked()//Обработчик кнопки "Выход"
{
    QMap <QString,QString> params;
    params["session_id"]=QString::number(session_id);
    params["username"]=ui->loginEdit->text();
    state=6;
    sendXml("logout",params);

}

void ChatClient::on_delDialog_clicked()//Обработчик кнопки "-" (Удалить диалог)
{
    QSqlQuery query;
    if (ui->listWidget->selectedItems().count()!=0){
        if (currentDialog==ui->listWidget->selectedItems()[0]->text())
            ui->textBrowser->clear();
        Dialogs.remove(ui->listWidget->selectedItems()[0]->text());
        delDialog(ui->listWidget->selectedItems()[0]->text());
    }
    updateDialogList();
}
void ChatClient::loadDialogs()//Процедура загрузки диалогов из СУБД
{
    QSqlQuery query;
    query.prepare("select dialog.username,msg.message from dialogs dialog join messages msg on msg.dialog_id=dialog.rowid where dialog.login_name=:login_name order by msg.timestamp;");
    query.bindValue(":login_name",ui->loginEdit->text());
    query.exec();
    while (query.next()){
        Dialogs[query.value(0).toString()].append(query.value(1).toString());
    }
    updateDialogList();
}
void ChatClient::saveDialog(QStringList msg,QString username)//Процедура сохранения сообщений в СУБД
{
    QSqlQuery query;
    query.prepare("insert into messages (dialog_id,timestamp,message) values ((select rowid from dialogs where username=:username and login_name=:login_name),:timestamp,:message);");
    query.bindValue(":timestamp",QDateTime::currentMSecsSinceEpoch());
    query.bindValue(":message",msg[0]);
    query.bindValue(":login_name",ui->loginEdit->text());
    query.bindValue(":username",username);
    query.exec();
    query.prepare("insert into messages (dialog_id,timestamp,message) values ((select rowid from dialogs where username=:username and login_name=:login_name),:timestamp,:message);");
    query.bindValue(":timestamp",QDateTime::currentMSecsSinceEpoch());
    query.bindValue(":message",msg[1]);
    query.bindValue(":login_name",ui->loginEdit->text());
    query.bindValue(":username",username);
    query.exec();

}
void ChatClient::checkSchema()//Процедура проверки корректности схемы СУБД (и ее реинициализации при необходимости)
{
    QSqlQuery query;
    query.exec("select count(*) from sqlite_master where name='messages';");
    query.next();
    if (query.value(0).toInt()==0)
        query.exec("create table messages (dialog_id integer,timestamp integer,message text)");
    query.exec("select count(*) from sqlite_master where name='dialogs';");
    query.next();
    if (query.value(0).toInt()==0)
        query.exec("create table dialogs (login_name text, username text)");

}
void ChatClient::delDialog(QString username)//Процедура удаления диалога из СУБД
{
    QSqlQuery query;
    query.prepare("select rowid from dialogs where login_name=:login_name and username=:username");
    query.bindValue(":login_name",ui->loginEdit->text());
    query.bindValue(":username",currentDialog);
    query.exec();
    query.next();
    int dialog_id=query.value(0).toInt();
    query.prepare("delete from messages where dialog_id=:dialog_id;");
    query.bindValue(":dialog_id",dialog_id);
    query.exec();
    query.prepare("delete from dialogs where login_name=:login_name and username=:username");
    query.bindValue(":login_name",ui->loginEdit->text());
    query.bindValue(":username",username);
    query.exec();
}

