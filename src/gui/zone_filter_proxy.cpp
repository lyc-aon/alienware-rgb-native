#include "zone_filter_proxy.h"
#include "zone_list_model.h"

ZoneFilterProxy::ZoneFilterProxy(QObject* parent) : QSortFilterProxyModel(parent) {}

void ZoneFilterProxy::setGroupFilter(const QString& group) {
    group_filter_ = group;
    invalidateFilter();
}

void ZoneFilterProxy::setSearchText(const QString& text) {
    search_text_ = text;
    invalidateFilter();
}

void ZoneFilterProxy::setActiveOnly(bool active_only) {
    active_only_ = active_only;
    invalidateFilter();
}

bool ZoneFilterProxy::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const {
    QModelIndex idx = sourceModel()->index(source_row, 0, source_parent);
    
    // Active-only filter
    if (active_only_ && !idx.data(ZoneListModel::ActiveRole).toBool()) {
        return false;
    }
    
    // Group filter. Special sentinel "__UNGROUPED__" matches zones with an
    // empty group string (for the accordion's "(Ungrouped)" bucket).
    const QString group = idx.data(ZoneListModel::GroupRole).toString();
    if (group_filter_ == QStringLiteral("__UNGROUPED__")) {
        if (!group.isEmpty()) return false;
    } else if (!group_filter_.isEmpty() && group != group_filter_) {
        return false;
    }
    
    // Search text filter
    if (!search_text_.isEmpty()) {
        int zone_id = idx.data(ZoneListModel::ZoneIdRole).toInt();
        QString label = idx.data(ZoneListModel::LabelRole).toString();
        
        QString hay = QString("%1 %2 %3")
            .arg(zone_id)
            .arg(label)
            .arg(group);
        
        if (!hay.contains(search_text_, Qt::CaseInsensitive)) {
            return false;
        }
    }
    
    return true;
}