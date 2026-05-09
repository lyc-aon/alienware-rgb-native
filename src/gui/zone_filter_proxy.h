#ifndef ZONE_FILTER_PROXY_H
#define ZONE_FILTER_PROXY_H

#include <QSortFilterProxyModel>

class ZoneFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit ZoneFilterProxy(QObject* parent = nullptr);

    void setGroupFilter(const QString& group);
    void setSearchText(const QString& text);
    void setActiveOnly(bool active_only);

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

private:
    QString group_filter_;
    QString search_text_;
    bool active_only_ = false;
};

#endif // ZONE_FILTER_PROXY_H