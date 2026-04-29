#include "GISEngine.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QtConcurrent>
#include <QDebug>

int GISEngine::s_connCount = 0;

GISEngine::GISEngine(QObject *parent) : QObject(parent) {}

GISEngine::~GISEngine() {
    if (m_db.isOpen()) m_db.close();
    QString connName = m_db.connectionName();
    m_db = QSqlDatabase(); // 必须先置空，再 removeDatabase
    QSqlDatabase::removeDatabase(connName);
}

bool GISEngine::connectToDatabase(const QString &host, int port,
                                   const QString &dbName,
                                   const QString &user,
                                   const QString &password) {
    QString connName = QString("spatialmind_%1").arg(s_connCount++);
    m_db = QSqlDatabase::addDatabase("QPSQL", connName);
    m_db.setHostName(host);
    m_db.setPort(port);
    m_db.setDatabaseName(dbName);
    m_db.setUserName(user);
    m_db.setPassword(password);

    if (!m_db.open()) {
        qWarning() << "[GISEngine] 数据库连接失败："
                   << m_db.lastError().text();
        m_connected = false;
        return false;
    }

    qDebug() << "[GISEngine] 数据库连接成功：" << dbName;
    m_connected = true;
    return true;
}

bool GISEngine::isConnected() const {
    return m_connected && m_db.isOpen();
}

bool GISEngine::isSafeQuery(const QString &sql) const {
    QString upper = sql.trimmed().toUpper();
    if (!upper.startsWith("SELECT")) return false;
    // 禁止危险关键字
    for (auto &kw : {"DROP","DELETE","INSERT","UPDATE",
                     "ALTER","CREATE","TRUNCATE","EXEC","GRANT"}) {
        if (upper.contains(kw)) return false;
    }
    return true;
}



void GISEngine::executeAsync(const QString &sql, std::function<void(const QString&, const QString&)> callback)
{
    // 注意：QSqlDatabase 不能跨线程共享，子线程里要用独立连接
    QString host     = m_db.hostName();
    int     port     = m_db.port();
    QString dbName   = m_db.databaseName();
    QString user     = m_db.userName();
    QString password = m_db.password();

    QtConcurrent::run([=]() {
        // 子线程创建独立的临时连接
        QString tmpName = QString("tmp_%1").arg(
            (quintptr)QThread::currentThreadId());
        {
            auto tmpDb = QSqlDatabase::addDatabase("QPSQL", tmpName);
            tmpDb.setHostName(host);
            tmpDb.setPort(port);
            tmpDb.setDatabaseName(dbName);
            tmpDb.setUserName(user);
            tmpDb.setPassword(password);

            QString result, error;
            if (tmpDb.open()) {
                QSqlQuery q(tmpDb);
                q.setForwardOnly(true);
                if (q.exec(sql) && q.next())
                {
                    result = q.value(0).toString();
                }
                else
                {
                    error  = q.lastError().text();
                    result = QString(
                        R"({"type":"FeatureCollection","features":[],"error":"%1"})")
                             .arg(error.replace('"','\''));
                }
                tmpDb.close();
            }
            else
            {
                result = R"({"type":"FeatureCollection","features":[],"error":"子线程连接失败"})";
            }

            // 切回主线程执行 callback
            QString r = result;
            QString e = error;
            QMetaObject::invokeMethod(qApp, [callback, r, e](){
                callback(r, e);
            }, Qt::QueuedConnection);
        }
        // 必须先让 tmpDb 析构，再 removeDatabase
        QSqlDatabase::removeDatabase(tmpName);
    });
}

QJsonArray GISEngine::buildTools() const {
    return QJsonArray{
        QJsonObject{
            {"type","function"},
            {"function", QJsonObject{
                {"name","spatial_query"},
                {"description",
                 "执行 PostGIS 空间查询，返回 GeoJSON FeatureCollection。"
                 "坐标系必须是 EPSG:4326。"
                 "必须用 ST_Transform(way,4326) 转换 way 字段。"
                 "结果必须包装成标准 FeatureCollection 格式返回单行单列文本。"},
                {"parameters", QJsonObject{
                    {"type","object"},
                    {"properties", QJsonObject{
                        {"sql", QJsonObject{
                            {"type","string"},
                            {"description",
                             "完整的 PostGIS SELECT 语句，"
                             "返回单行单列的 GeoJSON FeatureCollection 文本"}
                        }},
                        {"description", QJsonObject{
                            {"type","string"},
                            {"description","用中文描述这次查询的含义，展示给用户"}
                        }}
                    }},
                    {"required", QJsonArray{"sql","description"}}
                }}
            }}
        }
    };
}
