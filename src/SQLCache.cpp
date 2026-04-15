#include "SQLCache.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <algorithm>

SQLCache::SQLCache(QObject *parent) : QObject(parent) {
    load();
}

void SQLCache::addEntry(const QString &userQuery,
                        const QString &desc,
                        const QString &sql)
{
    // 相同描述的已有记录直接更新（避免重复示例）
    for (auto &e : m_entries) {
        if (e.desc == desc) {
            e.sql       = sql;
            e.userQuery = userQuery;
            e.time      = QDateTime::currentDateTime();
            save();
            return;
        }
    }

    SQLCacheEntry e;
    e.userQuery = userQuery;
    e.desc      = desc;
    e.sql       = sql;
    e.time      = QDateTime::currentDateTime();
    m_entries.prepend(e);

    if (m_entries.size() > 200)   // 最多缓存 200 条
        m_entries.removeLast();
    save();
    qDebug() << "[SQLCache] 已缓存" << m_entries.size() << "条 SQL";
}

QList<SQLCacheEntry> SQLCache::findSimilar(const QString &query, int topN) const
{
    QList<QPair<int, int>> scored; // {score, index}
    for (int i = 0; i < m_entries.size(); i++) {
        int s = similarity(query, m_entries[i].userQuery)
              + similarity(query, m_entries[i].desc);
        if (s > 0) scored.append({s, i});
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto &a, const auto &b){ return a.first > b.first; });

    QList<SQLCacheEntry> result;
    for (int k = 0; k < qMin(topN, (int)scored.size()); k++)
        result.append(m_entries[scored[k].second]);
    return result;
}

// 持久化
QString SQLCache::storagePath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/sql_cache.json";
}

void SQLCache::save() const {
    QJsonArray arr;
    for (const auto &e : m_entries) {
        arr.append(QJsonObject{
            {"userQuery", e.userQuery},
            {"desc",      e.desc},
            {"sql",       e.sql},
            {"time",      e.time.toString(Qt::ISODate)}
        });
    }
    QFile f(storagePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson());
}

void SQLCache::load() {
    QFile f(storagePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        SQLCacheEntry e;
        e.userQuery = o["userQuery"].toString();
        e.desc      = o["desc"].toString();
        e.sql       = o["sql"].toString();
        e.time      = QDateTime::fromString(o["time"].toString(), Qt::ISODate);
        m_entries.append(e);
    }
    qDebug() << "[SQLCache] 加载了" << m_entries.size() << "条缓存";
}

// 相似度计算
QSet<QString> SQLCache::tokenize(const QString &text)
{
    QSet<QString> tokens;
    QString buf;
    for (const QChar &c : text) {
        bool isCJK = (c.unicode() >= 0x4E00 && c.unicode() <= 0x9FFF);
        if (isCJK) {
            if (!buf.isEmpty()) { tokens.insert(buf); buf.clear(); }
            tokens.insert(QString(c));   // 每个汉字单独作为 token
        } else if (c.isLetterOrNumber()) {
            buf += c;
        } else {
            if (!buf.isEmpty()) { tokens.insert(buf.toLower()); buf.clear(); }
        }
    }
    if (!buf.isEmpty()) tokens.insert(buf.toLower());
    return tokens;
}

int SQLCache::similarity(const QString &a, const QString &b)
{
    QSet<QString> sa = tokenize(a);
    QSet<QString> sb = tokenize(b);
    return (sa & sb).size();   // 交集大小
}
