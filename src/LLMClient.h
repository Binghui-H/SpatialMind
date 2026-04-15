#ifndef LLMCLIENT_H
#define LLMCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>

class LLMClient : public QObject
{
    Q_OBJECT
public:
    explicit LLMClient(QObject *parent = nullptr);

    // 多轮对话（主通道）
    void chat(const QList<QJsonObject> &messages, const QJsonArray &tools);

    // 单次解读请求（独立通道，不影响主对话历史）
    void interpretAsync(const QString &prompt);

    // 动态注入空间上下文 + RAG 示例
    void setContextSuffix(const QString &suffix);

signals:
    void responseReady(const QString &content, const QJsonObject &toolCall);
    void interpretationReady(const QString &text);
    void errorOccurred(const QString &error);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_nam         = nullptr;
    QString                m_apiKey;
    QString                m_baseUrl;
    QString                m_model;
    QString                m_systemPrompt;
    QString                m_contextSuffix;  // 动态上下文（空间焦点 + RAG 示例）
};

#endif // LLMCLIENT_H
