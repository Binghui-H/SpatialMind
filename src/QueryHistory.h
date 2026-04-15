#pragma once
#include <QObject>
#include <QJsonArray>
#include <QDateTime>

struct HistoryEntry {
    QString     query;      // 用户原始输入
    QString     desc;       // AI 描述
    QString     geojson;    // 查询结果
    QDateTime   time;
};

class QueryHistory : public QObject {
    Q_OBJECT
public:
    explicit QueryHistory(QObject *parent = nullptr);

    void addEntry(const QString &query, const QString &desc, const QString &geojson);
    QList<HistoryEntry> entries() const { return m_entries; }
    void clear();

    // 持久化
    void save() const;
    void load();

    static QString storagePath();

private:
    QList<HistoryEntry> m_entries;
};
