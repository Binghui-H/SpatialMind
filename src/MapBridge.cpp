#include "MapBridge.h"

#include <QDebug>

MapBridge::MapBridge(QObject *parent)
{

}

void MapBridge::notifyMapReady()
{
    qDebug() << "[MapBridge] 地图就绪";
    emit mapReady();
}

void MapBridge::onFeatureClick(const QString &featureJson)
{
    qDebug() << "[MapBridge] 点击要素" << featureJson;
    // 后续处理...
}
