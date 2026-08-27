#include "MinecraftAccount.h"

#include <QColor>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>

#include <QDebug>
#include <QPainter>

#include "minecraft/auth/AccountData.h"
#include "minecraft/auth/AuthFlow.h"
#include "minecraft/auth/AuthSession.h"

MinecraftAccount::MinecraftAccount(QObject* parent) : QObject(parent)
{
    data.internalId = QUuid::createUuid().toString(QUuid::Id128);
}

MinecraftAccountPtr MinecraftAccount::loadFromJsonV3(const QJsonObject& json)
{
    MinecraftAccountPtr account(new MinecraftAccount());
    if (account->data.resumeStateFromV3(json)) {
        return account;
    }
    return nullptr;
}

MinecraftAccountPtr MinecraftAccount::createBlankMSA()
{
    MinecraftAccountPtr account(new MinecraftAccount());
    account->data.type = AccountType::MSA;
    return account;
}

MinecraftAccountPtr MinecraftAccount::createOffline(const QString& username)
{
    auto account = makeShared<MinecraftAccount>();
    account->data.type = AccountType::Offline;
    account->data.yggdrasilToken.token = "0";
    account->data.yggdrasilToken.validity = Validity::Certain;
    account->data.yggdrasilToken.issueInstant = QDateTime::currentDateTimeUtc();
    account->data.yggdrasilToken.extra["userName"] = username;
    account->data.yggdrasilToken.extra["clientToken"] = QUuid::createUuid().toString(QUuid::Id128);
    
    account->data.minecraftProfile.name = username;
    account->data.minecraftProfile.id = uuidFromUsername(username).toString(QUuid::Id128);
    account->data.minecraftProfile.validity = Validity::Certain;
    
    // [تعديل أساسي]: تفعيل الملكية وتصريح اللعب الكامل للحساب الأوفلاين
    account->data.minecraftEntitlement.ownsMinecraft = true;
    account->data.minecraftEntitlement.canPlayMinecraft = true;
    account->data.minecraftEntitlement.validity = Validity::Certain;
    
    account->data.accountState = AccountState::Offline;
    account->data.validity_ = Validity::Certain;
    return account;
}

QJsonObject MinecraftAccount::saveToJson() const
{
    return data.saveState();
}

AccountType MinecraftAccount::accountType() const
{
    return data.type;
}

bool MinecraftAccount::isOffline() const
{
    return data.type == AccountType::Offline;
}

bool MinecraftAccount::ownsMinecraft() const
{
    // [تعديل أساسي]: إرجاع true دائماً للحسابات الأوفلاين
    if (data.type == AccountType::Offline || isOffline()) {
        return true;
    }
    return data.minecraftEntitlement.ownsMinecraft;
}

bool MinecraftAccount::canPlayOnline() const
{
    return data.type != AccountType::Offline && ownsMinecraft() && data.minecraftProfile.isValid();
}

bool MinecraftAccount::canPlayOffline() const
{
    if (data.type == AccountType::Offline) {
        return !data.minecraftProfile.name.trimmed().isEmpty();
    }
    return data.minecraftProfile.isValid();
}

QString MinecraftAccount::profileName() const
{
    return data.profileName();
}

QString MinecraftAccount::profileId() const
{
    return data.profileId();
}

AccountState MinecraftAccount::accountState() const
{
    if (isOffline()) {
        return AccountState::Offline;
    }
    return data.accountState;
}

QPixmap MinecraftAccount::getFace(int width, int height) const
{
    QPixmap skinTexture;
    if (!skinTexture.loadFromData(data.minecraftProfile.skin.data, "PNG")) {
        return QPixmap();
    }
    QPixmap skin = QPixmap(8, 8);
    skin.fill(QColorConstants::Transparent);
    QPainter painter(&skin);
    painter.drawPixmap(0, 0, skinTexture.copy(8, 8, 8, 8));
    painter.drawPixmap(0, 0, skinTexture.copy(40, 8, 8, 8));
    return skin.scaled(width, height, Qt::KeepAspectRatio);
}

shared_qobject_ptr<AuthFlow> MinecraftAccount::login(bool useDeviceCode)
{
    if (isOffline()) {
        qDebug() << "Skipping online AuthFlow for offline account:" << profileName();
        emit changed();
        return nullptr;
    }

    Q_ASSERT(m_currentTask.get() == nullptr);

    m_currentTask.reset(new AuthFlow(&data, useDeviceCode ? AuthFlow::Action::DeviceCode : AuthFlow::Action::Login));
    connect(m_currentTask.get(), &Task::succeeded, this, &MinecraftAccount::authSucceeded);
    connect(m_currentTask.get(), &Task::failed, this, &MinecraftAccount::authFailed);
    connect(m_currentTask.get(), &Task::aborted, this, [this] { authFailed(tr("Aborted")); });
    emit activityChanged(true);
    return m_currentTask;
}

shared_qobject_ptr<AuthFlow> MinecraftAccount::refresh()
{
    if (isOffline()) {
        return nullptr;
    }

    if (m_currentTask) {
        return m_currentTask;
    }

    m_currentTask.reset(new AuthFlow(&data, AuthFlow::Action::Refresh));

    connect(m_currentTask.get(), &Task::succeeded, this, &MinecraftAccount::authSucceeded);
    connect(m_currentTask.get(), &Task::failed, this, &MinecraftAccount::authFailed);
    connect(m_currentTask.get(), &Task::aborted, this, [this] { authFailed(tr("Aborted")); });
    emit activityChanged(true);
    return m_currentTask;
}

shared_qobject_ptr<AuthFlow> MinecraftAccount::currentTask()
{
    return m_currentTask;
}

void MinecraftAccount::authSucceeded()
{
    m_currentTask.reset();
    emit changed();
    emit activityChanged(false);
}

void MinecraftAccount::authFailed(QString reason)
{
    switch (m_currentTask->taskState()) {
        case AccountTaskState::STATE_OFFLINE:
        case AccountTaskState::STATE_DISABLED: {
            // NOTE: user will need to fix this themselves.
        }
        case AccountTaskState::STATE_FAILED_SOFT: {
            // NOTE: this doesn't do much. There was an error of some sort.
        } break;
        case AccountTaskState::STATE_FAILED_HARD: {
            if (accountType() == AccountType::MSA) {
                data.msaToken.token = QString();
                data.msaToken.refresh_token = QString();
                data.msaToken.validity = Validity::None;
                data.validity_ = Validity::None;
            } else {
                data.yggdrasilToken.token = QString();
                data.yggdrasilToken.validity = Validity::None;
                data.validity_ = Validity::None;
            }
            emit changed();
        } break;
        case AccountTaskState::STATE_FAILED_GONE: {
            data.validity_ = Validity::None;
            emit changed();
        } break;
        case AccountTaskState::STATE_WORKING: {
            data.accountState = AccountState::Unchecked;
        } break;
        case AccountTaskState::STATE_CREATED:
        case AccountTaskState::STATE_SUCCEEDED: {
            // Not reachable here, as they are not failures.
        }
    }
    m_currentTask.reset();
    emit activityChanged(false);
}

QString MinecraftAccount::displayName() const
{
    if (isOffline()) {
        return profileName();
    }
    if (const QList validStates{ AccountState::Unchecked, AccountState::Working, AccountState::Offline, AccountState::Online }; !validStates.contains(accountState())) {
        return QString("⚠ %1").arg(profileName());
    }
    return profileName();
}

bool MinecraftAccount::isActive() const
{
    return !m_currentTask.isNull();
}

bool MinecraftAccount::shouldRefresh() const
{
    // [تعديل أساسي]: لا تقم بتحديث حسابات الأوفلاين مطلقاً
    if (data.type == AccountType::Offline || isOffline()) {
        return false;
    }

    if (isInUse()) {
        return false;
    }
    switch (data.validity_) {
        case Validity::Certain: {
            break;
        }
        case Validity::None: {
            return false;
        }
        case Validity::Assumed: {
            return true;
        }
    }
    auto now = QDateTime::currentDateTimeUtc();
    auto issuedTimestamp = data.yggdrasilToken.issueInstant;
    auto expiresTimestamp = data.yggdrasilToken.notAfter;

    if (!expiresTimestamp.isValid()) {
        expiresTimestamp = issuedTimestamp.addSecs(24 * 3600);
    }
    if (now.secsTo(expiresTimestamp) < (12 * 3600)) {
        return true;
    }
    return false;
}

void MinecraftAccount::fillSession(AuthSessionPtr session)
{
    if (!session) {
        return;
    }

    // volatile auth token (0 for offline)
    session->access_token = data.accessToken().isEmpty() ? "0" : data.accessToken();
    // profile name
    session->player_name = data.profileName();
    // profile ID
    session->uuid = data.profileId();
    if (session->uuid.isEmpty()) {
        session->uuid = uuidFromUsername(session->player_name).toString(QUuid::Id128);
    }
    
    // 'legacy' or 'mojang', depending on account type
    session->user_type = isOffline() ? "legacy" : typeString();
    
    if (isOffline()) {
        session->session = "token:0:" + session->uuid;
        session->status = AuthSession::PlayableOffline;
        session->wants_offline = true;
    } else {
        if (!session->access_token.isEmpty()) {
            session->session = "token:" + data.accessToken() + ":" + data.profileId();
        } else {
            session->session = "-";
        }
    }
}

void MinecraftAccount::decrementUses()
{
    Usable::decrementUses();
    if (!isInUse()) {
        emit changed();
        qWarning() << "Profile" << data.profileId() << "is no longer in use.";
    }
}

void MinecraftAccount::incrementUses()
{
    bool wasInUse = isInUse();
    Usable::incrementUses();
    if (!wasInUse) {
        emit changed();
        qWarning() << "Profile" << data.profileId() << "is now in use.";
    }
}

QUuid MinecraftAccount::uuidFromUsername(QString username)
{
    auto input = QString("OfflinePlayer:%1").arg(username.trimmed()).toUtf8();

    // basically a reimplementation of Java's UUID#nameUUIDFromBytes
    QByteArray digest = QCryptographicHash::hash(input, QCryptographicHash::Md5);

    auto bOr = [](QByteArray& array, qsizetype index, uint8_t value) { array[index] |= value; };
    auto bAnd = [](QByteArray& array, qsizetype index, uint8_t value) { array[index] &= value; };
    bAnd(digest, 6, 0x0f);  // clear version
    bOr(digest, 6, 0x30);   // set to version 3
    bAnd(digest, 8, 0x3f);  // clear variant
    bOr(digest, 8, 0x80);   // set to IETF variant

    return QUuid::fromRfc4122(digest);
}