#include "MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QWebEngineSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QToolBar>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setupUI();
    setupConnections();
    resize(1280, 800);
    setWindowTitle("SpatialMind - AI空间分析助手");
    m_mapView->load(QUrl("qrc:/web/map.html"));
    refreshHistoryPanel(); // 启动时展示持久化的历史
}

void MainWindow::connectDb(const DbConfig &cfg)
{
    bool ok = m_gis->connectToDatabase(
        cfg.host, cfg.port, cfg.dbName, cfg.user, cfg.password);
    updateDbStatus(ok, cfg.dbName);
    if (!ok)
        QMessageBox::warning(this, "连接失败",
            "PostgreSQL 连接失败，请点击「数据库」按钮重新配置。");
}

void MainWindow::updateDbStatus(bool ok, const QString &dbName)
{
    if (!m_dbStatus) return;
    if (ok) {
        m_dbStatus->setText(QString("● 已连接  %1").arg(dbName));
        m_dbStatus->setStyleSheet("color:#27ae60;font-size:12px;padding:0 8px");
    } else {
        m_dbStatus->setText("● 未连接  请配置数据库");
        m_dbStatus->setStyleSheet("color:#e74c3c;font-size:12px;padding:0 8px");
    }
}

void MainWindow::onSettingsClicked()
{
    auto *dlg = new SettingsDialog(this);
    connect(dlg, &SettingsDialog::dbConfigChanged,
            this, &MainWindow::onDbConfigChanged);
    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::onDbConfigChanged(const DbConfig &cfg)
{
    connectDb(cfg);
}



// Slots
void MainWindow::onSendClicked()
{
    QString text = m_input->text().trimmed();
    if (text.isEmpty()) return;
    m_input->clear();
    appendChat("user", text);
    m_conv->submitUserInput(text);
}

void MainWindow::onMapReady()
{
    qDebug() << "[MainWindow] 地图就绪";
}

void MainWindow::onHistoryItemClicked(QListWidgetItem *item)
{
    int idx = item->data(Qt::UserRole).toInt();
    const auto &entries = m_queryHistory->entries();
    if (idx >= 0 && idx < entries.size()) {
        const auto &e = entries[idx];
        highlightFeatures(e.geojson, e.desc);
        appendChat("assistant", "重新显示：" + e.desc);
    }
}

void MainWindow::appendChat(const QString &role, const QString &text)
{
    QString color = (role == "user") ? "#534AB7" : "#085041";
    QString label = (role == "user") ? "你" : "AI";
    m_chatLog->append(
        QString("<b style='color:%1'>%2</b>: %3")
        .arg(color, label, text.toHtmlEscaped()));
}

void MainWindow::highlightFeatures(const QString &geojson, const QString &desc)
{
    if (geojson.isEmpty()) {
        appendChat("assistant", "未找到相关数据：" + desc);
        return;
    }
    QJsonObject msg;
    msg["type"]    = "highlight";
    msg["geojson"] = geojson;
    msg["desc"]    = desc;
    QString json = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    qDebug() << "[Map] push highlight, geojson length:" << geojson.length();
    emit m_bridge->pushToMap(json);
}

void MainWindow::setBusy(bool busy)
{
    m_sendBtn->setEnabled(!busy);
    m_input->setEnabled(!busy);
}

void MainWindow::refreshHistoryPanel()
{
    m_history->clear();
    const auto &entries = m_queryHistory->entries();
    for (int i = 0; i < entries.size(); i++) {
        const auto &e = entries[i];
        auto *item = new QListWidgetItem(
            e.time.toString("HH:mm") + "  " + e.query, m_history);
        item->setData(Qt::UserRole, i);
        item->setToolTip(e.desc);
    }
}

void MainWindow::setupUI()
{
    // 工具栏：只放数据库按钮
    auto *toolbar = addToolBar("main");
    toolbar->setMovable(false);
    m_settingsBtn = new QToolButton;
    m_settingsBtn->setText("⚙  数据库");
    m_settingsBtn->setToolTip("配置数据库连接");
    toolbar->addWidget(m_settingsBtn);

    // 状态栏
    m_dbStatus = new QLabel;
    statusBar()->addPermanentWidget(m_dbStatus);

    // 主分割布局
    auto *root = new QSplitter(Qt::Horizontal, this);

    m_history = new QListWidget(root);
    m_history->setMaximumWidth(200);
    m_history->setMinimumWidth(0);
    m_history->setToolTip("点击可重新显示历史查询结果");

    m_mapView = new QWebEngineView(root);
    auto *ws  = m_mapView->page()->settings();
    ws->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    ws->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls,   true);
    ws->setAttribute(QWebEngineSettings::JavascriptEnabled,               true);
    ws->setAttribute(QWebEngineSettings::LocalStorageEnabled,             true);

    // 右侧面板
    auto *rightPanel = new QWidget(root);
    auto *rl = new QVBoxLayout(rightPanel);
    rl->setContentsMargins(8, 8, 8, 8);
    rl->setSpacing(4);

    rl->addWidget(new QLabel("对话", rightPanel));

    m_chatLog = new QTextBrowser(rightPanel);
    m_chatLog->setOpenExternalLinks(false);
    rl->addWidget(m_chatLog, 1);

    // 功能开关：直接放在输入框上方
    auto *featureRow = new QHBoxLayout;
    featureRow->setSpacing(16);

    m_chkSpatialCtx = new QCheckBox("空间上下文", rightPanel);
    m_chkSpatialCtx->setToolTip(
        "启用后记忆上次查询的结果坐标\n"
        "可用「附近」「这里」「最近的」等自然语言指代位置");

    m_chkRag = new QCheckBox("RAG 示例", rightPanel);
    m_chkRag->setToolTip(
        "启用后将历史成功 SQL 作为示例注入上下文\n"
        "提升相似查询的准确率");

    featureRow->addWidget(m_chkSpatialCtx);
    featureRow->addWidget(m_chkRag);
    featureRow->addStretch();
    rl->addLayout(featureRow);

    // 输入行
    auto *inputRow = new QHBoxLayout;
    m_input   = new QLineEdit(rightPanel);
    m_input->setPlaceholderText("输入空间查询（如：找出距地铁 500 米的学校）");
    m_sendBtn = new QPushButton("发送", rightPanel);
    m_sendBtn->setFixedWidth(52);
    inputRow->addWidget(m_input);
    inputRow->addWidget(m_sendBtn);
    rl->addLayout(inputRow);

    root->addWidget(m_history);
    root->addWidget(m_mapView);
    root->addWidget(rightPanel);
    root->setSizes({160, 820, 300});
    setCentralWidget(root);

    m_bridge  = new MapBridge(this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject("mapBridge", m_bridge);
    m_mapView->page()->setWebChannel(m_channel);

    m_llm          = new LLMClient(this);
    m_gis          = new GISEngine(this);
    m_queryHistory = new QueryHistory(this);
    m_conv         = new ConversationManager(m_llm, m_gis, m_queryHistory, this);

    // 从持久化读取开关初始状态
    m_chkSpatialCtx->setChecked(SettingsDialog::loadSpatialContext());
    m_chkRag->setChecked(SettingsDialog::loadUseRag());
    m_conv->setUseSpatialContext(m_chkSpatialCtx->isChecked());
    m_conv->setUseRag(m_chkRag->isChecked());

    // 启动时自动连接上次成功的数据库
    DbConfig cfg = SettingsDialog::loadDbConfig();
    if (!cfg.dbName.isEmpty())
        connectDb(cfg);
    else
        updateDbStatus(false);
}

void MainWindow::setupConnections()
{
    connect(m_sendBtn, &QPushButton::clicked,
            this, &MainWindow::onSendClicked);
    connect(m_input, &QLineEdit::returnPressed,
            this, &MainWindow::onSendClicked);
    connect(m_bridge, &MapBridge::mapReady,
            this, &MainWindow::onMapReady);
    connect(m_history, &QListWidget::itemClicked,
            this, &MainWindow::onHistoryItemClicked);
    connect(m_conv, &ConversationManager::chatAppended,
            this,   &MainWindow::appendChat);
    connect(m_conv, &ConversationManager::featuresHighlighted,
            this,   &MainWindow::highlightFeatures);
    connect(m_conv, &ConversationManager::busyChanged,
            this,   &MainWindow::setBusy);
    connect(m_conv, &ConversationManager::historyUpdated,
            this,   &MainWindow::refreshHistoryPanel);
    connect(m_settingsBtn, &QToolButton::clicked,
            this, &MainWindow::onSettingsClicked);

    // CheckBox 勾选即时生效并持久化
    connect(m_chkSpatialCtx, &QCheckBox::toggled, this, [this](bool on) {
        m_conv->setUseSpatialContext(on);
        QSettings("SpatialMind", "SpatialMind").setValue("feature/spatialContext", on);
        appendChat("system", QString("空间上下文已%1").arg(on ? "启用" : "关闭"));
    });
    connect(m_chkRag, &QCheckBox::toggled, this, [this](bool on) {
        m_conv->setUseRag(on);
        QSettings("SpatialMind", "SpatialMind").setValue("feature/rag", on);
        appendChat("system", QString("RAG 已%1").arg(on ? "启用" : "关闭"));
    });
}
