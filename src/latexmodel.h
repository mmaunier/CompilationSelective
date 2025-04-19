#pragma once
#include <QAbstractItemModel>
#include "latexparser.h"

class LatexModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit LatexModel(QObject* parent = nullptr);
    Q_INVOKABLE void loadFromFile(const QString &filePath);
    // Q_INVOKABLE void printTree(LatexNode *node, int depth);
    QModelIndex index(int row, int col, const QModelIndex &parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex&) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QHash<int, QByteArray> roleNames() const override; // doit être public

    Q_INVOKABLE void selectAllChildren(const QModelIndex& index, bool checked);
    Q_INVOKABLE bool hasChildren(const QModelIndex &index) const;
    bool hasData() const { return m_root != nullptr; }

    // Pour sauvegarder/restaurer l'état des cases à cocher
    QJsonObject saveCheckState() const;
    void restoreCheckState(const QJsonObject& state);

private:
    std::shared_ptr<LatexNode> m_root;
    void propagateCheckStateToChildren(LatexNode* parent, Qt::CheckState state);
    void updateParentCheckState(LatexNode* parent);

    // Méthodes auxiliaires pour la sauvegarde/restauration récursives
    void saveCheckStateRecursive(const std::shared_ptr<LatexNode>& node, QJsonObject& state) const;
    void restoreCheckStateRecursive(std::shared_ptr<LatexNode>& node, const QJsonObject& state);

    // Méthode auxiliaire pour recalculer l'état des parents
    void recalculateParentStates(std::shared_ptr<LatexNode>& node);
};