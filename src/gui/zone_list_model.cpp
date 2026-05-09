#include "zone_list_model.h"
#include <QColor>

ZoneListModel::ZoneListModel(ZoneMap& zone_map, QObject* parent)
    : QAbstractListModel(parent), zone_map_(zone_map) {}

int ZoneListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return zone_map_.zoneCount();
}

QVariant ZoneListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= zone_map_.zoneCount()) {
        return QVariant();
    }

    const Zone* zone = zone_map_.getZone(index.row());
    if (!zone) return QVariant();

    switch (role) {
        case ZoneIdRole:
            return zone->zone_id;
        case LabelRole:
            return QString::fromStdString(zone->label).isEmpty() 
                   ? QString("Zone %1").arg(zone->zone_id) 
                   : QString::fromStdString(zone->label);
        case GroupRole:
            return QString::fromStdString(zone->group);
        case ColorRole:
            return QColor(zone->r, zone->g, zone->b);
        case ActiveRole:
            return zone->active;
        default:
            return QVariant();
    }
}

void ZoneListModel::refresh() {
    beginResetModel();
    endResetModel();
}

void ZoneListModel::updateZoneColor(int zone_id, uint8_t r, uint8_t g, uint8_t b) {
    if (zone_id < 0 || zone_id >= zone_map_.zoneCount()) return;
    
    Zone* zone = zone_map_.getZone(zone_id);
    if (zone) {
        zone->r = r;
        zone->g = g;
        zone->b = b;
        
        QModelIndex idx = index(zone_id, 0);
        emit dataChanged(idx, idx, {ColorRole});
    }
}