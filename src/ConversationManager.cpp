#include "ConversationManager.h"
#include "LLMClient.h"
#include "GISEngine.h"
#include "QueryHistory.h"
#include "SQLCache.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

ConversationManager::ConversationManager(LLMClient    *llm,
                                          GISEngine    *gis,
                                          QueryHistory *history,
                                          QObject      *parent)
    : QObject(parent)
    , m_llm(llm)
    , m_gis(gis)
    , m_queryHistory(history)
    , m_sqlCache(new SQLCache(this))
{
    connect(m_llm, &LLMClient::responseReady,
            this,  &ConversationManager::onLLMResponse);
    connect(m_llm, &LLMClient::errorOccurred,
            this,  &ConversationManager::onLLMError);
    connect(m_llm, &LLMClient::interpretationReady,
            this,  &ConversationManager::onInterpretationReady);
}


void ConversationManager::submitUserInput(const QString &text)
{
    m_lastUserInput = text;
    m_messages.append(QJsonObject{{"role", "user"}, {"content", text}});
    compressIfNeeded();
    updateSpatialContext();   // 注入空间焦点 + RAG 示例
    emit busyChanged(true);
    m_llm->chat(m_messages, m_gis->buildTools());
}

void ConversationManager::onLLMResponse(const QString &content,
                                         const QJsonObject &toolCall)
{
    if (!toolCall.isEmpty())
        handleToolCall(toolCall);
    else
        handleTextResponse(content);
}

void ConversationManager::onLLMError(const QString &error)
{
    emit busyChanged(false);
    emit chatAppended("assistant", "错误：" + error);
    qWarning() << "[Conv] LLM error:" << error;
}

// 结果解读回调
void ConversationManager::onInterpretationReady(const QString &text)
{
    if (!text.isEmpty())
        emit chatAppended("assistant", text);
    // 解读完成后才解锁输入
    emit busyChanged(false);
}

// 工具调用
void ConversationManager::handleToolCall(const QJsonObject &toolCall)
{
    const QString func       = toolCall["function"].toObject()["name"].toString();
    const QString toolCallId = toolCall["id"].toString();
    const QString argsStr    = toolCall["function"].toObject()["arguments"].toString();
    const QJsonObject args   = QJsonDocument::fromJson(argsStr.toUtf8()).object();

    if (func != "spatial_query") return;

    const QString sql  = args["sql"].toString();
    const QString desc = args["description"].toString();

    m_messages.append(QJsonObject{
        {"role",    "assistant"},
        {"content", QJsonValue::Null},
        {"tool_calls", QJsonArray{
            QJsonObject{
                {"id",   toolCallId},
                {"type", "function"},
                {"function", QJsonObject{
                    {"name",      func},
                    {"arguments", argsStr}
                }}
            }
        }}
    });

    emit chatAppended("assistant", "正在查询：" + desc + "……");
    doExecuteQuery(sql, desc, toolCallId);
}

void ConversationManager::doExecuteQuery(const QString &sql,
                                          const QString &desc,
                                          const QString &toolCallId)
{
    m_gis->executeAsync(sql,
        [this, sql, desc, toolCallId](const QString &geojson, const QString &error)
        {
            // SQL 执行失败 → 反馈给 LLM 自动修正
            if (!error.isEmpty()) {
                emit chatAppended("assistant", "SQL 有误，正在自动修正……");
                m_messages.append(QJsonObject{
                    {"role",         "tool"},
                    {"content",      "执行失败：" + error},
                    {"tool_call_id", toolCallId}
                });
                m_messages.append(QJsonObject{
                    {"role",    "user"},
                    {"content", "上面的 SQL 执行报错，请修正后重新调用工具。"}
                });
                updateSpatialContext();
                m_llm->chat(m_messages, m_gis->buildTools());
                return;
            }

            // 解析结果
            const QJsonArray features =
                QJsonDocument::fromJson(geojson.toUtf8()).object()["features"].toArray();
            const int count = features.size();

            // 更新地图
            emit featuresHighlighted(geojson, desc);

            if (count == 0) {
                emit chatAppended("assistant","未找到符合条件的结果，可以尝试放宽范围或换个关键词。");
                m_messages.append(QJsonObject{
                    {"role",         "tool"},
                    {"content",      "查询无结果"},
                    {"tool_call_id", toolCallId}
                });
                emit busyChanged(false);
                return;
            }

            // 收集名称列表
            QStringList names;
            for (int i = 0; i < qMin(8, count); i++) {
                const QString n = features[i].toObject()["properties"]
                                  .toObject()["name"].toString();
                if (!n.isEmpty() && !names.contains(n))
                    names << n;
            }

            // 立即显示计数（快速反馈）
            QString quickSummary = QString("共找到 %1 个结果").arg(count);
            if (!names.isEmpty())
                quickSummary += "，包括：" + names.join("、");
            if (count > 8) quickSummary += " 等";
            emit chatAppended("assistant", quickSummary);

            // 更新空间上下文
            m_lastResultCenter = computeCenter(geojson);
            m_lastResultDesc   = desc;
            qDebug() << "[Conv] 空间焦点：" << m_lastResultCenter;

            // RAG：缓存成功的 SQL
            m_sqlCache->addEntry(m_lastUserInput, desc, sql);

            // 持久化查询历史
            m_queryHistory->addEntry(m_lastUserInput, desc, geojson);
            emit historyUpdated();

            // 补 tool 结果消息
            m_messages.append(QJsonObject{
                {"role",         "tool"},
                {"content",      "查询成功"},
                {"tool_call_id", toolCallId}
            });

            // 发起结果解读（解读完成后才 busyChanged(false)）
            requestInterpretation(desc, count, names);
        });
}

// 结果解读 prompt 组装
void ConversationManager::requestInterpretation(const QString &desc,
                                                 int count,
                                                 const QStringList &names)
{
    QString prompt =
        "你是一个 GIS 空间分析助手，用自然口语中文（1-2 句话，不超过 80 字）总结这次查询结果，"
        "可以给出简短的实用建议或有趣的空间发现，语气轻松。不要使用 bullet 点，直接输出总结句子。\n\n"
        "查询内容：" + desc + "\n"
        "找到 " + QString::number(count) + " 个结果，名称包括：" +
        (names.isEmpty() ? "（无名称信息）" : names.mid(0, 5).join("、")) + "\n\n"
        "总结：";

    m_llm->interpretAsync(prompt);
    // busyChanged(false) 推迟到 onInterpretationReady
}

// 纯文字回复处理
void ConversationManager::handleTextResponse(const QString &content)
{
    // 内嵌 GeoJSON 检测
    const int fcStart = content.indexOf(R"({"type":"FeatureCollection")");
    if (fcStart != -1) {
        QString geojson = content.mid(fcStart);
        int depth = 0, end = -1;
        for (int i = 0; i < geojson.length(); i++) {
            if      (geojson[i] == '{') depth++;
            else if (geojson[i] == '}') { if (--depth == 0) { end = i; break; } }
        }
        if (end != -1) geojson = geojson.left(end + 1);

        if (!QJsonDocument::fromJson(geojson.toUtf8()).isNull()) {
            const QString desc = content.left(fcStart).trimmed().isEmpty()
                                 ? "查询结果" : content.left(fcStart).trimmed();
            m_lastResultCenter = computeCenter(geojson);
            m_lastResultDesc   = desc;
            emit featuresHighlighted(geojson, desc);
            emit chatAppended("assistant", "已找到：" + desc);
            m_messages.append(QJsonObject{{"role", "assistant"}, {"content", content}});
            emit busyChanged(false);
            compressIfNeeded();
            return;
        }
    }

    // 假响应检测
    static const QStringList fakePatterns = {
        "查询成功", "已标注", "已在地图", "找到了", "已为您"
    };
    for (const auto &p : fakePatterns) {
        if (content.contains(p)) {
            emit chatAppended("assistant", "正在重新查询……");
            m_messages.append(QJsonObject{
                {"role",    "user"},
                {"content", "请调用 spatial_query 工具执行查询，不要直接回复文字。"}
            });
            updateSpatialContext();
            m_llm->chat(m_messages, m_gis->buildTools());
            return;
        }
    }

    // 正常文字回复
    emit chatAppended("assistant", content);
    m_messages.append(QJsonObject{{"role", "assistant"}, {"content", content}});
    emit busyChanged(false);
    compressIfNeeded();
}

// 上下文压缩（含 RAG 历史摘要）
void ConversationManager::compressIfNeeded()
{
    if (m_messages.size() <= 24) return;

    QStringList keywords;
    for (int i = 0; i < m_messages.size() - 12; i++) {
        const QString role = m_messages[i]["role"].toString();
        const QString text = m_messages[i]["content"].toString();
        if (role == "user" && text.length() < 60 && !text.startsWith("[历史摘要]"))
            keywords << text;
    }

    QString summary = "[历史摘要] 用户进行了以下查询：";
    summary += keywords.isEmpty() ? "多次空间查询" : keywords.join("、");
    if (!m_lastResultDesc.isEmpty())
        summary += "。最近有效结果：" + m_lastResultDesc;
    if (!m_lastResultCenter.isEmpty())
        summary += "，中心坐标：" + m_lastResultCenter;

    QList<QJsonObject> compressed;
    compressed.append(QJsonObject{{"role", "user"},      {"content", summary}});
    compressed.append(QJsonObject{{"role", "assistant"}, {"content", "好的，我记住了历史记录和当前空间焦点。"}});
    for (int i = m_messages.size() - 12; i < m_messages.size(); i++)
        compressed.append(m_messages[i]);

    m_messages = compressed;
    qDebug() << "[Conv] 压缩后消息数：" << m_messages.size();
}

// 空间焦点 + RAG 注入
QString ConversationManager::computeCenter(const QString &geojson) const
{
    const QJsonArray features =
        QJsonDocument::fromJson(geojson.toUtf8()).object()["features"].toArray();
    if (features.isEmpty()) return {};

    double sumLng = 0, sumLat = 0;
    int    count  = 0;

    auto addPt = [&](const QJsonArray &pt) {
        if (pt.size() >= 2) { sumLng += pt[0].toDouble(); sumLat += pt[1].toDouble(); count++; }
    };

    for (const auto &fv : features) {
        const QJsonObject geom  = fv.toObject()["geometry"].toObject();
        const QString     type  = geom["type"].toString();
        const QJsonArray  coords = geom["coordinates"].toArray();

        if (type == "Point") {
            addPt(coords);
        } else if (type == "MultiPoint" || type == "LineString") {
            for (const auto &p : coords) addPt(p.toArray());
        } else if (type == "MultiLineString" || type == "Polygon") {
            for (const auto &ring : coords)
                for (const auto &p : ring.toArray()) addPt(p.toArray());
        } else if (type == "MultiPolygon") {
            for (const auto &poly : coords)
                for (const auto &ring : poly.toArray())
                    for (const auto &p : ring.toArray()) addPt(p.toArray());
        }
    }

    return count > 0
        ? QString("%1,%2").arg(sumLng/count, 0,'f',6).arg(sumLat/count, 0,'f',6)
        : QString{};
}

// 根据开关决定注入哪些内容
void ConversationManager::updateSpatialContext()
{
    QString suffix;

    // 空间焦点
    if (m_useSpatialContext && !m_lastResultCenter.isEmpty()) {
        suffix +=
            "## 当前地图焦点\n"
            "上次查询「" + m_lastResultDesc + "」的结果中心（经度,纬度）：" +
            m_lastResultCenter + "\n"
            "- 用户说「这附近」「附近」「旁边」「最近的」时，以此坐标为基准\n"
            "- ST_DWithin 里用 ST_SetSRID(ST_MakePoint(经度,纬度),4326)::geography\n"
            "- 「最近的 N 个」用 ORDER BY ST_Distance(...) LIMIT N，不要用 ST_DWithin\n\n";
    }

    // RAG：注入相似成功 SQL 示例（受 m_useRag 开关控制）
    if (m_useRag) {
        const QList<SQLCacheEntry> examples =
            m_sqlCache->findSimilar(m_lastUserInput, 3);

        if (!examples.isEmpty()) {
            suffix += "## 参考示例（过去执行成功的相似查询）\n"
                      "遇到类似查询时可以参考这些 SQL 的写法：\n\n";
            for (int i = 0; i < examples.size(); i++) {
                const auto &e = examples[i];
                suffix += QString("示例%1：%2\nSQL：%3\n\n")
                          .arg(i + 1).arg(e.desc).arg(e.sql);
            }
        }
    }

    m_llm->setContextSuffix(suffix.trimmed());
}
