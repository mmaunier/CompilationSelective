#include "latexmodel.h"
#include <QUrl>
#include <QJsonObject>

LatexModel::LatexModel(QObject* parent) : QAbstractItemModel(parent) {}

void LatexModel::loadFromFile(const QString& filePath)
{
    QString localPath = filePath;
    if (filePath.startsWith("file://"))
        localPath = QUrl(filePath).toLocalFile();

    qDebug() << "Chargement du fichier:" << localPath;

    beginResetModel();
    m_root = LatexParser::parse(localPath);

    // // AJOUTER CE DEBUG TEMPORAIRE
    // if (m_root) {
    //     qDebug() << "Structure de l'arbre:";
    //     printTree(m_root.get(), 0);
    // }

    endResetModel();

    if (m_root) {
        qDebug() << "Arbre chargé avec" << (m_root->children.isEmpty() ? "aucun" : QString::number(m_root->children.size())) << "enfants";
    } else {
        qDebug() << "Échec du chargement de l'arbre";
    }
}

// // Fonction helper à ajouter dans la classe
// void LatexModel::printTree(LatexNode* node, int depth) {
//     if (!node) return;
//     QString indent = QString(" ").repeated(depth * 2);
//     qDebug() << indent << node->name << "a" << node->children.size() << "enfants";
//     qDebug() << indent << "Parent:" << (node->parent ? node->parent->name : "NULL");
//     for (const auto& child : node->children) {
//         printTree(child.get(), depth + 1);
//     }
// }

QModelIndex LatexModel::index(int row, int column, const QModelIndex &parent) const
{
if (!m_root || row < 0) return QModelIndex();

    if (!m_root || row < 0) return QModelIndex();

    LatexNode* parentNode = parent.isValid()
        ? static_cast<LatexNode*>(parent.internalPointer())
        : m_root.get();

    if (row >= parentNode->children.size()) return QModelIndex();

        return createIndex(row, column, parentNode->children[row].get());
}

QModelIndex LatexModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || !m_root) return QModelIndex();

    LatexNode* node = static_cast<LatexNode*>(child.internalPointer());
    if (!node || !node->parent) {
                return QModelIndex();
    }

    LatexNode* parentNode = node->parent;
    if (parentNode == m_root.get()) {
        // qDebug() << "parent(): Nœud" << node->name << "a pour parent la racine";
        return QModelIndex();
    }

    LatexNode* grandParent = parentNode->parent;
    if (!grandParent) {
        // qDebug() << "parent(): Le parent" << parentNode->name << "n'a pas de grand-parent";
        return QModelIndex();
    }

    int row = 0;
    for (const auto& sibling : grandParent->children) {
        if (sibling.get() == parentNode) break;
        ++row;
    }
    
    // qDebug() << "parent(): Retourne index pour" << parentNode->name << "en tant que parent de" << node->name;
    return createIndex(row, 0, parentNode);
}

int LatexModel::rowCount(const QModelIndex &parent) const
{
    LatexNode* parentNode = parent.isValid()
        ? static_cast<LatexNode*>(parent.internalPointer())
        : m_root.get();
    
    return parentNode ? parentNode->children.size() : 0;
}

int LatexModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant LatexModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_root) return {};
    LatexNode* node = static_cast<LatexNode*>(index.internalPointer());
    switch (role) {
        case Qt::DisplayRole:
            return node->name;
        case Qt::CheckStateRole:
            return node->checkState;  // Retourne directement l'état
        case Qt::ToolTipRole:
            return node->path;
        case Qt::UserRole + 1:
            return !node->children.isEmpty();
        case Qt::UserRole + 2:
            return !node->children.isEmpty();
        default:
            break;
    }
    return {};
}

QHash<int, QByteArray> LatexModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "display";
    roles[Qt::CheckStateRole] = "checkState";
    roles[Qt::ToolTipRole] = "path";
    roles[Qt::UserRole + 1] = "hasChildren";
    roles[Qt::UserRole + 2] = "isTreeNode";
    return roles;
}

Qt::ItemFlags LatexModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
}

bool LatexModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || !m_root)
        return false;

    LatexNode* node = static_cast<LatexNode*>(index.internalPointer());
    
    if (role == Qt::CheckStateRole) {
        Qt::CheckState state = static_cast<Qt::CheckState>(value.toInt());
        
        // Si l'état est partiellement coché et l'utilisateur clique, 
        // on bascule vers coché
        if (node->checkState == Qt::PartiallyChecked && state != Qt::Unchecked) {
            state = Qt::Checked;
        }
            
        node->checkState = state;

        // Propager aux enfants
        propagateCheckStateToChildren(node, state);
        
        // Mettre à jour le parent
        updateParentCheckState(node->parent);
        
        // Notifier le changement
        emit dataChanged(index, index, {role});
        return true;
    }

    return false;
}

// Fonction auxiliaire pour propager l'état aux enfants
void LatexModel::propagateCheckStateToChildren(LatexNode* parent, Qt::CheckState state)
{
    if (!parent) return;
    
    for (int i = 0; i < parent->children.size(); ++i) {
        auto& child = parent->children[i];
        child->checkState = state;
        
        QModelIndex childIndex = createIndex(i, 0, child.get());
        emit dataChanged(childIndex, childIndex, {Qt::CheckStateRole});
        
        // Récursivement propager aux enfants
        propagateCheckStateToChildren(child.get(), state);
    }
}

// Fonction auxiliaire pour mettre à jour l'état du parent
void LatexModel::updateParentCheckState(LatexNode* parent)
{
    if (!parent) return;
    
    int totalChildren = parent->children.size();
    if (totalChildren == 0) return;
    
    int checkedCount = 0;
    int uncheckedCount = 0;
    int partiallyCheckedCount = 0;
    
    for (const auto& child : parent->children) {
        switch (child->checkState) {
            case Qt::Checked:
                checkedCount++;
                break;
            case Qt::Unchecked:
                uncheckedCount++;
                break;
            case Qt::PartiallyChecked:
                partiallyCheckedCount++;
                break;
        }
    }
    
    Qt::CheckState newState;
    if (checkedCount == totalChildren) {
        newState = Qt::Checked;
    } else if (uncheckedCount == totalChildren) {
        newState = Qt::Unchecked;
    } else {
        newState = Qt::PartiallyChecked;
    }
    
    // Si l'état change
    if (newState != parent->checkState) {
        parent->checkState = newState;
        
        // Trouver l'index du parent
        int row = 0;
        if (parent->parent) {
            for (const auto& sibling : parent->parent->children) {
                if (sibling.get() == parent) break;
                ++row;
            }
        }
        
        QModelIndex parentIndex = createIndex(row, 0, parent);
        emit dataChanged(parentIndex, parentIndex, {Qt::CheckStateRole});
        
        // Recursive update to grandparent
        updateParentCheckState(parent->parent);
    }
}

void LatexModel::selectAllChildren(const QModelIndex& index, bool checked)
{
    if (!index.isValid()) return;
    LatexNode* node = static_cast<LatexNode*>(index.internalPointer());
    std::function<void(LatexNode*)> setChecked = [&](LatexNode* n) {
        n->checkState = checked ? Qt::Checked : Qt::Unchecked;
        for (const auto& child : n->children)
            setChecked(child.get());
    };
    setChecked(node);
    emit dataChanged(index, index, {Qt::CheckStateRole});
}

bool LatexModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid()) return m_root && !m_root->children.isEmpty();
    
    LatexNode* node = static_cast<LatexNode*>(parent.internalPointer());
    bool result = node && !node->children.isEmpty();
    // qDebug() << "hasChildren pour" << (node ? node->name : "invalid") << "=" << result;
    return result;
}

// Implémentation des méthodes pour sauvegarder/restaurer l'état

QJsonObject LatexModel::saveCheckState() const
{
    QJsonObject state;
    if (m_root) {
        saveCheckStateRecursive(m_root, state);
    }
    return state;
}

void LatexModel::saveCheckStateRecursive(const std::shared_ptr<LatexNode>& node, QJsonObject& state) const
{
    if (!node) return;
    
    // Sauvegarder explicitement comme un entier pour préserver les trois états
    if (!node->path.isEmpty()) {
        state[node->path] = static_cast<int>(node->checkState);
    }
    
    for (const auto& child : node->children) {
        saveCheckStateRecursive(child, state);
    }
}

void LatexModel::restoreCheckState(const QJsonObject& state)
{
    if (m_root) {
        // D'abord restaurer l'état de tous les nœuds individuels
        restoreCheckStateRecursive(m_root, state);
        
        // Ensuite, recalculer l'état des parents pour garantir la cohérence
        recalculateParentStates(m_root);
        
        // Notifier la vue de la mise à jour
        emit dataChanged(QModelIndex(), QModelIndex(), {Qt::CheckStateRole});
    }
}

void LatexModel::restoreCheckStateRecursive(std::shared_ptr<LatexNode>& node, const QJsonObject& state)
{
    if (!node) return;
    
    if (!node->path.isEmpty() && state.contains(node->path)) {
        // Convertir explicitement en Qt::CheckState
        node->checkState = static_cast<Qt::CheckState>(state[node->path].toInt());
    }
    
    for (auto& child : node->children) {
        restoreCheckStateRecursive(child, state);
    }
}

void LatexModel::recalculateParentStates(std::shared_ptr<LatexNode>& node)
{
    if (!node || node->children.isEmpty()) return;
    
    // D'abord recalculer récursivement l'état des nœuds plus profonds
    for (auto& child : node->children) {
        recalculateParentStates(child);
    }
    
    // Ensuite recalculer l'état de ce nœud en fonction des enfants
    int totalChildren = node->children.size();
    int checkedCount = 0;
    int uncheckedCount = 0;
    
    for (const auto& child : node->children) {
        switch (child->checkState) {
            case Qt::Checked:
                checkedCount++;
                break;
            case Qt::Unchecked:
                uncheckedCount++;
                break;
            // Pas besoin de traiter Qt::PartiallyChecked - il compte comme "ni coché ni décoché"
        }
    }
    
    if (checkedCount == totalChildren) {
        node->checkState = Qt::Checked;
    } else if (uncheckedCount == totalChildren) {
        node->checkState = Qt::Unchecked;
    } else {
        node->checkState = Qt::PartiallyChecked;
    }
}