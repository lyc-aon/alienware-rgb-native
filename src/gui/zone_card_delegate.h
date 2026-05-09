#ifndef ZONE_CARD_DELEGATE_H
#define ZONE_CARD_DELEGATE_H

#include <QStyledItemDelegate>

class ZoneCardDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ZoneCardDelegate(QObject* parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, 
               const QModelIndex& index) const override;

private:
    static constexpr int CARD_WIDTH = 160;
    static constexpr int CARD_HEIGHT = 76;
};

#endif // ZONE_CARD_DELEGATE_H