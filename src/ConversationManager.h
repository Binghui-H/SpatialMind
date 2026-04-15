#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>

class LLMClient;
class GISEngine;
class QueryHistory;
class SQLCache;

class ConversationManager : public QObject {
    Q_OBJECT
public:
    explicit ConversationManager(LLMClient    *llm,
                                  GISEngine    *gis,
                                  QueryHistory *history,
                                  QObject      *parent = nullptr);

    void submitUserInput(const QString &text);

    void setUseSpatialContext(bool on) { m_useSpatialContext = on; }
    void setUseRag(bool on)           { m_useRag = on; }
    bool useSpatialContext() const    { return m_useSpatialContext; }
    bool useRag()            const    { return m_useRag; }

signals:
    void chatAppended(const QString &role, const QString &text);
    void featuresHighlighted(const QString &geojson, const QString &desc);
    void busyChanged(bool busy);
    void historyUpdated();

private slots:
    void onLLMResponse(const QString &content, const QJsonObject &toolCall);
    void onLLMError(const QString &error);
    void onInterpretationReady(const QString &text);

private:
    void handleToolCall(const QJsonObject &toolCall);
    void handleTextResponse(const QString &content);
    void doExecuteQuery(const QString &sql,
                        const QString &desc,
                        const QString &toolCallId);
    void requestInterpretation(const QString &desc,
                               int count,
                               const QStringList &names);
    void compressIfNeeded();
    QString computeCenter(const QString &geojson) const;
    void    updateSpatialContext();  // 内部根据两个开关决定注入什么

    LLMClient    *m_llm;
    GISEngine    *m_gis;
    QueryHistory *m_queryHistory;
    SQLCache     *m_sqlCache;

    QList<QJsonObject> m_messages;
    QString            m_lastUserInput;
    QString            m_lastResultCenter;
    QString            m_lastResultDesc;

    bool m_useSpatialContext = true;
    bool m_useRag            = false;
};
