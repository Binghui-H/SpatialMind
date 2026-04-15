#pragma once
#include <QObject>
#include <QDateTime>
#include <QSet>

struct SQLCacheEntry {
    QString   userQuery;   // 用户原始输入
    QString   desc;        // AI 描述
    QString   sql;         // 执行成功的 SQL
    QDateTime time;
};

class SQLCache : public QObject {
    Q_OBJECT
public:
    explicit SQLCache(QObject *parent = nullptr);

    // 存入一条成功的 SQL
    void addEntry(const QString &userQuery,
                  const QString &desc,
                  const QString &sql);

    // 按关键词相似度检索 topN 条
    QList<SQLCacheEntry> findSimilar(const QString &query, int topN = 3) const;

    void save() const;
    void load();
    static QString storagePath();

private:
    static QSet<QString> tokenize(const QString &text);
    static int           similarity(const QString &a, const QString &b);

    QList<SQLCacheEntry> m_entries;
};
