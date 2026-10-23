#pragma once

#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QKeySequence>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextImageFormat>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>
#include <QUrl>

#include <functional>

inline QWidget* createDocumentStudioPage(const QString& documentsDirectory,
                                         const QString& normalPrinterName,
                                         QWidget* parent,
                                         std::function<QString()> businessNameProvider = {},
                                         std::function<QString()> logoPathProvider = {},
                                         std::function<QString()> normalPrinterProvider = {}) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Documents"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);
    layout->addWidget(new QLabel(
        QStringLiteral("Create and edit company documents. Saved copies do not change business transactions."),
        page));

    auto* toolbar = new QHBoxLayout();
    auto addTool = [page, toolbar](const QString& label, const std::function<void()>& action) {
        auto* button = new QPushButton(label, page);
        QObject::connect(button, &QPushButton::clicked, page, action);
        toolbar->addWidget(button);
        return button;
    };

    auto* editor = new QTextEdit(page);
    editor->setAcceptRichText(true);
    editor->setPlaceholderText(QStringLiteral("Start typing a company document..."));
    editor->setMinimumHeight(300);
    const auto brandedHtml = [editor, businessNameProvider, logoPathProvider] {
        QTextDocument document;
        document.setHtml(editor->toHtml());
        QTextCursor cursor(&document);
        cursor.movePosition(QTextCursor::Start);
        const QString logoPath = logoPathProvider ? logoPathProvider() : QString();
        if (!logoPath.isEmpty() && QFileInfo(logoPath).isFile()) {
            QTextImageFormat image;
            image.setName(QUrl::fromLocalFile(logoPath).toString());
            image.setWidth(120);
            cursor.insertImage(image);
            cursor.insertBlock();
        }
        const QString businessName = businessNameProvider ? businessNameProvider() : QString();
        if (!businessName.trimmed().isEmpty()) {
            QTextCharFormat titleFormat;
            titleFormat.setFontWeight(QFont::Bold);
            titleFormat.setFontPointSize(14);
            cursor.insertText(businessName, titleFormat);
            cursor.insertBlock();
            cursor.insertBlock();
        }
        return document.toHtml();
    };

    auto* fontName = new QFontComboBox(page);
    fontName->setMaximumWidth(190);
    QObject::connect(fontName, &QFontComboBox::currentFontChanged, editor, [editor](const QFont& font) {
        QTextCharFormat format;
        format.setFontFamilies({font.family()});
        editor->mergeCurrentCharFormat(format);
    });
    toolbar->addWidget(fontName);

    auto* fontSize = new QComboBox(page);
    fontSize->addItems({QStringLiteral("8"), QStringLiteral("10"), QStringLiteral("11"),
                        QStringLiteral("12"), QStringLiteral("14"), QStringLiteral("16"),
                        QStringLiteral("18"), QStringLiteral("24"), QStringLiteral("32")});
    fontSize->setCurrentText(QStringLiteral("11"));
    QObject::connect(fontSize, &QComboBox::currentTextChanged, editor, [editor](const QString& value) {
        bool ok = false;
        const double size = value.toDouble(&ok);
        if (!ok || size < 6 || size > 72) return;
        QTextCharFormat format;
        format.setFontPointSize(size);
        editor->mergeCurrentCharFormat(format);
    });
    toolbar->addWidget(fontSize);

    auto* bold = addTool(QStringLiteral("Bold"), [editor] {
        QTextCharFormat format;
        format.setFontWeight(editor->fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
        editor->mergeCurrentCharFormat(format);
    });
    bold->setShortcut(QKeySequence::Bold);
    auto* italic = addTool(QStringLiteral("Italic"), [editor] {
        QTextCharFormat format;
        format.setFontItalic(!editor->fontItalic());
        editor->mergeCurrentCharFormat(format);
    });
    italic->setShortcut(QKeySequence::Italic);
    auto* underline = addTool(QStringLiteral("Underline"), [editor] {
        QTextCharFormat format;
        format.setFontUnderline(!editor->fontUnderline());
        editor->mergeCurrentCharFormat(format);
    });
    underline->setShortcut(QKeySequence::Underline);
    addTool(QStringLiteral("Text Color"), [editor, page] {
        const QColor color = QColorDialog::getColor(editor->textColor(), page, QStringLiteral("Text color"));
        if (!color.isValid()) return;
        QTextCharFormat format;
        format.setForeground(color);
        editor->mergeCurrentCharFormat(format);
    });
    addTool(QStringLiteral("Align Left"), [editor] { editor->setAlignment(Qt::AlignLeft); });
    addTool(QStringLiteral("Align Center"), [editor] { editor->setAlignment(Qt::AlignHCenter); });
    addTool(QStringLiteral("Align Right"), [editor] { editor->setAlignment(Qt::AlignRight); });
    addTool(QStringLiteral("Undo"), [editor] { editor->undo(); });
    addTool(QStringLiteral("Redo"), [editor] { editor->redo(); });
    addTool(QStringLiteral("Find / Replace"), [editor, page] {
        bool ok = false;
        const QString find = QInputDialog::getText(page, QStringLiteral("Find text"),
            QStringLiteral("Find:"), QLineEdit::Normal, QString(), &ok);
        if (!ok || find.isEmpty()) return;
        const QString replacement = QInputDialog::getText(page, QStringLiteral("Replace text"),
            QStringLiteral("Replace with (leave blank to remove):"), QLineEdit::Normal, QString(), &ok);
        if (!ok) return;
        QTextDocument* document = editor->document();
        QTextCursor cursor(document);
        cursor.beginEditBlock();
        while (!(cursor = document->find(find, cursor, QTextDocument::FindCaseSensitively)).isNull()) {
            cursor.insertText(replacement);
        }
        cursor.endEditBlock();
    });
    layout->addLayout(toolbar);
    layout->addWidget(editor, 1);

    auto* actions = new QHBoxLayout();
    auto* status = new QLabel(QStringLiteral("Unsaved document"), page);
    actions->addWidget(status, 1);
    auto* open = new QPushButton(QStringLiteral("Open Document"), page);
    auto* clear = new QPushButton(QStringLiteral("New Document"), page);
    auto* save = new QPushButton(QStringLiteral("Save Document"), page);
    auto* pdf = new QPushButton(QStringLiteral("Export PDF"), page);
    auto* print = new QPushButton(QStringLiteral("Print / Preview"), page);
    actions->addWidget(open);
    actions->addWidget(clear);
    actions->addWidget(save);
    actions->addWidget(pdf);
    actions->addWidget(print);
    layout->addLayout(actions);

    const QString directory = QDir(documentsDirectory).filePath(QStringLiteral("Documents"));
    if (!QDir().mkpath(directory)) {
        status->setText(QStringLiteral("Could not create the Documents folder."));
        open->setEnabled(false);
        clear->setEnabled(false);
        save->setEnabled(false);
        pdf->setEnabled(false);
        print->setEnabled(false);
        return page;
    }

    QObject::connect(open, &QPushButton::clicked, page, [page, editor, status, directory] {
        const QString path = QFileDialog::getOpenFileName(page, QStringLiteral("Open document"), directory,
            QStringLiteral("Company documents (*.html *.htm *.txt);;All files (*)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::critical(page, QStringLiteral("Could not open document"), file.errorString());
            return;
        }
        const QString contents = QString::fromUtf8(file.readAll());
        if (QFileInfo(path).suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0)
            editor->setPlainText(contents);
        else
            editor->setHtml(contents);
        page->setProperty("documentPath", path);
        status->setText(QStringLiteral("Opened: %1").arg(QDir::toNativeSeparators(path)));
    });
    QObject::connect(clear, &QPushButton::clicked, page, [page, editor, status] {
        editor->clear();
        page->setProperty("documentPath", QString());
        status->setText(QStringLiteral("Unsaved document"));
    });
    QObject::connect(save, &QPushButton::clicked, page,
        [page, editor, status, directory, brandedHtml] {
            QString path = page->property("documentPath").toString();
            if (path.isEmpty()) {
                path = QFileDialog::getSaveFileName(page, QStringLiteral("Save document"),
                    QDir(directory).filePath(QStringLiteral("company_document.html")),
                    QStringLiteral("HTML document (*.html);;Text document (*.txt)"));
            }
            if (path.isEmpty()) return;
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QMessageBox::critical(page, QStringLiteral("Could not save document"), file.errorString());
                return;
            }
            QTextStream output(&file);
            if (QFileInfo(path).suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0)
                output << editor->toPlainText();
            else
                output << brandedHtml();
            if (output.status() != QTextStream::Ok) {
                QMessageBox::critical(page, QStringLiteral("Could not save document"), file.errorString());
                return;
            }
            page->setProperty("documentPath", path);
            status->setText(QStringLiteral("Saved: %1").arg(QDir::toNativeSeparators(path)));
        });
    QObject::connect(pdf, &QPushButton::clicked, page, [page, editor, directory, brandedHtml] {
        const QString path = QFileDialog::getSaveFileName(page, QStringLiteral("Export document as PDF"),
            QDir(directory).filePath(QStringLiteral("company_document.pdf")),
            QStringLiteral("PDF document (*.pdf)"));
        if (path.isEmpty()) return;
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        QTextDocument document;
        document.setHtml(brandedHtml());
        document.print(&printer);
        if (!QFileInfo::exists(path) || QFileInfo(path).size() == 0) {
            QMessageBox::critical(page, QStringLiteral("PDF export failed"),
                                  QStringLiteral("The PDF could not be created."));
            return;
        }
        QMessageBox::information(page, QStringLiteral("PDF exported"),
                                 QStringLiteral("Saved document to %1").arg(path));
    });
    QObject::connect(print, &QPushButton::clicked, page,
        [page, editor, normalPrinterName, normalPrinterProvider, brandedHtml] {
        QPrinter printer(QPrinter::HighResolution);
        const QString selectedPrinter = normalPrinterProvider
            ? normalPrinterProvider() : normalPrinterName;
        if (!selectedPrinter.trimmed().isEmpty())
            printer.setPrinterName(selectedPrinter.trimmed());
        QPrintDialog dialog(&printer, page);
        dialog.setWindowTitle(QStringLiteral("Print document"));
        if (dialog.exec() != QDialog::Accepted) return;
        QTextDocument document;
        document.setHtml(brandedHtml());
        document.print(&printer);
    });
    return page;
}
