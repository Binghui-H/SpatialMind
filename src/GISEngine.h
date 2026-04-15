#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QJsonArray>
#include <functional>

class GISEngine : public QObject {
    Q_OBJECT
public:
    explicit GISEngine(QObject *parent = nullptr);
    ~GISEngine();

    // 初始化数据库连接，返回是否成功
    bool connectToDatabase(const QString &host,
                           int port,
                           const QString &dbName,
                           const QString &user,
                           const QString &password);

    // 同步执行查询，返回 GeoJSON 字符串
    QString executeQuery(const QString &sql);

    // 异步执行，结果通过 callback 回调到主线程
    void executeAsync(const QString &sql,std::function<void(const QString&, const QString&)> callback);

    // 检查 SQL 安全性（只允许 SELECT）
    bool isSafeQuery(const QString &sql) const;

    // 返回给 LLM 的工具定义
    QJsonArray buildTools() const;

    bool isConnected() const;

private:
    QSqlDatabase m_db;
    bool m_connected = false;
    static int s_connCount; // 多实例时连接名不重复
};
