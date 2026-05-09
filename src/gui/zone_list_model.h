#ifndef ZONE_LIST_MODEL_H
#define ZONE_LIST_MODEL_H

#include <QAbstractListModel>
#include <QColor>
#include "../models/zone_map.h"

class ZoneListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum ZoneRoles {
        ZoneIdRole = Qt::UserRole + 1,
        LabelRole,
        GroupRole,
        ColorRole,
        ActiveRole
    };

    explicit ZoneListModel(ZoneMap& zone_map, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    void refresh();
    void updateZoneColor(int zone_id, uint8_t r, uint8_t g, uint8_t b);
    
    ZoneMap& zoneMap() { return zone_map_; }

private:
    ZoneMap& zone_map_;
};

#endif // ZONE_LIST_MODEL_H