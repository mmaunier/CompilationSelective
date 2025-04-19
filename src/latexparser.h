#pragma once
#include <QString>
#include <QVector>
#include <memory>
#include <Qt> // Added to include Qt::CheckState

struct LatexNode {
    QString name;
    QString path;
    QVector<std::shared_ptr<LatexNode>> children;
    LatexNode* parent = nullptr;
    Qt::CheckState checkState = Qt::Unchecked;  // Replaces bool isChecked
};

class LatexParser {
public:
    static std::shared_ptr<LatexNode> parse(const QString& filePath, const QString& baseDir = QString());

private:
    static void reorganizeByCategories(std::shared_ptr<LatexNode>& node);
};