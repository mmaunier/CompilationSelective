#include "latexparser.h"
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>
#include <QDebug>

std::shared_ptr<LatexNode> LatexParser::parse(const QString& filePath, const QString& baseDir)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Impossible d'ouvrir le fichier:" << filePath;
        return nullptr;
    }

    QString dir = baseDir.isEmpty() ? QFileInfo(filePath).absolutePath() : baseDir;
    auto root = std::make_shared<LatexNode>();
    root->name = QFileInfo(filePath).fileName().replace(".tex", "");
    root->path = QFileInfo(filePath).absoluteFilePath();

    QTextStream in(&file);
    QString content = in.readAll();
    
    // Regex améliorée pour capturer correctement \import{dir}{file}
    QRegularExpression importRe(R"(\\import\s*\{([^}]*)\}\s*\{([^}]*)\})");
    QRegularExpressionMatchIterator matches = importRe.globalMatch(content);

    qDebug() << "Analyse du fichier:" << filePath;
    
    while (matches.hasNext()) {
        QRegularExpressionMatch match = matches.next();
        QString relDir = match.captured(1);
        QString relFile = match.captured(2);
        
        // Ignorer les lignes commentées
        int pos = match.capturedStart();
        QString line = content.mid(qMax(0, content.lastIndexOf('\n', pos)), pos - content.lastIndexOf('\n', pos));
        if (line.trimmed().startsWith('%')) {
            continue;
        }
        
        QString importPath = QDir(dir).filePath(relDir + "/" + relFile + ".tex");
        qDebug() << "Import trouvé:" << importPath;
        
        auto child = parse(importPath, QDir(dir).filePath(relDir));
        if (child) {
            child->parent = root.get(); // VÉRIFIER CETTE LIGNE
            root->children.append(child);
            qDebug() << "Ajout de l'enfant:" << child->name;
        }
    }

    // Après avoir créé la structure, regrouper les nœuds par catégories
    if (root && !root->children.isEmpty()) {
        reorganizeByCategories(root);
    }
    
    return root;
}

void LatexParser::reorganizeByCategories(std::shared_ptr<LatexNode>& node)
{
    // 1. Ne pas réorganiser les nœuds qui sont déjà des catégories
    if (node->name == "PEDA" || node->name == "DOCS" || node->name == "EVALS") {
        return;
    }
    
    // Collections pour regrouper les nœuds
    std::vector<std::shared_ptr<LatexNode>> pedaNodes;
    std::vector<std::shared_ptr<LatexNode>> docNodes;
    std::vector<std::shared_ptr<LatexNode>> evalNodes;
    std::vector<std::shared_ptr<LatexNode>> otherNodes;
    
    // 2. Classifier les nœuds existants
    for (auto& child : node->children) {
        if (child->name.contains("_peda_")) {
            pedaNodes.push_back(child);
        } else if (child->name.contains("_doc_")) {
            docNodes.push_back(child);
        } else if (child->name.contains("_eval_")) {
            evalNodes.push_back(child);
        } else {
            otherNodes.push_back(child);
        }
    }
    
    // 3. Si nous avons des nœuds à regrouper, procéder à la réorganisation
    if (!pedaNodes.empty() || !docNodes.empty() || !evalNodes.empty()) {
        // Vider les enfants actuels
        node->children.clear();
        
        // D'abord ajouter les nœuds "autres"
        for (auto& other : otherNodes) {
            node->children.push_back(other);
        }
        
        // Créer les groupes selon les besoins
        if (!pedaNodes.empty()) {
            auto pedaGroup = std::make_shared<LatexNode>();
            pedaGroup->name = "PEDA";
            pedaGroup->path = node->path; 
            pedaGroup->parent = node.get();
            
            for (auto& peda : pedaNodes) {
                peda->parent = pedaGroup.get(); 
                pedaGroup->children.push_back(peda);
            }
            
            node->children.push_back(pedaGroup);
        }
        
        // [DOCS et EVALS similaires...]
        
        // Groupe DOCS
        if (!docNodes.empty()) {
            auto docGroup = std::make_shared<LatexNode>();
            docGroup->name = "DOCS";
            docGroup->path = node->path;
            docGroup->parent = node.get();
            
            for (auto& doc : docNodes) {
                doc->parent = docGroup.get();
                docGroup->children.push_back(doc);
            }
            
            node->children.push_back(docGroup);
        }
        
        // Groupe EVALS
        if (!evalNodes.empty()) {
            auto evalGroup = std::make_shared<LatexNode>();
            evalGroup->name = "EVALS";
            evalGroup->path = node->path;
            evalGroup->parent = node.get();
            
            for (auto& eval : evalNodes) {
                eval->parent = evalGroup.get();
                evalGroup->children.push_back(eval);
            }
            
            node->children.push_back(evalGroup);
        }
    }
    
    // 4. IMPORTANT: Appliquer la réorganisation aux autres nœuds APRÈS avoir fait le regroupement
    for (auto& child : node->children) {
        // Ne pas réorganiser les groupes qu'on vient de créer
        if (child->name != "PEDA" && child->name != "DOCS" && child->name != "EVALS") {
            reorganizeByCategories(child);
        }
    }
}