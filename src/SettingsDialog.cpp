#include "SettingsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QSettings>
#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlError>

const QString SettingsDialog::ORG = "SpatialMind";
const QString SettingsDialog::APP = "SpatialMind";

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("数据库连接");
    setMinimumWidth(420);
    setupUI();
    loadLastConfig();
    refreshHistoryList();
}

void SettingsDialog::setupUI()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(18, 18, 18, 18);

    // 历史连接
    auto *grpHist = new QGroupBox("历史连接（点击填入）");
    auto *vbHist  = new QVBoxLayout(grpHist);
    vbHist->setSpacing(4);

    m_historyList = new QListWidget;
    m_historyList->setFixedHeight(88);
    m_historyList->setAlternatingRowColors(true);
    vbHist->addWidget(m_historyList);

    m_btnClearHist = new QPushButton("清除历史");
    m_btnClearHist->setFixedWidth(80);
    auto *hbClr = new QHBoxLayout;
    hbClr->addStretch();
    hbClr->addWidget(m_btnClearHist);
    vbHist->addLayout(hbClr);

    root->addWidget(grpHist);

    // 连接参数
    auto *grpParam = new QGroupBox("连接参数");
    auto *form     = new QFormLayout(grpParam);
    form->setSpacing(8);

    m_host     = new QLineEdit; m_host->setPlaceholderText("localhost");
    m_port     = new QSpinBox;  m_port->setRange(1, 65535); m_port->setValue(5434);
    m_dbName   = new QLineEdit; m_dbName->setPlaceholderText("数据库名");
    m_user     = new QLineEdit; m_user->setPlaceholderText("postgres");
    m_password = new QLineEdit; m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText("密码");

    form->addRow("主机",   m_host);
    form->addRow("端口",   m_port);
    form->addRow("数据库", m_dbName);
    form->addRow("用户名", m_user);
    form->addRow("密码",   m_password);

    root->addWidget(grpParam);

    // 按钮行
    auto *btnRow = new QHBoxLayout;
    m_btnTest   = new QPushButton("测试连接");
    m_btnOk     = new QPushButton("连接");
    m_btnCancel = new QPushButton("取消");
    m_btnOk->setDefault(true);
    btnRow->addWidget(m_btnTest);
    btnRow->addStretch();
    btnRow->addWidget(m_btnCancel);
    btnRow->addWidget(m_btnOk);
    root->addLayout(btnRow);

    connect(m_historyList, &QListWidget::currentRowChanged,
            this, &SettingsDialog::onHistoryRowChanged);
    connect(m_btnClearHist, &QPushButton::clicked,
            this, &SettingsDialog::onClearHistory);
    connect(m_btnTest,   &QPushButton::clicked, this, &SettingsDialog::onTestConnect);
    connect(m_btnOk,     &QPushButton::clicked, this, &SettingsDialog::onAccept);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

// 历史列表
void SettingsDialog::refreshHistoryList()
{
    m_historyList->clear();
    QSettings s(ORG, APP);
    int n = s.beginReadArray("db/history");
    for (int i = 0; i < n; i++) {
        s.setArrayIndex(i);
        m_historyList->addItem(
            QString("%1@%2:%3/%4")
                .arg(s.value("user").toString())
                .arg(s.value("host").toString())
                .arg(s.value("port").toInt())
                .arg(s.value("name").toString()));
    }
    s.endArray();
}

void SettingsDialog::onHistoryRowChanged(int row)
{
    if (row < 0) return;
    QSettings s(ORG, APP);
    int n = s.beginReadArray("db/history");
    if (row < n) {
        s.setArrayIndex(row);
        m_host->setText(    s.value("host",     "localhost").toString());
        m_port->setValue(   s.value("port",     5434).toInt());
        m_dbName->setText(  s.value("name",     "").toString());
        m_user->setText(    s.value("user",     "postgres").toString());
        m_password->setText(s.value("password", "").toString());
    }
    s.endArray();
}

void SettingsDialog::onClearHistory()
{
    QSettings(ORG, APP).remove("db/history");
    refreshHistoryList();
}

// 写入历史（去重 + 最新置顶，最多 10 条）
void SettingsDialog::appendToHistory(const DbConfig &cfg)
{
    struct Row { QString host, name, user, password; int port; };
    QList<Row> rows;

    QSettings s(ORG, APP);
    int n = s.beginReadArray("db/history");
    for (int i = 0; i < n; i++) {
        s.setArrayIndex(i);
        rows.append({ s.value("host").toString(),
                      s.value("name").toString(),
                      s.value("user").toString(),
                      s.value("password").toString(),
                      s.value("port").toInt() });
    }
    s.endArray();

    // 去重
    for (int i = rows.size() - 1; i >= 0; i--) {
        const Row &r = rows[i];
        if (r.host == cfg.host && r.port == cfg.port
            && r.name == cfg.dbName && r.user == cfg.user)
            rows.removeAt(i);
    }

    // 最新插头部
    rows.prepend({ cfg.host, cfg.dbName, cfg.user, cfg.password, cfg.port });
    while (rows.size() > 10) rows.removeLast();

    s.beginWriteArray("db/history");
    for (int i = 0; i < rows.size(); i++) {
        s.setArrayIndex(i);
        s.setValue("host",     rows[i].host);
        s.setValue("port",     rows[i].port);
        s.setValue("name",     rows[i].name);
        s.setValue("user",     rows[i].user);
        s.setValue("password", rows[i].password);
    }
    s.endArray();
}

// 上次成功连接（启动时自动连接用）
void SettingsDialog::loadLastConfig()
{
    QSettings s(ORG, APP);
    m_host->setText(    s.value("db/last/host",     "localhost").toString());
    m_port->setValue(   s.value("db/last/port",     5434).toInt());
    m_dbName->setText(  s.value("db/last/name",     "").toString());
    m_user->setText(    s.value("db/last/user",     "postgres").toString());
    m_password->setText(s.value("db/last/password", "").toString());
}

void SettingsDialog::saveLastConfig(const DbConfig &cfg)
{
    QSettings s(ORG, APP);
    s.setValue("db/last/host",     cfg.host);
    s.setValue("db/last/port",     cfg.port);
    s.setValue("db/last/name",     cfg.dbName);
    s.setValue("db/last/user",     cfg.user);
    s.setValue("db/last/password", cfg.password);
}

// 静态读取（MainWindow 启动时用）
DbConfig SettingsDialog::loadDbConfig()
{
    QSettings s(ORG, APP);
    DbConfig cfg;
    cfg.host     = s.value("db/last/host",     "localhost").toString();
    cfg.port     = s.value("db/last/port",     5434).toInt();
    cfg.dbName   = s.value("db/last/name",     "").toString();
    cfg.user     = s.value("db/last/user",     "postgres").toString();
    cfg.password = s.value("db/last/password", "").toString();
    return cfg;
}

bool SettingsDialog::loadSpatialContext() {
    return QSettings(ORG, APP).value("feature/spatialContext", true).toBool();
}

bool SettingsDialog::loadUseRag() {
    return QSettings(ORG, APP).value("feature/rag", false).toBool();
}

// 测试连接（不保存）
void SettingsDialog::onTestConnect()
{
    static int idx = 0;
    const QString tmp = QString("sm_test_%1").arg(idx++);
    {
        auto db = QSqlDatabase::addDatabase("QPSQL", tmp);
        db.setHostName(m_host->text().trimmed());
        db.setPort(m_port->value());
        db.setDatabaseName(m_dbName->text().trimmed());
        db.setUserName(m_user->text().trimmed());
        db.setPassword(m_password->text());
        if (db.open()) {
            db.close();
            QMessageBox::information(this, "连接成功",
                QString("✓  %1:%2/%3")
                    .arg(m_host->text())
                    .arg(m_port->value())
                    .arg(m_dbName->text()));
        } else {
            QMessageBox::warning(this, "连接失败", db.lastError().text());
        }
    }
    QSqlDatabase::removeDatabase(tmp);
}

void SettingsDialog::onAccept()
{
    DbConfig cfg;
    cfg.host     = m_host->text().trimmed();
    cfg.port     = m_port->value();
    cfg.dbName   = m_dbName->text().trimmed();
    cfg.user     = m_user->text().trimmed();
    cfg.password = m_password->text();

    // 验证连接
    static int idx = 0;
    const QString tmp = QString("sm_conn_%1").arg(idx++);
    bool ok = false;
    {
        auto db = QSqlDatabase::addDatabase("QPSQL", tmp);
        db.setHostName(cfg.host);   db.setPort(cfg.port);
        db.setDatabaseName(cfg.dbName);
        db.setUserName(cfg.user);   db.setPassword(cfg.password);
        ok = db.open();
        if (ok) db.close();
        else QMessageBox::warning(this, "连接失败", db.lastError().text());
    }
    QSqlDatabase::removeDatabase(tmp);

    if (!ok) return;   // 连接失败不关闭对话框

    saveLastConfig(cfg);
    appendToHistory(cfg);
    emit dbConfigChanged(cfg);
    accept();
}
