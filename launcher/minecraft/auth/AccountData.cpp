#include "AccountData.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {

QString generateOfflineUUIDv3(const QString& username)
{
    if (username.trimmed().isEmpty()) {
        return QString();
    }
    QByteArray input = QString("OfflinePlayer:%1").arg(username.trimmed()).toUtf8();
    QByteArray hash = QCryptographicHash::hash(input, QCryptographicHash::Md5);
    if (hash.size() < 16) {
        return QString();
    }
    hash[6] = static_cast<char>((hash[6] & 0x0f) | 0x30); // Version 3
    hash[8] = static_cast<char>((hash[8] & 0x3f) | 0x80); // Variant IETF

    const QString hex = QString::fromLatin1(hash.toHex());
    return QString("%1-%2-%3-%4-%5")
        .arg(hex.mid(0, 8))
        .arg(hex.mid(8, 4))
        .arg(hex.mid(12, 4))
        .arg(hex.mid(16, 4))
        .arg(hex.mid(20, 12));
}

void tokenToJSONV3(QJsonObject& parent, const Token& t, const char* tokenName)
{
    if (!t.persistent) {
        return;
    }
    QJsonObject out;
    if (t.issueInstant.isValid()) {
        out["iat"] = QJsonValue(t.issueInstant.toMSecsSinceEpoch() / 1000);
    }

    if (t.notAfter.isValid()) {
        out["exp"] = QJsonValue(t.notAfter.toMSecsSinceEpoch() / 1000);
    }

    bool save = false;
    if (!t.token.isEmpty()) {
        out["token"] = QJsonValue(t.token);
        save = true;
    }
    if (!t.refresh_token.isEmpty()) {
        out["refresh_token"] = QJsonValue(t.refresh_token);
        save = true;
    }
    if (t.extra.size()) {
        out["extra"] = QJsonObject::fromVariantMap(t.extra);
        save = true;
    }
    if (save) {
        parent[tokenName] = out;
    }
}

Token tokenFromJSONV3(const QJsonObject& parent, const char* tokenName)
{
    Token out;
    auto tokenObject = parent.value(tokenName).toObject();
    if (tokenObject.isEmpty()) {
        return out;
    }
    auto issueInstant = tokenObject.value("iat");
    if (issueInstant.isDouble()) {
        out.issueInstant = QDateTime::fromMSecsSinceEpoch(((int64_t)issueInstant.toDouble()) * 1000);
    }

    auto notAfter = tokenObject.value("exp");
    if (notAfter.isDouble()) {
        out.notAfter = QDateTime::fromMSecsSinceEpoch(((int64_t)notAfter.toDouble()) * 1000);
    }

    auto token = tokenObject.value("token");
    if (token.isString()) {
        out.token = token.toString();
        out.validity = Validity::Assumed;
    }

    auto refresh_token = tokenObject.value("refresh_token");
    if (refresh_token.isString()) {
        out.refresh_token = refresh_token.toString();
    }

    auto extra = tokenObject.value("extra");
    if (extra.isObject()) {
        out.extra = extra.toObject().toVariantMap();
    }
    return out;
}

void profileToJSONV3(QJsonObject& parent, MinecraftProfile p, const char* tokenName)
{
    if (p.id.isEmpty() && p.name.isEmpty()) {
        return;
    }
    QJsonObject out;
    out["id"] = QJsonValue(p.id);
    out["name"] = QJsonValue(p.name);
    if (!p.currentCape.isEmpty()) {
        out["cape"] = p.currentCape;
    }

    if (!p.skin.url.isEmpty() || p.skin.data.size() > 0) {
        QJsonObject skinObj;
        skinObj["id"] = p.skin.id;
        skinObj["url"] = p.skin.url;
        skinObj["variant"] = p.skin.variant.isEmpty() ? "classic" : p.skin.variant;
        if (p.skin.data.size()) {
            skinObj["data"] = QString::fromLatin1(p.skin.data.toBase64());
        }
        out["skin"] = skinObj;
    }

    QJsonArray capesArray;
    for (auto& cape : p.capes) {
        QJsonObject capeObj;
        capeObj["id"] = cape.id;
        capeObj["url"] = cape.url;
        capeObj["alias"] = cape.alias;
        if (cape.data.size()) {
            capeObj["data"] = QString::fromLatin1(cape.data.toBase64());
        }
        capesArray.push_back(capeObj);
    }
    if (!capesArray.isEmpty()) {
        out["capes"] = capesArray;
    }
    parent[tokenName] = out;
}

MinecraftProfile profileFromJSONV3(const QJsonObject& parent, const char* tokenName)
{
    MinecraftProfile out;
    auto tokenObject = parent.value(tokenName).toObject();
    if (tokenObject.isEmpty()) {
        return out;
    }
    {
        auto idV = tokenObject.value("id");
        auto nameV = tokenObject.value("name");
        if (!nameV.isString() || nameV.toString().trimmed().isEmpty()) {
            qWarning() << "profile name is missing or invalid";
            return MinecraftProfile();
        }
        out.name = nameV.toString();
        
        if (idV.isString() && !idV.toString().isEmpty()) {
            out.id = idV.toString();
        } else {
            out.id = generateOfflineUUIDv3(out.name);
        }
    }

    {
        auto skinV = tokenObject.value("skin");
        if (skinV.isObject()) {
            auto skinObj = skinV.toObject();
            auto idV = skinObj.value("id");
            auto urlV = skinObj.value("url");
            auto variantV = skinObj.value("variant");

            if (idV.isString()) out.skin.id = idV.toString();
            if (urlV.isString()) {
                out.skin.url = urlV.toString();
                out.skin.url.replace("http://textures.minecraft.net", "https://textures.minecraft.net");
            }
            if (variantV.isString()) {
                out.skin.variant = variantV.toString();
            } else {
                out.skin.variant = "classic";
            }

            auto dataV = skinObj.value("data");
            if (dataV.isString()) {
                out.skin.data = QByteArray::fromBase64(dataV.toString().toLatin1());
            }
        }
    }

    {
        auto capesV = tokenObject.value("capes");
        if (capesV.isArray()) {
            auto capesArray = capesV.toArray();
            for (auto capeV : capesArray) {
                if (capeV.isObject()) {
                    auto capeObj = capeV.toObject();
                    Cape cape;
                    cape.id = capeObj.value("id").toString();
                    cape.url = capeObj.value("url").toString();
                    cape.url.replace("http://textures.minecraft.net", "https://textures.minecraft.net");
                    cape.alias = capeObj.value("alias").toString();

                    auto dataV = capeObj.value("data");
                    if (dataV.isString()) {
                        cape.data = QByteArray::fromBase64(dataV.toString().toLatin1());
                    }
                    if (!cape.id.isEmpty()) {
                        out.capes[cape.id] = cape;
                    }
                }
            }
        }
    }

    {
        auto capeV = tokenObject.value("cape");
        if (capeV.isString()) {
            auto currentCape = capeV.toString();
            if (out.capes.contains(currentCape)) {
                out.currentCape = currentCape;
            }
        }
    }

    out.validity = Validity::Assumed;
    return out;
}

void entitlementToJSONV3(QJsonObject& parent, MinecraftEntitlement p)
{
    if (p.validity == Validity::None) {
        return;
    }
    QJsonObject out;
    out["ownsMinecraft"] = QJsonValue(p.ownsMinecraft);
    out["canPlayMinecraft"] = QJsonValue(p.canPlayMinecraft);
    parent["entitlement"] = out;
}

bool entitlementFromJSONV3(const QJsonObject& parent, MinecraftEntitlement& out)
{
    auto entitlementObject = parent.value("entitlement").toObject();
    if (entitlementObject.isEmpty()) {
        return false;
    }
    {
        auto ownsMinecraftV = entitlementObject.value("ownsMinecraft");
        auto canPlayMinecraftV = entitlementObject.value("canPlayMinecraft");
        if (!ownsMinecraftV.isBool() || !canPlayMinecraftV.isBool()) {
            qWarning() << "mandatory attributes are missing or of unexpected type";
            return false;
        }
        out.canPlayMinecraft = canPlayMinecraftV.toBool(false);
        out.ownsMinecraft = ownsMinecraftV.toBool(false);
        out.validity = Validity::Assumed;
    }
    return true;
}

}  // namespace

bool AccountData::resumeStateFromV3(QJsonObject data)
{
    auto typeV = data.value("type");
    if (!typeV.isString()) {
        qWarning() << "Failed to parse account data: type is missing.";
        return false;
    }
    auto typeS = typeV.toString();
    if (typeS == "MSA") {
        type = AccountType::MSA;
    } else if (typeS == "Offline") {
        type = AccountType::Offline;
    } else {
        qWarning() << "Failed to parse account data: type is not recognized.";
        return false;
    }

    if (type == AccountType::MSA) {
        auto clientIDV = data.value("msa-client-id");
        if (clientIDV.isString()) {
            msaClientID = clientIDV.toString();
        }
        msaToken = tokenFromJSONV3(data, "msa");
        userToken = tokenFromJSONV3(data, "utoken");
        mojangservicesToken = tokenFromJSONV3(data, "xrp-mc");
    }

    yggdrasilToken = tokenFromJSONV3(data, "ygg");
    if (yggdrasilToken.token == "offline" || (type == AccountType::Offline && yggdrasilToken.token.isEmpty())) {
        yggdrasilToken.token = "0";
        yggdrasilToken.validity = Validity::Assumed;
    }

    minecraftProfile = profileFromJSONV3(data, "profile");

    if (type == AccountType::Offline) {
        if (minecraftProfile.id.isEmpty() && !minecraftProfile.name.isEmpty()) {
            minecraftProfile.id = generateOfflineUUIDv3(minecraftProfile.name);
            minecraftProfile.validity = Validity::Assumed;
        }
        minecraftEntitlement.canPlayMinecraft = true;
        minecraftEntitlement.ownsMinecraft = true;
        minecraftEntitlement.validity = Validity::Assumed;
    } else {
        if (!entitlementFromJSONV3(data, minecraftEntitlement)) {
            if (minecraftProfile.validity != Validity::None) {
                minecraftEntitlement.canPlayMinecraft = true;
                minecraftEntitlement.ownsMinecraft = true;
                minecraftEntitlement.validity = Validity::Assumed;
            }
        }
    }

    validity_ = minecraftProfile.validity;
    return true;
}

QJsonObject AccountData::saveState() const
{
    QJsonObject output;
    if (type == AccountType::MSA) {
        output["type"] = "MSA";
        output["msa-client-id"] = msaClientID;
        tokenToJSONV3(output, msaToken, "msa");
        tokenToJSONV3(output, userToken, "utoken");
        tokenToJSONV3(output, mojangservicesToken, "xrp-mc");
    } else if (type == AccountType::Offline) {
        output["type"] = "Offline";
    }

    tokenToJSONV3(output, yggdrasilToken, "ygg");
    profileToJSONV3(output, minecraftProfile, "profile");

    if (type == AccountType::Offline) {
        MinecraftEntitlement offlineEntitlement;
        offlineEntitlement.ownsMinecraft = true;
        offlineEntitlement.canPlayMinecraft = true;
        offlineEntitlement.validity = Validity::Assumed;
        entitlementToJSONV3(output, offlineEntitlement);
    } else {
        entitlementToJSONV3(output, minecraftEntitlement);
    }

    return output;
}

QString AccountData::accessToken() const
{
    if (type == AccountType::Offline && (yggdrasilToken.token.isEmpty() || yggdrasilToken.token == "offline")) {
        return "0";
    }
    return yggdrasilToken.token;
}

QString AccountData::profileId() const
{
    if (type == AccountType::Offline && minecraftProfile.id.isEmpty() && !minecraftProfile.name.isEmpty()) {
        return generateOfflineUUIDv3(minecraftProfile.name);
    }
    return minecraftProfile.id;
}

QString AccountData::profileName() const
{
    if (minecraftProfile.name.size() == 0) {
        return QObject::tr("No Minecraft profile");
    }

    return minecraftProfile.name;
}

QString AccountData::lastError() const
{
    return errorString;
}