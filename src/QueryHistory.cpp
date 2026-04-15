#include "QueryHistory.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QDebug>

QueryHistory::QueryHistory(QObject *parent) : QObject(parent) {
    load();
}

void QueryHistory::addEntry(const QString &query, const QString &desc, const QString &geojson) {
    HistoryEntry e;
    e.query   = query;
    e.desc    = desc;
    e.geojson = geojson;
    e.time    = QDateTime::currentDateTime();
    m_entries.prepend(e);        // 最新的放最前
    if (m_entries.size() > 100)  // 最多保留 100 条
        m_entries.removeLast();
    save();
}

void QueryHistory::clear() {
    m_entries.clear();
    save();
}

QString QueryHistory::storagePath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/query_history.json";
}

void QueryHistory::save() const {
    QJsonArray arr;
    for (const auto &e : m_entries) {
        arr.append(QJsonObject{
            {"query",   e.query},
            {"desc",    e.desc},
            {"geojson", e.geojson},
            {"time",    e.time.toString(Qt::ISODate)}
        });
    }
    QFile f(storagePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson());
}

void QueryHistory::load() {
    QFile f(storagePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto &v : arr) {
        QJsonObject o = v.toObject();
        HistoryEntry e;
        e.query   = o["query"].toString();
        e.desc    = o["desc"].toString();
        e.geojson = o["geojson"].toString();
        e.time    = QDateTime::fromString(o["time"].toString(), Qt::ISODate);
        m_entries.append(e);
    }
    qDebug() << "[History] 加载了" << m_entries.size() << "条历史记录";
}
