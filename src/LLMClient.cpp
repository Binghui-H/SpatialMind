#include "LLMClient.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QNetworkProxy>
#include <QFile>

LLMClient::LLMClient(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    // 加载配置
    QFile fJson(":/promptProject/prompt.json");
    if (fJson.open(QIODevice::ReadOnly)) {
        QJsonObject cfg = QJsonDocument::fromJson(fJson.readAll()).object();
        m_baseUrl = cfg.value("base_url").toString();
        m_model   = cfg.value("model").toString();
        QString keyEnv = cfg.value("api_key_env").toString();
        m_apiKey  = qgetenv(keyEnv.toUtf8());
    }

    // 单独加载 system prompt
    QFile fSys(":/promptProject/system.txt");
    if (fSys.open(QIODevice::ReadOnly))
        m_systemPrompt = QString::fromUtf8(fSys.readAll());

    qDebug() << "[LLM] prompt length:" << m_systemPrompt.length()
             << "  api_key length:"    << m_apiKey.length();

    connect(m_nam, &QNetworkAccessManager::finished,
            this,  &LLMClient::onReplyFinished);
}

void LLMClient::setContextSuffix(const QString &suffix)
{
    m_contextSuffix = suffix;
}

void LLMClient::chat(const QList<QJsonObject> &messages, const QJsonArray &tools)
{
    QJsonArray msgArr;

    // system prompt = 基础 prompt + 空间上下文 + RAG 示例（若有）
    QString fullPrompt = m_systemPrompt;
    if (!m_contextSuffix.isEmpty())
        fullPrompt += "\n\n" + m_contextSuffix;

    msgArr.append(QJsonObject{{"role", "system"}, {"content", fullPrompt}});
    for (const auto &m : messages) msgArr.append(m);

    QJsonObject body;
    body["model"]       = m_model;
    body["messages"]    = msgArr;
    body["tools"]       = tools;
    body["tool_choice"] = "auto";
    body["max_tokens"]  = 1024;
    body["temperature"] = 0.1;
    body["stream"]      = false;

    QNetworkRequest req(QUrl(m_baseUrl + "/chat/completions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    req.setTransferTimeout(30000);

    // 不设 is_interpret 属性 → 走主通道处理
    m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

// 独立解读请求
void LLMClient::interpretAsync(const QString &prompt)
{
    QJsonObject body;
    body["model"]       = m_model;
    body["max_tokens"]  = 200;   // 解读只需短文本
    body["temperature"] = 0.5;   // 稍高温度，语言更自然
    body["stream"]      = false;
    body["messages"]    = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", prompt}}
    };

    QNetworkRequest req(QUrl(m_baseUrl + "/chat/completions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    req.setTransferTimeout(15000);  // 解读超时短一些

    QNetworkReply *reply = m_nam->post(
        req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    // 标记为解读请求，onReplyFinished 里分流处理
    reply->setProperty("is_interpret", true);
}

// 统一响应处理
void LLMClient::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    bool isInterpret = reply->property("is_interpret").toBool();

    if (reply->error() != QNetworkReply::NoError) {
        if (isInterpret)
            emit interpretationReady({});    // 解读失败：静默降级
        else
            emit errorOccurred("网络错误：" + reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        if (isInterpret) emit interpretationReady({});
        else             emit errorOccurred("响应解析失败：" + data.left(200));
        return;
    }

    QJsonObject root = doc.object();
    if (root.contains("error")) {
        QString msg = root["error"].toObject()["message"].toString();
        if (isInterpret) emit interpretationReady({});
        else             emit errorOccurred("API 错误：" + msg);
        return;
    }

    QJsonObject message = root["choices"].toArray()[0].toObject()["message"].toObject();

    if (isInterpret) {
        // 解读通道：只取文字，不处理 tool_call
        emit interpretationReady(message["content"].toString().trimmed());
        return;
    }

    // 主通道：原有逻辑
    if (message.contains("tool_calls")) {
        emit responseReady({}, message["tool_calls"].toArray()[0].toObject());
    } else {
        emit responseReady(message["content"].toString(), {});
    }
}
