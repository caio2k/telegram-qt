/*
    Copyright (C) 2014-2015 Alexandr Akulich <akulichalexander@gmail.com>

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
    LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
    OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
    WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "MainWindow.hpp"
#include "ui_MainWindow.h"

#include "CAppInformation.hpp"
#include "CTelegramCore.hpp"
#include "CContactModel.hpp"
#include "CMessagingModel.hpp"
#include "CChatInfoModel.hpp"

#include <QCompleter>
#include <QToolTip>
#include <QStringListModel>

#include <QDebug>

#ifdef CREATE_MEDIA_FILES
#include <QDir>
#endif

#include <QFile>
#include <QFileDialog>

#include <QPixmapCache>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_contactsModel(new CContactsModel(this)),
    m_messagingModel(new CMessagingModel(this)),
    m_chatContactsModel(new CContactsModel(this)),
    m_chatMessagingModel(new CMessagingModel(this)),
    m_chatInfoModel(new CChatInfoModel(this)),
    m_activeChatId(0),
    m_core(new CTelegramCore(this)),
    m_registered(false),
    m_appState(AppStateNone)
{
    ui->setupUi(this);
    ui->contactListTable->setModel(m_contactsModel);
    ui->messagingView->setModel(m_messagingModel);
    ui->groupChatContacts->setModel(m_chatContactsModel);
    ui->groupChatChatsList->setModel(m_chatInfoModel);
    ui->groupChatMessagingView->setModel(m_chatMessagingModel);

    QCompleter *comp = new QCompleter(m_contactsModel, this);
    ui->messagingContactPhone->setCompleter(comp);
    ui->groupChatContactPhone->setCompleter(comp);

    connect(ui->secretOpenFile, SIGNAL(clicked()), SLOT(loadSecretFromBrowsedFile()));

    // Telepathy Morse app info
    CAppInformation appInfo;
    appInfo.setAppId(14617);
    appInfo.setAppHash(QLatin1String("e17ac360fd072f83d5d08db45ce9a121"));
    appInfo.setAppVersion(QLatin1String("0.1"));
    appInfo.setDeviceInfo(QLatin1String("pc"));
    appInfo.setOsInfo(QLatin1String("GNU/Linux"));
    appInfo.setLanguageCode(QLatin1String("en"));

    m_core->setAppInformation(&appInfo);

    connect(m_core, SIGNAL(connected()),
            SLOT(whenConnected()));
    connect(m_core, SIGNAL(phoneStatusReceived(QString,bool,bool)),
            SLOT(whenPhoneStatusReceived(QString,bool,bool)));
    connect(m_core, SIGNAL(phoneCodeRequired()),
            SLOT(whenPhoneCodeRequested()));
    connect(m_core, SIGNAL(authSignErrorReceived(TelegramNamespace::AuthSignError,QString)),
            SLOT(whenAuthSignErrorReceived(TelegramNamespace::AuthSignError,QString)));
    connect(m_core, SIGNAL(authenticated()),
            SLOT(whenAuthenticated()));
    connect(m_core, SIGNAL(initializated()),
            SLOT(whenInitializated()));
    connect(m_core, SIGNAL(contactListChanged()),
            SLOT(whenContactListChanged()));
    connect(m_core, SIGNAL(avatarReceived(QString,QByteArray,QString,QString)),
            SLOT(whenAvatarReceived(QString,QByteArray,QString)));
    connect(m_core, SIGNAL(messageMediaDataReceived(QString,quint32,QByteArray,QString,TelegramNamespace::MessageType)),
            SLOT(whenMessageMediaDataReceived(QString,quint32,QByteArray,QString)));
    connect(m_core, SIGNAL(messageReceived(QString,QString,TelegramNamespace::MessageType,quint32,quint32,quint32)),
            SLOT(whenMessageReceived(QString,QString,TelegramNamespace::MessageType,quint32,quint32,quint32)));
    connect(m_core, SIGNAL(chatMessageReceived(quint32,QString,QString,TelegramNamespace::MessageType,quint32,quint32,quint32)),
            SLOT(whenChatMessageReceived(quint32,QString,QString,TelegramNamespace::MessageType)));
    connect(m_core, SIGNAL(contactChatTypingStatusChanged(quint32,QString,bool)),
            SLOT(whenContactChatTypingStatusChanged(quint32,QString,bool)));
    connect(m_core, SIGNAL(contactTypingStatusChanged(QString,bool)),
            SLOT(whenContactTypingStatusChanged(QString,bool)));
    connect(m_core, SIGNAL(contactStatusChanged(QString,TelegramNamespace::ContactStatus)),
            SLOT(whenContactStatusChanged(QString)));
    connect(m_core, SIGNAL(sentMessageStatusChanged(QString,quint64,TelegramNamespace::MessageDeliveryStatus)),
            m_messagingModel, SLOT(setMessageDeliveryStatus(QString,quint64,TelegramNamespace::MessageDeliveryStatus)));

    connect(m_core, SIGNAL(chatAdded(quint32)), SLOT(whenChatAdded(quint32)));
    connect(m_core, SIGNAL(chatChanged(quint32)), SLOT(whenChatChanged(quint32)));

    ui->groupChatContacts->hideColumn(CContactsModel::Blocked);

    ui->mainSplitter->setSizes(QList<int>() << 0 << 100);
    ui->groupChatSplitter->setSizes(QList<int>() << 550 << 450 << 300);

    ui->groupChatChatsList->setColumnWidth(CChatInfoModel::Id, 30);

    ui->blockContact->hide();
    ui->unblockContact->hide();

    QFile helpFile(QLatin1String(":/USAGE"));
    helpFile.open(QIODevice::ReadOnly);
    ui->helpView->setPlainText(helpFile.readAll());

    setAppState(AppStateNone);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::whenConnected()
{
    setAppState(AppStateConnected);
    ui->phoneNumber->setFocus();
}

void MainWindow::whenAuthenticated()
{
    setAppState(AppStateSignedIn);

    if (ui->workLikeClient->isChecked()) {
        m_core->setOnlineStatus(true);
    }
}

void MainWindow::whenInitializated()
{
    setAppState(AppStateReady);

    const QString selfContact = m_core->selfPhone();
    ui->phoneNumber->setText(selfContact);
    ui->firstName->setText(m_core->contactFirstName(selfContact));
    ui->lastName->setText(m_core->contactLastName(selfContact));
}

void MainWindow::whenPhoneStatusReceived(const QString &phone, bool registered, bool invited)
{
    if (phone == ui->phoneNumber->text()) {
        QString registeredText = registered ? tr("Registered") : tr("Not registered");
        QString invitedText = invited ? tr("invited") : tr("not invited");
        ui->phoneStatus->setText(QString(QLatin1String("%1, %2")).arg(registeredText).arg(invitedText));

        setRegistered(registered);
    } else {
        qDebug() << "Warning: Received status for different phone number" << phone << registered << invited;
    }
}

void MainWindow::whenPhoneCodeRequested()
{
    setAppState(AppStateCodeSent);
}

void MainWindow::whenAuthSignErrorReceived(TelegramNamespace::AuthSignError errorCode, const QString &errorMessage)
{
    switch (errorCode) {
    case TelegramNamespace::AuthSignErrorPhoneNumberIsInvalid:
        if (m_appState == AppStateCodeRequested) {
            QToolTip::showText(ui->phoneNumber->mapToGlobal(QPoint(0, 0)), tr("Phone number is not valid"));
            m_appState = AppStateNone;
        }
        break;
    case TelegramNamespace::AuthSignErrorPhoneCodeIsExpired:
        QToolTip::showText(ui->confirmationCode->mapToGlobal(QPoint(0, 0)), tr("Phone code is expired"));
        break;
    case TelegramNamespace::AuthSignErrorPhoneCodeIsInvalid:
        QToolTip::showText(ui->confirmationCode->mapToGlobal(QPoint(0, 0)), tr("Phone code is invalid"));
        break;
    default:
        qDebug() << "Unknown auth sign error:" << errorMessage;
        return;
    }

    ui->confirmationCode->setFocus();
}

void MainWindow::whenContactListChanged()
{
    setContactList(m_contactsModel, m_core->contactList());
    for (int i = 0; i < ui->contactListTable->model()->rowCount(); ++i) {
        ui->contactListTable->setRowHeight(i, 64);
    }
}

void MainWindow::whenAvatarReceived(const QString &contact, const QByteArray &data, const QString &mimeType)
{
    qDebug() << Q_FUNC_INFO << mimeType;

#ifdef CREATE_MEDIA_FILES
    QDir dir;
    dir.mkdir("avatars");

    QFile avatarFile(QString("avatars/%1.jpg").arg(contact));
    avatarFile.open(QIODevice::WriteOnly);
    avatarFile.write(data);
    avatarFile.close();
#endif

    QPixmap avatar = QPixmap::fromImage(QImage::fromData(data));

    if (avatar.isNull()) {
        return;
    }

    avatar = avatar.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QPixmapCache::insert(m_core->contactAvatarToken(contact), avatar);

    updateAvatar(contact);
}

void MainWindow::whenMessageMediaDataReceived(const QString &contact, quint32 messageId, const QByteArray &data, const QString &mimeType)
{
    qDebug() << Q_FUNC_INFO << mimeType;

#ifdef CREATE_MEDIA_FILES
    QDir dir;
    dir.mkdir("messagesData");

    QFile mediaFile(QString("messagesData/%1-%2.%3").arg(contact)
                    .arg(messageId, 10, 10, QLatin1Char('0'))
                    .arg(mimeType.section(QLatin1Char('/'), 1, 1)));

    mediaFile.open(QIODevice::WriteOnly);
    mediaFile.write(data);
    mediaFile.close();
#else
    Q_UNUSED(contact);
#endif

    const QPixmap photo = QPixmap::fromImage(QImage::fromData(data));

    if (photo.isNull()) {
        return;
    }

    int row = m_messagingModel->setMessageMediaData(messageId, photo);

    if (row >= 0) {
        ui->messagingView->setColumnWidth(CMessagingModel::Message, photo.width());
        ui->messagingView->setRowHeight(row, photo.height());
    }
}

void MainWindow::whenMessageReceived(const QString &phone, const QString &message, TelegramNamespace::MessageType type, quint32 messageId, quint32 flags, quint32 timestamp)
{
    bool outgoing = flags & TelegramNamespace::MessageFlagOut;
    m_messagingModel->addMessage(phone, message, type, outgoing, messageId, timestamp);

    if (!outgoing && (m_contactLastMessageList.value(phone) < messageId)) {
        m_contactLastMessageList.insert(phone, messageId);

        if (ui->settingsReadMessages->isChecked()) {
            if (ui->tabWidget->currentWidget() == ui->tabMessaging) {
                m_core->setMessageRead(phone, messageId);
            }
        }
    }

    if (type == TelegramNamespace::MessageTypePhoto) {
        m_core->requestMessageMediaData(messageId);
    }
}

void MainWindow::whenChatMessageReceived(quint32 chatId, const QString &phone, const QString &message, TelegramNamespace::MessageType type)
{
    if (m_activeChatId != chatId) {
        return;
    }

    m_chatMessagingModel->addMessage(phone, message, type, /* outgoing */ false);
}

void MainWindow::whenContactChatTypingStatusChanged(quint32 chatId, const QString &phone, bool status)
{
    if (m_activeChatId != chatId) {
        return;
    }

    m_chatContactsModel->setTypingStatus(phone, status);
}

void MainWindow::whenContactTypingStatusChanged(const QString &contact, bool typingStatus)
{
    m_contactsModel->setTypingStatus(contact, typingStatus);
    ui->messagingContactTypingStatus->setText(m_contactsModel->data(ui->messagingContactPhone->text(), CContactsModel::TypingStatus).toString());
}

void MainWindow::whenContactStatusChanged(const QString &contact)
{
    m_contactsModel->setContactStatus(contact, m_core->contactStatus(contact));
    m_contactsModel->setContactLastOnline(contact, m_core->contactLastOnline(contact));
}

void MainWindow::whenChatAdded(quint32 chatId)
{
    m_chatInfoModel->addChat(chatId);
    setActiveChat(chatId);
//    whenChatChanged(chatId);
}

void MainWindow::whenChatChanged(quint32 chatId)
{
    if (!m_chatInfoModel->haveChat(chatId)) {
        // Workaround for temporary TelegramQt issues
        m_chatInfoModel->addChat(chatId);
    }

    TelegramNamespace::GroupChat chat;
    if (m_core->getChatInfo(&chat, chatId)) {
        m_chatInfoModel->setChat(chat);
    }

    if (chatId != m_activeChatId) {
        return;
    }

    ui->groupChatName->setText(chat.title);

    QStringList participants;
    if (!m_core->getChatParticipants(&participants, chatId)) {
        qDebug() << Q_FUNC_INFO << "Unable to get chat participants. Invalid chat?";
    }

    setContactList(m_chatContactsModel, participants);
    for (int i = 0; i < m_chatContactsModel->rowCount(); ++i) {
        ui->groupChatContacts->setRowHeight(i, 64);
    }
}

void MainWindow::on_connectButton_clicked()
{
    QByteArray secretInfo = QByteArray::fromHex(ui->secretInfo->toPlainText().toLatin1());

    QString serverIp = ui->mainDcRadio->isChecked() ? QLatin1String("149.154.175.50") : QLatin1String("149.154.175.10");

    quint32 flags = 0;
    if (ui->settingsReceivingFilterReadMessages->isChecked()) {
        flags |= TelegramNamespace::MessageFlagRead;
    }
    if (ui->settingsReceivingFilterOutMessages->isChecked()) {
        flags |= TelegramNamespace::MessageFlagOut;
    }
    m_core->setMessageReceivingFilterFlags(flags);
    m_core->setAcceptableMessageTypes(TelegramNamespace::MessageTypeText|TelegramNamespace::MessageTypePhoto);

    if (secretInfo.isEmpty())
        m_core->initConnection(serverIp, 443);
    else {
        m_core->restoreConnection(secretInfo);
    }
}

void MainWindow::on_secondConnectButton_clicked()
{
    ui->tabWidget->setCurrentWidget(ui->tabMain);
    on_connectButton_clicked();
}

void MainWindow::on_requestCode_clicked()
{
    if (ui->phoneNumber->text().isEmpty()) {
        return;
    }

    m_core->requestPhoneStatus(ui->phoneNumber->text());
    m_core->requestPhoneCode(ui->phoneNumber->text());

    m_appState = AppStateCodeRequested;
}

void MainWindow::on_signButton_clicked()
{
    if (m_registered) {
        m_core->signIn(ui->phoneNumber->text(), ui->confirmationCode->text());
    } else {
        m_core->signUp(ui->phoneNumber->text(), ui->confirmationCode->text(), ui->firstName->text(), ui->lastName->text());
    }
}

void MainWindow::on_getSecretInfo_clicked()
{
    ui->secretInfo->setPlainText(m_core->connectionSecretInfo().toHex());
}

void MainWindow::setRegistered(bool newRegistered)
{
    m_registered = newRegistered;

    ui->firstName->setDisabled(m_registered);
    ui->firstNameLabel->setDisabled(m_registered);
    ui->lastName->setDisabled(m_registered);
    ui->lastNameLabel->setDisabled(m_registered);

    if (m_registered) {
        ui->signButton->setText(tr("Sign in"));
    } else {
        ui->signButton->setText(tr("Sign up"));
    }
}

void MainWindow::setAppState(MainWindow::AppState newState)
{
    m_appState = newState;

    ui->confirmationCode->setEnabled(m_appState == AppStateCodeSent);

    ui->setStatusOnline->setVisible(m_appState >= AppStateSignedIn);
    ui->setStatusOffline->setVisible(m_appState >= AppStateSignedIn);

    ui->phoneNumber->setEnabled(m_appState < AppStateCodeSent);

    switch (m_appState) {
    case AppStateNone:
        ui->connectButton->setVisible(true);
        ui->restoreSession->setVisible(true);

        ui->phoneNumber->setEnabled(true);

        ui->requestCode->setVisible(false);
        ui->signButton->setVisible(false);
        break;
    case AppStateConnected:
        ui->connectionState->setText(tr("Connected..."));
        ui->connectButton->setVisible(false);
        ui->restoreSession->setVisible(false);
        ui->requestCode->setVisible(true);
        ui->signButton->setVisible(true);
        break;
    case AppStateCodeSent:
        ui->connectionState->setText(tr("Code sent..."));
        ui->confirmationCode->setFocus();
        break;
    case AppStateSignedIn:
        ui->connectionState->setText(tr("Signed in..."));
        ui->requestCode->setVisible(false);
        ui->signButton->setVisible(false);

        ui->phoneNumber->setEnabled(false);
        break;
    case AppStateReady:
        ui->connectionState->setText(tr("Ready"));
        ui->requestCode->setVisible(false);
        ui->signButton->setVisible(false);

        ui->phoneNumber->setEnabled(false);
        break;
    default:
        break;
    }
}

void MainWindow::on_addContact_clicked()
{
    m_core->addContact(ui->currentContact->text());
    ui->currentContact->clear();
}

void MainWindow::on_deleteContact_clicked()
{
    m_core->deleteContact(ui->currentContact->text());
    ui->currentContact->clear();
}

void MainWindow::on_messagingSendButton_clicked()
{
    quint64 id = m_core->sendMessage(ui->messagingContactPhone->text(), ui->messagingMessage->text());

    m_messagingModel->addMessage(ui->messagingContactPhone->text(), ui->messagingMessage->text(), TelegramNamespace::MessageTypeText, /* outgoing */ true, id);

    ui->messagingMessage->clear();
}

void MainWindow::on_messagingMessage_textChanged(const QString &arg1)
{
    m_core->setTyping(ui->messagingContactPhone->text(), !arg1.isEmpty());
}

void MainWindow::on_messagingContactPhone_textChanged(const QString &arg1)
{
    ui->messagingContactTypingStatus->setText(m_contactsModel->data(arg1, CContactsModel::TypingStatus).toString());
}

void MainWindow::on_setStatusOnline_clicked()
{
    m_core->setOnlineStatus(/* online */ true);
}

void MainWindow::on_setStatusOffline_clicked()
{
    m_core->setOnlineStatus(/* online */ false);
}

void MainWindow::on_contactListTable_doubleClicked(const QModelIndex &index)
{
    const QModelIndex phoneIndex = m_contactsModel->index(index.row(), CContactsModel::Phone);

    ui->messagingContactPhone->setText(m_contactsModel->data(phoneIndex).toString());
    ui->tabWidget->setCurrentWidget(ui->tabMessaging);
    ui->messagingMessage->setFocus();
}

void MainWindow::on_messagingView_doubleClicked(const QModelIndex &index)
{
    const QModelIndex phoneIndex = m_messagingModel->index(index.row(), CMessagingModel::Phone);

    ui->messagingContactPhone->setText(m_messagingModel->data(phoneIndex).toString());
    ui->tabWidget->setCurrentWidget(ui->tabMessaging);
    ui->messagingMessage->setFocus();
}

void MainWindow::on_groupChatChatsList_doubleClicked(const QModelIndex &index)
{
    const quint32 clickedChat = m_chatInfoModel->index(index.row(), CChatInfoModel::Id).data().toUInt();
    setActiveChat(clickedChat);
}

void MainWindow::on_tabWidget_currentChanged(int index)
{
    Q_UNUSED(index);

    if (ui->tabWidget->currentWidget() == ui->tabMessaging) {
        if (ui->settingsReadMessages->isChecked()) {
            readAllMessages();
        }
    }
}

void MainWindow::on_groupChatCreateChat_clicked()
{
    m_activeChatId = m_core->createChat(m_chatContactsModel->contacts(), ui->groupChatName->text());
    m_chatContactsModel->addContact(m_core->selfPhone());

    ui->groupChatCreateChat->setText(tr("Leave chat"));
    ui->groupChatCreateChat->setEnabled(false);
}

void MainWindow::on_groupChatAddContact_clicked()
{
    m_chatContactsModel->addContact(ui->groupChatContactPhone->text());
    ui->groupChatContactPhone->clear();
}

void MainWindow::on_groupChatRemoveContact_clicked()
{
    m_chatContactsModel->removeContact(ui->groupChatContactPhone->text());
    ui->groupChatContactPhone->clear();
}

void MainWindow::on_groupChatSendButton_clicked()
{
    m_core->sendChatMessage(m_activeChatId, ui->groupChatMessage->text());

    m_chatMessagingModel->addMessage(m_core->selfPhone(), ui->groupChatMessage->text(), TelegramNamespace::MessageTypeText, /* outgoing */ true);
    ui->groupChatMessage->clear();
}

void MainWindow::on_groupChatMessage_textChanged(const QString &arg1)
{
    m_core->setChatTyping(m_activeChatId, !arg1.isEmpty());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)

    if (ui->workLikeClient->isChecked()) {
        m_core->setOnlineStatus(false);
    }
}

void MainWindow::readAllMessages()
{
    foreach (const QString &contact, m_contactLastMessageList.keys()) {
        m_core->setMessageRead(contact, m_contactLastMessageList.value(contact));
    }
}

void MainWindow::setContactList(CContactsModel *contactsModel, const QStringList &newContactList)
{
    contactsModel->setContactList(newContactList);

    foreach (const QString &contact, newContactList) {
        updateAvatar(contact);
        contactsModel->setContactStatus(contact, m_core->contactStatus(contact));
        contactsModel->setContactLastOnline(contact, m_core->contactLastOnline(contact));
        contactsModel->setContactFullName(contact, m_core->contactFirstName(contact) + QLatin1Char(' ') + m_core->contactLastName(contact));
    }
}

void MainWindow::updateAvatar(const QString &contact)
{
    const QString token = m_core->contactAvatarToken(contact);
    QPixmap avatar;

    if (QPixmapCache::find(token, &avatar)) {
        m_contactsModel->setContactAvatar(contact, avatar);
        m_chatContactsModel->setContactAvatar(contact, avatar);
    } else {
        m_core->requestContactAvatar(contact);
    }
}

void MainWindow::on_contactListTable_clicked(const QModelIndex &index)
{
    const QModelIndex correctIndex = m_contactsModel->index(index.row(), CContactsModel::Phone);
    ui->currentContact->setText(correctIndex.data().toString());
}

void MainWindow::on_secretSaveAs_clicked()
{
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Save secret info..."));
    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    file.write(ui->secretInfo->toPlainText().toLatin1());
}

void MainWindow::setActiveChat(quint32 id)
{
    m_activeChatId = id;

    TelegramNamespace::GroupChat chat;
    m_core->getChatInfo(&chat, id);
    ui->groupChatName->setText(chat.title);

    QStringList participants;
    if (!m_core->getChatParticipants(&participants, id)) {
        qDebug() << Q_FUNC_INFO << "Unable to get chat participants. Invalid chat?";
    }

    setContactList(m_chatContactsModel, participants);
    for (int i = 0; i < m_chatContactsModel->rowCount(); ++i) {
        ui->groupChatContacts->setRowHeight(i, 64);
    }
}

void MainWindow::on_restoreSession_clicked()
{
    loadSecretFromBrowsedFile();

    if (ui->secretInfo->toPlainText().isEmpty()) {
        return;
    }

    on_connectButton_clicked();
}

void MainWindow::loadSecretFromBrowsedFile()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Load secret info..."));
    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    ui->secretInfo->setPlainText(file.readAll());
}
