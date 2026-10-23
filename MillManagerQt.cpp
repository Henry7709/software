#include <QApplication>
#include <QAbstractItemView>
#include <QAbstractSocket>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDesktopServices>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QEventLoop>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QInputDialog>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMessageAuthenticationCode>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPageLayout>
#include <QPageSize>
#include <QPixmap>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPlainTextEdit>
#include <QPair>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleFactory>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleOption>
#include <QTableWidget>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>
#include <QWidget>

#include <cmath>
#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>

#include "DocumentStudio.h"
#include "KiyambiPage.h"

namespace {

constexpr quint16 kPhonePort = 8765;
constexpr int kMaxRequestBytes = 65536;
constexpr int kSwitchAccountExitCode = 42;

QString digestText(const QString& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}

double calculatePaye(double monthlyIncome) {
    const double income = std::max(0.0, monthlyIncome);
    if (income <= 235000) return 0;
    if (income <= 335000) return (income - 235000) * 0.10;
    if (income <= 410000) return 10000 + (income - 335000) * 0.20;
    double tax = 25000 + (income - 410000) * 0.30;
    if (income > 10000000) tax += (income - 10000000) * 0.10;
    return tax;
}

QString randomToken() {
    QByteArray bytes;
    for (int i = 0; i < 32; ++i)
        bytes.append(static_cast<char>(QRandomGenerator::system()->generate() & 0xff));
    return QString::fromLatin1(bytes.toHex());
}

struct PhoneSession {
    QString username;
    QString role;
    QString publicKey;
    qint64 id = 0;
    bool desktopChat = false;
};

struct ReportEntry {
    QString type;
    QString date;
    QString reference;
    QString party;
    QString product;
    QString grade;
    QString account;
    double kg = 0;
    double outputKg = 0;
    double price = 0;
    double total = 0;
    double paid = 0;
    qint64 id = 0;
};

struct ProgressMonth {
    QString month;
    double sales = 0;
    double milling = 0;
    double purchases = 0;
    double expenses = 0;
    double payroll = 0;
    double tax = 0;
    double cash = 0;
    double bank = 0;
    double boughtKg = 0;
    double soldKg = 0;
};

struct ManagerReportRow {
    qint64 id = 0;
    QString month;
    QString title;
    QString submittedBy;
    QString submittedAt;
    QString readAt;
};

class InputError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Product {
    bool grain;

    QString name() const {
        return grain ? QStringLiteral("Mill Manager F") : QStringLiteral("Mill Manager");
    }

    QString dataDirectory() const {
        return grain ? QStringLiteral("Mill_Manager_F_Data")
                     : QStringLiteral("Mateete_Coffee_Mill_Data");
    }

    QString databaseFile() const {
        return grain ? QStringLiteral("mill_manager_f.db")
                     : QStringLiteral("mateete_coffee_mill.db");
    }

    QString purchaseTable() const {
        return grain ? QStringLiteral("mill_purchases") : QStringLiteral("coffee_batches");
    }
    QString saleTable() const {
        return grain ? QStringLiteral("mill_sales") : QStringLiteral("sales");
    }

    QStringList purchaseProducts() const {
        return grain ? QStringList{QStringLiteral("Maize"), QStringLiteral("Cassava"), QStringLiteral("Maize Flour"),
                                   QStringLiteral("Cassava Flour"), QStringLiteral("Broken Feed"), QStringLiteral("Kyakyu")}
                     : QStringList{QStringLiteral("Kasse"), QStringLiteral("Kibooko")};
    }

    QStringList saleProducts() const {
        return grain
            ? QStringList{QStringLiteral("Maize"), QStringLiteral("Cassava"),
                          QStringLiteral("Maize Flour"), QStringLiteral("Cassava Flour"),
                          QStringLiteral("Broken Feed"), QStringLiteral("Kyakyu")}
            : QStringList{QStringLiteral("Kasse"), QStringLiteral("Kakuta"), QStringLiteral("Kibooko")};
    }
};

QString sqlError(const QSqlQuery& query) {
    return query.lastError().text();
}

bool databaseHasTable(QSqlDatabase& database, const QString& table) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?"));
    query.addBindValue(table);
    if (!query.exec())
        throw std::runtime_error(query.lastError().text().toStdString());
    return query.next();
}

QString dashboardMoneyText(double value) {
    if (!std::isfinite(value))
        throw std::runtime_error("Dashboard encountered a non-finite money value.");
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    return locale.toString(value, 'f', 0) + QStringLiteral(" UGX");
}

QString dashboardWeightText(double value) {
    if (!std::isfinite(value))
        throw std::runtime_error("Dashboard encountered a non-finite stock weight.");
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    return locale.toString(value, 'f', 2) + QStringLiteral(" kg");
}

QString groupedMoney(double value) {
    if (!std::isfinite(value))
        throw std::runtime_error("A table contains a non-finite money value.");
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(value, 'f', 0);
}

void configureBusinessTable(QTableWidget* table) {
    table->setMinimumHeight(180);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->setShowGrid(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(32);
    table->horizontalHeader()->setMinimumSectionSize(76);
    table->horizontalHeader()->setDefaultSectionSize(120);
    table->horizontalHeader()->setStretchLastSection(true);
}

struct AppTheme {
    bool dark = false;
    QColor primary{QStringLiteral("#286b43")};
    QColor background{QStringLiteral("#f3f5f7")};
    QString backgroundImage;
};

QString appStyleSheet(const AppTheme& theme) {
    const QColor surface = theme.dark ? QColor(QStringLiteral("#2b3239")) : QColor(QStringLiteral("#ffffff"));
    const QColor alternate = theme.dark ? QColor(QStringLiteral("#333b43")) : QColor(QStringLiteral("#f5f8f6"));
    const QColor text = theme.dark ? QColor(QStringLiteral("#e6e9ec")) : QColor(QStringLiteral("#1f2933"));
    const QColor muted = theme.dark ? QColor(QStringLiteral("#b7c0c7")) : QColor(QStringLiteral("#737b80"));
    const QColor border = theme.dark ? QColor(QStringLiteral("#49535d")) : QColor(QStringLiteral("#dce3df"));
    const QColor selected = theme.dark ? theme.primary.darker(145) : theme.primary.lighter(175);
    const QColor sidebar = theme.dark ? QColor(QStringLiteral("#171c21")) : theme.primary.darker(175);
    const QString image = theme.backgroundImage.isEmpty() ? QString() :
        QStringLiteral("background-image:url(\"%1\");background-position:center;background-repeat:no-repeat;")
            .arg(QUrl::fromLocalFile(theme.backgroundImage).toString(QUrl::FullyEncoded));
    return QStringLiteral(
        "QMainWindow,QWidget#appRoot { background-color:%1; color:%2; }"
        "QWidget#appRoot { %3 }"
        "QWidget#mainBody,QStackedWidget,QScrollArea#pageScrollArea { background:transparent; }"
        "QWidget#appHeader { background:%4; color:white; }"
        "QLabel#headerTitle { color:white; font-size:20px; font-weight:700; }"
        "QLabel#headerSubtitle,QLabel#headerIdentity,QLabel#headerClock { color:#eaf3ed; }"
        "QLabel#headerClock { font-weight:700; }"
        "QWidget#sidebar { background:%5; }"
        "QLabel#menuCaption { color:%6; font-size:10px; font-weight:700; letter-spacing:1px; }"
        "QPushButton#navButton { color:%7; background:transparent; border:0; text-align:left; "
        "padding:7px 14px; font-size:13px; font-weight:600; }"
        "QPushButton#navButton:hover,QPushButton#sideAction:hover { background:%8; }"
        "QPushButton#navButton[active=\"true\"] { color:white; background:%4; border-left:4px solid %9; padding-left:12px; }"
        "QPushButton#sideAction { color:%7; background:transparent; border:0; text-align:left; padding:6px 14px; }"
        "QPushButton#windowControl { color:white; background:rgba(255,255,255,32); border:0; "
        "border-radius:4px; padding:4px 9px; min-height:20px; font-weight:600; }"
        "QLabel#pageTitle,QLabel#dashboardTitle { color:%2; font-size:22px; font-weight:700; }"
        "QLabel#dashboardTitle { font-size:26px; }"
        "QLabel#dashboardWelcome,QLabel#dashboardCaption,QLabel#developerCredit { color:%10; }"
        "QLabel#dashboardNote { color:%10; font-size:11px; font-weight:600; }"
        "QLabel#dashboardNote[negative=\"true\"],QLabel#dashboardMetricValue[negative=\"true\"] { color:#d9534f; }"
        "QLabel#dashboardMetricValue { color:%4; font-size:17px; font-weight:700; }"
        "QFrame#dashboardCard { background:%11; border:1px solid %12; }"
        "QFrame#dashboardAccent { background:%4; border:0; }"
        "QLabel#dashboardCaption { font-size:10px; font-weight:700; }"
        "QGroupBox { background:%11; color:%2; border:1px solid %12; border-radius:7px; "
        "margin-top:12px; padding:12px; font-weight:600; }"
        "QGroupBox::title { subcontrol-origin:margin; left:10px; padding:1px 5px; color:%2; "
        "background:%11; }"
        "QLineEdit,QComboBox,QDateEdit,QPlainTextEdit,QTextEdit,QSpinBox,QDoubleSpinBox { "
        "background:%11; color:%2; border:1px solid %12; border-radius:4px; padding:4px 7px; "
        "min-height:26px; selection-background-color:%4; selection-color:white; }"
        "QComboBox::drop-down,QDateEdit::drop-down { width:28px; border:0; }"
        "QPushButton { background:%4; color:white; border:0; border-radius:4px; "
        "padding:3px 9px; min-height:0px; max-height:32px; }"
        "QPushButton:hover { background:%13; }"
        "QPushButton:pressed { background:%4; border:1px solid %9; }"
        "QPushButton:focus { border:1px solid %9; }"
        "QPushButton:disabled { background:%12; color:%10; }"
        "QPushButton#quickAction { background:%14; color:%2; border-radius:0; padding:3px 9px; "
        "min-height:0px; max-height:32px; font-weight:600; }"
        "QPushButton#quickAction:hover { background:%15; }"
        "QTableWidget { background:%11; alternate-background-color:%16; color:%2; "
        "border:1px solid %12; border-radius:5px; gridline-color:%12; "
        "selection-background-color:%17; selection-color:%2; }"
        "QHeaderView::section { background:%18; color:%2; border:0; border-bottom:2px solid %4; "
        "padding:8px 10px; font-weight:700; }"
        "QTableWidget::item { padding:5px 8px; border-bottom:1px solid %12; }"
        "QTableWidget::item:selected { background:%17; color:%2; }"
        "QStatusBar { background:%18; color:%10; }"
    ).arg(theme.background.name()).arg(text.name()).arg(image).arg(theme.primary.name())
     .arg(sidebar.name())
     .arg(theme.dark ? QStringLiteral("#bac9d3") : QStringLiteral("#bdd3c4"))
     .arg(QStringLiteral("#e6eee9"))
     .arg(theme.dark ? QStringLiteral("#343d45") : theme.primary.darker(150).name())
     .arg(theme.dark ? QStringLiteral("#b3e1c1") : QStringLiteral("#9dd4ad"))
     .arg(muted.name()).arg(surface.name()).arg(border.name())
     .arg(theme.primary.darker(115).name())
     .arg(theme.dark ? QStringLiteral("#3b444c") : QStringLiteral("#d6d6d6"))
     .arg(theme.dark ? QStringLiteral("#48535d") : QStringLiteral("#c5d4ca"))
     .arg(alternate.name()).arg(selected.name())
     .arg(theme.dark ? QStringLiteral("#343d45") : QStringLiteral("#e9efeb"));
}

class CompactButtonStyle final : public QProxyStyle {
public:
    explicit CompactButtonStyle(QStyle* baseStyle) : QProxyStyle(baseStyle) {}

    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size,
                           const QWidget* widget) const override {
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == QStyle::CT_PushButton)
            result.setHeight(std::min(result.height(), 32));
        return result;
    }
};

QString businessBrandHtml(const QString& businessName, const QString& logoPath) {
    QString result = QStringLiteral(
        "<div style='text-align:center;margin:0 0 12px 0;font-family:sans-serif'>");
    if (!logoPath.trimmed().isEmpty() && QFileInfo(logoPath).isFile() && !QPixmap(logoPath).isNull()) {
        result += QStringLiteral("<img src='%1' style='max-width:140px;max-height:70px'><br>")
            .arg(QUrl::fromLocalFile(logoPath).toString(QUrl::FullyEncoded).toHtmlEscaped());
    }
    result += QStringLiteral("<strong>%1</strong></div>")
        .arg(businessName.toHtmlEscaped());
    return result;
}

QString detectedPosPrinterName() {
    const QStringList markers{
        QStringLiteral("pos"), QStringLiteral("thermal"), QStringLiteral("receipt"),
        QStringLiteral("58mm"), QStringLiteral("80mm"), QStringLiteral("xprinter"),
        QStringLiteral("zjiang"), QStringLiteral("tm-t"), QStringLiteral("bixolon"),
        QStringLiteral("citizen ct"), QStringLiteral("sunmi")
    };
    for (const QPrinterInfo& printer : QPrinterInfo::availablePrinters()) {
        const QString name = (printer.printerName() + QLatin1Char(' ') +
                              printer.makeAndModel()).toLower();
        if (std::any_of(markers.cbegin(), markers.cend(),
                [&name](const QString& marker) { return name.contains(marker); }))
            return printer.printerName();
    }
    return {};
}

int detectedReceiptWidthMm(const QString& printerName) {
    if (printerName.isEmpty())
        return 80;
    const QPrinterInfo printer = QPrinterInfo::printerInfo(printerName);
    int supportedRollWidth = 0;
    for (const QPageSize& size : printer.supportedPageSizes()) {
        const QSizeF millimeters = size.size(QPageSize::Millimeter);
        const double width = std::min(millimeters.width(), millimeters.height());
        if (width >= 48 && width <= 100)
            supportedRollWidth = static_cast<int>(std::lround(width));
    }
    if (supportedRollWidth >= 48)
        return supportedRollWidth;
    const QString name = (printerName + QLatin1Char(' ') + printer.makeAndModel()).toLower();
    return name.contains(QStringLiteral("58")) ? 58 : 80;
}

void configureReceiptPage(QPrinter& printer, int widthMm) {
    const int width = widthMm == 58 ? 58 : 80;
    const QPageSize receiptPage(QSizeF(width, 300), QPageSize::Millimeter,
                                QStringLiteral("Mill receipt roll"), QPageSize::ExactMatch);
    printer.setPageSize(receiptPage);
    printer.setPageMargins(QMarginsF(2, 2, 2, 2), QPageLayout::Millimeter);
}

bool configureReceiptPrinter(QPrinter& printer, const QString& printerName, int widthMm) {
    printer.setPrinterName(printerName);
    if (!printer.isValid())
        return false;
    configureReceiptPage(printer, widthMm);
    return true;
}

void formatTableColumn(QTableWidget* table, int column, int decimals = 0) {
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* item = table->item(row, column);
        if (!item || item->text().trimmed().isEmpty())
            continue;
        QString numericText = item->text();
        numericText.remove(QLatin1Char(','));
        bool valid = false;
        const double value = numericText.toDouble(&valid);
        if (!valid || !std::isfinite(value))
            continue;
        item->setText(locale.toString(value, 'f', decimals));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}

void execSql(QSqlDatabase& db, const QString& sql) {
    QSqlQuery query(db);
    if (!query.exec(sql)) throw std::runtime_error(query.lastError().text().toStdString());
}

void execScript(QSqlDatabase& db, const QString& sql) {
    for (const QString& statement : sql.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        execSql(db, statement.trimmed());
}

QByteArray pbkdf2(const QByteArray& password, const QByteArray& salt, int iterations = 60000) {
    QByteArray block = salt + QByteArray::fromHex("00000001");
    QByteArray result = QMessageAuthenticationCode::hash(block, password, QCryptographicHash::Sha256);
    QByteArray value = result;
    for (int i = 1; i < iterations; ++i) {
        value = QMessageAuthenticationCode::hash(value, password, QCryptographicHash::Sha256);
        for (qsizetype j = 0; j < result.size(); ++j) result[j] = static_cast<char>(result[j] ^ value[j]);
    }
    return result;
}

QString hashPin(const QString& pin) {
    QByteArray salt;
    for (int i = 0; i < 16; ++i) salt.append(static_cast<char>(QRandomGenerator::system()->generate() & 0xff));
    const QByteArray saltText = salt.toHex();
    return QStringLiteral("pbkdf2$%1$%2")
        .arg(QString::fromLatin1(saltText), QString::fromLatin1(pbkdf2(pin.toUtf8(), saltText).toHex()));
}

bool constantTimeEqual(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size()) return false;
    unsigned char difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    return difference == 0;
}

bool verifyPin(const QString& pin, const QString& stored) {
    const QStringList parts = stored.split(QLatin1Char('$'));
    if (parts.size() == 3 && parts[0] == QStringLiteral("pbkdf2")) {
        const QByteArray saltText = parts[1].toLatin1();
        const QByteArray expected = QByteArray::fromHex(parts[2].toLatin1());
        return !expected.isEmpty() && constantTimeEqual(pbkdf2(pin.toUtf8(), saltText), expected);
    }
    // Older database users stored a single unsalted SHA-256 digest.
    const QByteArray legacy = QCryptographicHash::hash(pin.toUtf8(), QCryptographicHash::Sha256).toHex();
    return constantTimeEqual(legacy, stored.toLatin1());
}

class Store {
public:
    Store(const Product& product, const QString& path) : product_(product) {
        const QString connectionName = QStringLiteral("mill_manager_%1").arg(
            QRandomGenerator::system()->generate64());
        db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db_.setDatabaseName(path);
        if (!db_.open()) throw std::runtime_error(db_.lastError().text().toStdString());
        execSql(db_, QStringLiteral("PRAGMA foreign_keys=ON"));
        execSql(db_, QStringLiteral("PRAGMA busy_timeout=8000"));
        legacyGrainSharedSchema_ = product_.grain && databaseHasTable(db_, QStringLiteral("coffee_batches")) &&
                                   !databaseHasTable(db_, QStringLiteral("mill_purchases"));
        initialize();
    }

    ~Store() {
        const QString connectionName = db_.connectionName();
        db_.close();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }

    QSqlDatabase& db() { return db_; }
    bool grainPackage() const { return product_.grain; }

    QString roleFor(const QString& username) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("SELECT role FROM users WHERE username=? AND active=1"));
        query.addBindValue(username);
        if (!query.exec()) throw std::runtime_error(sqlError(query).toStdString());
        return query.next() ? query.value(0).toString() : QString();
    }

    QString pinHashFor(const QString& username) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("SELECT pin_hash FROM users WHERE username=? AND active=1"));
        query.addBindValue(username);
        if (!query.exec()) throw std::runtime_error(sqlError(query).toStdString());
        return query.next() ? query.value(0).toString() : QString();
    }

    void changePin(const QString& username, const QString& oldPin, const QString& newPin) {
        const QString stored = pinHashFor(username);
        if (stored.isEmpty() || !verifyPin(oldPin, stored))
            throw InputError("The current PIN is incorrect.");
        if (newPin.size() < 4) throw InputError("PIN must contain at least four characters.");
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("UPDATE users SET pin_hash=? WHERE username=? AND active=1"));
        query.addBindValue(hashPin(newPin));
        query.addBindValue(username);
        run(query);
        if (query.numRowsAffected() != 1) throw std::runtime_error("Could not update the PIN.");
    }

    QString setting(const QString& key, const QString& fallback = {}) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("SELECT value FROM app_settings WHERE key=?"));
        query.addBindValue(key);
        run(query);
        return query.next() ? query.value(0).toString() : fallback;
    }

    void setSetting(const QString& key, const QString& value) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("INSERT INTO app_settings(key,value) VALUES(?,?) "
                                     "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
        query.addBindValue(key);
        query.addBindValue(value);
        run(query);
    }

    QString managerReportSummary(const QString& month) {
        const QDate monthStart = QDate::fromString(month + QStringLiteral("-01"), Qt::ISODate);
        if (!monthStart.isValid() || monthStart.toString(QStringLiteral("yyyy-MM")) != month)
            throw InputError("Enter the report month in YYYY-MM format.");
        const QString start = monthStart.toString(Qt::ISODate);
        const QString end = monthStart.addMonths(1).addDays(-1).toString(Qt::ISODate);
        const auto sumFor = [this, &start, &end](const QString& table, const QString& column) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT COALESCE(SUM(%1),0) FROM %2 WHERE date BETWEEN ? AND ?")
                .arg(column, table));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            return query.next() ? query.value(0).toDouble() : 0.0;
        };
        const double purchases = sumFor(product_.purchaseTable(), QStringLiteral("total"));
        const double sales = sumFor(product_.saleTable(), QStringLiteral("total"));
        const double milling = sumFor(QStringLiteral("milling"), QStringLiteral("total"));
        const double expenses = sumFor(QStringLiteral("expenses"), QStringLiteral("amount"));
        const double payroll = sumFor(QStringLiteral("payroll"), QStringLiteral("paid"));
        const double tax = sumFor(QStringLiteral("tax_payments"), QStringLiteral("amount"));
        const QString stockTable = product_.grain ? QStringLiteral("grain_stock_movements")
                                                  : QStringLiteral("stock_movements");
        const QString stockProduct = product_.grain ? QStringLiteral("product")
                                                    : QStringLiteral("coffee_type");
        QSqlQuery stock(db_);
        if (!stock.exec(QStringLiteral("SELECT %1,COALESCE(SUM(kg),0) FROM %2 GROUP BY %1 ORDER BY %1")
                            .arg(stockProduct, stockTable)))
            throw std::runtime_error(sqlError(stock).toStdString());
        QStringList stockLines;
        while (stock.next())
            stockLines.push_back(QStringLiteral("%1: %2 kg").arg(stock.value(0).toString())
                .arg(stock.value(1).toDouble(), 0, 'f', 2));
        const QList<ReportEntry> credits = reportRows(QDate::currentDate(), QDate::currentDate(),
            QStringLiteral("Open Credits"), {});
        QStringList creditLines;
        for (const ReportEntry& credit : credits)
            creditLines.push_back(QStringLiteral("%1 | %2 | UGX %3")
                .arg(credit.reference, credit.party).arg(credit.total, 0, 'f', 0));
        return QStringLiteral(
            "MONTH SUMMARY (%1 to %2)\n"
            "Purchases: UGX %3\nSales: UGX %4\nCustomer milling: UGX %5\n"
            "Expenses: UGX %6\nPayroll paid: UGX %7\nTax paid: UGX %8\n"
            "Cash balance now: UGX %9\nBank balance now: UGX %10\n\n"
            "CURRENT STOCK\n%11\n\nOPEN CREDIT POSITIONS\n%12")
            .arg(start, end)
            .arg(purchases, 0, 'f', 0).arg(sales, 0, 'f', 0).arg(milling, 0, 'f', 0)
            .arg(expenses, 0, 'f', 0).arg(payroll, 0, 'f', 0).arg(tax, 0, 'f', 0)
            .arg(balance(QStringLiteral("Cash")), 0, 'f', 0).arg(balance(QStringLiteral("Bank")), 0, 'f', 0)
            .arg(stockLines.isEmpty() ? QStringLiteral("No stock recorded.") : stockLines.join(QLatin1Char('\n')),
                 creditLines.isEmpty() ? QStringLiteral("No open balances.") : creditLines.join(QLatin1Char('\n')));
    }

    qint64 submitManagerReport(const QString& username, const QString& month, const QString& title,
                               const QString& notes) {
        const QString role = roleFor(username);
        if (role != QStringLiteral("Manager") && role != QStringLiteral("Admin"))
            throw InputError("Only a Manager or Admin can submit a monthly report.");
        if (title.trimmed().isEmpty() || title.size() > 200 || notes.size() > 20000)
            throw InputError("Enter a report title and keep the written report under 20,000 characters.");
        const QString summary = managerReportSummary(month);
        const QString submittedAt = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        const QString reportText = QStringLiteral("%1\n%2\nMonth: %3\nTitle: %4\nSubmitted by: %5\nSubmitted at: %6\n\nMANAGER'S REPORT\n%7\n\n%8")
            .arg(product_.name(), QString(64, QLatin1Char('=')), month, title.trimmed(), username, submittedAt,
                 notes.trimmed().isEmpty() ? QStringLiteral("(no written notes)") : notes.trimmed(), summary);
        qint64 reportId = 0;
        transaction([&] {
            QSqlQuery insert(db_);
            insert.prepare(QStringLiteral("INSERT INTO manager_reports"
                "(month,submitted_by,submitted_at,report_text,notes,title) VALUES(?,?,?,?,?,?)"));
            insert.addBindValue(month);
            insert.addBindValue(username);
            insert.addBindValue(submittedAt);
            insert.addBindValue(reportText);
            insert.addBindValue(notes.trimmed());
            insert.addBindValue(title.trimmed());
            run(insert);
            reportId = insert.lastInsertId().toLongLong();
            audit(username, QStringLiteral("Manager Report"),
                  QStringLiteral("Submitted monthly report for %1").arg(month));
        });
        return reportId;
    }

    QList<ManagerReportRow> managerReports() {
        QSqlQuery query(db_);
        if (!query.exec(QStringLiteral("SELECT id,month,COALESCE(title,''),submitted_by,submitted_at,"
                                      "COALESCE(read_at,'') FROM manager_reports ORDER BY id DESC")))
            throw std::runtime_error(sqlError(query).toStdString());
        QList<ManagerReportRow> rows;
        while (query.next()) {
            ManagerReportRow row;
            row.id = query.value(0).toLongLong();
            row.month = query.value(1).toString();
            row.title = query.value(2).toString();
            row.submittedBy = query.value(3).toString();
            row.submittedAt = query.value(4).toString();
            row.readAt = query.value(5).toString();
            rows.push_back(row);
        }
        return rows;
    }

    QString openManagerReport(qint64 id, const QString& username) {
        if (roleFor(username) != QStringLiteral("Admin"))
            throw InputError("Only an Admin can read Manager monthly reports.");
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("SELECT report_text,COALESCE(read_at,'') FROM manager_reports WHERE id=?"));
        query.addBindValue(id);
        run(query);
        if (!query.next()) throw InputError("The selected Manager report no longer exists.");
        const QString text = query.value(0).toString();
        if (query.value(1).toString().isEmpty()) {
            QSqlQuery markRead(db_);
            markRead.prepare(QStringLiteral("UPDATE manager_reports SET read_at=?,read_by=? WHERE id=? AND read_at IS NULL"));
            markRead.addBindValue(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            markRead.addBindValue(username);
            markRead.addBindValue(id);
            run(markRead);
            audit(username, QStringLiteral("Manager Report"), QStringLiteral("Read report #%1").arg(id));
        }
        return text;
    }

    void deleteManagerReport(qint64 id, const QString& username) {
        if (roleFor(username) != QStringLiteral("Admin"))
            throw InputError("Only an Admin can delete Manager monthly reports.");
        transaction([&] {
            QSqlQuery remove(db_);
            remove.prepare(QStringLiteral("DELETE FROM manager_reports WHERE id=?"));
            remove.addBindValue(id);
            run(remove);
            if (remove.numRowsAffected() != 1) throw InputError("The selected Manager report no longer exists.");
            audit(username, QStringLiteral("Manager Report"), QStringLiteral("Deleted report #%1").arg(id));
        });
    }

    QStringList availableMillingProducts() {
        const QString raw = setting(QStringLiteral("MILL_ALLOWED_TYPES"),
                                    product_.grain ? product_.saleProducts().join(QLatin1Char(',')) : QStringLiteral("Kibooko"));
        QStringList result;
        for (const QString& value : raw.split(QLatin1Char(','), Qt::SkipEmptyParts))
            if (!value.trimmed().isEmpty()) result.push_back(value.trimmed());
        return result.isEmpty() ? (product_.grain ? product_.saleProducts() : QStringList{QStringLiteral("Kibooko")}) : result;
    }

    QStringList managerUsers() {
        QSqlQuery query(db_);
        if (!query.exec(QStringLiteral("SELECT username FROM users WHERE active=1 AND role='Manager' ORDER BY username")))
            throw std::runtime_error(sqlError(query).toStdString());
        QStringList users;
        while (query.next()) users.push_back(query.value(0).toString());
        return users;
    }

    QString createPhoneLinkCode(const QString& username, const QString& role, bool desktopClient = false) {
        if (role != QStringLiteral("Admin") && role != QStringLiteral("Manager"))
            throw InputError("Only Admin and Manager phone links are allowed.");
        if (roleFor(username) != role) throw InputError("The selected account does not have that role.");
        const QString code = QString::number(QRandomGenerator::system()->bounded(100000000))
                                 .rightJustified(8, QLatin1Char('0'));
        transaction([&] {
            QSqlQuery expire(db_);
            expire.prepare(QStringLiteral("UPDATE phone_link_codes SET used_at=datetime('now','localtime') "
                                          "WHERE username=? AND used_at IS NULL"));
            expire.addBindValue(username);
            run(expire);
            if (desktopClient) {
                QSqlQuery replaceDesktop(db_);
                replaceDesktop.prepare(QStringLiteral(
                    "UPDATE linked_phones SET status='REMOVED' "
                    "WHERE username=? AND is_desktop=1 AND status='ACTIVE'"));
                replaceDesktop.addBindValue(username);
                run(replaceDesktop);
            }
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("INSERT INTO phone_link_codes(code_hash,username,role,is_desktop,created_at,expires_at) "
                                         "VALUES(?,?,?,?,datetime('now','localtime'),datetime('now','localtime','+10 minutes'))"));
            query.addBindValue(digestText(code));
            query.addBindValue(username);
            query.addBindValue(role);
            query.addBindValue(desktopClient ? 1 : 0);
            run(query);
            audit(username, QStringLiteral("Phone link code"),
                  desktopClient ? role + QStringLiteral(" desktop chat") : role);
        });
        return code;
    }

    QString pairPhone(const QString& code, const QString& deviceName, const QString& publicKey) {
        static const QRegularExpression base64(QStringLiteral("^[A-Za-z0-9+/]+={0,2}$"));
        const QByteArray publicKeyBytes = QByteArray::fromBase64(publicKey.toLatin1());
        if (code.size() != 8 || deviceName.trimmed().isEmpty() ||
            !base64.match(publicKey).hasMatch() || publicKeyBytes.size() < 64 ||
            publicKeyBytes.size() > 512)
            throw InputError("Enter the eight-digit code, phone name, and a valid phone encryption key.");
        QString token;
        transaction([&] {
            QSqlQuery find(db_);
            find.prepare(QStringLiteral("SELECT id,username,role,is_desktop FROM phone_link_codes "
                "WHERE code_hash=? AND used_at IS NULL AND expires_at>datetime('now','localtime')"));
            find.addBindValue(digestText(code));
            run(find);
            if (!find.next()) throw InputError("The link code is invalid, expired, or already used.");
            const qint64 codeId = find.value(0).toLongLong();
            const QString username = find.value(1).toString();
            const QString role = find.value(2).toString();
            const bool desktopClient = find.value(3).toBool();
            find.finish();
            if (roleFor(username) != role) throw InputError("The account for this link is no longer active.");
            if (!desktopClient) {
                const int roleLimit = role == QStringLiteral("Admin") ? 2 : 1;
                QSqlQuery activeDevices(db_);
                activeDevices.prepare(QStringLiteral(
                    "SELECT COUNT(*) FROM linked_phones WHERE status='ACTIVE' AND role=? AND is_desktop=0"));
                activeDevices.addBindValue(role);
                run(activeDevices);
                if (!activeDevices.next() || activeDevices.value(0).toInt() >= roleLimit)
                    throw InputError(role == QStringLiteral("Admin")
                        ? "Both Admin phone slots are already in use. Remove an Admin phone before linking another."
                        : "The Manager phone slot is already in use. Remove the linked Manager phone before linking another.");
            }
            token = randomToken();
            QSqlQuery insert(db_);
            insert.prepare(QStringLiteral("INSERT INTO linked_phones(device_name,username,role,token_hash,public_key,is_desktop,linked_at,last_seen,status) "
                                          "VALUES(?,?,?,?,?,?,datetime('now','localtime'),datetime('now','localtime'),'ACTIVE')"));
            insert.addBindValue(deviceName.trimmed());
            insert.addBindValue(username);
            insert.addBindValue(role);
            insert.addBindValue(digestText(token));
            insert.addBindValue(publicKey);
            insert.addBindValue(desktopClient ? 1 : 0);
            run(insert);
            QSqlQuery consume(db_);
            consume.prepare(QStringLiteral("UPDATE phone_link_codes SET used_at=datetime('now','localtime') WHERE id=? AND used_at IS NULL"));
            consume.addBindValue(codeId);
            run(consume);
            if (consume.numRowsAffected() != 1) throw InputError("The link code has already been used.");
        });
        return token;
    }

    QJsonArray phoneContacts(const PhoneSession& session) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT p.id,p.device_name,p.username,p.role,p.public_key "
            "FROM linked_phones p JOIN users u ON u.username=p.username "
            "WHERE p.status='ACTIVE' AND u.active=1 AND p.id<>? ORDER BY p.role,p.username,p.id"));
        query.addBindValue(session.id);
        run(query);
        QJsonArray contacts;
        while (query.next())
            contacts.append(QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                                        {QStringLiteral("deviceName"), query.value(1).toString()},
                                        {QStringLiteral("username"), query.value(2).toString()},
                                        {QStringLiteral("role"), query.value(3).toString()},
                                        {QStringLiteral("publicKey"), query.value(4).toString()}});
        return contacts;
    }

    QJsonArray phoneMessages(const PhoneSession& session, qint64 afterId, qint64 contactId) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT id,sender_device_id,recipient_device_id,payload,sent_at FROM phone_messages_e2ee "
            "WHERE id>? AND ((sender_device_id=? AND recipient_device_id=?) "
            "OR (sender_device_id=? AND recipient_device_id=?)) ORDER BY id LIMIT 200"));
        query.addBindValue(std::max<qint64>(0, afterId));
        query.addBindValue(session.id);
        query.addBindValue(contactId);
        query.addBindValue(contactId);
        query.addBindValue(session.id);
        run(query);
        QJsonArray messages;
        while (query.next()) {
            const qint64 senderId = query.value(1).toLongLong();
            const qint64 recipientId = query.value(2).toLongLong();
            const QJsonObject payload = QJsonDocument::fromJson(query.value(3).toByteArray()).object();
            messages.append(QJsonObject{
                {QStringLiteral("id"), query.value(0).toLongLong()},
                {QStringLiteral("senderDeviceId"), senderId},
                {QStringLiteral("recipientDeviceId"), recipientId},
                {QStringLiteral("envelope"), payload.value(QString::number(session.id)).toObject()},
                {QStringLiteral("sentAt"), query.value(4).toString()}
            });
        }
        return messages;
    }

    void sendPhoneMessage(const PhoneSession& session, qint64 recipientId,
                          const QJsonObject& recipientEnvelope, const QJsonObject& senderEnvelope) {
        const auto validEnvelope = [](const QJsonObject& envelope) {
            const QString iv = envelope.value(QStringLiteral("iv")).toString();
            const QString ciphertext = envelope.value(QStringLiteral("ciphertext")).toString();
            static const QRegularExpression base64(QStringLiteral("^[A-Za-z0-9+/]+={0,2}$"));
            const QByteArray ivBytes = QByteArray::fromBase64(iv.toLatin1());
            const QByteArray ciphertextBytes = QByteArray::fromBase64(ciphertext.toLatin1());
            return base64.match(iv).hasMatch() && ivBytes.size() == 12 &&
                   base64.match(ciphertext).hasMatch() && ciphertextBytes.size() >= 16 &&
                   ciphertextBytes.size() <= 8192;
        };
        if (recipientId <= 0 || recipientId == session.id ||
            !validEnvelope(recipientEnvelope) || !validEnvelope(senderEnvelope))
            throw InputError("A valid encrypted message for both linked devices is required.");
        QSqlQuery linked(db_);
        linked.prepare(QStringLiteral(
            "SELECT 1 FROM linked_phones p JOIN users u ON u.username=p.username "
            "WHERE p.id=? AND p.status='ACTIVE' AND u.active=1 AND u.role=p.role "
            "AND p.public_key<>'' LIMIT 1"));
        linked.addBindValue(recipientId);
        run(linked);
        if (!linked.next())
            throw InputError("The selected linked device is no longer active or has no encryption key.");
        QJsonObject payload;
        payload.insert(QString::number(recipientId), recipientEnvelope);
        payload.insert(QString::number(session.id), senderEnvelope);
        QSqlQuery insert(db_);
        insert.prepare(QStringLiteral(
            "INSERT INTO phone_messages_e2ee(sender_device_id,recipient_device_id,payload,sent_at) "
            "VALUES(?,?,?,datetime('now','localtime'))"));
        insert.addBindValue(session.id);
        insert.addBindValue(recipientId);
        insert.addBindValue(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        run(insert);
    }

    QJsonObject phoneReports(const QDate& from, const QDate& to, const QString& type,
                             const QString& search) {
        static const QStringList types = {
            QStringLiteral("All"), QStringLiteral("Purchase"), QStringLiteral("Sale"),
            QStringLiteral("Milling"), QStringLiteral("Expense"), QStringLiteral("Money"),
            QStringLiteral("Payroll"), QStringLiteral("Tax Payment"),
            QStringLiteral("Credit Payment"), QStringLiteral("Bank Deposit"),
            QStringLiteral("Company Milling"), QStringLiteral("Customer Balances"),
            QStringLiteral("Supplier Balances"), QStringLiteral("Open Credits"),
            QStringLiteral("Stock Balance"), QStringLiteral("Daily Cash & Bank"),
            QStringLiteral("Monthly Cash Flow"), QStringLiteral("Profit Summary"),
            QStringLiteral("Activity Log")
        };
        if (!from.isValid() || !to.isValid() || from > to ||
            !types.contains(type) || search.size() > 100)
            throw InputError("Choose a valid report type, date range, and search phrase.");
        const QList<ReportEntry> rows = reportRows(from, to, type, search);
        QJsonArray records;
        const qsizetype count = std::min<qsizetype>(rows.size(), 500);
        for (qsizetype i = 0; i < count; ++i) {
            const ReportEntry& row = rows.at(i);
            records.append(QJsonObject{
                {QStringLiteral("type"), row.type}, {QStringLiteral("date"), row.date},
                {QStringLiteral("reference"), row.reference}, {QStringLiteral("party"), row.party},
                {QStringLiteral("product"), row.product}, {QStringLiteral("grade"), row.grade},
                {QStringLiteral("kg"), row.kg}, {QStringLiteral("outputKg"), row.outputKg},
                {QStringLiteral("unitPrice"), row.price}, {QStringLiteral("total"), row.total},
                {QStringLiteral("paid"), row.paid}, {QStringLiteral("account"), row.account}
            });
        }
        return QJsonObject{{QStringLiteral("records"), records},
                           {QStringLiteral("truncated"), rows.size() > count},
                           {QStringLiteral("total"), rows.size()}};
    }

    bool phoneSession(const QString& token, PhoneSession& session) {
        if (token.isEmpty()) return false;
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("SELECT id,username,role,public_key,is_desktop FROM linked_phones "
            "WHERE token_hash=? AND status='ACTIVE'"));
        query.addBindValue(digestText(token));
        run(query);
        if (!query.next()) return false;
        session.id = query.value(0).toLongLong();
        session.username = query.value(1).toString();
        session.role = query.value(2).toString();
        session.publicKey = query.value(3).toString();
        session.desktopChat = query.value(4).toBool();
        query.finish();
        if (roleFor(session.username) != session.role) {
            removePhone(session.id);
            return false;
        }
        QSqlQuery seen(db_);
        seen.prepare(QStringLiteral("UPDATE linked_phones SET last_seen=datetime('now','localtime') WHERE id=? AND status='ACTIVE'"));
        seen.addBindValue(session.id);
        run(seen);
        return true;
    }

    QList<QStringList> linkedPhones() {
        QSqlQuery query(db_);
        if (!query.exec(QStringLiteral("SELECT id,device_name,username,role,linked_at,last_seen,status "
                                       "FROM linked_phones ORDER BY id DESC")))
            throw std::runtime_error(sqlError(query).toStdString());
        QList<QStringList> rows;
        while (query.next()) {
            QStringList row;
            for (int i = 0; i < 7; ++i) row.push_back(query.value(i).toString());
            rows.push_back(row);
        }
        return rows;
    }

    void removePhone(qint64 id) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("UPDATE linked_phones SET status='REMOVED' WHERE id=? AND status='ACTIVE'"));
        query.addBindValue(id);
        run(query);
    }

    void createManager(const QString& username, const QString& pin) {
        if (username.trimmed().isEmpty() || pin.size() < 4)
            throw InputError("Enter a manager username and a PIN of at least four characters.");
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("INSERT INTO users(username,pin_hash,role,active) VALUES(?,?,'Manager',1)"));
        query.addBindValue(username.trimmed());
        query.addBindValue(hashPin(pin));
        if (!query.exec()) throw std::runtime_error(sqlError(query).toStdString());
    }

    QString savePurchase(const QString& username, const QString& supplier, const QString& product,
                         const QString& grade, double kg, double price, double paid,
                         const QString& account) {
        if (!std::isfinite(kg) || !std::isfinite(price) || !std::isfinite(paid) ||
            !std::isfinite(kg * price) || kg <= 0 || price < 0 || paid < 0 ||
            !availablePurchaseProducts().contains(product) ||
            (account != QStringLiteral("Cash") && account != QStringLiteral("Bank")))
            throw InputError("Enter a product, positive weight, and non-negative price and payment.");
        QString reference;
        transaction([&] {
            const QString personId = ensurePerson(QStringLiteral("suppliers"), QStringLiteral("supplier_code"),
                                                  QStringLiteral("SUP"), supplier);
            const QString table = product_.purchaseTable();
            QSqlQuery insert(db_);
            if (product_.grain) {
                insert.prepare(QStringLiteral("INSERT INTO %1(supplier_id,product,grade,kg,price_per_kg,total,paid,date,receipt_no,notes,payment_account) "
                                              "VALUES(?,?,?,?,?,?,?,?,?,?,?)").arg(table));
            } else {
                insert.prepare(QStringLiteral("INSERT INTO %1(supplier_id,coffee_type,grade,kg,price_per_kg,total,paid,date,receipt_no,notes,payment_account) "
                                              "VALUES(?,?,?,?,?,?,?,?,?,?,?)").arg(table));
            }
            insert.addBindValue(personId);
            insert.addBindValue(product);
            insert.addBindValue(grade);
            insert.addBindValue(kg);
            insert.addBindValue(price);
            insert.addBindValue(kg * price);
            insert.addBindValue(paid);
            insert.addBindValue(QDate::currentDate().toString(Qt::ISODate));
            insert.addBindValue(QString());
            insert.addBindValue(QString());
            insert.addBindValue(account);
            run(insert);
            const qint64 id = insert.lastInsertId().toLongLong();
            reference = QStringLiteral("PUR-%1").arg(id, 5, 10, QLatin1Char('0'));
            QSqlQuery receipt(db_);
            receipt.prepare(QStringLiteral("UPDATE %1 SET receipt_no=? WHERE id=?").arg(table));
            receipt.addBindValue(reference);
            receipt.addBindValue(id);
            run(receipt);
            addStock(product, grade, QStringLiteral("PURCHASE"), kg, reference,
                     product_.grain ? QStringLiteral("Grain purchased") : QStringLiteral("Coffee purchased"));
            if (paid > 0) addCash(QStringLiteral("OUT"), product_.grain ? QStringLiteral("Grain Purchase") : QStringLiteral("Coffee Purchase"), paid, reference, account);
            audit(username, QStringLiteral("Purchase"), supplier + QStringLiteral("; ") + product);
        });
        return reference;
    }

    QString saveSale(const QString& username, const QString& customer, const QString& product,
                     const QString& grade, double kg, double price, double received,
                     const QString& account) {
        if (!std::isfinite(kg) || !std::isfinite(price) || !std::isfinite(received) ||
            !std::isfinite(kg * price) || kg <= 0 || price < 0 || received < 0 ||
            !availableSaleProducts().contains(product) ||
            (account != QStringLiteral("Cash") && account != QStringLiteral("Bank")))
            throw InputError("Enter a product, positive weight, and non-negative price and payment.");
        QString invoice;
        transaction([&] {
            const QString personId = ensurePerson(QStringLiteral("customers"), QStringLiteral("buyer_code"),
                                                  QStringLiteral("CUS"), customer);
            const QString table = product_.saleTable();
            QSqlQuery insert(db_);
            if (product_.grain) {
                insert.prepare(QStringLiteral("INSERT INTO %1(customer_id,product,grade,kg,price_per_kg,total,received,date,invoice_no,received_account) "
                                              "VALUES(?,?,?,?,?,?,?,?,?,?)").arg(table));
            } else {
                insert.prepare(QStringLiteral("INSERT INTO %1(customer_id,coffee_type,grade,kg,price_per_kg,total,received,date,invoice_no,received_account) "
                                              "VALUES(?,?,?,?,?,?,?,?,?,?)").arg(table));
            }
            insert.addBindValue(personId);
            insert.addBindValue(product);
            insert.addBindValue(grade);
            insert.addBindValue(kg);
            insert.addBindValue(price);
            insert.addBindValue(kg * price);
            insert.addBindValue(received);
            insert.addBindValue(QDate::currentDate().toString(Qt::ISODate));
            insert.addBindValue(QString());
            insert.addBindValue(account);
            run(insert);
            const qint64 id = insert.lastInsertId().toLongLong();
            invoice = QStringLiteral("INV-%1").arg(id, 5, 10, QLatin1Char('0'));
            QSqlQuery update(db_);
            update.prepare(QStringLiteral("UPDATE %1 SET invoice_no=? WHERE id=?").arg(table));
            update.addBindValue(invoice);
            update.addBindValue(id);
            run(update);
            addStock(product, grade, QStringLiteral("SALE"), -kg, invoice,
                     product_.grain ? QStringLiteral("Grain/product sold") : QStringLiteral("Coffee sold"));
            if (received > 0) addCash(QStringLiteral("IN"), product_.grain ? QStringLiteral("Grain Sale") : QStringLiteral("Coffee Sale"), received, invoice, account);
            {
                QSqlQuery invoiceRow(db_);
                invoiceRow.prepare(QStringLiteral("INSERT INTO invoices(sale_id,customer_id,invoice_no,issue_date,amount,status) "
                                                  "VALUES(?,?,?,?,?,?)"));
                invoiceRow.addBindValue(id);
                invoiceRow.addBindValue(personId);
                invoiceRow.addBindValue(invoice);
                invoiceRow.addBindValue(QDate::currentDate().toString(Qt::ISODate));
                invoiceRow.addBindValue(kg * price);
                invoiceRow.addBindValue(received >= kg * price ? QStringLiteral("PAID") : QStringLiteral("CREDIT"));
                run(invoiceRow);
            }
            audit(username, QStringLiteral("Sale"), customer + QStringLiteral("; ") + product);
        });
        return invoice;
    }

    void saveMilling(const QString& username, const QString& inputProduct, double inputKg,
                     double flourKg, double feedKg, double kyakyuKg) {
        if (!product_.grain) throw std::runtime_error("Milling runs are available in Mill Manager F only.");
        if ((inputProduct != QStringLiteral("Maize") && inputProduct != QStringLiteral("Cassava")) ||
            !std::isfinite(inputKg) || !std::isfinite(flourKg) || !std::isfinite(feedKg) ||
            !std::isfinite(kyakyuKg) || inputKg <= 0 || flourKg < 0 || feedKg < 0 || kyakyuKg < 0 ||
            flourKg + feedKg + kyakyuKg > inputKg) {
            throw InputError("Enter a valid maize or cassava input; output weights cannot exceed input weight.");
        }
        transaction([&] {
            if (productStock(inputProduct) < inputKg)
                throw InputError("There is not enough of that grain in stock for this milling run.");
            QSqlQuery runQuery(db_);
            runQuery.prepare(QStringLiteral(
                "INSERT INTO milling_runs(input_product,input_kg,flour_kg,feed_kg,kyakyu_kg,loss_kg,date,operator) "
                "VALUES(?,?,?,?,?,?,?,?)"));
            runQuery.addBindValue(inputProduct);
            runQuery.addBindValue(inputKg);
            runQuery.addBindValue(flourKg);
            runQuery.addBindValue(feedKg);
            runQuery.addBindValue(kyakyuKg);
            runQuery.addBindValue(inputKg - flourKg - feedKg - kyakyuKg);
            runQuery.addBindValue(QDate::currentDate().toString(Qt::ISODate));
            runQuery.addBindValue(username);
            run(runQuery);
            const QString reference = QStringLiteral("CM-%1")
                .arg(runQuery.lastInsertId().toLongLong(), 5, 10, QLatin1Char('0'));
            const QString flour = inputProduct == QStringLiteral("Maize")
                ? QStringLiteral("Maize Flour") : QStringLiteral("Cassava Flour");
            addStock(inputProduct, QString(), QStringLiteral("INTERNAL_USE"), -inputKg,
                     reference, QStringLiteral("Input to milling"));
            if (flourKg > 0)
                addStock(flour, QString(), QStringLiteral("INTERNAL_PRODUCE"), flourKg,
                         reference, QStringLiteral("Milled flour"));
            if (feedKg > 0)
                addStock(QStringLiteral("Broken Feed"), QString(), QStringLiteral("INTERNAL_PRODUCE"),
                         feedKg, reference, QStringLiteral("Milling by-product"));
            if (kyakyuKg > 0)
                addStock(QStringLiteral("Kyakyu"), QString(), QStringLiteral("INTERNAL_PRODUCE"),
                         kyakyuKg, reference, QStringLiteral("Milling husks"));
            audit(username, QStringLiteral("Company Milling"), QString::number(inputKg, 'f', 2) + QStringLiteral(" kg ") +
                  inputProduct + QStringLiteral(" -> ") +
                  QString::number(flourKg + feedKg + kyakyuKg, 'f', 2) + QStringLiteral(" kg"));
        });
    }

    void saveCoffeeMilling(const QString& username, const QString& customer, const QString& coffee,
                           double inputKg, double outputKg, double charge, double received,
                           const QString& account) {
        if (!availableMillingProducts().contains(coffee) || customer.trimmed().isEmpty() ||
            !std::isfinite(inputKg) || !std::isfinite(outputKg) || !std::isfinite(charge) ||
            !std::isfinite(received) || !std::isfinite(inputKg * charge) ||
            inputKg <= 0 || outputKg < 0 || outputKg > inputKg || charge < 0 || received < 0 ||
            (account != QStringLiteral("Cash") && account != QStringLiteral("Bank")))
            throw InputError("Enter a valid customer milling job; output cannot exceed input.");
        transaction([&] {
            const QString customerId = ensurePerson(QStringLiteral("customers"), QStringLiteral("buyer_code"),
                                                     QStringLiteral("CUS"), customer);
            QSqlQuery insert(db_);
            insert.prepare(QStringLiteral("INSERT INTO milling(customer_id,coffee_type,grade,kg_in,kg_out,loss_kg,"
                "charge_per_kg,total,paid,date,receipt_no,notes,received_account) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
            insert.addBindValue(customerId);
            insert.addBindValue(coffee);
            insert.addBindValue(QStringLiteral("Unspecified"));
            insert.addBindValue(inputKg);
            insert.addBindValue(outputKg);
            insert.addBindValue(inputKg - outputKg);
            insert.addBindValue(charge);
            insert.addBindValue(inputKg * charge);
            insert.addBindValue(received);
            insert.addBindValue(QDate::currentDate().toString(Qt::ISODate));
            insert.addBindValue(QString());
            insert.addBindValue(QString());
            insert.addBindValue(account);
            run(insert);
            const qint64 id = insert.lastInsertId().toLongLong();
            const QString reference = QStringLiteral("MIL-%1").arg(id, 5, 10, QLatin1Char('0'));
            QSqlQuery receipt(db_);
            receipt.prepare(QStringLiteral("UPDATE milling SET receipt_no=? WHERE id=?"));
            receipt.addBindValue(reference);
            receipt.addBindValue(id);
            run(receipt);
            if (received > 0)
                addCash(QStringLiteral("IN"), QStringLiteral("Milling Income"), received, reference, account);
            audit(username, QStringLiteral("Milling"), customer + QStringLiteral("; ") + coffee);
        });
    }

    void saveMoney(const QString& username, const QString& account, const QString& kind,
                   const QString& category, double amount, const QDate& date,
                   const QString& reference, const QString& description) {
        if ((account != QStringLiteral("Cash") && account != QStringLiteral("Bank")) ||
            (kind != QStringLiteral("IN") && kind != QStringLiteral("OUT")) ||
            category.trimmed().isEmpty() || !date.isValid() || !std::isfinite(amount) || amount <= 0)
            throw InputError("Enter a valid account, direction, category, amount, and date.");
        transaction([&] {
            addCash(kind, category.trimmed(), amount, reference.trimmed(), account);
            QSqlQuery updateDate(db_);
            updateDate.prepare(QStringLiteral("UPDATE cash_transactions SET date=?,description=? "
                                               "WHERE id=last_insert_rowid()"));
            updateDate.addBindValue(date.toString(Qt::ISODate));
            updateDate.addBindValue(description.trimmed());
            run(updateDate);
            if (kind == QStringLiteral("OUT")) {
                QSqlQuery expense(db_);
                expense.prepare(QStringLiteral("INSERT INTO expenses(date,category,description,amount,paid_from,reference) "
                                                "VALUES(?,?,?,?,?,?)"));
                expense.addBindValue(date.toString(Qt::ISODate));
                expense.addBindValue(category.trimmed());
                expense.addBindValue(description.trimmed());
                expense.addBindValue(amount);
                expense.addBindValue(account);
                expense.addBindValue(reference.trimmed());
                run(expense);
            }
            audit(username, QStringLiteral("Money"), kind + QStringLiteral(" ") + account +
                  QStringLiteral(" ") + QString::number(amount, 'f', 2) + QStringLiteral(" - ") + category.trimmed());
        });
    }

    QStringList activeWorkers() {
        QSqlQuery query(db_);
        if (!query.exec(QStringLiteral("SELECT worker_code,name FROM workers WHERE active=1 ORDER BY name")))
            throw std::runtime_error(sqlError(query).toStdString());
        QStringList result;
        while (query.next())
            result.push_back(query.value(0).toString() + QStringLiteral(" - ") + query.value(1).toString());
        return result;
    }

    void createWorker(const QString& username, const QString& name, const QString& job,
                      const QString& payType, double rate) {
        if (name.trimmed().isEmpty() || name.size() > 200 ||
            (payType != QStringLiteral("Monthly Salary") && payType != QStringLiteral("Daily Wage") &&
             payType != QStringLiteral("Weekly Wage")) || !std::isfinite(rate) || rate < 0)
            throw InputError("Enter a worker name, valid pay type, and non-negative pay rate.");
        transaction([&] {
            QSqlQuery insert(db_);
            insert.prepare(QStringLiteral("INSERT INTO workers(name,job,pay_type,rate,active) VALUES(?,?,?, ?,1)"));
            insert.addBindValue(name.trimmed());
            insert.addBindValue(job.trimmed());
            insert.addBindValue(payType);
            insert.addBindValue(rate);
            run(insert);
            const qint64 id = insert.lastInsertId().toLongLong();
            QSqlQuery code(db_);
            code.prepare(QStringLiteral("UPDATE workers SET worker_code=? WHERE id=?"));
            code.addBindValue(QStringLiteral("WKR-%1").arg(id, 4, 10, QLatin1Char('0')));
            code.addBindValue(id);
            run(code);
            audit(username, QStringLiteral("Worker"), QStringLiteral("Created ") + name.trimmed());
        });
    }

    QList<QStringList> people(const QString& type) {
        QString sql;
        if (type == QStringLiteral("Customer")) {
            sql = QStringLiteral("SELECT id,COALESCE(buyer_code,''),name,COALESCE(phone,''),"
                "COALESCE(tin,''),COALESCE(address,''),COALESCE(notes,''),'','','','1' "
                "FROM customers ORDER BY name");
        } else if (type == QStringLiteral("Supplier")) {
            sql = QStringLiteral("SELECT id,COALESCE(supplier_code,''),name,COALESCE(phone,''),"
                "COALESCE(id_no,''),COALESCE(address,''),COALESCE(notes,''),'','','','1' "
                "FROM suppliers ORDER BY name");
        } else if (type == QStringLiteral("Worker")) {
            sql = QStringLiteral("SELECT id,COALESCE(worker_code,''),name,COALESCE(phone,''),"
                "COALESCE(id_no,''),COALESCE(address,''),COALESCE(notes,''),COALESCE(job,''),"
                "COALESCE(pay_type,''),CAST(COALESCE(rate,0) AS TEXT),CAST(COALESCE(active,1) AS TEXT) "
                "FROM workers ORDER BY name");
        } else {
            throw InputError("Select a valid register.");
        }
        QSqlQuery query(db_);
        if (!query.exec(sql)) throw std::runtime_error(sqlError(query).toStdString());
        QList<QStringList> result;
        while (query.next()) {
            QStringList row;
            for (int column = 0; column < 11; ++column) row.push_back(query.value(column).toString());
            result.push_back(row);
        }
        return result;
    }

    qint64 personId(const QString& type, const QString& entered) {
        if ((type != QStringLiteral("Customer") && type != QStringLiteral("Supplier")) ||
            entered.trimmed().isEmpty() || entered.size() > 200)
            throw InputError("Enter a valid customer or supplier.");
        const QString table = type == QStringLiteral("Customer")
            ? QStringLiteral("customers") : QStringLiteral("suppliers");
        const QString codeColumn = type == QStringLiteral("Customer")
            ? QStringLiteral("buyer_code") : QStringLiteral("supplier_code");
        QString code = entered.trimmed();
        const qsizetype delimiter = code.indexOf(QStringLiteral(" - "));
        if (delimiter >= 0) code.truncate(delimiter);
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("SELECT id FROM %1 WHERE upper(%2)=upper(?) OR lower(trim(name))=lower(trim(?)) LIMIT 1")
            .arg(table, codeColumn));
        query.addBindValue(code);
        query.addBindValue(entered.trimmed());
        run(query);
        if (!query.next()) throw InputError("That customer or supplier is not in the register.");
        return query.value(0).toLongLong();
    }

    double creditPosition(const QString& type, qint64 id) {
        if (id <= 0 || (type != QStringLiteral("Customer") && type != QStringLiteral("Supplier")))
            throw InputError("Select a registered customer or supplier.");
        QSqlQuery query(db_);
        if (type == QStringLiteral("Customer")) {
            QString sql = QStringLiteral("SELECT COALESCE((SELECT SUM(total-received) FROM %1 WHERE customer_id=?),0)")
                .arg(product_.saleTable());
            sql += QStringLiteral("+COALESCE((SELECT SUM(total-paid) FROM milling WHERE customer_id=?),0)");
            sql += QStringLiteral("-COALESCE((SELECT SUM(amount) FROM credit_payments WHERE party_type='Customer' AND party_id=?),0)");
            query.prepare(sql);
            query.addBindValue(id);
            query.addBindValue(id);
            query.addBindValue(id);
        } else {
            query.prepare(QStringLiteral("SELECT -COALESCE((SELECT SUM(total-paid) FROM %1 WHERE supplier_id=?),0)"
                "+COALESCE((SELECT SUM(amount) FROM credit_payments WHERE party_type='Supplier' AND party_id=?),0)")
                .arg(product_.purchaseTable()));
            query.addBindValue(id);
            query.addBindValue(id);
        }
        run(query);
        return query.next() ? query.value(0).toDouble() : 0.0;
    }

    QList<QStringList> linkedPersonPairs() {
        QSqlQuery query(db_);
        if (!query.exec(QStringLiteral("SELECT l.supplier_id,s.supplier_code,s.name,l.customer_id,"
                "c.buyer_code,c.name,l.match_reason FROM person_links l "
                "JOIN suppliers s ON s.id=l.supplier_id JOIN customers c ON c.id=l.customer_id "
                "ORDER BY s.name,c.name")))
            throw std::runtime_error(sqlError(query).toStdString());
        QList<QStringList> result;
        while (query.next()) {
            const qint64 supplierId = query.value(0).toLongLong();
            const qint64 customerId = query.value(3).toLongLong();
            result.push_back({QString::number(supplierId), query.value(1).toString(), query.value(2).toString(),
                QString::number(customerId), query.value(4).toString(), query.value(5).toString(),
                query.value(6).toString(), QString::number(creditPosition(QStringLiteral("Customer"), customerId), 'f', 2),
                QString::number(creditPosition(QStringLiteral("Supplier"), supplierId), 'f', 2)});
        }
        return result;
    }

    double availableCreditOffset(qint64 supplierId, qint64 customerId) {
        QSqlQuery link(db_);
        link.prepare(QStringLiteral("SELECT 1 FROM person_links WHERE supplier_id=? AND customer_id=?"));
        link.addBindValue(supplierId);
        link.addBindValue(customerId);
        run(link);
        if (!link.next()) throw InputError("Credit offsets require linked supplier and customer records.");
        const double receivable = creditPosition(QStringLiteral("Customer"), customerId);
        const double payable = -creditPosition(QStringLiteral("Supplier"), supplierId);
        return std::max(0.0, std::min(receivable, payable));
    }

    void saveCreditOffset(const QString& username, qint64 supplierId, qint64 customerId,
                          double amount, const QDate& date, const QString& reference,
                          const QString& notes) {
        if (!std::isfinite(amount) || amount <= 0 || !date.isValid())
            throw InputError("Enter a positive offset amount and a valid date.");
        transaction([&] {
            const double available = availableCreditOffset(supplierId, customerId);
            if (amount > available + 0.004)
                throw InputError("The offset cannot exceed the smaller of the customer's receivable and supplier payable.");
            QSqlQuery supplier(db_);
            supplier.prepare(QStringLiteral("SELECT supplier_code FROM suppliers WHERE id=?"));
            supplier.addBindValue(supplierId);
            run(supplier);
            if (!supplier.next()) throw InputError("The linked supplier record no longer exists.");
            const QString supplierCode = supplier.value(0).toString();
            QSqlQuery customer(db_);
            customer.prepare(QStringLiteral("SELECT buyer_code FROM customers WHERE id=?"));
            customer.addBindValue(customerId);
            run(customer);
            if (!customer.next()) throw InputError("The linked customer record no longer exists.");
            const QString customerCode = customer.value(0).toString();
            for (const auto& entry : {
                    qMakePair(QStringLiteral("Customer"), QStringLiteral("Offset with ") + supplierCode),
                    qMakePair(QStringLiteral("Supplier"), QStringLiteral("Offset with ") + customerCode)}) {
                QSqlQuery insert(db_);
                insert.prepare(QStringLiteral("INSERT INTO credit_payments"
                    "(party_type,party_id,purpose,amount,account,date,reference,notes) "
                    "VALUES(?,?,?,?,?,?,?,?)"));
                insert.addBindValue(entry.first);
                insert.addBindValue(entry.first == QStringLiteral("Customer") ? customerId : supplierId);
                insert.addBindValue(entry.second);
                insert.addBindValue(amount);
                insert.addBindValue(QStringLiteral("Offset"));
                insert.addBindValue(date.toString(Qt::ISODate));
                insert.addBindValue(reference.trimmed());
                insert.addBindValue(notes.trimmed());
                run(insert);
            }
            audit(username, QStringLiteral("Credit Offset"),
                  QStringLiteral("%1 / %2; UGX %3").arg(customerCode, supplierCode).arg(amount, 0, 'f', 2));
        });
    }

    void linkPersonRecords(const QString& username, qint64 supplierId, qint64 customerId,
                           const QString& reason = QStringLiteral("linked by Admin")) {
        if (roleFor(username) != QStringLiteral("Admin") && roleFor(username) != QStringLiteral("Manager"))
            throw InputError("Only an Admin or Manager can link customer and supplier records.");
        if (supplierId <= 0 || customerId <= 0)
            throw InputError("Choose both a supplier and customer record.");
        transaction([&] {
            for (const auto& item : {qMakePair(QStringLiteral("suppliers"), supplierId),
                                     qMakePair(QStringLiteral("customers"), customerId)}) {
                QSqlQuery exists(db_);
                exists.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE id=?").arg(item.first));
                exists.addBindValue(item.second);
                run(exists);
                if (!exists.next()) throw InputError("One of the selected people no longer exists.");
            }
            QSqlQuery removeBlock(db_);
            removeBlock.prepare(QStringLiteral("DELETE FROM person_link_blocks WHERE supplier_id=? AND customer_id=?"));
            removeBlock.addBindValue(supplierId);
            removeBlock.addBindValue(customerId);
            run(removeBlock);
            QSqlQuery link(db_);
            link.prepare(QStringLiteral("INSERT OR IGNORE INTO person_links"
                "(supplier_id,customer_id,match_reason,created_at) VALUES(?,?,?,?)"));
            link.addBindValue(supplierId);
            link.addBindValue(customerId);
            link.addBindValue(reason);
            link.addBindValue(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            run(link);
            audit(username, QStringLiteral("Person Link"),
                  QStringLiteral("Linked supplier #%1 to customer #%2").arg(supplierId).arg(customerId));
        });
    }

    int autoLinkPeople(const QString& username) {
        if (roleFor(username) != QStringLiteral("Admin") && roleFor(username) != QStringLiteral("Manager"))
            throw InputError("Only an Admin or Manager can match customer and supplier records.");
        const auto normalize = [](QString value, bool phone) {
            if (phone) {
                value.remove(QRegularExpression(QStringLiteral("\\D")));
                if (value.size() > 9) value = value.right(9);
            } else {
                value.remove(QRegularExpression(QStringLiteral("[^0-9A-Za-z]")));
                value = value.toUpper();
            }
            return value;
        };
        struct Person { qint64 id; QString name; QString phone; QString identity; };
        QList<Person> suppliers;
        QList<Person> customers;
        QSqlQuery query(db_);
        if (!query.exec(QStringLiteral("SELECT id,name,COALESCE(phone,''),COALESCE(id_no,'') FROM suppliers")))
            throw std::runtime_error(sqlError(query).toStdString());
        while (query.next()) suppliers.push_back({query.value(0).toLongLong(), query.value(1).toString(),
            query.value(2).toString(), query.value(3).toString()});
        if (!query.exec(QStringLiteral("SELECT id,name,COALESCE(phone,''),COALESCE(tin,'') FROM customers")))
            throw std::runtime_error(sqlError(query).toStdString());
        while (query.next()) customers.push_back({query.value(0).toLongLong(), query.value(1).toString(),
            query.value(2).toString(), query.value(3).toString()});
        int added = 0;
        transaction([&] {
            for (const Person& supplier : suppliers) {
                for (const Person& customer : customers) {
                    const QString supplierPhone = normalize(supplier.phone, true);
                    const QString customerPhone = normalize(customer.phone, true);
                    const QString supplierId = normalize(supplier.identity, false);
                    const QString customerId = normalize(customer.identity, false);
                    QString reason;
                    if (supplierId.size() >= 5 && supplierId == customerId) reason = QStringLiteral("same ID");
                    else if (supplierPhone.size() >= 7 && supplierPhone == customerPhone)
                        reason = QStringLiteral("same phone");
                    if (reason.isEmpty()) continue;
                    QSqlQuery blocked(db_);
                    blocked.prepare(QStringLiteral("SELECT 1 FROM person_link_blocks WHERE supplier_id=? AND customer_id=?"));
                    blocked.addBindValue(supplier.id);
                    blocked.addBindValue(customer.id);
                    run(blocked);
                    if (blocked.next()) continue;
                    QSqlQuery link(db_);
                    link.prepare(QStringLiteral("INSERT OR IGNORE INTO person_links"
                        "(supplier_id,customer_id,match_reason,created_at) VALUES(?,?,?,?)"));
                    link.addBindValue(supplier.id);
                    link.addBindValue(customer.id);
                    link.addBindValue(reason);
                    link.addBindValue(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
                    run(link);
                    added += link.numRowsAffected() > 0 ? 1 : 0;
                }
            }
            if (added > 0)
                audit(username, QStringLiteral("Person Link"), QStringLiteral("Auto-linked %1 matching pair(s)").arg(added));
        });
        return added;
    }

    void unlinkPersonRecords(const QString& username, const QString& type, qint64 id) {
        if (roleFor(username) != QStringLiteral("Admin") && roleFor(username) != QStringLiteral("Manager"))
            throw InputError("Only an Admin or Manager can unlink customer and supplier records.");
        const QString column = type == QStringLiteral("Supplier") ? QStringLiteral("supplier_id")
            : type == QStringLiteral("Customer") ? QStringLiteral("customer_id") : QString();
        if (column.isEmpty() || id <= 0) throw InputError("Select a customer or supplier to unlink.");
        transaction([&] {
            QSqlQuery links(db_);
            links.prepare(QStringLiteral("SELECT supplier_id,customer_id FROM person_links WHERE %1=?").arg(column));
            links.addBindValue(id);
            run(links);
            QList<QPair<qint64, qint64>> pairs;
            while (links.next()) pairs.push_back({links.value(0).toLongLong(), links.value(1).toLongLong()});
            for (const auto& pair : pairs) {
                QSqlQuery block(db_);
                block.prepare(QStringLiteral("INSERT OR IGNORE INTO person_link_blocks(supplier_id,customer_id) VALUES(?,?)"));
                block.addBindValue(pair.first);
                block.addBindValue(pair.second);
                run(block);
            }
            QSqlQuery remove(db_);
            remove.prepare(QStringLiteral("DELETE FROM person_links WHERE %1=?").arg(column));
            remove.addBindValue(id);
            run(remove);
            audit(username, QStringLiteral("Person Link"),
                  QStringLiteral("Unlinked %1 record #%2").arg(type).arg(id));
        });
    }

    void savePerson(const QString& username, const QString& type, qint64 id, const QString& name,
                    const QString& phone, const QString& identity, const QString& address,
                    const QString& notes, const QString& job, const QString& payType,
                    double rate, bool active) {
        if ((type != QStringLiteral("Customer") && type != QStringLiteral("Supplier") &&
             type != QStringLiteral("Worker")) || name.trimmed().isEmpty() || name.size() > 200 ||
            phone.size() > 100 || identity.size() > 100 || address.size() > 300 || notes.size() > 2000 ||
            job.size() > 200 || !std::isfinite(rate) || rate < 0 ||
            (type == QStringLiteral("Worker") && payType != QStringLiteral("Monthly Salary") &&
             payType != QStringLiteral("Daily Wage") && payType != QStringLiteral("Weekly Wage")))
            throw InputError("Enter a valid name and register details.");
        const bool creating = id <= 0;
        transaction([&] {
            const QString table = type == QStringLiteral("Customer") ? QStringLiteral("customers")
                : type == QStringLiteral("Supplier") ? QStringLiteral("suppliers") : QStringLiteral("workers");
            const QString identityColumn = type == QStringLiteral("Customer") ? QStringLiteral("tin")
                : QStringLiteral("id_no");
            const QString codeColumn = type == QStringLiteral("Customer") ? QStringLiteral("buyer_code")
                : type == QStringLiteral("Supplier") ? QStringLiteral("supplier_code") : QStringLiteral("worker_code");
            if (id > 0) {
                QSqlQuery update(db_);
                if (type == QStringLiteral("Worker")) {
                    update.prepare(QStringLiteral("UPDATE workers SET name=?,phone=?,id_no=?,address=?,notes=?,"
                        "job=?,pay_type=?,rate=?,active=? WHERE id=?"));
                    update.addBindValue(name.trimmed());
                    update.addBindValue(phone.trimmed());
                    update.addBindValue(identity.trimmed());
                    update.addBindValue(address.trimmed());
                    update.addBindValue(notes.trimmed());
                    update.addBindValue(job.trimmed());
                    update.addBindValue(payType);
                    update.addBindValue(rate);
                    update.addBindValue(active ? 1 : 0);
                    update.addBindValue(id);
                } else {
                    update.prepare(QStringLiteral("UPDATE %1 SET name=?,phone=?,%2=?,address=?,notes=? WHERE id=?")
                        .arg(table, identityColumn));
                    update.addBindValue(name.trimmed());
                    update.addBindValue(phone.trimmed());
                    update.addBindValue(identity.trimmed());
                    update.addBindValue(address.trimmed());
                    update.addBindValue(notes.trimmed());
                    update.addBindValue(id);
                }
                run(update);
                if (update.numRowsAffected() != 1) throw InputError("The selected register record no longer exists.");
            } else {
                QSqlQuery insert(db_);
                if (type == QStringLiteral("Worker")) {
                    insert.prepare(QStringLiteral("INSERT INTO workers(name,phone,id_no,address,notes,job,pay_type,rate,active) "
                        "VALUES(?,?,?,?,?,?,?,?,?)"));
                    insert.addBindValue(name.trimmed());
                    insert.addBindValue(phone.trimmed());
                    insert.addBindValue(identity.trimmed());
                    insert.addBindValue(address.trimmed());
                    insert.addBindValue(notes.trimmed());
                    insert.addBindValue(job.trimmed());
                    insert.addBindValue(payType);
                    insert.addBindValue(rate);
                    insert.addBindValue(active ? 1 : 0);
                } else {
                    insert.prepare(QStringLiteral("INSERT INTO %1(name,phone,%2,address,notes) VALUES(?,?,?,?,?)")
                        .arg(table, identityColumn));
                    insert.addBindValue(name.trimmed());
                    insert.addBindValue(phone.trimmed());
                    insert.addBindValue(identity.trimmed());
                    insert.addBindValue(address.trimmed());
                    insert.addBindValue(notes.trimmed());
                }
                run(insert);
                const qint64 newId = insert.lastInsertId().toLongLong();
                QSqlQuery code(db_);
                code.prepare(QStringLiteral("UPDATE %1 SET %2=? WHERE id=?").arg(table, codeColumn));
                code.addBindValue(QStringLiteral("%1-%2")
                    .arg(type == QStringLiteral("Customer") ? QStringLiteral("CUS")
                         : type == QStringLiteral("Supplier") ? QStringLiteral("SUP") : QStringLiteral("WKR"))
                    .arg(newId, 4, 10, QLatin1Char('0')));
                code.addBindValue(newId);
                run(code);
                id = newId;
            }
            audit(username, type + QStringLiteral(" register"),
                  QStringLiteral("%1 %2").arg(creating ? QStringLiteral("Created") : QStringLiteral("Updated"), name.trimmed()));
        });
    }

    void savePayroll(const QString& username, const QString& worker, const QString& period,
                     double days, double overtime, double gross, double paye, double deductions,
                     double net, double paid, const QDate& date, const QString& account,
                     const QString& notes) {
        const QString code = worker.section(QStringLiteral(" - "), 0, 0).trimmed();
        QSqlQuery find(db_);
        find.prepare(QStringLiteral("SELECT id,name,worker_code FROM workers WHERE active=1 AND "
                                    "(worker_code=? OR lower(trim(name))=lower(trim(?))) LIMIT 1"));
        find.addBindValue(code);
        find.addBindValue(worker);
        run(find);
        if (!find.next()) throw InputError("Select an active worker.");
        const qint64 workerId = find.value(0).toLongLong();
        const QString workerName = find.value(1).toString();
        const QString workerCode = find.value(2).toString();
        const QDate periodDate = QDate::fromString(period + QStringLiteral("-01"), Qt::ISODate);
        if (period.size() != 7 || !periodDate.isValid() ||
            periodDate.toString(QStringLiteral("yyyy-MM")) != period || !date.isValid() ||
            !std::isfinite(days) || !std::isfinite(overtime) ||
            !std::isfinite(gross) || !std::isfinite(paye) || !std::isfinite(deductions) ||
            !std::isfinite(net) || !std::isfinite(paid) || days < 0 || overtime < 0 ||
            gross <= 0 || paye < 0 || deductions < 0 || net < 0 || paid < 0 ||
            (account != QStringLiteral("Cash") && account != QStringLiteral("Bank")))
            throw InputError("Enter a valid payroll period, date, amounts, and payment account.");
        transaction([&] {
            QSqlQuery insert(db_);
            insert.prepare(QStringLiteral("INSERT INTO payroll(worker_id,period,days,overtime_hours,gross,paye,deductions,net,paid,date,notes,paid_from) "
                                          "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
            insert.addBindValue(workerId);
            insert.addBindValue(period);
            insert.addBindValue(days);
            insert.addBindValue(overtime);
            insert.addBindValue(gross);
            insert.addBindValue(paye);
            insert.addBindValue(deductions);
            insert.addBindValue(net);
            insert.addBindValue(paid);
            insert.addBindValue(date.toString(Qt::ISODate));
            insert.addBindValue(notes.trimmed());
            insert.addBindValue(account);
            run(insert);
            if (paid > 0)
                addCash(QStringLiteral("OUT"), QStringLiteral("Worker Payment"), paid, workerCode, account);
            QSqlQuery cashDate(db_);
            cashDate.prepare(QStringLiteral("UPDATE cash_transactions SET date=?,description=? "
                                            "WHERE id=last_insert_rowid() AND category='Worker Payment'"));
            cashDate.addBindValue(date.toString(Qt::ISODate));
            cashDate.addBindValue(QStringLiteral("Payroll ") + period + QStringLiteral(" - ") + workerName);
            run(cashDate);
            audit(username, QStringLiteral("Payroll"), workerCode + QStringLiteral(" - ") + workerName);
        });
    }

    void saveTaxPayment(const QString& username, const QString& taxType, const QString& period,
                       double amount, const QDate& date, const QString& reference, const QString& notes,
                       const QString& account) {
        if (taxType.trimmed().isEmpty() || period.trimmed().isEmpty() || !std::isfinite(amount) ||
            amount <= 0 || !date.isValid() ||
            (account != QStringLiteral("Cash") && account != QStringLiteral("Bank")))
            throw InputError("Enter a tax type, period, positive amount, and valid payment date.");
        transaction([&] {
            QSqlQuery insert(db_);
            insert.prepare(QStringLiteral("INSERT INTO tax_payments(tax_type,period,amount,date,reference,notes) "
                                          "VALUES(?,?,?,?,?,?)"));
            insert.addBindValue(taxType.trimmed());
            insert.addBindValue(period.trimmed());
            insert.addBindValue(amount);
            insert.addBindValue(date.toString(Qt::ISODate));
            insert.addBindValue(reference.trimmed());
            insert.addBindValue(notes.trimmed());
            run(insert);
            addCash(QStringLiteral("OUT"), QStringLiteral("Tax payment"), amount, reference.trimmed(), account);
            QSqlQuery cashDate(db_);
            cashDate.prepare(QStringLiteral("UPDATE cash_transactions SET date=? WHERE id=last_insert_rowid()"));
            cashDate.addBindValue(date.toString(Qt::ISODate));
            run(cashDate);
            audit(username, QStringLiteral("Tax payment"), taxType.trimmed() + QStringLiteral("; ") + period.trimmed());
        });
    }

    void saveCreditPayment(const QString& username, const QString& partyType, qint64 partyId,
                           double amount, const QString& account, const QDate& date,
                           const QString& reference, const QString& notes) {
        if ((partyType != QStringLiteral("Customer") && partyType != QStringLiteral("Supplier")) ||
            partyId <= 0 || !std::isfinite(amount) || amount <= 0 || !date.isValid() ||
            (account != QStringLiteral("Cash") && account != QStringLiteral("Bank")))
            throw InputError("Enter a valid customer or supplier, positive payment, date, and account.");
        const QString table = partyType == QStringLiteral("Customer")
            ? QStringLiteral("customers") : QStringLiteral("suppliers");
        const QString codeColumn = partyType == QStringLiteral("Customer")
            ? QStringLiteral("buyer_code") : QStringLiteral("supplier_code");
        QSqlQuery person(db_);
        person.prepare(QStringLiteral("SELECT name,%1 FROM %2 WHERE id=?").arg(codeColumn, table));
        person.addBindValue(partyId);
        run(person);
        if (!person.next()) throw InputError("The selected person no longer exists.");
        const QString name = person.value(0).toString();
        const QString code = person.value(1).toString();
        double position = 0;
        if (partyType == QStringLiteral("Customer")) {
            QSqlQuery sales(db_);
            sales.prepare(QStringLiteral("SELECT COALESCE(SUM(total-received),0) FROM %1 WHERE customer_id=?")
                              .arg(product_.saleTable()));
            sales.addBindValue(partyId);
            run(sales);
            position = sales.next() ? sales.value(0).toDouble() : 0;
            {
                QSqlQuery milling(db_);
                milling.prepare(QStringLiteral("SELECT COALESCE(SUM(total-paid),0) FROM milling WHERE customer_id=?"));
                milling.addBindValue(partyId);
                run(milling);
                if (milling.next()) position += milling.value(0).toDouble();
            }
            QSqlQuery payments(db_);
            payments.prepare(QStringLiteral("SELECT COALESCE(SUM(amount),0) FROM credit_payments "
                                            "WHERE party_type='Customer' AND party_id=?"));
            payments.addBindValue(partyId);
            run(payments);
            if (payments.next()) position -= payments.value(0).toDouble();
        } else {
            QSqlQuery purchases(db_);
            purchases.prepare(QStringLiteral("SELECT COALESCE(SUM(total-paid),0) FROM %1 WHERE supplier_id=?")
                                  .arg(product_.purchaseTable()));
            purchases.addBindValue(partyId);
            run(purchases);
            position = purchases.next() ? -purchases.value(0).toDouble() : 0;
            QSqlQuery payments(db_);
            payments.prepare(QStringLiteral("SELECT COALESCE(SUM(amount),0) FROM credit_payments "
                                            "WHERE party_type='Supplier' AND party_id=?"));
            payments.addBindValue(partyId);
            run(payments);
            if (payments.next()) position += payments.value(0).toDouble();
        }
        const QString cashKind = partyType == QStringLiteral("Customer")
            ? (position >= 0 ? QStringLiteral("IN") : QStringLiteral("OUT"))
            : (position <= 0 ? QStringLiteral("OUT") : QStringLiteral("IN"));
        const QString category = partyType == QStringLiteral("Customer")
            ? (cashKind == QStringLiteral("IN") ? QStringLiteral("Credit payment from customer")
                                                 : QStringLiteral("Refund to customer"))
            : (cashKind == QStringLiteral("OUT") ? QStringLiteral("Credit payment to supplier")
                                                  : QStringLiteral("Advance returned by supplier"));
        transaction([&] {
            double remaining = amount;
            if (partyType == QStringLiteral("Customer") && position >= 0) {
                const QStringList purposes{QStringLiteral("Sales"), QStringLiteral("Milling")};
                for (const QString& purpose : purposes) {
                    QSqlQuery outstanding(db_);
                    if (purpose == QStringLiteral("Sales")) {
                        outstanding.prepare(QStringLiteral("SELECT COALESCE(SUM(total-received),0) FROM %1 "
                                                           "WHERE customer_id=?").arg(product_.saleTable()));
                    } else {
                        outstanding.prepare(QStringLiteral("SELECT COALESCE(SUM(total-paid),0) FROM milling "
                                                           "WHERE customer_id=?"));
                    }
                    outstanding.addBindValue(partyId);
                    run(outstanding);
                    double due = outstanding.next() ? outstanding.value(0).toDouble() : 0;
                    QSqlQuery alreadyPaid(db_);
                    alreadyPaid.prepare(QStringLiteral("SELECT COALESCE(SUM(amount),0) FROM credit_payments "
                        "WHERE party_type='Customer' AND party_id=? AND purpose=?"));
                    alreadyPaid.addBindValue(partyId);
                    alreadyPaid.addBindValue(purpose);
                    run(alreadyPaid);
                    if (alreadyPaid.next()) due -= alreadyPaid.value(0).toDouble();
                    const double allocation = std::min(remaining, std::max(0.0, due));
                    if (allocation > 0.004) {
                        QSqlQuery insert(db_);
                        insert.prepare(QStringLiteral("INSERT INTO credit_payments"
                            "(party_type,party_id,purpose,amount,account,date,reference,notes) "
                            "VALUES('Customer',?,?,?,?,?,?,?)"));
                        insert.addBindValue(partyId);
                        insert.addBindValue(purpose);
                        insert.addBindValue(allocation);
                        insert.addBindValue(account);
                        insert.addBindValue(date.toString(Qt::ISODate));
                        insert.addBindValue(reference.trimmed());
                        insert.addBindValue(notes.trimmed());
                        run(insert);
                        remaining -= allocation;
                    }
                }
                if (remaining > 0.004) {
                    QSqlQuery insert(db_);
                    insert.prepare(QStringLiteral("INSERT INTO credit_payments"
                        "(party_type,party_id,purpose,amount,account,date,reference,notes) "
                        "VALUES('Customer',?,'Advance / over-payment',?,?,?,?,?)"));
                    insert.addBindValue(partyId);
                    insert.addBindValue(remaining);
                    insert.addBindValue(account);
                    insert.addBindValue(date.toString(Qt::ISODate));
                    insert.addBindValue(reference.trimmed());
                    insert.addBindValue(notes.trimmed());
                    run(insert);
                }
            } else {
                const QString purpose = partyType == QStringLiteral("Customer")
                    ? QStringLiteral("Refund to customer")
                    : (position <= 0 ? QStringLiteral("Purchase") : QStringLiteral("Advance returned"));
                const double signedAmount = partyType == QStringLiteral("Customer")
                    ? -amount : (position <= 0 ? amount : -amount);
                QSqlQuery insert(db_);
                insert.prepare(QStringLiteral("INSERT INTO credit_payments"
                    "(party_type,party_id,purpose,amount,account,date,reference,notes) VALUES(?,?,?,?,?,?,?,?)"));
                insert.addBindValue(partyType);
                insert.addBindValue(partyId);
                insert.addBindValue(purpose);
                insert.addBindValue(signedAmount);
                insert.addBindValue(account);
                insert.addBindValue(date.toString(Qt::ISODate));
                insert.addBindValue(reference.trimmed());
                insert.addBindValue(notes.trimmed());
                run(insert);
            }
            QSqlQuery cash(db_);
            cash.prepare(QStringLiteral("INSERT INTO cash_transactions"
                "(date,account,kind,category,amount,reference,description) VALUES(?,?,?,?,?,?,?)"));
            cash.addBindValue(date.toString(Qt::ISODate));
            cash.addBindValue(account);
            cash.addBindValue(cashKind);
            cash.addBindValue(category);
            cash.addBindValue(amount);
            cash.addBindValue(reference.trimmed().isEmpty() ? code : reference.trimmed());
            cash.addBindValue(QStringLiteral("%1 - %2").arg(code, name));
            run(cash);
            audit(username, QStringLiteral("Credit payment"),
                  QStringLiteral("%1 %2; UGX %3").arg(partyType, code).arg(amount, 0, 'f', 2));
        });
    }

    void saveBankDeposit(const QString& username, const QString& receiptNumber, const QDate& date,
                         const QString& teller, const QString& receiptCode, double amount,
                         const QString& bankName, const QString& notes) {
        if (receiptNumber.trimmed().isEmpty() || !date.isValid() || !std::isfinite(amount) ||
            amount <= 0 || bankName.trimmed().isEmpty())
            throw InputError("Enter a receipt number, valid date, positive amount, and bank name.");
        transaction([&] {
            QSqlQuery duplicate(db_);
            duplicate.prepare(QStringLiteral("SELECT 1 FROM bank_transfers WHERE receipt_number=? AND receipt_code=?"));
            duplicate.addBindValue(receiptNumber.trimmed());
            duplicate.addBindValue(receiptCode.trimmed());
            run(duplicate);
            if (duplicate.next()) throw InputError("This deposit receipt number and code are already recorded.");
            QSqlQuery insert(db_);
            insert.prepare(QStringLiteral("INSERT INTO bank_transfers"
                "(receipt_number,deposit_date,teller,receipt_code,amount,bank_name,notes,created_at) "
                "VALUES(?,?,?,?,?,?,?,?)"));
            insert.addBindValue(receiptNumber.trimmed());
            insert.addBindValue(date.toString(Qt::ISODate));
            insert.addBindValue(teller.trimmed());
            insert.addBindValue(receiptCode.trimmed());
            insert.addBindValue(amount);
            insert.addBindValue(bankName.trimmed());
            insert.addBindValue(notes.trimmed());
            insert.addBindValue(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            run(insert);
            for (const auto& movement : {qMakePair(QStringLiteral("Cash"), QStringLiteral("OUT")),
                                         qMakePair(QStringLiteral("Bank"), QStringLiteral("IN"))}) {
                QSqlQuery cash(db_);
                cash.prepare(QStringLiteral("INSERT INTO cash_transactions"
                    "(date,account,kind,category,amount,reference,description) VALUES(?,?,?,'Bank Deposit',?,?,?)"));
                cash.addBindValue(date.toString(Qt::ISODate));
                cash.addBindValue(movement.first);
                cash.addBindValue(movement.second);
                cash.addBindValue(amount);
                cash.addBindValue(receiptNumber.trimmed());
                cash.addBindValue(QStringLiteral("Bank deposit at %1").arg(bankName.trimmed()));
                run(cash);
            }
            audit(username, QStringLiteral("Bank Deposit"),
                  QStringLiteral("Receipt %1; UGX %2").arg(receiptNumber.trimmed()).arg(amount, 0, 'f', 2));
        });
    }

    void saveCompanyMilling(const QString& username, const QString& inputType, const QString& inputGrade,
                            double inputKg, double outputKg, const QString& outputGrade,
                            const QString& notes) {
        if (product_.grain) throw InputError("Company-owned milling is available in Mill Manager only.");
        const QString outputType = inputType == QStringLiteral("Kakuta") ? QStringLiteral("Kibooko")
            : inputType == QStringLiteral("Kibooko") ? QStringLiteral("Kasse") : QString();
        if (outputType.isEmpty() || inputGrade.trimmed().isEmpty() || outputGrade.trimmed().isEmpty() ||
            !std::isfinite(inputKg) || !std::isfinite(outputKg) || inputKg <= 0 ||
            outputKg <= 0 || outputKg > inputKg)
            throw InputError("Choose a valid input and grade; output must be positive and no greater than input.");
        transaction([&] {
            QSqlQuery stock(db_);
            stock.prepare(QStringLiteral("SELECT COALESCE(SUM(kg),0) FROM stock_movements "
                "WHERE coffee_type=? AND grade=?"));
            stock.addBindValue(inputType);
            stock.addBindValue(inputGrade.trimmed());
            run(stock);
            if (!stock.next() || stock.value(0).toDouble() + 0.0001 < inputKg)
                throw InputError("There is not enough of that coffee grade in stock for company milling.");
            QSqlQuery insert(db_);
            insert.prepare(QStringLiteral("INSERT INTO company_milling"
                "(date,input_type,input_grade,kg_in,output_type,output_grade,kg_out,loss_kg,notes,recorded_by) "
                "VALUES(?,?,?,?,?,?,?,?,?,?)"));
            insert.addBindValue(QDate::currentDate().toString(Qt::ISODate));
            insert.addBindValue(inputType);
            insert.addBindValue(inputGrade.trimmed());
            insert.addBindValue(inputKg);
            insert.addBindValue(outputType);
            insert.addBindValue(outputGrade.trimmed());
            insert.addBindValue(outputKg);
            insert.addBindValue(inputKg - outputKg);
            insert.addBindValue(notes.trimmed());
            insert.addBindValue(username);
            run(insert);
            const QString reference = QStringLiteral("CM-%1").arg(insert.lastInsertId().toLongLong(), 5, 10, QLatin1Char('0'));
            addStock(inputType, inputGrade.trimmed(), QStringLiteral("INTERNAL_USE"), -inputKg,
                     reference, QStringLiteral("Used to produce %1").arg(outputType));
            addStock(outputType, outputGrade.trimmed(), QStringLiteral("INTERNAL_PRODUCE"), outputKg,
                     reference, QStringLiteral("Produced from %1").arg(inputType));
            audit(username, QStringLiteral("Company Milling"),
                  QStringLiteral("%1 kg %2 to %3 kg %4; loss %5 kg")
                      .arg(inputKg, 0, 'f', 2).arg(inputType).arg(outputKg, 0, 'f', 2)
                      .arg(outputType).arg(inputKg - outputKg, 0, 'f', 2));
        });
    }

    QList<ProgressMonth> progressMonths(int count) {
        if (count != 6 && count != 12 && count != 24 && count != 36)
            throw InputError("Choose a 6, 12, 24, or 36 month progress range.");
        const auto sum = [this](const QString& table, const QString& column,
                                    const QString& start, const QString& end) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT COALESCE(SUM(%1),0) FROM %2 WHERE date BETWEEN ? AND ?")
                .arg(column, table));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            return query.next() ? query.value(0).toDouble() : 0.0;
        };
        const auto balanceAt = [this](const QString& account, const QDate& date) {
            QSqlQuery opening(db_);
            opening.prepare(QStringLiteral("SELECT month,cash,bank FROM monthly_opening_balances "
                "WHERE month<=? AND confirmed=1 ORDER BY month DESC LIMIT 1"));
            opening.addBindValue(date.toString(QStringLiteral("yyyy-MM")));
            run(opening);
            QString movementStart;
            double amount = 0;
            if (opening.next()) {
                movementStart = opening.value(0).toString() + QStringLiteral("-01");
                amount = opening.value(account == QStringLiteral("Cash") ? 1 : 2).toDouble();
            } else {
                QSqlQuery settingQuery(db_);
                settingQuery.prepare(QStringLiteral("SELECT value FROM app_settings WHERE key=?"));
                settingQuery.addBindValue(account == QStringLiteral("Cash")
                        ? QStringLiteral("OPENING_CASH") : QStringLiteral("OPENING_BANK"));
                run(settingQuery);
                if (settingQuery.next()) amount = settingQuery.value(0).toDouble();
            }
            QString sql = QStringLiteral("SELECT COALESCE(SUM(CASE WHEN kind='IN' THEN amount ELSE -amount END),0) "
                "FROM cash_transactions WHERE account=? AND date<=?");
            if (!movementStart.isEmpty()) sql += QStringLiteral(" AND date>=?");
            QSqlQuery movements(db_);
            movements.prepare(sql);
            movements.addBindValue(account);
            movements.addBindValue(date.toString(Qt::ISODate));
            if (!movementStart.isEmpty()) movements.addBindValue(movementStart);
            run(movements);
            return amount + (movements.next() ? movements.value(0).toDouble() : 0.0);
        };

        QList<ProgressMonth> result;
        QDate firstMonth = QDate::currentDate().addMonths(1 - count);
        firstMonth = firstMonth.addDays(1 - firstMonth.day());
        for (int offset = 0; offset < count; ++offset) {
            const QDate monthStart = firstMonth.addMonths(offset);
            const QDate monthEnd = monthStart.addMonths(1).addDays(-1);
            const QString start = monthStart.toString(Qt::ISODate);
            const QString end = monthEnd.toString(Qt::ISODate);
            ProgressMonth month;
            month.month = monthStart.toString(QStringLiteral("MMM yy"));
            month.sales = sum(product_.saleTable(), QStringLiteral("total"), start, end);
            month.milling = sum(QStringLiteral("milling"), QStringLiteral("total"), start, end);
            month.purchases = sum(product_.purchaseTable(), QStringLiteral("total"), start, end);
            month.expenses = sum(QStringLiteral("expenses"), QStringLiteral("amount"), start, end);
            month.payroll = sum(QStringLiteral("payroll"), QStringLiteral("gross"), start, end);
            month.tax = sum(QStringLiteral("tax_payments"), QStringLiteral("amount"), start, end);
            month.cash = balanceAt(QStringLiteral("Cash"), monthEnd);
            month.bank = balanceAt(QStringLiteral("Bank"), monthEnd);
            month.boughtKg = sum(product_.purchaseTable(), QStringLiteral("kg"), start, end);
            month.soldKg = sum(product_.saleTable(), QStringLiteral("kg"), start, end);
            result.push_back(month);
        }
        return result;
    }

    QList<ReportEntry> reportRows(const QDate& from, const QDate& to,
                                  const QString& type, const QString& search,
                                  const QString& person = {}) {
        QList<ReportEntry> rows;
        const QString start = from.toString(Qt::ISODate);
        const QString end = to.toString(Qt::ISODate);
        const auto appendTransactions = [&](bool purchase) {
            const QString table = purchase ? product_.purchaseTable() : product_.saleTable();
            const QString partyTable = purchase ? QStringLiteral("suppliers") : QStringLiteral("customers");
            const QString idColumn = purchase ? QStringLiteral("supplier_id") : QStringLiteral("customer_id");
            const QString refColumn = purchase ? QStringLiteral("receipt_no") : QStringLiteral("invoice_no");
            const QString amountColumn = purchase ? QStringLiteral("paid") : QStringLiteral("received");
            const QString accountColumn = purchase ? QStringLiteral("payment_account") : QStringLiteral("received_account");
            const QString productColumn = product_.grain ? QStringLiteral("product")
                                                          : QStringLiteral("coffee_type");
            QSqlQuery query(db_);
            query.prepare(QStringLiteral(
                "SELECT t.id,t.date,COALESCE(t.%1,''),COALESCE(p.name,''),COALESCE(t.%2,''),"
                "COALESCE(t.grade,''),t.kg,t.price_per_kg,t.total,t.%3,COALESCE(t.%4,'Cash') "
                "FROM %5 t LEFT JOIN %6 p ON p.id=t.%7 WHERE t.date BETWEEN ? AND ? "
                "ORDER BY t.date DESC,t.id DESC")
                .arg(refColumn, productColumn, amountColumn, accountColumn, table, partyTable, idColumn));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = purchase ? QStringLiteral("Purchase") : QStringLiteral("Sale");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = query.value(2).toString();
                row.party = query.value(3).toString();
                row.product = query.value(4).toString();
                row.grade = query.value(5).toString();
                row.kg = query.value(6).toDouble();
                row.price = query.value(7).toDouble();
                row.total = query.value(8).toDouble();
                row.paid = query.value(9).toDouble();
                row.account = query.value(10).toString();
                rows.push_back(row);
            }
        };
        if (type == QStringLiteral("All") || type == QStringLiteral("Purchase"))
            appendTransactions(true);
        if (type == QStringLiteral("All") || type == QStringLiteral("Sale"))
            appendTransactions(false);
        if (type == QStringLiteral("All") || type == QStringLiteral("Milling")) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral(
                "SELECT m.id,m.date,COALESCE(m.receipt_no,''),COALESCE(c.name,''),COALESCE(m.coffee_type,''),"
                "COALESCE(m.grade,''),m.kg_in,m.kg_out,m.charge_per_kg,m.total,m.paid,COALESCE(m.received_account,'Cash') "
                "FROM milling m LEFT JOIN customers c ON c.id=m.customer_id WHERE m.date BETWEEN ? AND ? "
                "ORDER BY m.date DESC,m.id DESC"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Milling");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = query.value(2).toString();
                row.party = query.value(3).toString();
                row.product = query.value(4).toString();
                row.grade = query.value(5).toString();
                row.kg = query.value(6).toDouble();
                row.outputKg = query.value(7).toDouble();
                row.price = query.value(8).toDouble();
                row.total = query.value(9).toDouble();
                row.paid = query.value(10).toDouble();
                row.account = query.value(11).toString();
                rows.push_back(row);
            }
        }
        if (type == QStringLiteral("All") || type == QStringLiteral("Expense")) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT id,date,COALESCE(reference,''),COALESCE(description,''),"
                "COALESCE(category,''),amount,COALESCE(paid_from,'Cash') FROM expenses "
                "WHERE date BETWEEN ? AND ? ORDER BY date DESC,id DESC"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Expense");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = query.value(2).toString();
                row.party = query.value(3).toString();
                row.product = query.value(4).toString();
                row.total = query.value(5).toDouble();
                row.paid = row.total;
                row.account = query.value(6).toString();
                rows.push_back(row);
            }
        }
        if (type == QStringLiteral("All") || type == QStringLiteral("Money")) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT id,date,COALESCE(reference,''),COALESCE(description,''),"
                "COALESCE(category,''),account,kind,amount FROM cash_transactions "
                "WHERE date BETWEEN ? AND ? ORDER BY date DESC,id DESC"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Money ") + query.value(6).toString();
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = query.value(2).toString();
                row.party = query.value(3).toString();
                row.product = query.value(4).toString();
                row.grade = query.value(5).toString();
                row.total = query.value(7).toDouble();
                row.paid = row.total;
                row.account = query.value(5).toString();
                rows.push_back(row);
            }
        }
        if (type == QStringLiteral("All") || type == QStringLiteral("Payroll")) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT p.id,p.date,p.period,COALESCE(w.worker_code,'')||' - '||"
                "COALESCE(w.name,''),p.days,p.gross,p.paye,p.net,p.paid FROM payroll p "
                "LEFT JOIN workers w ON w.id=p.worker_id WHERE p.date BETWEEN ? AND ? "
                "ORDER BY p.date DESC,p.id DESC"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Payroll");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = query.value(2).toString();
                row.party = query.value(3).toString();
                row.product = QStringLiteral("Net pay");
                row.kg = query.value(4).toDouble();
                row.total = query.value(7).toDouble();
                row.paid = query.value(8).toDouble();
                row.grade = QStringLiteral("PAYE %1").arg(query.value(6).toDouble(), 0, 'f', 0);
                row.price = query.value(5).toDouble();
                rows.push_back(row);
            }
        }
        if (type == QStringLiteral("All") || type == QStringLiteral("Tax Payment")) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT id,date,COALESCE(reference,''),tax_type,period,amount "
                "FROM tax_payments WHERE date BETWEEN ? AND ? ORDER BY date DESC,id DESC"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Tax Payment");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = query.value(2).toString();
                row.party = query.value(3).toString();
                row.product = query.value(4).toString();
                row.total = query.value(5).toDouble();
                row.paid = row.total;
                rows.push_back(row);
            }
        }
        if (type == QStringLiteral("All") || type == QStringLiteral("Credit Payment")) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral(
                "SELECT p.id,p.date,COALESCE(p.reference,''),p.party_type,"
                "CASE WHEN p.party_type='Customer' THEN COALESCE(c.name,'') ELSE COALESCE(s.name,'') END,"
                "p.purpose,p.amount,p.account FROM credit_payments p "
                "LEFT JOIN customers c ON p.party_type='Customer' AND c.id=p.party_id "
                "LEFT JOIN suppliers s ON p.party_type='Supplier' AND s.id=p.party_id "
                "WHERE p.date BETWEEN ? AND ? ORDER BY p.date DESC,p.id DESC"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Credit Payment");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = query.value(2).toString();
                row.grade = query.value(3).toString();
                row.party = query.value(4).toString();
                row.product = query.value(5).toString();
                row.total = query.value(6).toDouble();
                row.paid = row.total;
                row.account = query.value(7).toString();
                rows.push_back(row);
            }
        }
        if (type == QStringLiteral("All") || type == QStringLiteral("Bank Deposit")) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT id,deposit_date,receipt_number,teller,bank_name,amount,receipt_code "
                "FROM bank_transfers WHERE deposit_date BETWEEN ? AND ? ORDER BY deposit_date DESC,id DESC"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Bank Deposit");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = query.value(2).toString();
                row.party = query.value(3).toString();
                row.product = query.value(4).toString();
                row.grade = query.value(6).toString();
                row.total = query.value(5).toDouble();
                row.paid = row.total;
                rows.push_back(row);
            }
        }
        if (!product_.grain && (type == QStringLiteral("All") || type == QStringLiteral("Company Milling"))) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT id,date,input_type,input_grade,kg_in,output_type,output_grade,"
                "kg_out,loss_kg,COALESCE(recorded_by,''),COALESCE(notes,'') FROM company_milling "
                "WHERE date BETWEEN ? AND ? ORDER BY date DESC,id DESC"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Company Milling");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = QStringLiteral("CM-%1").arg(row.id, 5, 10, QLatin1Char('0'));
                row.party = query.value(9).toString();
                row.product = query.value(5).toString();
                row.grade = query.value(6).toString();
                row.kg = query.value(4).toDouble();
                row.outputKg = query.value(7).toDouble();
                row.price = query.value(8).toDouble();
                row.total = row.outputKg;
                row.account = query.value(10).toString();
                rows.push_back(row);
            }
        }
        if (product_.grain && (type == QStringLiteral("All") || type == QStringLiteral("Company Milling"))) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral(
                "SELECT id,date,input_product,input_kg,flour_kg,feed_kg,kyakyu_kg,operator "
                "FROM milling_runs WHERE date BETWEEN ? AND ? ORDER BY date DESC,id DESC"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Company Milling");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = QStringLiteral("CM-%1").arg(row.id, 5, 10, QLatin1Char('0'));
                row.product = query.value(2).toString();
                row.kg = query.value(3).toDouble();
                row.outputKg = query.value(4).toDouble() + query.value(5).toDouble() +
                               query.value(6).toDouble();
                row.total = row.outputKg;
                row.party = query.value(7).toString();
                rows.push_back(row);
            }
        }
        if (type == QStringLiteral("Daily Cash & Bank")) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT id,date,account,kind,category,reference,description,amount "
                "FROM cash_transactions WHERE date BETWEEN ? AND ? ORDER BY date,id"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Daily Cash & Bank");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString();
                row.reference = query.value(5).toString();
                row.party = query.value(3).toString() + QStringLiteral(" / ") + query.value(2).toString();
                row.product = query.value(4).toString();
                row.grade = query.value(6).toString();
                row.total = query.value(3).toString() == QStringLiteral("IN")
                    ? query.value(7).toDouble() : 0;
                row.paid = query.value(3).toString() == QStringLiteral("OUT")
                    ? query.value(7).toDouble() : 0;
                rows.push_back(row);
            }
        }
        if (type == QStringLiteral("Monthly Cash Flow")) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT substr(date,1,7),account,kind,COALESCE(category,''),"
                "SUM(amount) FROM cash_transactions WHERE date BETWEEN ? AND ? "
                "GROUP BY substr(date,1,7),account,kind,category ORDER BY substr(date,1,7),account,kind,category"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Monthly Cash Flow");
                row.date = query.value(0).toString();
                row.party = query.value(2).toString() + QStringLiteral(" / ") + query.value(1).toString();
                row.product = query.value(3).toString();
                row.total = query.value(2).toString() == QStringLiteral("IN")
                    ? query.value(4).toDouble() : 0;
                row.paid = query.value(2).toString() == QStringLiteral("OUT")
                    ? query.value(4).toDouble() : 0;
                rows.push_back(row);
            }
        }
        const auto appendBalances = [&](const QString& partyType) {
            const QDate asAt = QDate::currentDate();
            if (asAt < from || asAt > to) return;
            const bool customer = partyType == QStringLiteral("Customer");
            const QString table = customer ? QStringLiteral("customers") : QStringLiteral("suppliers");
            const QString codeColumn = customer ? QStringLiteral("buyer_code") : QStringLiteral("supplier_code");
            QSqlQuery peopleQuery(db_);
            peopleQuery.prepare(QStringLiteral("SELECT id,name,%1 FROM %2 ORDER BY name").arg(codeColumn, table));
            run(peopleQuery);
            while (peopleQuery.next()) {
                const qint64 personId = peopleQuery.value(0).toLongLong();
                const QString name = peopleQuery.value(1).toString();
                const QString code = peopleQuery.value(2).toString();
                QSqlQuery positionQuery(db_);
                QString positionSql;
                if (customer) {
                    positionSql = QStringLiteral("SELECT COALESCE((SELECT SUM(total-received) FROM %1 "
                        "WHERE customer_id=?),0)").arg(product_.saleTable());
                    positionSql += QStringLiteral("+COALESCE((SELECT SUM(total-paid) FROM milling WHERE customer_id=?),0)");
                    positionSql += QStringLiteral("-COALESCE((SELECT SUM(amount) FROM credit_payments "
                        "WHERE party_type='Customer' AND party_id=?),0)");
                    positionQuery.prepare(positionSql);
                    positionQuery.addBindValue(personId);
                    positionQuery.addBindValue(personId);
                    positionQuery.addBindValue(personId);
                } else {
                    positionQuery.prepare(QStringLiteral("SELECT -COALESCE((SELECT SUM(total-paid) FROM %1 "
                        "WHERE supplier_id=?),0)+COALESCE((SELECT SUM(amount) FROM credit_payments "
                        "WHERE party_type='Supplier' AND party_id=?),0)").arg(product_.purchaseTable()));
                    positionQuery.addBindValue(personId);
                    positionQuery.addBindValue(personId);
                }
                run(positionQuery);
                const double position = positionQuery.next() ? positionQuery.value(0).toDouble() : 0;
                if (std::abs(position) < 0.5) continue;
                const bool include = type == QStringLiteral("Open Credits") ||
                    (type == QStringLiteral("Customer Balances") && customer) ||
                    (type == QStringLiteral("Supplier Balances") && !customer);
                if (!include) continue;
                ReportEntry row;
                row.type = type;
                row.date = asAt.toString(Qt::ISODate);
                row.id = personId;
                row.reference = code;
                row.party = name;
                row.product = position > 0 ? QStringLiteral("Owes business")
                                           : QStringLiteral("Business owes party");
                row.total = type == QStringLiteral("Supplier Balances") ? -position : position;
                row.paid = row.total;
                rows.push_back(row);
            }
        };
        if (type == QStringLiteral("Customer Balances") || type == QStringLiteral("Open Credits"))
            appendBalances(QStringLiteral("Customer"));
        if (type == QStringLiteral("Supplier Balances") || type == QStringLiteral("Open Credits"))
            appendBalances(QStringLiteral("Supplier"));
        if (type == QStringLiteral("Stock Balance")) {
            const QDate asAt = QDate::currentDate();
            if (asAt < from || asAt > to) return rows;
            const QString table = product_.grain ? QStringLiteral("grain_stock_movements")
                                                 : QStringLiteral("stock_movements");
            const QString productColumn = product_.grain ? QStringLiteral("product")
                                                         : QStringLiteral("coffee_type");
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT %1,COALESCE(grade,''),COALESCE(SUM(kg),0) FROM %2 "
                "GROUP BY %1,COALESCE(grade,'') ORDER BY %1,grade").arg(productColumn, table));
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Stock Balance");
                row.date = asAt.toString(Qt::ISODate);
                row.product = query.value(0).toString();
                row.grade = query.value(1).toString();
                row.kg = query.value(2).toDouble();
                row.total = row.kg;
                rows.push_back(row);
            }
        }
        if (type == QStringLiteral("Profit Summary")) {
            const auto amountForPeriod = [&](const QString& sql) {
                QSqlQuery query(db_);
                query.prepare(sql);
                query.addBindValue(start);
                query.addBindValue(end);
                run(query);
                return query.next() ? query.value(0).toDouble() : 0.0;
            };
            const double sales = amountForPeriod(QStringLiteral("SELECT COALESCE(SUM(total),0) FROM %1 WHERE date BETWEEN ? AND ?")
                .arg(product_.saleTable()));
            const double milling = amountForPeriod(
                QStringLiteral("SELECT COALESCE(SUM(total),0) FROM milling WHERE date BETWEEN ? AND ?"));
            const double purchases = amountForPeriod(QStringLiteral("SELECT COALESCE(SUM(total),0) FROM %1 WHERE date BETWEEN ? AND ?")
                .arg(product_.purchaseTable()));
            const double expenses = amountForPeriod(QStringLiteral("SELECT COALESCE(SUM(amount),0) FROM expenses WHERE date BETWEEN ? AND ?"));
            const double payroll = amountForPeriod(QStringLiteral("SELECT COALESCE(SUM(gross),0) FROM payroll WHERE date BETWEEN ? AND ?"));
            const double taxes = amountForPeriod(QStringLiteral("SELECT COALESCE(SUM(amount),0) FROM tax_payments WHERE date BETWEEN ? AND ?"));
            const QList<QPair<QString, double>> amounts = {
                {QStringLiteral("Coffee/grain sales"), sales},
                {QStringLiteral("Customer milling charges"), milling},
                {QStringLiteral("Purchases"), -purchases},
                {QStringLiteral("Expenses"), -expenses},
                {QStringLiteral("Gross payroll"), -payroll},
                {QStringLiteral("Tax payments"), -taxes},
                {QStringLiteral("Net profit/loss"), sales + milling - purchases - expenses - payroll - taxes}
            };
            for (const auto& amount : amounts) {
                ReportEntry row;
                row.type = QStringLiteral("Profit Summary");
                row.date = end;
                row.party = amount.first;
                row.total = amount.second;
                row.paid = amount.second;
                rows.push_back(row);
            }
        }
        if (type == QStringLiteral("Activity Log")) {
            QSqlQuery query(db_);
            query.prepare(QStringLiteral("SELECT id,COALESCE(recorded_at,date),username,action,details FROM audit_log "
                "WHERE substr(COALESCE(recorded_at,date),1,10) BETWEEN ? AND ? ORDER BY id DESC"));
            query.addBindValue(start);
            query.addBindValue(end);
            run(query);
            while (query.next()) {
                ReportEntry row;
                row.type = QStringLiteral("Activity Log");
                row.id = query.value(0).toLongLong();
                row.date = query.value(1).toString().left(19);
                row.party = query.value(2).toString();
                row.reference = query.value(3).toString();
                row.product = query.value(4).toString();
                rows.push_back(row);
            }
        }
        const QString needle = search.trimmed();
        const QString personFilter = person.trimmed();
        if (!needle.isEmpty()) {
            for (auto it = rows.begin(); it != rows.end();) {
                const QString haystack = it->type + QLatin1Char(' ') + it->date + QLatin1Char(' ') +
                    it->reference + QLatin1Char(' ') + it->party + QLatin1Char(' ') + it->product +
                    QLatin1Char(' ') + it->grade;
                if (!haystack.contains(needle, Qt::CaseInsensitive)) it = rows.erase(it);
                else ++it;
            }
        }
        if (!personFilter.isEmpty()) {
            for (auto it = rows.begin(); it != rows.end();) {
                if (!it->party.contains(personFilter, Qt::CaseInsensitive)) it = rows.erase(it);
                else ++it;
            }
        }
        std::stable_sort(rows.begin(), rows.end(), [](const ReportEntry& a, const ReportEntry& b) {
            return a.date == b.date ? a.id > b.id : a.date > b.date;
        });
        return rows;
    }

    void editReportRecord(const QString& username, const ReportEntry& original,
                          const QDate& date, const QString& party, const QString& product,
                          const QString& grade, double kg, double outputKg, double price, double paid,
                          const QString& account) {
        const bool coffeeMilling = original.type == QStringLiteral("Milling");
        if (original.type != QStringLiteral("Purchase") && original.type != QStringLiteral("Sale") && !coffeeMilling)
            throw InputError("This report record is view-only.");
        if (!date.isValid() || party.trimmed().isEmpty() || grade.trimmed().isEmpty() ||
            !std::isfinite(kg) || !std::isfinite(price) || !std::isfinite(paid) ||
            !std::isfinite(outputKg) || !std::isfinite(kg * price) || kg <= 0 ||
            (coffeeMilling && (outputKg < 0 || outputKg > kg)) || price < 0 || paid < 0 ||
            (account != QStringLiteral("Cash") && account != QStringLiteral("Bank")))
            throw InputError("Enter a valid date, party, product, weight, price, payment, and account.");
        const bool purchase = original.type == QStringLiteral("Purchase");
        const QString table = coffeeMilling ? QStringLiteral("milling") :
            purchase ? product_.purchaseTable() : product_.saleTable();
        const QString partyIdColumn = purchase ? QStringLiteral("supplier_id") : QStringLiteral("customer_id");
        const QString partyTable = purchase ? QStringLiteral("suppliers") : QStringLiteral("customers");
        const QString codeColumn = purchase ? QStringLiteral("supplier_code") : QStringLiteral("buyer_code");
        const QString productColumn = product_.grain ? QStringLiteral("product")
                                                     : QStringLiteral("coffee_type");
        const QString amountColumn = purchase ? QStringLiteral("paid") : QStringLiteral("received");
        const QString accountColumn = purchase ? QStringLiteral("payment_account") : QStringLiteral("received_account");
        const QString refColumn = purchase ? QStringLiteral("receipt_no") :
            coffeeMilling ? QStringLiteral("receipt_no") : QStringLiteral("invoice_no");
        const QString stockTable = product_.grain ? QStringLiteral("grain_stock_movements")
                                                  : QStringLiteral("stock_movements");
        const QString stockProductColumn = product_.grain ? QStringLiteral("product")
                                                           : QStringLiteral("coffee_type");
        if (!(purchase ? availablePurchaseProducts() : coffeeMilling ? availableMillingProducts() : availableSaleProducts()).contains(product))
            throw InputError("Select a product enabled for this transaction type.");

        transaction([&] {
            const QString partyId = ensurePerson(partyTable, codeColumn,
                purchase ? QStringLiteral("SUP") : QStringLiteral("CUS"), party);
            QSqlQuery update(db_);
            if (coffeeMilling) {
                update.prepare(QStringLiteral("UPDATE milling SET customer_id=?,coffee_type=?,grade=?,kg_in=?,kg_out=?,loss_kg=?,"
                    "charge_per_kg=?,total=?,paid=?,date=?,%1=? WHERE id=?").arg(accountColumn));
                update.addBindValue(partyId);
                update.addBindValue(product);
                update.addBindValue(grade.trimmed());
                update.addBindValue(kg);
                update.addBindValue(outputKg);
                update.addBindValue(kg - outputKg);
                update.addBindValue(price);
                update.addBindValue(kg * price);
                update.addBindValue(paid);
                update.addBindValue(date.toString(Qt::ISODate));
                update.addBindValue(account);
                update.addBindValue(original.id);
            } else {
                update.prepare(QStringLiteral("UPDATE %1 SET %2=?,%3=?,grade=?,kg=?,price_per_kg=?,total=?,%4=?,%5=?,date=? WHERE id=?")
                    .arg(table, partyIdColumn, productColumn, amountColumn, accountColumn));
                update.addBindValue(partyId);
                update.addBindValue(product);
                update.addBindValue(grade.trimmed());
                update.addBindValue(kg);
                update.addBindValue(price);
                update.addBindValue(kg * price);
                update.addBindValue(paid);
                update.addBindValue(account);
                update.addBindValue(date.toString(Qt::ISODate));
                update.addBindValue(original.id);
            }
            run(update);
            if (update.numRowsAffected() != 1) throw InputError("The selected report record no longer exists.");

            QSqlQuery referenceQuery(db_);
            referenceQuery.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE id=?").arg(refColumn, table));
            referenceQuery.addBindValue(original.id);
            run(referenceQuery);
            if (!referenceQuery.next()) throw InputError("The selected report record could not be reloaded.");
            const QString reference = referenceQuery.value(0).toString();

            if (!coffeeMilling) {
                QSqlQuery stock(db_);
                stock.prepare(QStringLiteral("UPDATE %1 SET date=?,%2=?,grade=?,kg=? WHERE reference=? AND movement=?")
                    .arg(stockTable, stockProductColumn));
                stock.addBindValue(date.toString(Qt::ISODate));
                stock.addBindValue(product);
                stock.addBindValue(grade.trimmed());
                stock.addBindValue(purchase ? kg : -kg);
                stock.addBindValue(reference);
                stock.addBindValue(purchase ? QStringLiteral("PURCHASE") : QStringLiteral("SALE"));
                run(stock);
                if (stock.numRowsAffected() == 0)
                    addStock(product, grade.trimmed(), purchase ? QStringLiteral("PURCHASE") : QStringLiteral("SALE"),
                             purchase ? kg : -kg, reference, purchase ? QStringLiteral("Purchase edited from report")
                                                                      : QStringLiteral("Sale edited from report"));
            }

            const QString cashKind = purchase ? QStringLiteral("OUT") : QStringLiteral("IN");
            const QString cashCategory = purchase ? QStringLiteral("Purchase") :
                coffeeMilling ? QStringLiteral("Milling Income") : QStringLiteral("Sale");
            QSqlQuery cashFind(db_);
            cashFind.prepare(QStringLiteral("SELECT id FROM cash_transactions WHERE reference=? AND kind=? AND category=? ORDER BY id"));
            cashFind.addBindValue(reference);
            cashFind.addBindValue(cashKind);
            cashFind.addBindValue(cashCategory);
            run(cashFind);
            if (paid == 0) {
                QSqlQuery remove(db_);
                remove.prepare(QStringLiteral("DELETE FROM cash_transactions WHERE reference=? AND kind=? AND category=?"));
                remove.addBindValue(reference);
                remove.addBindValue(cashKind);
                remove.addBindValue(cashCategory);
                run(remove);
            } else if (cashFind.next()) {
                QSqlQuery cashUpdate(db_);
                cashUpdate.prepare(QStringLiteral("UPDATE cash_transactions SET date=?,account=?,amount=? WHERE id=?"));
                cashUpdate.addBindValue(date.toString(Qt::ISODate));
                cashUpdate.addBindValue(account);
                cashUpdate.addBindValue(paid);
                cashUpdate.addBindValue(cashFind.value(0));
                run(cashUpdate);
            } else {
                addCash(cashKind, cashCategory, paid, reference, account);
            }
            if (!purchase && !coffeeMilling) {
                QSqlQuery invoice(db_);
                invoice.prepare(QStringLiteral("UPDATE invoices SET customer_id=?,issue_date=?,amount=?,status=? WHERE sale_id=?"));
                invoice.addBindValue(partyId);
                invoice.addBindValue(date.toString(Qt::ISODate));
                invoice.addBindValue(kg * price);
                invoice.addBindValue(paid >= kg * price ? QStringLiteral("PAID") : QStringLiteral("CREDIT"));
                invoice.addBindValue(original.id);
                run(invoice);
            }
            audit(username, QStringLiteral("Report record edited"),
                  original.type + QStringLiteral(" ") + reference);
        });
    }

    QJsonObject dashboard() {
        QJsonObject result;
        result.insert(QStringLiteral("cash"), balance(QStringLiteral("Cash")));
        result.insert(QStringLiteral("bank"), balance(QStringLiteral("Bank")));
        const QString stockTable = product_.grain ? QStringLiteral("grain_stock_movements")
                                                  : QStringLiteral("stock_movements");
        const QString stockColumn = product_.grain ? QStringLiteral("product")
                                                   : QStringLiteral("coffee_type");
        QSqlQuery stockQuery(db_);
        if (!stockQuery.exec(QStringLiteral("SELECT %1,COALESCE(SUM(kg),0) FROM %2 GROUP BY %1")
                                 .arg(stockColumn, stockTable)))
            throw std::runtime_error(sqlError(stockQuery).toStdString());
        QJsonObject stockByProduct;
        double stockTotal = 0;
        while (stockQuery.next()) {
            const double kg = stockQuery.value(1).toDouble();
            stockByProduct.insert(stockQuery.value(0).toString(), kg);
            stockTotal += kg;
        }
        result.insert(QStringLiteral("stockKg"), stockTotal);
        result.insert(QStringLiteral("stockByProduct"), stockByProduct);
        result.insert(QStringLiteral("boughtKg"), scalar(QStringLiteral(
            "SELECT COALESCE(SUM(kg),0) FROM %1").arg(product_.purchaseTable())));
        result.insert(QStringLiteral("soldKg"), scalar(QStringLiteral(
            "SELECT COALESCE(SUM(kg),0) FROM %1").arg(product_.saleTable())));
        result.insert(QStringLiteral("salesRevenue"), scalar(QStringLiteral(
            "SELECT COALESCE(SUM(total),0) FROM %1").arg(product_.saleTable())));
        result.insert(QStringLiteral("salesReceived"), scalar(QStringLiteral(
            "SELECT COALESCE(SUM(received),0) FROM %1").arg(product_.saleTable())));
        result.insert(QStringLiteral("receivable"),
                      kiyambi_detail::creditTotal(db_, product_.grain, true));
        result.insert(QStringLiteral("payable"),
                      kiyambi_detail::creditTotal(db_, product_.grain, false));
        result.insert(QStringLiteral("millingRevenue"),
                      scalar(QStringLiteral("SELECT COALESCE(SUM(total),0) FROM milling")));
        double outstanding = scalar(QStringLiteral(
            "SELECT COALESCE(SUM(total-received),0) FROM %1").arg(product_.saleTable()));
        outstanding += scalar(QStringLiteral("SELECT COALESCE(SUM(total-paid),0) FROM milling"));
        result.insert(QStringLiteral("outstanding"), outstanding);
        result.insert(QStringLiteral("expenses"), scalar(
            QStringLiteral("SELECT COALESCE(SUM(amount),0) FROM expenses")));
        result.insert(QStringLiteral("workersPaid"), scalar(
            QStringLiteral("SELECT COALESCE(SUM(paid),0) FROM payroll")));
        return result;
    }

    QStringList availablePurchaseProducts() {
        const QString raw = setting(QStringLiteral("BUY_ALLOWED_TYPES"), product_.purchaseProducts().join(QLatin1Char(',')));
        QStringList result;
        for (const QString& value : raw.split(QLatin1Char(','), Qt::SkipEmptyParts))
            if (!value.trimmed().isEmpty()) result.push_back(value.trimmed());
        return result.isEmpty() ? product_.purchaseProducts() : result;
    }

    QStringList availableSaleProducts() {
        if (!product_.grain && setting(QStringLiteral("SELL_ONLY_KASSE"), QStringLiteral("1")) == QStringLiteral("1"))
            return {QStringLiteral("Kasse")};
        const QString raw = setting(QStringLiteral("SELL_ALLOWED_TYPES"), product_.saleProducts().join(QLatin1Char(',')));
        QStringList result;
        for (const QString& value : raw.split(QLatin1Char(','), Qt::SkipEmptyParts))
            if (!value.trimmed().isEmpty()) result.push_back(value.trimmed());
        return result.isEmpty() ? product_.saleProducts() : result;
    }

    double savedPrice(const QString& kind, const QString& name, const QString& grade) {
        const QString value = setting(kind + QStringLiteral("_PRICE::") + name + QStringLiteral("::") + grade);
        if (value.isEmpty()) return 0;
        bool ok = false;
        const double price = value.toDouble(&ok);
        if (!ok || !std::isfinite(price) || price < 0)
            throw std::runtime_error("A saved coffee price is invalid. Correct it in the existing app's Settings.");
        return price;
    }

    QList<QStringList> recentRows(bool purchase) {
        const QString table = purchase ? product_.purchaseTable() : product_.saleTable();
        const QString partyTable = purchase ? QStringLiteral("suppliers") : QStringLiteral("customers");
        const QString idColumn = purchase ? QStringLiteral("supplier_id") : QStringLiteral("customer_id");
        const QString codeColumn = purchase ? QStringLiteral("supplier_code") : QStringLiteral("buyer_code");
        const QString productColumn = product_.grain ? QStringLiteral("product")
                                                     : QStringLiteral("coffee_type");
        const QString paymentColumn = purchase ? QStringLiteral("paid") : QStringLiteral("received");
        const QString referenceColumn = purchase ? QStringLiteral("receipt_no") : QStringLiteral("invoice_no");
        const QString sql = QStringLiteral(
            "SELECT t.date,COALESCE(t.%1,''),COALESCE(p.%2,'')||' - '||COALESCE(p.name,''),"
            "t.%3,t.grade,t.kg,t.total,t.%4 FROM %5 t LEFT JOIN %6 p ON p.id=t.%7 "
            "ORDER BY t.id DESC LIMIT 12")
            .arg(referenceColumn, codeColumn, productColumn, paymentColumn, table, partyTable, idColumn);
        QSqlQuery query(db_);
        if (!query.exec(sql)) throw std::runtime_error(sqlError(query).toStdString());
        QList<QStringList> rows;
        while (query.next()) {
            const double total = query.value(6).toDouble();
            const double paid = query.value(7).toDouble();
            rows.push_back({query.value(0).toString(), query.value(1).toString(),
                query.value(2).toString(), query.value(3).toString(), query.value(4).toString(),
                QString::number(query.value(5).toDouble(), 'f', 2),
                QString::number(total, 'f', 0), QString::number(paid, 'f', 0),
                QString::number(total - paid, 'f', 0)});
        }
        return rows;
    }

private:
    template<typename Action>
    void transaction(Action action) {
        execSql(db_, QStringLiteral("BEGIN IMMEDIATE"));
        try {
            action();
            execSql(db_, QStringLiteral("COMMIT"));
        } catch (...) {
            QSqlQuery rollback(db_);
            rollback.exec(QStringLiteral("ROLLBACK"));
            throw;
        }
    }

    static void run(QSqlQuery& query) {
        if (!query.exec())
            throw std::runtime_error(QStringLiteral("%1: %2").arg(query.lastQuery(), sqlError(query)).toStdString());
    }

    QString ensurePerson(const QString& table, const QString& codeColumn, const QString& prefix,
                         const QString& entered) {
        if (entered.trimmed().isEmpty() || entered.size() > 200)
            throw InputError("Enter a supplier or customer name (up to 200 characters).");
        const QString typed = entered.trimmed();
        QString code = typed;
        const qsizetype delimiter = code.indexOf(QStringLiteral(" - "));
        if (delimiter >= 0) code.truncate(delimiter);
        QSqlQuery find(db_);
        find.prepare(QStringLiteral("SELECT id FROM %1 WHERE upper(%2)=upper(?) OR lower(trim(name))=lower(trim(?)) LIMIT 1")
                     .arg(table, codeColumn));
        find.addBindValue(code);
        find.addBindValue(typed);
        run(find);
        if (find.next()) return find.value(0).toString();
        QSqlQuery next(db_);
        next.prepare(QStringLiteral("SELECT COALESCE(MAX(CAST(substr(%1,5) AS INTEGER)),0)+1 FROM %2 WHERE %1 LIKE ?")
                     .arg(codeColumn, table));
        next.addBindValue(prefix + QStringLiteral("-%"));
        run(next);
        next.next();
        const QString generatedCode = QStringLiteral("%1-%2").arg(prefix).arg(next.value(0).toInt(), 4, 10, QLatin1Char('0'));
        QSqlQuery insert(db_);
        insert.prepare(QStringLiteral("INSERT INTO %1(name,%2) VALUES(?,?)").arg(table, codeColumn));
        insert.addBindValue(entered.trimmed());
        insert.addBindValue(generatedCode);
        run(insert);
        return QString::number(insert.lastInsertId().toLongLong());
    }

    void addStock(const QString& product, const QString& grade, const QString& movement,
                  double kg, const QString& reference, const QString& notes) {
        QSqlQuery query(db_);
        query.prepare(product_.grain
            ? QStringLiteral("INSERT INTO grain_stock_movements(date,product,grade,movement,kg,reference,notes) "
                             "VALUES(?,?,?,?,?,?,?)")
            : QStringLiteral("INSERT INTO stock_movements(date,coffee_type,grade,movement,kg,reference,notes) "
                             "VALUES(?,?,?,?,?,?,?)"));
        query.addBindValue(QDate::currentDate().toString(Qt::ISODate));
        query.addBindValue(product);
        query.addBindValue(grade);
        query.addBindValue(movement);
        query.addBindValue(kg);
        query.addBindValue(reference);
        query.addBindValue(notes);
        run(query);
    }

    void addCash(const QString& kind, const QString& category, double amount,
                 const QString& reference, const QString& account) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("INSERT INTO cash_transactions(date,account,kind,category,amount,reference,description) "
                                     "VALUES(?,?,?,?,?,?,?)"));
        query.addBindValue(QDate::currentDate().toString(Qt::ISODate));
        query.addBindValue(account);
        query.addBindValue(kind);
        query.addBindValue(category);
        query.addBindValue(amount);
        query.addBindValue(reference);
        query.addBindValue(category);
        run(query);
    }

    void audit(const QString& username, const QString& action, const QString& details) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("INSERT INTO audit_log(date,username,action,details) VALUES(?,?,?,?)"));
        query.addBindValue(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        query.addBindValue(username);
        query.addBindValue(action);
        query.addBindValue(details);
        run(query);
    }

    double scalar(const QString& sql) {
        QSqlQuery query(db_);
        if (!query.exec(sql)) throw std::runtime_error(sqlError(query).toStdString());
        return query.next() ? query.value(0).toDouble() : 0;
    }

    double productStock(const QString& product) {
        QSqlQuery query(db_);
        query.prepare(product_.grain
            ? QStringLiteral("SELECT COALESCE(SUM(kg),0) FROM grain_stock_movements WHERE product=?")
            : QStringLiteral("SELECT COALESCE(SUM(kg),0) FROM stock_movements WHERE coffee_type=?"));
        query.addBindValue(product);
        run(query);
        return query.next() ? query.value(0).toDouble() : 0;
    }

    double balance(const QString& account) {
        QSqlQuery opening(db_);
        opening.prepare(QStringLiteral("SELECT month,cash,bank FROM monthly_opening_balances "
            "WHERE month<=strftime('%Y-%m','now','localtime') AND confirmed=1 ORDER BY month DESC LIMIT 1"));
        run(opening);
        QString startDate;
        double amount = 0;
        if (opening.next()) {
            startDate = opening.value(0).toString() + QStringLiteral("-01");
            amount = opening.value(account == QStringLiteral("Cash") ? 1 : 2).toDouble();
        } else {
            QSqlQuery settingQuery(db_);
            settingQuery.prepare(QStringLiteral("SELECT value FROM app_settings WHERE key=?"));
            settingQuery.addBindValue(account == QStringLiteral("Cash")
                ? QStringLiteral("OPENING_CASH") : QStringLiteral("OPENING_BANK"));
            run(settingQuery);
            if (settingQuery.next()) {
                bool ok = false;
                amount = settingQuery.value(0).toString().toDouble(&ok);
                if (!ok || !std::isfinite(amount))
                    throw std::runtime_error("Invalid opening account balance.");
            }
        }
        QSqlQuery movements(db_);
        movements.prepare(QStringLiteral("SELECT COALESCE(SUM(CASE WHEN kind='IN' THEN amount ELSE -amount END),0) "
            "FROM cash_transactions WHERE account=?%1")
            .arg(startDate.isEmpty() ? QString() : QStringLiteral(" AND date>=?")));
        movements.addBindValue(account);
        if (!startDate.isEmpty()) movements.addBindValue(startDate);
        run(movements);
        return amount + (movements.next() ? movements.value(0).toDouble() : 0);
    }

    void initialize() {
        execScript(db_, QStringLiteral(
            "CREATE TABLE IF NOT EXISTS suppliers(id INTEGER PRIMARY KEY,name TEXT NOT NULL,phone TEXT,id_no TEXT,address TEXT,notes TEXT,supplier_code TEXT);"
            "CREATE TABLE IF NOT EXISTS customers(id INTEGER PRIMARY KEY,name TEXT NOT NULL,phone TEXT,tin TEXT,address TEXT,notes TEXT,buyer_code TEXT);"
            "CREATE TABLE IF NOT EXISTS workers(id INTEGER PRIMARY KEY,name TEXT NOT NULL,phone TEXT,job TEXT,pay_type TEXT DEFAULT 'Salary',rate REAL DEFAULT 0,active INTEGER DEFAULT 1,worker_code TEXT,id_no TEXT,address TEXT,notes TEXT);"
            "CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY,username TEXT UNIQUE,pin_hash TEXT,role TEXT DEFAULT 'Owner',active INTEGER DEFAULT 1);"
            "CREATE TABLE IF NOT EXISTS app_settings(key TEXT PRIMARY KEY,value TEXT);"
            "CREATE TABLE IF NOT EXISTS cash_transactions(id INTEGER PRIMARY KEY,date TEXT,account TEXT,kind TEXT,category TEXT,amount REAL,reference TEXT,description TEXT);"
            "CREATE TABLE IF NOT EXISTS expenses(id INTEGER PRIMARY KEY,date TEXT,category TEXT,description TEXT,amount REAL,paid_from TEXT,reference TEXT);"
            "CREATE TABLE IF NOT EXISTS payroll(id INTEGER PRIMARY KEY,worker_id INTEGER,period TEXT,days REAL,overtime_hours REAL,gross REAL,paye REAL,deductions REAL,net REAL,paid REAL,date TEXT,notes TEXT,paid_from TEXT);"
            "CREATE TABLE IF NOT EXISTS tax_settings(id INTEGER PRIMARY KEY,name TEXT UNIQUE,value REAL);"
            "CREATE TABLE IF NOT EXISTS tax_payments(id INTEGER PRIMARY KEY,tax_type TEXT,period TEXT,amount REAL,date TEXT,reference TEXT,notes TEXT);"
            "CREATE TABLE IF NOT EXISTS stock_adjustments(id INTEGER PRIMARY KEY,date TEXT,coffee_type TEXT,grade TEXT,kg REAL,reason TEXT);"
            "CREATE TABLE IF NOT EXISTS milling(id INTEGER PRIMARY KEY,customer_id INTEGER,coffee_type TEXT,grade TEXT,kg_in REAL,kg_out REAL,loss_kg REAL,charge_per_kg REAL,total REAL,paid REAL,date TEXT,receipt_no TEXT,notes TEXT,received_account TEXT DEFAULT 'Cash',due_date TEXT);"
            "CREATE TABLE IF NOT EXISTS credit_payments(id INTEGER PRIMARY KEY AUTOINCREMENT,party_type TEXT NOT NULL,party_id INTEGER NOT NULL,purpose TEXT NOT NULL,amount REAL NOT NULL DEFAULT 0,account TEXT DEFAULT 'Cash',date TEXT NOT NULL,reference TEXT,notes TEXT);"
            "CREATE TABLE IF NOT EXISTS bank_transfers(id INTEGER PRIMARY KEY AUTOINCREMENT,receipt_number TEXT NOT NULL,deposit_date TEXT NOT NULL,teller TEXT,receipt_code TEXT,amount REAL NOT NULL DEFAULT 0,bank_name TEXT,notes TEXT,created_at TEXT);"
            "CREATE TABLE IF NOT EXISTS audit_log(id INTEGER PRIMARY KEY,date TEXT,username TEXT,action TEXT,details TEXT);"
            "CREATE TABLE IF NOT EXISTS company_milling(id INTEGER PRIMARY KEY AUTOINCREMENT,date TEXT NOT NULL,input_type TEXT NOT NULL,input_grade TEXT,kg_in REAL NOT NULL DEFAULT 0,output_type TEXT NOT NULL,output_grade TEXT,kg_out REAL NOT NULL DEFAULT 0,loss_kg REAL NOT NULL DEFAULT 0,notes TEXT,recorded_by TEXT,recorded_at TEXT);"
            "CREATE TABLE IF NOT EXISTS manager_reports(id INTEGER PRIMARY KEY AUTOINCREMENT,month TEXT NOT NULL,submitted_by TEXT NOT NULL,submitted_at TEXT NOT NULL,report_text TEXT NOT NULL,read_at TEXT,read_by TEXT,notes TEXT,title TEXT);"
            "CREATE TABLE IF NOT EXISTS person_links(id INTEGER PRIMARY KEY AUTOINCREMENT,supplier_id INTEGER,customer_id INTEGER,match_reason TEXT,created_at TEXT,UNIQUE(supplier_id,customer_id));"
            "CREATE TABLE IF NOT EXISTS person_link_blocks(supplier_id INTEGER,customer_id INTEGER,UNIQUE(supplier_id,customer_id));"
            "CREATE TABLE IF NOT EXISTS remote_devices(id INTEGER PRIMARY KEY AUTOINCREMENT,device_name TEXT,username TEXT,token_hash TEXT UNIQUE,linked_at TEXT,last_seen TEXT,active INTEGER DEFAULT 1);"
            "CREATE TABLE IF NOT EXISTS linked_phones(id INTEGER PRIMARY KEY AUTOINCREMENT,device_name TEXT NOT NULL,username TEXT NOT NULL,role TEXT NOT NULL,token_hash TEXT NOT NULL,public_key TEXT NOT NULL DEFAULT '',linked_at TEXT NOT NULL,last_seen TEXT,status TEXT NOT NULL DEFAULT 'ACTIVE');"
            "CREATE TABLE IF NOT EXISTS phone_link_codes(id INTEGER PRIMARY KEY AUTOINCREMENT,code_hash TEXT NOT NULL,username TEXT NOT NULL,role TEXT NOT NULL,created_at TEXT NOT NULL,expires_at TEXT NOT NULL,used_at TEXT);"
            "CREATE TABLE IF NOT EXISTS phone_messages(id INTEGER PRIMARY KEY AUTOINCREMENT,sender TEXT NOT NULL,sender_role TEXT NOT NULL,recipient TEXT NOT NULL,recipient_role TEXT NOT NULL,message TEXT NOT NULL,sent_at TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS phone_messages_e2ee(id INTEGER PRIMARY KEY AUTOINCREMENT,sender_device_id INTEGER NOT NULL,recipient_device_id INTEGER NOT NULL,payload TEXT NOT NULL,sent_at TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS monthly_opening_balances(month TEXT PRIMARY KEY,cash REAL NOT NULL DEFAULT 0,bank REAL NOT NULL DEFAULT 0,updated_at TEXT,updated_by TEXT,confirmed INTEGER DEFAULT 1);"));
        ensureColumn(QStringLiteral("linked_phones"), QStringLiteral("public_key"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
        ensureColumn(QStringLiteral("linked_phones"), QStringLiteral("is_desktop"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
        ensureColumn(QStringLiteral("phone_link_codes"), QStringLiteral("is_desktop"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
        ensureColumn(QStringLiteral("suppliers"), QStringLiteral("supplier_code"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("customers"), QStringLiteral("buyer_code"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("workers"), QStringLiteral("worker_code"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("workers"), QStringLiteral("id_no"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("workers"), QStringLiteral("address"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("workers"), QStringLiteral("notes"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("milling"), QStringLiteral("received_account"), QStringLiteral("TEXT DEFAULT 'Cash'"));
        ensureColumn(QStringLiteral("milling"), QStringLiteral("due_date"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("payroll"), QStringLiteral("paid_from"), QStringLiteral("TEXT DEFAULT 'Cash'"));
        ensureColumn(QStringLiteral("audit_log"), QStringLiteral("recorded_at"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("company_milling"), QStringLiteral("recorded_at"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("manager_reports"), QStringLiteral("notes"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("manager_reports"), QStringLiteral("title"), QStringLiteral("TEXT"));
        execScript(db_, QStringLiteral(
            "CREATE TABLE IF NOT EXISTS coffee_batches(id INTEGER PRIMARY KEY,supplier_id INTEGER,coffee_type TEXT,grade TEXT,kg REAL,price_per_kg REAL,total REAL,paid REAL,date TEXT,receipt_no TEXT,notes TEXT,payment_account TEXT DEFAULT 'Cash',due_date TEXT);"
            "CREATE TABLE IF NOT EXISTS milling(id INTEGER PRIMARY KEY,customer_id INTEGER,coffee_type TEXT,grade TEXT,kg_in REAL,kg_out REAL,loss_kg REAL,charge_per_kg REAL,total REAL,paid REAL,date TEXT,receipt_no TEXT,notes TEXT,received_account TEXT DEFAULT 'Cash',due_date TEXT);"
            "CREATE TABLE IF NOT EXISTS sales(id INTEGER PRIMARY KEY AUTOINCREMENT,customer_id INTEGER,coffee_type TEXT,grade TEXT,kg REAL,price_per_kg REAL,total REAL,received REAL,date TEXT,invoice_no TEXT,transport REAL DEFAULT 0,other_cost REAL DEFAULT 0,notes TEXT,received_account TEXT DEFAULT 'Cash',due_date TEXT);"
            "CREATE TABLE IF NOT EXISTS stock_movements(id INTEGER PRIMARY KEY,date TEXT,coffee_type TEXT,grade TEXT,movement TEXT,kg REAL,reference TEXT,notes TEXT);"
            "CREATE TABLE IF NOT EXISTS invoices(id INTEGER PRIMARY KEY AUTOINCREMENT,sale_id INTEGER,customer_id INTEGER,invoice_no TEXT,issue_date TEXT,due_date TEXT,amount REAL DEFAULT 0,status TEXT DEFAULT 'UNPAID',notes TEXT);"));
        if (product_.grain) {
            execScript(db_, QStringLiteral(
                "CREATE TABLE IF NOT EXISTS mill_purchases(id INTEGER PRIMARY KEY AUTOINCREMENT,supplier_id INTEGER,product TEXT,grade TEXT,kg REAL,price_per_kg REAL,total REAL,paid REAL,date TEXT,receipt_no TEXT,notes TEXT,payment_account TEXT DEFAULT 'Cash');"
                "CREATE TABLE IF NOT EXISTS mill_sales(id INTEGER PRIMARY KEY AUTOINCREMENT,customer_id INTEGER,product TEXT,grade TEXT,kg REAL,price_per_kg REAL,total REAL,received REAL,date TEXT,invoice_no TEXT,received_account TEXT DEFAULT 'Cash');"
                "CREATE TABLE IF NOT EXISTS grain_stock_movements(id INTEGER PRIMARY KEY AUTOINCREMENT,date TEXT,product TEXT,grade TEXT,movement TEXT,kg REAL,reference TEXT,notes TEXT);"
                "CREATE TABLE IF NOT EXISTS milling_runs(id INTEGER PRIMARY KEY AUTOINCREMENT,input_product TEXT,input_kg REAL,flour_kg REAL,feed_kg REAL,kyakyu_kg REAL,loss_kg REAL,date TEXT,operator TEXT);"));
        }
        ensureColumn(QStringLiteral("coffee_batches"), QStringLiteral("payment_account"), QStringLiteral("TEXT DEFAULT 'Cash'"));
        ensureColumn(QStringLiteral("coffee_batches"), QStringLiteral("due_date"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("milling"), QStringLiteral("coffee_type"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("milling"), QStringLiteral("grade"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("milling"), QStringLiteral("received_account"), QStringLiteral("TEXT DEFAULT 'Cash'"));
        ensureColumn(QStringLiteral("milling"), QStringLiteral("due_date"), QStringLiteral("TEXT"));
        ensureColumn(QStringLiteral("sales"), QStringLiteral("received_account"), QStringLiteral("TEXT DEFAULT 'Cash'"));
        ensureColumn(QStringLiteral("sales"), QStringLiteral("due_date"), QStringLiteral("TEXT"));
        // recorded_at stamp on every transaction table, exactly like Coffee.py (execSql: triggers contain ';').
        for (const QString& table : {QStringLiteral("suppliers"), QStringLiteral("customers"), QStringLiteral("workers"),
                                     QStringLiteral("coffee_batches"), QStringLiteral("milling"), QStringLiteral("sales"),
                                     QStringLiteral("cash_transactions"), QStringLiteral("expenses"), QStringLiteral("payroll"),
                                     QStringLiteral("tax_payments"), QStringLiteral("stock_adjustments"),
                                     QStringLiteral("stock_movements"), QStringLiteral("credit_payments"),
                                     QStringLiteral("invoices"), QStringLiteral("audit_log"), QStringLiteral("bank_transfers"),
                                     QStringLiteral("company_milling")}) {
            ensureColumn(table, QStringLiteral("recorded_at"), QStringLiteral("TEXT"));
            execSql(db_, QStringLiteral("CREATE TRIGGER IF NOT EXISTS trg_%1_recorded_at AFTER INSERT ON %1 FOR EACH ROW "
                                        "WHEN NEW.recorded_at IS NULL OR NEW.recorded_at='' BEGIN UPDATE %1 SET recorded_at = "
                                        "strftime('%Y-%m-%d %H:%M:%S','now','localtime') WHERE rowid = NEW.rowid; END").arg(table));
        }
        // One-time legacy clean-up, same rules as Coffee.py (BUY- codes -> CUS-, permanent codes, short product labels).
        execSql(db_, QStringLiteral("UPDATE customers SET buyer_code='CUS-'||substr(buyer_code,5) WHERE buyer_code LIKE 'BUY-%'"));
        execSql(db_, QStringLiteral("UPDATE suppliers SET supplier_code='SUP-'||printf('%04d',id) WHERE supplier_code IS NULL OR supplier_code=''"));
        execSql(db_, QStringLiteral("UPDATE customers SET buyer_code='CUS-'||printf('%04d',id) WHERE buyer_code IS NULL OR buyer_code=''"));
        execSql(db_, QStringLiteral("UPDATE workers SET worker_code='WKR-'||printf('%04d',id) WHERE worker_code IS NULL OR worker_code=''"));
        ensureColumn(QStringLiteral("users"), QStringLiteral("active"), QStringLiteral("INTEGER DEFAULT 1"));
        ensureColumn(QStringLiteral("monthly_opening_balances"), QStringLiteral("confirmed"), QStringLiteral("INTEGER DEFAULT 1"));
        QSqlQuery activate(db_);
        if (!activate.exec(QStringLiteral("UPDATE users SET role='Admin' WHERE role IN ('Owner','Administrator')")))
            throw std::runtime_error(sqlError(activate).toStdString());
        QSqlQuery owner(db_);
        if (!owner.exec(QStringLiteral("SELECT id FROM users WHERE username='owner'")))
            throw std::runtime_error(sqlError(owner).toStdString());
        if (!owner.next()) {
            QSqlQuery add(db_);
            add.prepare(QStringLiteral("INSERT OR IGNORE INTO users(username,pin_hash,role,active) VALUES('owner',?,'Admin',1)"));
            add.addBindValue(hashPin(QStringLiteral("1234")));
            run(add);
        }
        execSql(db_, QStringLiteral("INSERT OR IGNORE INTO users(username,pin_hash,role,active) VALUES('guest','','Guest',1)"));
        const QString allGrain = product_.saleProducts().join(QLatin1Char(','));
        const auto seed = [this](const QString& key, const QString& value) {
            QSqlQuery q(db_);
            q.prepare(QStringLiteral("INSERT OR IGNORE INTO app_settings(key,value) VALUES(?,?)"));
            q.addBindValue(key); q.addBindValue(value); run(q);
        };
        seed(QStringLiteral("BUY_ALLOWED_TYPES"), product_.grain ? allGrain : QStringLiteral("Kasse,Kibooko"));
        seed(QStringLiteral("MILL_ALLOWED_TYPES"), product_.grain ? allGrain : QStringLiteral("Kibooko"));
        seed(QStringLiteral("SELL_ALLOWED_TYPES"), product_.grain ? allGrain : QStringLiteral("Kasse"));
        seed(QStringLiteral("SELL_ONLY_KASSE"), product_.grain ? QStringLiteral("0") : QStringLiteral("1"));
        for (const QString& kind : {QStringLiteral("BUY"), QStringLiteral("SELL")})
            for (const QString& t : (product_.grain ? product_.saleProducts() : QStringList{"Kakuta", "Kibooko", "Kasse"}))
                for (const QString& g : (product_.grain ? QStringList{"Standard"} : QStringList{"Grade A", "Grade B"}))
                    seed(kind + QStringLiteral("_PRICE::") + t + QStringLiteral("::") + g, QStringLiteral("0"));
        execSql(db_, QStringLiteral("INSERT OR IGNORE INTO app_settings(key,value) VALUES('OPENING_CASH','0')"));
        execSql(db_, QStringLiteral("INSERT OR IGNORE INTO app_settings(key,value) VALUES('OPENING_BANK','0')"));
        execSql(db_, QStringLiteral("INSERT OR IGNORE INTO app_settings(key,value) VALUES('business_name','Mateete Coffee Mill')"));
        execSql(db_, QStringLiteral("INSERT OR IGNORE INTO app_settings(key,value) VALUES('location','Mateete, Sembabule District, Uganda')"));
        execSql(db_, QStringLiteral("INSERT OR IGNORE INTO tax_settings(name,value) VALUES('VAT_RATE',0.18)"));
        execSql(db_, QStringLiteral("INSERT OR IGNORE INTO tax_settings(name,value) VALUES('VAT_REGISTERED',0)"));
        execSql(db_, QStringLiteral("INSERT OR IGNORE INTO tax_settings(name,value) VALUES('WITHHOLDING_RATE',0)"));
        execSql(db_, QStringLiteral("INSERT OR IGNORE INTO tax_settings(name,value) VALUES('OTHER_TAX_RATE',0)"));
        if (legacyGrainSharedSchema_)
            migrateLegacyGrainData();
    }

    void migrateLegacyGrainData() {
        transaction([&] {
            execSql(db_, QStringLiteral(
                "INSERT OR IGNORE INTO mill_purchases"
                "(id,supplier_id,product,grade,kg,price_per_kg,total,paid,date,receipt_no,notes,payment_account) "
                "SELECT id,supplier_id,coffee_type,grade,kg,price_per_kg,total,paid,date,receipt_no,notes,payment_account "
                "FROM coffee_batches"));
            execSql(db_, QStringLiteral(
                "INSERT OR IGNORE INTO mill_sales"
                "(id,customer_id,product,grade,kg,price_per_kg,total,received,date,invoice_no,received_account) "
                "SELECT id,customer_id,coffee_type,grade,kg,price_per_kg,total,received,date,invoice_no,received_account "
                "FROM sales"));
            execSql(db_, QStringLiteral(
                "INSERT OR IGNORE INTO grain_stock_movements"
                "(id,date,product,grade,movement,kg,reference,notes) "
                "SELECT id,date,coffee_type,grade,movement,kg,reference,notes FROM stock_movements"));
            execSql(db_, QStringLiteral(
                "INSERT OR IGNORE INTO milling_runs"
                "(id,input_product,input_kg,flour_kg,feed_kg,kyakyu_kg,loss_kg,date,operator) "
                "SELECT id,input_type,kg_in,"
                "CASE WHEN lower(output_type) LIKE '%flour%' THEN kg_out ELSE 0 END,"
                "CASE WHEN lower(output_type) LIKE '%feed%' OR lower(output_type) LIKE '%broken%' THEN kg_out ELSE 0 END,"
                "CASE WHEN lower(output_type) LIKE '%kyakyu%' THEN kg_out ELSE 0 END,"
                "loss_kg,date,COALESCE(recorded_by,'') FROM company_milling"));
        });
    }

    void ensureColumn(const QString& table, const QString& column, const QString& definition) {
        QSqlQuery info(db_);
        if (!info.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
            throw std::runtime_error(sqlError(info).toStdString());
        while (info.next()) {
            if (info.value(1).toString() == column) return;
        }
        execSql(db_, QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, definition));
    }

    Product product_;
    QSqlDatabase db_;
    bool legacyGrainSharedSchema_ = false;
};

class PhoneServer : public QTcpServer {
public:
    PhoneServer(Store& store, Product product, QObject* parent = nullptr)
        : QTcpServer(parent), store_(store), product_(product) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket* socket = nextPendingConnection();
                buffers_.insert(socket, {});
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { receive(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    buffers_.remove(socket);
                    socket->deleteLater();
                });
            }
        });
    }

    QString addressText(quint16 port) const {
        QStringList tailscaleUrls;
        QStringList otherUrls;
        for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
            if (!(interface.flags() & QNetworkInterface::IsUp) ||
                (interface.flags() & QNetworkInterface::IsLoopBack))
                continue;
            const bool tailscale = interface.name().contains(QStringLiteral("tailscale"), Qt::CaseInsensitive) ||
                interface.humanReadableName().contains(QStringLiteral("tailscale"), Qt::CaseInsensitive);
            for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
                const QHostAddress address = entry.ip();
                if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback())
                    continue;
                const QString url = QStringLiteral("http://%1:%2").arg(address.toString()).arg(port);
                (tailscale ? tailscaleUrls : otherUrls).push_back(url);
            }
        }
        QStringList urls = tailscaleUrls + otherUrls;
        if (urls.isEmpty()) return QStringLiteral("No network IPv4 address is available.");
        QString result = urls.join(QLatin1Char('\n'));
        if (!tailscaleUrls.isEmpty())
            result.prepend(QStringLiteral("Tailscale (recommended):\n"));
        return result;
    }

    bool start(quint16 port) {
        if (isListening()) close();
        return listen(QHostAddress::AnyIPv4, port);
    }

private:
    void receive(QTcpSocket* socket) {
        QByteArray& buffer = buffers_[socket];
        buffer += socket->readAll();
        if (buffer.size() > kMaxRequestBytes) {
            respond(socket, 413, QByteArrayLiteral("{\"error\":\"Request too large\"}"));
            return;
        }
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QByteArray headers = buffer.left(headerEnd);
        qsizetype contentLength = 0;
        for (const QByteArray& line : headers.split('\n')) {
            const QByteArray lower = line.trimmed().toLower();
            if (lower.startsWith("content-length:")) contentLength = lower.mid(15).trimmed().toLongLong();
        }
        if (contentLength < 0 || contentLength > kMaxRequestBytes) {
            respond(socket, 413, QByteArrayLiteral("{\"error\":\"Request too large\"}"));
            return;
        }
        if (buffer.size() < headerEnd + 4 + contentLength) return;
        const QList<QByteArray> lines = headers.split('\n');
        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        if (requestLine.size() < 2) {
            respond(socket, 400, QByteArrayLiteral("{\"error\":\"Bad request\"}"));
            return;
        }
        const QByteArray method = requestLine[0];
        const QList<QByteArray> targetParts = requestLine[1].split('?');
        const QByteArray path = targetParts.value(0);
        const QByteArray rawQuery = targetParts.value(1);
        const QByteArray body = buffer.mid(headerEnd + 4, contentLength);
        const QByteArray cookie = findHeader(headers, "cookie");
        const QString token = sessionToken(cookie);
        try {
            route(socket, method, path, rawQuery, body, token);
        } catch (const InputError& error) {
            respond(socket, 400, QJsonDocument(QJsonObject{{QStringLiteral("error"), QString::fromUtf8(error.what())}}).toJson(QJsonDocument::Compact));
        } catch (const std::exception& error) {
            respond(socket, 500, QJsonDocument(QJsonObject{{QStringLiteral("error"), QString::fromUtf8(error.what())}}).toJson(QJsonDocument::Compact));
        }
    }

    static QByteArray findHeader(const QByteArray& headers, const QByteArray& name) {
        for (const QByteArray& line : headers.split('\n')) {
            const QByteArray value = line.trimmed();
            if (value.toLower().startsWith(name.toLower() + ':'))
                return value.mid(name.size() + 1).trimmed();
        }
        return {};
    }

    static QString sessionToken(const QByteArray& cookie) {
        for (const QByteArray& part : cookie.split(';')) {
            const QByteArray item = part.trimmed();
            if (item.startsWith("mill_phone=")) return QString::fromLatin1(item.mid(11));
        }
        return {};
    }

    void route(QTcpSocket* socket, const QByteArray& method, const QByteArray& path,
               const QByteArray& rawQuery, const QByteArray& body, const QString& token) {
        const QJsonObject json = QJsonDocument::fromJson(body).object();
        if (method == "GET" && path == "/") {
            respond(socket, 200, phonePage().toUtf8(), "text/html; charset=utf-8");
            return;
        }
        if (method == "POST" && path == "/api/pair") {
            const QString address = socket->peerAddress().toString();
            const auto failure = loginFailures_.value(address);
            if (failure.second > QDateTime::currentDateTime() && failure.first >= 5) {
                respond(socket, 429, QByteArrayLiteral("{\"error\":\"Too many attempts; wait five minutes\"}"));
                return;
            }
            const QString code = json.value(QStringLiteral("code")).toString().trimmed();
            const QString device = json.value(QStringLiteral("device")).toString().trimmed();
            try {
                const QString phoneToken = store_.pairPhone(
                    code, device, json.value(QStringLiteral("publicKey")).toString());
                loginFailures_.remove(address);
                respond(socket, 200, QByteArrayLiteral("{\"ok\":true}"), "application/json",
                        "mill_phone=" + phoneToken.toLatin1() +
                        "; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=31536000");
            } catch (const InputError&) {
                auto state = failure;
                const QDateTime now = QDateTime::currentDateTime();
                if (!state.second.isValid() || state.second <= now) state = {0, now.addSecs(300)};
                ++state.first;
                loginFailures_.insert(address, state);
                throw;
            }
            return;
        }
        if (method == "GET" && path == "/api/session") {
            PhoneSession session;
            if (!store_.phoneSession(token, session)) {
                respond(socket, 401, QByteArrayLiteral("{\"error\":\"Pair this phone using a current link code\"}"));
                return;
            }
            respond(socket, 200, QJsonDocument(QJsonObject{{QStringLiteral("username"), session.username},
                                                           {QStringLiteral("deviceId"), session.id},
                                                           {QStringLiteral("role"), session.role},
                                                           {QStringLiteral("desktopChat"), session.desktopChat},
                                                           {QStringLiteral("publicKey"), session.publicKey},
                                                           {QStringLiteral("dashboard"), store_.dashboard()}})
                                    .toJson(QJsonDocument::Compact));
            return;
        }
        PhoneSession session;
        if (!store_.phoneSession(token, session)) {
            respond(socket, 401, QByteArrayLiteral("{\"error\":\"Pair this phone using a current link code\"}"));
            return;
        }
        if (method == "GET" && path == "/api/contacts") {
            respond(socket, 200, QJsonDocument(store_.phoneContacts(session)).toJson(QJsonDocument::Compact));
            return;
        }
        if (method == "GET" && path == "/api/reports") {
            const QUrlQuery query(QString::fromUtf8(rawQuery));
            const QDate from = QDate::fromString(query.queryItemValue(QStringLiteral("from")), Qt::ISODate);
            const QDate to = QDate::fromString(query.queryItemValue(QStringLiteral("to")), Qt::ISODate);
            const QString type = query.queryItemValue(QStringLiteral("type"));
            const QString search = query.queryItemValue(QStringLiteral("search"));
            respond(socket, 200, QJsonDocument(store_.phoneReports(from, to, type, search))
                                    .toJson(QJsonDocument::Compact));
            return;
        }
        if (method == "GET" && path == "/api/messages") {
            const QUrlQuery query(QString::fromUtf8(rawQuery));
            bool validAfter = false;
            bool validContact = false;
            const qint64 after = query.queryItemValue(QStringLiteral("after")).toLongLong(&validAfter);
            const qint64 contact = query.queryItemValue(QStringLiteral("contact")).toLongLong(&validContact);
            if (!query.hasQueryItem(QStringLiteral("after")) || !validAfter || after < 0 ||
                !query.hasQueryItem(QStringLiteral("contact")) || !validContact || contact <= 0) {
                respond(socket, 400, QByteArrayLiteral("{\"error\":\"Invalid message cursor or contact\"}"));
                return;
            }
            respond(socket, 200, QJsonDocument(store_.phoneMessages(session, after, contact))
                                    .toJson(QJsonDocument::Compact));
            return;
        }
        if (method == "POST" && path == "/api/messages") {
            const QJsonValue recipientValue = json.value(QStringLiteral("recipientDeviceId"));
            const double recipientNumber = recipientValue.toDouble(-1);
            if (!recipientValue.isDouble() || !std::isfinite(recipientNumber) ||
                std::floor(recipientNumber) != recipientNumber || recipientNumber < 1 ||
                recipientNumber > 9007199254740991.0)
                throw InputError("The recipient device ID is invalid.");
            store_.sendPhoneMessage(session,
                static_cast<qint64>(recipientNumber),
                json.value(QStringLiteral("envelopeToRecipient")).toObject(),
                json.value(QStringLiteral("envelopeToSender")).toObject());
            respond(socket, 201, QByteArrayLiteral("{\"ok\":true}"));
            return;
        }
        if (method == "POST" && path.startsWith("/api/") &&
            session.role == QStringLiteral("Admin")) {
            respond(socket, 403,
                    QByteArrayLiteral("{\"error\":\"Admin phone links are view-only; use a Manager phone to enter records.\"}"));
            return;
        }
        if (method == "POST" && path == "/api/purchases") {
            store_.savePurchase(session.username, json.value("party").toString(), json.value("product").toString(),
                                json.value("grade").toString(), number(json, "kg"), number(json, "price"),
                                number(json, "paid"), json.value("account").toString("Cash"));
            respond(socket, 201, QByteArrayLiteral("{\"ok\":true}"));
            return;
        }
        if (method == "POST" && path == "/api/sales") {
            store_.saveSale(session.username, json.value("party").toString(), json.value("product").toString(),
                            json.value("grade").toString(), number(json, "kg"), number(json, "price"),
                            number(json, "paid"), json.value("account").toString("Cash"));
            respond(socket, 201, QByteArrayLiteral("{\"ok\":true}"));
            return;
        }
        if (method == "POST" && path == "/api/milling" && product_.grain) {
            store_.saveMilling(session.username, json.value("product").toString(), number(json, "kg"),
                               number(json, "flour"), number(json, "feed"), number(json, "kyakyu"));
            respond(socket, 201, QByteArrayLiteral("{\"ok\":true}"));
            return;
        }
        if (method == "POST" && path == "/api/milling" && !product_.grain) {
            store_.saveCoffeeMilling(session.username, json.value("party").toString(),
                json.value("product").toString(), number(json, "kg"), number(json, "output"),
                number(json, "charge"), number(json, "paid"), json.value("account").toString("Cash"));
            respond(socket, 201, QByteArrayLiteral("{\"ok\":true}"));
            return;
        }
        if (method == "POST" && path == "/api/money") {
            store_.saveMoney(session.username, json.value("account").toString(),
                json.value("kind").toString(), json.value("category").toString(),
                number(json, "amount"), QDate::currentDate(), json.value("reference").toString(),
                json.value("description").toString());
            respond(socket, 201, QByteArrayLiteral("{\"ok\":true}"));
            return;
        }
        if (method == "POST" && path == "/api/credit") {
            const QString partyType = json.value("partyType").toString();
            store_.saveCreditPayment(session.username, partyType,
                store_.personId(partyType, json.value("party").toString()), number(json, "amount"),
                json.value("account").toString("Cash"), QDate::currentDate(),
                json.value("reference").toString(), json.value("notes").toString());
            respond(socket, 201, QByteArrayLiteral("{\"ok\":true}"));
            return;
        }
        if (method == "POST" && path == "/api/bank-deposit") {
            store_.saveBankDeposit(session.username, json.value("receipt").toString(),
                QDate::currentDate(), json.value("teller").toString(), json.value("receiptCode").toString(),
                number(json, "amount"), json.value("bank").toString(), json.value("notes").toString());
            respond(socket, 201, QByteArrayLiteral("{\"ok\":true}"));
            return;
        }
        respond(socket, 404, QByteArrayLiteral("{\"error\":\"Not found\"}"));
    }

    static double number(const QJsonObject& json, const char* key) {
        const QJsonValue value = json.value(QString::fromLatin1(key));
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0)
            throw InputError((QStringLiteral("Invalid numeric field: ") + QString::fromLatin1(key)).toStdString());
        return value.toDouble();
    }

    static void respond(QTcpSocket* socket, int status, const QByteArray& body,
                        const QByteArray& type = "application/json",
                        const QByteArray& cookie = {}) {
        const QByteArray reason = status == 200 ? "OK" : status == 201 ? "Created" :
            status == 400 ? "Bad Request" : status == 401 ? "Unauthorized" :
            status == 403 ? "Forbidden" :
            status == 404 ? "Not Found" : status == 409 ? "Conflict" :
            status == 413 ? "Payload Too Large" : status == 429 ? "Too Many Requests" :
            status == 500 ? "Internal Server Error" : "Error";
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + reason +
            "\r\nConnection: close\r\nContent-Type: " + type +
            "\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'\r\nCache-Control: no-store\r\n";
        if (!cookie.isEmpty()) response += "Set-Cookie: " + cookie + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QString phonePage() const {
        QString products;
        for (const QString& item : store_.availableSaleProducts())
            products += QStringLiteral("<option>%1</option>").arg(item.toHtmlEscaped());
        QString purchaseProducts;
        for (const QString& item : store_.availablePurchaseProducts())
            purchaseProducts += QStringLiteral("<option>%1</option>").arg(item.toHtmlEscaped());
        const QString milling = product_.grain ? QStringLiteral(
            "<section><h2>Mill maize or cassava</h2><form id='mill'>"
            "<label>Input grain<select name='product'><option>Maize</option><option>Cassava</option></select></label>"
            "<label>Input kg<input name='kg' type='number' min='0.01' step='any' required></label>"
            "<label>Flour kg<input name='flour' type='number' min='0' step='any' value='0' required></label>"
            "<label>Chicken feed (broken/bran) kg<input name='feed' type='number' min='0' step='any' value='0' required></label>"
            "<label>Kyakyu kg<input name='kyakyu' type='number' min='0' step='any' value='0' required></label>"
            "<button>Save milling run</button></form></section>") : QString();
        const QString coffeeMilling = product_.grain ? QString() : QStringLiteral(
            "<section><h2>Customer coffee milling</h2><form id='coffeeMill'>"
            "<label>Customer<input name='party' required></label>"
            "<label>Coffee type<select name='product'>%1</select></label>"
            "<label>Input kg<input name='kg' type='number' min='0.01' step='any' required></label>"
            "<label>Output kg<input name='output' type='number' min='0' step='any' required></label>"
            "<label>Charge per kg (UGX)<input name='charge' type='number' min='0' step='any' required></label>"
            "<label>Amount received (UGX)<input name='paid' type='number' min='0' step='any' value='0' required></label>"
            "<label>Received into<select name='account'><option>Cash</option><option>Bank</option></select></label>"
            "<button>Save milling job</button></form></section>").arg(
                [&store = store_] {
                    QString options;
                    for (const QString& item : store.availableMillingProducts())
                        options += QStringLiteral("<option>%1</option>").arg(item.toHtmlEscaped());
                    return options;
                }());
        const QString moneyForm = QStringLiteral(
            "<section><h2>Record money in or out</h2><form id='money'>"
            "<label>Account<select name='account'><option>Cash</option><option>Bank</option></select></label>"
            "<label>Direction<select name='kind'><option>OUT</option><option>IN</option></select></label>"
            "<label>Category<input name='category' value='Other expense' required></label>"
            "<label>Amount (UGX)<input name='amount' type='number' min='0.01' step='any' required></label>"
            "<label>Reference<input name='reference'></label><label>Description<input name='description'></label>"
            "<button>Save money entry</button></form></section>");
        const QString creditForm = QStringLiteral(
            "<section><h2>Settle credit</h2><form id='credit'>"
            "<label>Party type<select name='partyType'><option>Customer</option><option>Supplier</option></select></label>"
            "<label>Registered customer/supplier ID or name<input name='party' required></label>"
            "<label>Amount (UGX)<input name='amount' type='number' min='0.01' step='any' required></label>"
            "<label>Account<select name='account'><option>Cash</option><option>Bank</option></select></label>"
            "<label>Reference<input name='reference'></label><label>Notes<input name='notes'></label>"
            "<button>Record credit payment</button></form></section>");
        const QString depositForm = QStringLiteral(
            "<section><h2>Record bank deposit</h2><form id='deposit'>"
            "<label>Receipt number<input name='receipt' required></label>"
            "<label>Teller<input name='teller'></label><label>Receipt code<input name='receiptCode'></label>"
            "<label>Amount (UGX)<input name='amount' type='number' min='0.01' step='any' required></label>"
            "<label>Bank<input name='bank' required></label><label>Notes<input name='notes'></label>"
            "<button>Save bank deposit</button></form></section>");
        const QString chatPanel = QStringLiteral(
            "<section id='chat'><h2>Linked-device chat</h2>"
            "<p id='chatStatus' class='status'>Choose a linked device below to open its private chat.</p>"
            "<div id='chatList' aria-label='Linked-device conversations'></div>"
            "<label>Chat with<select id='chatContact'></select></label>"
            "<p id='chatFingerprint'>Verify a contact's encryption fingerprint with them over a separate trusted channel.</p>"
            "<div id='chatMessages' aria-live='polite'></div>"
            "<form id='chatForm'><label>Message<textarea name='message' maxlength='2000' rows='3' required></textarea></label>"
            "<button id='sendMessage' disabled>Send message</button></form></section>");
        return QStringLiteral(R"HTML(<!doctype html><html><meta name="viewport" content="width=device-width,initial-scale=1"><meta charset="utf-8">
<title>%1 phone access</title><style>body{font:16px system-ui;max-width:1000px;margin:auto;padding:16px;background:#f3f6f4;color:#1f2933}h1{color:#174a2d}section,form{background:white;padding:16px;margin:14px 0;border-radius:10px}label{display:block;margin:10px 0}input,select,button{box-sizing:border-box;width:100%;padding:12px;margin-top:5px;font:inherit}button{background:#286b43;color:white;border:0;border-radius:6px}button:disabled{opacity:.5}#app{display:none}.status{color:#286b43}.table-scroll{overflow:auto}table{border-collapse:collapse;width:100%;min-width:760px}th,td{padding:8px;border:1px solid #ccd5cf;text-align:left}th{background:#e8efea}td.num{text-align:right;white-space:nowrap}#chatMessages{max-height:340px;min-height:100px;overflow:auto;background:#f8faf9;border:1px solid #d6dfd9;border-radius:6px;padding:8px}.message{padding:8px;border-bottom:1px solid #ddd;white-space:pre-wrap;overflow-wrap:anywhere}.filters{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}#chatList{display:flex;flex-wrap:wrap;gap:8px;margin:12px 0}#chatList button{width:auto;margin:0}.chatChoice.active{outline:3px solid #86bd98;background:#174a2d}</style>
<h1>%1</h1><p>Enter the one-time eight-digit code from the desktop. The code automatically links this phone as Admin or Manager; no role selection is needed here. Open this page using the HTTPS URL from Tailscale Serve.</p>
<section id="pair"><h2>Link this phone</h2><form id="pairForm"><label>Phone name<input name="device" autocomplete="off" placeholder="My phone" required></label><label>Eight-digit link code<input name="code" inputmode="numeric" maxlength="8" pattern="[0-9]{8}" required></label><button>Link phone</button></form><p id="pairStatus" class="status"></p><p>For encrypted chat, open this app using HTTPS through Tailscale Serve so your browser can use its secure cryptography.</p></section>
<div id="app"><p id="who" class="status"></p><p id="totals"></p>
%9
<section id="recordsPanel"><h2>Business records and reports</h2><p>Read-only view of records from the desktop business database.</p><div class="filters"><label>Report type<select id="reportType"><option>All</option><option>Purchase</option><option>Sale</option><option>Milling</option><option>Expense</option><option>Money</option><option>Payroll</option><option>Tax Payment</option><option>Credit Payment</option><option>Bank Deposit</option><option>Company Milling</option><option>Customer Balances</option><option>Supplier Balances</option><option>Open Credits</option><option>Stock Balance</option><option>Daily Cash &amp; Bank</option><option>Monthly Cash Flow</option><option>Profit Summary</option><option>Activity Log</option></select></label><label>From<input id="reportFrom" type="date"></label><label>To<input id="reportTo" type="date"></label><label>Search<input id="reportSearch" maxlength="100" placeholder="Name, product, or reference"></label></div><button id="loadReports" type="button">Refresh records</button><p id="reportStatus" class="status" aria-live="polite"></p><div class="table-scroll"><table id="reportTable"><thead><tr><th>Date</th><th>Type</th><th>Reference</th><th>Party / details</th><th>Product</th><th>Grade / account</th><th>Weight / output</th><th>Total</th><th>Paid</th></tr></thead><tbody></tbody></table></div></section>
<div id="managerActions">
<section><h2>Record purchase</h2><form id="purchase"><label>Supplier<input name="party" required></label><label>Product<select name="product">%2</select></label><label>Grade<input name="grade" value="Grade A"></label><label>Weight (kg)<input name="kg" type="number" min="0.01" step="any" required></label><label>Price per kg (UGX)<input name="price" type="number" min="0" step="any" required></label><label>Amount paid (UGX)<input name="paid" type="number" min="0" step="any" value="0" required></label><label>Paid from<select name="account"><option>Cash</option><option>Bank</option></select></label><button>Save purchase</button></form></section>
<section><h2>Record sale</h2><form id="sale"><label>Customer<input name="party" required></label><label>Product<select name="product">%3</select></label><label>Grade<input name="grade" value="Grade A"></label><label>Weight (kg)<input name="kg" type="number" min="0.01" step="any" required></label><label>Price per kg (UGX)<input name="price" type="number" min="0" step="any" required></label><label>Amount received (UGX)<input name="paid" type="number" min="0" step="any" value="0" required></label><label>Received into<select name="account"><option>Cash</option><option>Bank</option></select></label><button>Save sale</button></form></section>%4%5%6%7%8</div>
</div>
<script>
async function api(url,data){let r=await fetch(url,{method:data?'POST':'GET',headers:data?{'Content-Type':'application/json'}:{},body:data?JSON.stringify(data):undefined});let j=await r.json();if(!r.ok)throw Error(j.error||'Request failed');return j}
function formData(f){return Object.fromEntries(new FormData(f).entries())}
function b64(bytes){let s='';for(let b of new Uint8Array(bytes))s+=String.fromCharCode(b);return btoa(s)}
function unb64(value){return Uint8Array.from(atob(value),c=>c.charCodeAt(0))}
function keyStore(action,id,value){return new Promise((resolve,reject)=>{let open=indexedDB.open('MillManagerPhoneKeys',1);open.onupgradeneeded=()=>open.result.createObjectStore('keys');open.onerror=()=>reject(open.error);open.onsuccess=()=>{let db=open.result,tx=db.transaction('keys',action==='get'?'readonly':'readwrite'),store=tx.objectStore('keys'),req=action==='get'?store.get(String(id)):store.put(value,String(id));req.onsuccess=()=>resolve(req.result);req.onerror=()=>reject(req.error);tx.oncomplete=()=>db.close()}})}
async function makeKeyPair(){return crypto.subtle.generateKey({name:'ECDH',namedCurve:'P-256'},false,['deriveKey'])}
async function publicKeyText(pair){return b64(await crypto.subtle.exportKey('spki',pair.publicKey))}
async function sharedKey(privateKey,publicText){let publicKey=await crypto.subtle.importKey('spki',unb64(publicText),{name:'ECDH',namedCurve:'P-256'},false,[]);return crypto.subtle.deriveKey({name:'ECDH',public:publicKey},privateKey,{name:'AES-GCM',length:256},false,['encrypt','decrypt'])}
async function encryptFor(privateKey,publicText,senderId,recipientId,message){let key=await sharedKey(privateKey,publicText),iv=crypto.getRandomValues(new Uint8Array(12)),aad=new TextEncoder().encode(senderId+':'+recipientId),ciphertext=await crypto.subtle.encrypt({name:'AES-GCM',iv,additionalData:aad},key,new TextEncoder().encode(message));return {iv:b64(iv),ciphertext:b64(ciphertext)}}
async function decryptFrom(privateKey,publicText,senderId,recipientId,envelope){let key=await sharedKey(privateKey,publicText),aad=new TextEncoder().encode(senderId+':'+recipientId),plain=await crypto.subtle.decrypt({name:'AES-GCM',iv:unb64(envelope.iv),additionalData:aad},key,unb64(envelope.ciphertext));return new TextDecoder().decode(plain)}
async function fingerprint(publicText){let digest=new Uint8Array(await crypto.subtle.digest('SHA-256',unb64(publicText)));return Array.from(digest,b=>b.toString(16).padStart(2,'0')).join('').match(/.{1,4}/g).join(' ')}
let sessionInfo=null,contacts=[],chatCursor=0,chatBusy=false,chatSelection=0;
async function refresh(){try{let s=await api('/api/session');sessionInfo=s;document.getElementById('pair').style.display='none';document.getElementById('app').style.display='block';document.getElementById('who').textContent=s.desktopChat?'Secure desktop chat · '+s.username+' ('+s.role+')':'Linked as: '+s.username+' ('+s.role+')'+(s.role==='Admin'?' — view only':' — record entry enabled');document.getElementById('totals').hidden=s.desktopChat;document.getElementById('recordsPanel').hidden=s.desktopChat;document.getElementById('managerActions').style.display=s.desktopChat?'none':(s.role==='Manager'?'block':'none');let from=new Date();from.setFullYear(from.getFullYear()-1);if(!document.getElementById('reportFrom').value)document.getElementById('reportFrom').value=from.toISOString().slice(0,10);if(!document.getElementById('reportTo').value)document.getElementById('reportTo').value=new Date().toISOString().slice(0,10)}catch(e){}}
document.getElementById('pairForm').onsubmit=async e=>{e.preventDefault();let status=document.getElementById('pairStatus');status.textContent='';try{if(!isSecureContext||!crypto.subtle||!indexedDB)throw Error('Encrypted linking requires HTTPS through Tailscale Serve or a secure local browser.');let pair=await makeKeyPair(),d=formData(e.target);d.publicKey=await publicKeyText(pair);await api('/api/pair',d);let s=await api('/api/session');await keyStore('put','device:'+s.deviceId,pair.privateKey);e.target.reset();await refresh();await loadContacts();await loadRecords()}catch(x){status.textContent='Link failed: '+x.message}};
const desktopPairCode=location.hash.startsWith('#desktop-pair=')?location.hash.slice('#desktop-pair='.length):'';
if(/^\d{8}$/.test(desktopPairCode)){document.querySelector('#pairForm [name=device]').value='Desktop chat';document.querySelector('#pairForm [name=code]').value=desktopPairCode;document.querySelector('#pair h2').textContent='Link this desktop chat';document.querySelector('body>p').textContent='This local browser is being linked as a separate secure chat participant.';history.replaceState(null,'',location.pathname+location.search);setTimeout(()=>document.getElementById('pairForm').requestSubmit(),0)}
for(let id of ['purchase','sale'])document.getElementById(id).onsubmit=async e=>{e.preventDefault();let d=formData(e.target);for(let k of ['kg','price','paid'])d[k]=Number(d[k]);if(d.paid>d.kg*d.price&&!confirm('The payment is more than the total. Continue?'))return;try{await api('/api/'+(id==='purchase'?'purchases':'sales'),d);alert('Saved');e.target.reset();await refresh()}catch(x){alert(x.message)}};
let mill=document.getElementById('mill');if(mill)mill.onsubmit=async e=>{e.preventDefault();let d=formData(e.target);for(let k of ['kg','flour','feed','kyakyu'])d[k]=Number(d[k]);try{await api('/api/milling',d);alert('Milling run saved');e.target.reset();await refresh()}catch(x){alert(x.message)}};
let coffeeMill=document.getElementById('coffeeMill');if(coffeeMill)coffeeMill.onsubmit=async e=>{e.preventDefault();let d=formData(e.target);for(let k of ['kg','output','charge','paid'])d[k]=Number(d[k]);try{await api('/api/milling',d);alert('Milling job saved');e.target.reset();await refresh()}catch(x){alert(x.message)}};
let money=document.getElementById('money');if(money)money.onsubmit=async e=>{e.preventDefault();let d=formData(e.target);d.amount=Number(d.amount);try{await api('/api/money',d);alert('Money entry saved');e.target.reset();await refresh()}catch(x){alert(x.message)}};
let credit=document.getElementById('credit');if(credit)credit.onsubmit=async e=>{e.preventDefault();let d=formData(e.target);d.amount=Number(d.amount);try{await api('/api/credit',d);alert('Credit payment saved');e.target.reset();await refresh()}catch(x){alert(x.message)}};
let deposit=document.getElementById('deposit');if(deposit)deposit.onsubmit=async e=>{e.preventDefault();let d=formData(e.target);d.amount=Number(d.amount);try{await api('/api/bank-deposit',d);alert('Bank deposit saved');e.target.reset();await refresh()}catch(x){alert(x.message)}};
const chatStatus=document.getElementById('chatStatus'),chatMessages=document.getElementById('chatMessages'),chatContact=document.getElementById('chatContact');
function renderChatList(){let list=document.getElementById('chatList');list.replaceChildren();if(!contacts.length){let empty=document.createElement('p');empty.textContent='No other linked devices are available yet. Link a phone or desktop chat from the Mill Manager.';list.appendChild(empty);return}for(let c of contacts){let button=document.createElement('button');button.type='button';button.className='chatChoice'+(String(c.id)===chatContact.value?' active':'');button.textContent=c.deviceName+' — '+c.username+' ('+c.role+')';button.disabled=!c.publicKey;button.onclick=()=>{chatContact.value=String(c.id);chatContact.dispatchEvent(new Event('change'))};list.appendChild(button)}}
async function loadContacts(){try{let selected=chatContact.value;contacts=await api('/api/contacts');chatContact.replaceChildren();let prompt=document.createElement('option');prompt.value='';prompt.textContent='Select a linked device to open a chat';chatContact.appendChild(prompt);for(let c of contacts){let o=document.createElement('option');o.value=c.id;o.textContent=c.deviceName+' — '+c.username+' ('+c.role+')';o.disabled=!c.publicKey;chatContact.appendChild(o)}chatContact.value=selected&&contacts.some(c=>String(c.id)===selected&&c.publicKey)?selected:'';renderChatList();chatStatus.textContent=contacts.some(c=>c.publicKey)?'Select a linked device to open that private conversation. Messages are encrypted on this device.':'No other linked device with an encryption key is available yet.';document.getElementById('sendMessage').disabled=!chatContact.value;await updateFingerprint();await loadMessages()}catch(e){chatStatus.textContent='Chat unavailable: '+e.message}}
async function updateFingerprint(){let contact=contacts.find(c=>String(c.id)===chatContact.value),node=document.getElementById('chatFingerprint');if(!contact||!contact.publicKey){node.textContent='No encryption key available for this device yet.';return}node.textContent='Contact key fingerprint — compare with '+contact.username+' over a separate trusted channel: '+await fingerprint(contact.publicKey)}
async function loadMessages(){let contactId=chatContact.value,contact=contacts.find(c=>String(c.id)===contactId);if(chatBusy||!contact||!sessionInfo)return;let selection=chatSelection;chatBusy=true;try{let messages=await api('/api/messages?after='+chatCursor+'&contact='+encodeURIComponent(contact.id)),privateKey=await keyStore('get','device:'+sessionInfo.deviceId);if(!privateKey)throw Error('This device encryption key is missing. Remove and relink this device to restore chat.');for(let m of messages){if(selection!==chatSelection||chatContact.value!==contactId)return;let row=document.createElement('p');row.className='message';let senderId=Number(m.senderDeviceId),recipientId=Number(m.recipientDeviceId),outgoing=senderId===sessionInfo.deviceId,sender=outgoing?'You':contact.deviceName;let label=document.createElement('strong');label.textContent=sender+' · '+m.sentAt;let text=document.createElement('span');try{text.textContent=await decryptFrom(privateKey,outgoing?sessionInfo.publicKey:contact.publicKey,senderId,recipientId,m.envelope)}catch(_){text.textContent='[Unable to decrypt: device encryption keys do not match]'}if(selection!==chatSelection||chatContact.value!==contactId)return;row.append(label,document.createElement('br'),text);chatMessages.appendChild(row);chatCursor=Math.max(chatCursor,Number(m.id))}if(messages.length&&selection===chatSelection)chatMessages.scrollTop=chatMessages.scrollHeight}catch(e){if(selection===chatSelection)chatStatus.textContent='Chat problem: '+e.message}finally{chatBusy=false}}
chatContact.onchange=()=>{chatSelection++;chatCursor=0;chatMessages.replaceChildren();renderChatList();document.getElementById('sendMessage').disabled=!chatContact.value;chatStatus.textContent=chatContact.value?'Loading conversation…':'Select a linked device to open that private conversation.';updateFingerprint();loadMessages()};
document.getElementById('chatForm').onsubmit=async e=>{e.preventDefault();let message=new FormData(e.target).get('message').trim(),contact=contacts.find(c=>String(c.id)===chatContact.value);if(!contact||!contact.publicKey){chatStatus.textContent='Choose a linked phone with a registered encryption key.';return}if(!message||message.length>2000)return;try{let privateKey=await keyStore('get','device:'+sessionInfo.deviceId);if(!privateKey)throw Error('This phone encryption key is missing. Remove and relink this phone.');let [toRecipient,toSender]=await Promise.all([encryptFor(privateKey,contact.publicKey,sessionInfo.deviceId,contact.id,message),encryptFor(privateKey,sessionInfo.publicKey,sessionInfo.deviceId,contact.id,message)]);await api('/api/messages',{recipientDeviceId:Number(contact.id),envelopeToRecipient:toRecipient,envelopeToSender:toSender});e.target.reset();await loadMessages()}catch(x){chatStatus.textContent='Message not sent: '+x.message}};
async function loadRecords(){let status=document.getElementById('reportStatus');try{let p=new URLSearchParams({from:document.getElementById('reportFrom').value,to:document.getElementById('reportTo').value,type:document.getElementById('reportType').value,search:document.getElementById('reportSearch').value}),result=await api('/api/reports?'+p),body=document.querySelector('#reportTable tbody');body.replaceChildren();for(let r of result.records){let tr=document.createElement('tr');let vals=[r.date,r.type,r.reference,r.party,r.product,r.grade||r.account,[r.kg,r.outputKg].filter(n=>n).join(' / '),r.total,r.paid];for(let i=0;i<vals.length;i++){let td=document.createElement('td');td.textContent=typeof vals[i]==='number'&&i>=7?new Intl.NumberFormat().format(vals[i]):String(vals[i]||'');if(i>=6)td.className='num';tr.appendChild(td)}body.appendChild(tr)}status.textContent=result.total+' record(s)'+(result.truncated?' shown (first 500; narrow the date range or search to see fewer).':'')}catch(e){status.textContent='Could not load records: '+e.message}}
document.getElementById('loadReports').onclick=loadRecords;
refresh();
loadContacts();setTimeout(loadRecords,500);setInterval(refresh,5000);setInterval(loadMessages,1500);setInterval(loadContacts,10000);
</script></html>)HTML").arg(product_.name().toHtmlEscaped(), purchaseProducts, products, milling,
                                      coffeeMilling, moneyForm, creditForm, depositForm, chatPanel);
    }

    Store& store_;
    Product product_;
    QHash<QTcpSocket*, QByteArray> buffers_;
    QHash<QString, QPair<int, QDateTime>> loginFailures_;
};

class EntryForm : public QWidget {
public:
    EntryForm(Store& store, Product product, bool purchase, QString username, bool canWrite,
              QWidget* parent = nullptr)
        : QWidget(parent), store_(store), product_(product), purchase_(purchase),
          username_(std::move(username)), canWrite_(canWrite) {
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout();
        layout->addLayout(form);
        party_ = new QLineEdit(this);
        productBox_ = new QComboBox(this);
        productBox_->addItems(purchase_ ? store_.availablePurchaseProducts() : store_.availableSaleProducts());
        grade_ = new QLineEdit(QStringLiteral("Grade A"), this);
        kg_ = new QLineEdit(this);
        price_ = new QLineEdit(this);
        paid_ = new QLineEdit(QStringLiteral("0"), this);
        account_ = new QComboBox(this);
        account_->addItems({QStringLiteral("Cash"), QStringLiteral("Bank")});
        form->addRow(purchase_ ? QStringLiteral("Supplier") : QStringLiteral("Customer"), party_);
        form->addRow(QStringLiteral("Product"), productBox_);
        form->addRow(QStringLiteral("Grade"), grade_);
        form->addRow(QStringLiteral("Weight (kg)"), kg_);
        form->addRow(QStringLiteral("Price per kg (UGX)"), price_);
        form->addRow(purchase_ ? QStringLiteral("Amount paid (UGX)") : QStringLiteral("Amount received (UGX)"), paid_);
        form->addRow(purchase_ ? QStringLiteral("Paid from") : QStringLiteral("Received into"), account_);
        auto* save = new QPushButton(purchase_ ? QStringLiteral("Save purchase") : QStringLiteral("Save sale"), this);
        form->addRow(save);
        save->setEnabled(canWrite_);
        auto* printLast = new QPushButton(QStringLiteral("Print Selected Receipt (POS)"), this);
        printLast->setEnabled(false);
        form->addRow(printLast);
        total_ = new QLabel(this);
        layout->addWidget(total_);
        recent_ = new QTableWidget(this);
        configureBusinessTable(recent_);
        recent_->setColumnCount(9);
        recent_->setHorizontalHeaderLabels({QStringLiteral("Date"), QStringLiteral("Reference"),
            purchase_ ? QStringLiteral("Supplier") : QStringLiteral("Customer"), QStringLiteral("Product"),
            QStringLiteral("Grade"), QStringLiteral("Kg"), QStringLiteral("Total (UGX)"),
            purchase_ ? QStringLiteral("Paid (UGX)") : QStringLiteral("Received (UGX)"),
            QStringLiteral("Balance (UGX)")});
        recent_->setSelectionBehavior(QAbstractItemView::SelectRows);
        recent_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(recent_);
        connect(kg_, &QLineEdit::textChanged, this, [this] { updateTotals(); });
        connect(price_, &QLineEdit::textChanged, this, [this] { updateTotals(); });
        connect(paid_, &QLineEdit::textChanged, this, [this] { updateTotals(); });
        connect(productBox_, &QComboBox::currentTextChanged, this, [this] { loadSavedPrice(); });
        connect(grade_, &QLineEdit::editingFinished, this, [this] { loadSavedPrice(); });
        connect(save, &QPushButton::clicked, this, [this] { this->save(); });
        connect(recent_, &QTableWidget::itemSelectionChanged, this, [this, printLast] {
            printLast->setEnabled(recent_->currentRow() >= 0);
        });
        connect(printLast, &QPushButton::clicked, this, [this] {
            const int row = recent_->currentRow();
            if (row < 0 || !recent_->item(row, 1)) return;
            printReceipt(recent_->item(row, 1)->text());
        });
        refreshRows();
        auto* refreshTimer = new QTimer(this);
        connect(refreshTimer, &QTimer::timeout, this, [this] { refreshRows(); });
        refreshTimer->start(5000);
    }

private:
    void loadSavedPrice() {
        try {
            const double saved = store_.savedPrice(purchase_ ? QStringLiteral("BUY") : QStringLiteral("SELL"),
                                                    productBox_->currentText(), grade_->text());
            if (saved > 0) price_->setText(QString::number(saved, 'f', 0));
        } catch (const std::exception& error) {
            QMessageBox::critical(this, product_.name(), QString::fromUtf8(error.what()));
        }
    }

    void updateTotals() {
        bool kgOk = false, priceOk = false, paidOk = false;
        const double kg = kg_->text().toDouble(&kgOk);
        const double price = price_->text().toDouble(&priceOk);
        const double paid = paid_->text().toDouble(&paidOk);
        if (!kgOk || !priceOk || !paidOk) {
            total_->setText(QStringLiteral("Enter weight, price, and payment to calculate the total."));
            return;
        }
        const double total = kg * price;
        total_->setText(QStringLiteral("Total: UGX %1    Balance: UGX %2")
            .arg(total, 0, 'f', 0).arg(total - paid, 0, 'f', 0));
    }

    void refreshRows() {
        try {
            const auto rows = store_.recentRows(purchase_);
            recent_->setRowCount(rows.size());
            for (int row = 0; row < rows.size(); ++row) {
                for (int column = 0; column < rows[row].size(); ++column)
                    recent_->setItem(row, column, new QTableWidgetItem(rows[row][column]));
            }
            formatTableColumn(recent_, 5, 2);
            formatTableColumn(recent_, 6);
            formatTableColumn(recent_, 7);
            formatTableColumn(recent_, 8);
            recent_->resizeColumnsToContents();
        } catch (const std::exception& error) {
            QMessageBox::critical(this, product_.name(), QString::fromUtf8(error.what()));
        }
    }

    void printReceipt(const QString& reference) {
        QString printerName = store_.setting(QStringLiteral("POS_PRINTER_NAME")).trimmed();
        if (printerName.isEmpty())
            printerName = detectedPosPrinterName();
        if (printerName.isEmpty()) {
            QMessageBox::warning(this, product_.name(),
                QStringLiteral("No POS/thermal printer was detected. Connect it, then choose it under Settings > Receipt and document printers."));
            return;
        }
        store_.setSetting(QStringLiteral("POS_PRINTER_NAME"), printerName);
        const QString table = purchase_ ? product_.purchaseTable() : product_.saleTable();
        const QString partyTable = purchase_ ? QStringLiteral("suppliers") : QStringLiteral("customers");
        const QString idColumn = purchase_ ? QStringLiteral("supplier_id") : QStringLiteral("customer_id");
        const QString codeColumn = purchase_ ? QStringLiteral("supplier_code") : QStringLiteral("buyer_code");
        const QString productColumn = product_.grain
            ? QStringLiteral("product") : QStringLiteral("coffee_type");
        const QString paymentColumn = purchase_ ? QStringLiteral("paid") : QStringLiteral("received");
        const QString referenceColumn = purchase_ ? QStringLiteral("receipt_no") : QStringLiteral("invoice_no");
        QSqlQuery query(store_.db());
        query.prepare(QStringLiteral(
            "SELECT t.date,COALESCE(p.%1,''),COALESCE(p.name,''),COALESCE(t.%2,''),"
            "COALESCE(t.grade,''),COALESCE(t.kg,0),COALESCE(t.total,0),COALESCE(t.%3,0),"
            "COALESCE(t.%4,'Cash') FROM %5 t LEFT JOIN %6 p ON p.id=t.%7 WHERE t.%8=?")
            .arg(codeColumn, productColumn, paymentColumn,
                 purchase_ ? QStringLiteral("payment_account") : QStringLiteral("received_account"),
                 table, partyTable, idColumn, referenceColumn));
        query.addBindValue(reference);
        if (!query.exec() || !query.next()) {
            QMessageBox::critical(this, product_.name(),
                QStringLiteral("Could not load the receipt: %1").arg(query.lastError().text()));
            return;
        }
        const double total = query.value(6).toDouble();
        const double paid = query.value(7).toDouble();
        const QString title = purchase_ ? QStringLiteral("PURCHASE RECEIPT") : QStringLiteral("SALES INVOICE");
        QString html = QStringLiteral(
            "<html><body style='font-family:monospace;font-size:9pt'>"
            "<center><b>%1</b><br>%2<br>%3</center><hr>"
            "<b>%4:</b> %5<br><b>Date:</b> %6<br><b>Reference:</b> %7<br>"
            "<b>Product:</b> %8<br><b>Grade:</b> %9<br><b>Weight:</b> %10 kg<br>"
            "<b>Price/kg:</b> UGX %11<br><hr><b>Total:</b> UGX %12<br>"
            "<b>%13:</b> UGX %14<br><b>Balance:</b> UGX %15<br>"
            "<b>Account:</b> %16<br><br><center>Thank you</center></body></html>")
            .arg(store_.setting(QStringLiteral("business_name"),
                                product_.grain ? QStringLiteral("Mill Manager F")
                                               : QStringLiteral("Mateete Coffee Mill")).toHtmlEscaped(),
                 store_.setting(QStringLiteral("location")).toHtmlEscaped(),
                 product_.name().toHtmlEscaped(),
                 purchase_ ? QStringLiteral("Supplier") : QStringLiteral("Customer"),
                 (query.value(1).toString() + QStringLiteral(" - ") + query.value(2).toString()).toHtmlEscaped(),
                 query.value(0).toString().toHtmlEscaped(), reference.toHtmlEscaped(),
                 query.value(3).toString().toHtmlEscaped(), query.value(4).toString().toHtmlEscaped(),
                 QString::number(query.value(5).toDouble(), 'f', 2),
                 QString::number(total / std::max(query.value(5).toDouble(), 0.000001), 'f', 2),
                 QString::number(total, 'f', 0),
                 purchase_ ? QStringLiteral("Paid") : QStringLiteral("Received"),
                 QString::number(paid, 'f', 0), QString::number(total - paid, 'f', 0),
                 query.value(8).toString().toHtmlEscaped());
        const QString logoPath = store_.setting(QStringLiteral("BUSINESS_LOGO_PATH"));
        if (!logoPath.isEmpty() && QFileInfo(logoPath).isFile() && !QPixmap(logoPath).isNull()) {
            const QString image = QStringLiteral("<img src='%1' style='max-width:140px;max-height:70px'><br>")
                .arg(QUrl::fromLocalFile(logoPath).toString(QUrl::FullyEncoded).toHtmlEscaped());
            html.replace(QStringLiteral("<center>"), QStringLiteral("<center>") + image);
        }

        QPrinter printer(QPrinter::HighResolution);
        const int receiptWidth = store_.setting(QStringLiteral("POS_PAPER_WIDTH_MM"),
            QString::number(detectedReceiptWidthMm(printerName))).toInt() == 58 ? 58 : 80;
        if (!configureReceiptPrinter(printer, printerName, receiptWidth)) {
            QMessageBox::critical(this, product_.name(),
                QStringLiteral("The configured POS printer is unavailable. Check its connection and Settings."));
            return;
        }
        QTextDocument document;
        document.setHtml(html);
        document.print(&printer);
    }

    void save() {
        if (!canWrite_) {
            QMessageBox::warning(this, product_.name(), QStringLiteral("This account has read-only access."));
            return;
        }
        bool kgOk = false, priceOk = false, paidOk = false;
        const double kg = kg_->text().toDouble(&kgOk);
        const double price = price_->text().toDouble(&priceOk);
        const double paid = paid_->text().toDouble(&paidOk);
        if (!kgOk || !priceOk || !paidOk) {
            QMessageBox::warning(this, product_.name(), QStringLiteral("Enter valid numbers for weight, price, and payment."));
            return;
        }
        try {
            const QString reference = purchase_
                ? store_.savePurchase(username_, party_->text(), productBox_->currentText(), grade_->text(), kg, price, paid, account_->currentText())
                : store_.saveSale(username_, party_->text(), productBox_->currentText(), grade_->text(), kg, price, paid, account_->currentText());
            QMessageBox::information(this, product_.name(), QStringLiteral("Record saved."));
            if (QMessageBox::question(this, product_.name(),
                    QStringLiteral("Print the receipt on the POS printer now?")) == QMessageBox::Yes)
                printReceipt(reference);
            party_->clear();
            kg_->clear();
            price_->clear();
            paid_->setText(QStringLiteral("0"));
            refreshRows();
        } catch (const std::exception& error) {
            QMessageBox::critical(this, product_.name(), QString::fromUtf8(error.what()));
        }
    }

    Store& store_;
    Product product_;
    bool purchase_;
    QString username_;
    bool canWrite_;
    QLineEdit* party_ = nullptr;
    QComboBox* productBox_ = nullptr;
    QLineEdit* grade_ = nullptr;
    QLineEdit* kg_ = nullptr;
    QLineEdit* price_ = nullptr;
    QLineEdit* paid_ = nullptr;
    QComboBox* account_ = nullptr;
    QLabel* total_ = nullptr;
    QTableWidget* recent_ = nullptr;
};

bool login(Store& store, QString& username) {
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Sign in to Mill Manager"));
    auto* layout = new QFormLayout(&dialog);
    auto* user = new QLineEdit(QStringLiteral("owner"), &dialog);
    auto* pin = new QLineEdit(&dialog);
    pin->setEchoMode(QLineEdit::Password);
    layout->addRow(QStringLiteral("Username"), user);
    layout->addRow(QStringLiteral("PIN"), pin);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString role = store.roleFor(user->text());
        const QString hash = store.pinHashFor(user->text());
        if (role.isEmpty() || (!(user->text() == QStringLiteral("guest") && pin->text().isEmpty()) &&
                               !verifyPin(pin->text(), hash))) {
            QMessageBox::warning(&dialog, QStringLiteral("Sign in failed"), QStringLiteral("Incorrect username or PIN."));
            return;
        }
        username = user->text();
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return false;
    return true;
}

void createManager(Store& store, QWidget* parent) {
    bool ok = false;
    const QString username = QInputDialog::getText(parent, QStringLiteral("Manager phone account"),
        QStringLiteral("New manager username:"), QLineEdit::Normal, QStringLiteral("manager"), &ok);
    if (!ok) return;
    const QString pin = QInputDialog::getText(parent, QStringLiteral("Manager phone account"),
        QStringLiteral("Manager PIN (at least 4 characters):"), QLineEdit::Password, QString(), &ok);
    if (!ok) return;
    try {
        store.createManager(username, pin);
        QMessageBox::information(parent, QStringLiteral("Manager account"), QStringLiteral("Manager phone account created."));
    } catch (const std::exception& error) {
        QMessageBox::critical(parent, QStringLiteral("Could not create account"), QString::fromUtf8(error.what()));
    }
}

struct PlotSeries {
    QString name;
    QVector<double> values;
    QColor color;
};

class ProgressPlot : public QWidget {
public:
    explicit ProgressPlot(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(320);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setData(const QString& title, const QStringList& labels, const QVector<PlotSeries>& series) {
        title_ = title;
        labels_ = labels;
        series_ = series;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().text().color());
        painter.drawText(QRect(12, 8, width() - 24, 24), Qt::AlignLeft | Qt::AlignVCenter, title_);
        if (labels_.isEmpty() || series_.isEmpty()) {
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No progress data available."));
            return;
        }
        const QRectF plot(64, 42, std::max(1, width() - 88), std::max(1, height() - 94));
        double minimum = 0;
        double maximum = 0;
        for (const PlotSeries& series : series_) {
            for (double value : series.values) {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }
        if (std::abs(maximum - minimum) < 0.001) maximum = minimum + 1;
        const auto yFor = [&](double value) {
            return plot.bottom() - ((value - minimum) / (maximum - minimum)) * plot.height();
        };
        painter.setPen(QPen(palette().mid().color(), 1));
        for (int tick = 0; tick <= 4; ++tick) {
            const double value = minimum + (maximum - minimum) * tick / 4.0;
            const double y = yFor(value);
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.drawText(QRectF(4, y - 9, 54, 18), Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(value, 'g', 4));
        }
        painter.setPen(QPen(palette().text().color(), 1));
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
        const int labelStep = std::max(1, static_cast<int>((labels_.size() + 9) / 10));
        for (int i = 0; i < labels_.size(); i += labelStep) {
            const double x = labels_.size() == 1 ? plot.center().x()
                : plot.left() + plot.width() * i / (labels_.size() - 1);
            painter.drawText(QRectF(x - 26, plot.bottom() + 5, 52, 20),
                             Qt::AlignHCenter | Qt::AlignTop, labels_[i]);
        }
        for (const PlotSeries& series : series_) {
            if (series.values.isEmpty()) continue;
            QPainterPath path;
            for (int i = 0; i < series.values.size(); ++i) {
                const double x = series.values.size() == 1 ? plot.center().x()
                    : plot.left() + plot.width() * i / (series.values.size() - 1);
                const QPointF point(x, yFor(series.values[i]));
                if (i == 0) path.moveTo(point);
                else path.lineTo(point);
            }
            painter.setPen(QPen(series.color, 2));
            painter.drawPath(path);
            for (int i = 0; i < series.values.size(); ++i) {
                const double x = series.values.size() == 1 ? plot.center().x()
                    : plot.left() + plot.width() * i / (series.values.size() - 1);
                painter.setBrush(series.color);
                painter.drawEllipse(QPointF(x, yFor(series.values[i])), 3.5, 3.5);
            }
        }
        int legendX = std::max(90, width() - 200);
        int legendY = 12;
        for (const PlotSeries& series : series_) {
            painter.setPen(QPen(series.color, 3));
            painter.drawLine(legendX, legendY + 8, legendX + 20, legendY + 8);
            painter.setPen(palette().text().color());
            painter.drawText(legendX + 26, legendY, 160, 18, Qt::AlignLeft | Qt::AlignVCenter, series.name);
            legendY += 20;
        }
    }

private:
    QString title_;
    QStringList labels_;
    QVector<PlotSeries> series_;
};

QWidget* createProgressPage(Store& store, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* controls = new QHBoxLayout();
    auto* metric = new QComboBox(page);
    metric->addItems({QStringLiteral("Revenue vs Costs vs Profit"),
        QStringLiteral("Cash & Bank Position"), QStringLiteral("Coffee/Grain Bought vs Sold (Kg)"),
        QStringLiteral("Sales vs Milling Revenue")});
    auto* range = new QComboBox(page);
    for (int months : {6, 12, 24, 36})
        range->addItem(QStringLiteral("Last %1 months").arg(months), months);
    controls->addWidget(new QLabel(QStringLiteral("Metric"), page));
    controls->addWidget(metric);
    controls->addWidget(new QLabel(QStringLiteral("Range"), page));
    controls->addWidget(range);
    controls->addStretch();
    layout->addLayout(controls);
    auto* plot = new ProgressPlot(page);
    layout->addWidget(plot);
    const auto refresh = [&store, page, metric, range, plot] {
        try {
            const auto months = store.progressMonths(range->currentData().toInt());
            QStringList labels;
            for (const ProgressMonth& month : months) labels.push_back(month.month);
            QVector<PlotSeries> series;
            const QList<ProgressMonth> monthList = months;
            const auto makeSeries = [&monthList](const QString& name, const QColor& color,
                                                 double (*value)(const ProgressMonth&)) {
                PlotSeries item;
                item.name = name;
                item.color = color;
                for (const ProgressMonth& month : monthList) item.values.push_back(value(month));
                return item;
            };
            const QString selected = metric->currentText();
            if (selected == QStringLiteral("Revenue vs Costs vs Profit")) {
                series.push_back(makeSeries(QStringLiteral("Revenue"), QColor(40, 120, 75),
                    [](const ProgressMonth& m) { return m.sales + m.milling; }));
                series.push_back(makeSeries(QStringLiteral("Costs"), QColor(205, 92, 70),
                    [](const ProgressMonth& m) { return m.purchases + m.expenses + m.payroll + m.tax; }));
                series.push_back(makeSeries(QStringLiteral("Profit"), QColor(55, 110, 190),
                    [](const ProgressMonth& m) { return m.sales + m.milling - m.purchases - m.expenses - m.payroll - m.tax; }));
            } else if (selected == QStringLiteral("Cash & Bank Position")) {
                series.push_back(makeSeries(QStringLiteral("Cash"), QColor(40, 120, 75),
                    [](const ProgressMonth& m) { return m.cash; }));
                series.push_back(makeSeries(QStringLiteral("Bank"), QColor(55, 110, 190),
                    [](const ProgressMonth& m) { return m.bank; }));
            } else if (selected == QStringLiteral("Coffee/Grain Bought vs Sold (Kg)")) {
                series.push_back(makeSeries(QStringLiteral("Bought"), QColor(40, 120, 75),
                    [](const ProgressMonth& m) { return m.boughtKg; }));
                series.push_back(makeSeries(QStringLiteral("Sold"), QColor(205, 92, 70),
                    [](const ProgressMonth& m) { return m.soldKg; }));
            } else {
                series.push_back(makeSeries(QStringLiteral("Sales"), QColor(40, 120, 75),
                    [](const ProgressMonth& m) { return m.sales; }));
                series.push_back(makeSeries(QStringLiteral("Milling"), QColor(55, 110, 190),
                    [](const ProgressMonth& m) { return m.milling; }));
            }
            plot->setData(selected, labels, series);
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Progress"), QString::fromUtf8(error.what()));
        }
    };
    QObject::connect(metric, &QComboBox::currentTextChanged, page, refresh);
    QObject::connect(range, &QComboBox::currentTextChanged, page, refresh);
    refresh();
    return page;
}

QWidget* createManagerReportsPage(Store& store, const QString& username, bool admin, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    if (admin) {
        auto* split = new QHBoxLayout();
        auto* reports = new QTableWidget(page);
        configureBusinessTable(reports);
        reports->setColumnCount(6);
        reports->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Month"),
            QStringLiteral("Title"), QStringLiteral("Submitted by"), QStringLiteral("Submitted"),
            QStringLiteral("Status")});
        reports->setSelectionBehavior(QAbstractItemView::SelectRows);
        reports->setSelectionMode(QAbstractItemView::SingleSelection);
        reports->setEditTriggers(QAbstractItemView::NoEditTriggers);
        auto* text = new QPlainTextEdit(page);
        text->setReadOnly(true);
        split->addWidget(reports, 2);
        split->addWidget(text, 3);
        layout->addLayout(split);
        auto* actions = new QHBoxLayout();
        auto* refresh = new QPushButton(QStringLiteral("Refresh"), page);
        auto* remove = new QPushButton(QStringLiteral("Delete Selected"), page);
        remove->setEnabled(false);
        actions->addWidget(refresh);
        actions->addWidget(remove);
        actions->addStretch();
        layout->addLayout(actions);
        const auto load = [&store, page, reports, remove] {
            try {
                const auto rows = store.managerReports();
                reports->setRowCount(rows.size());
                for (int i = 0; i < rows.size(); ++i) {
                    const ManagerReportRow& row = rows[i];
                    const QStringList values = {QString::number(row.id), row.month, row.title,
                        row.submittedBy, row.submittedAt,
                        row.readAt.isEmpty() ? QStringLiteral("NEW") : QStringLiteral("Read")};
                    for (int column = 0; column < values.size(); ++column)
                        reports->setItem(i, column, new QTableWidgetItem(values[column]));
                }
                reports->resizeColumnsToContents();
                remove->setEnabled(reports->currentRow() >= 0);
            } catch (const std::exception& error) {
                QMessageBox::critical(page, QStringLiteral("Manager Reports"), QString::fromUtf8(error.what()));
            }
        };
        QObject::connect(refresh, &QPushButton::clicked, page, load);
        QObject::connect(reports, &QTableWidget::itemSelectionChanged, page, [reports, remove] {
            remove->setEnabled(reports->currentRow() >= 0);
        });
        QObject::connect(reports, &QTableWidget::cellDoubleClicked, page,
            [&store, username, page, reports, text, load](int row, int) {
                if (row < 0 || !reports->item(row, 0)) return;
                try {
                    const qint64 id = reports->item(row, 0)->text().toLongLong();
                    text->setPlainText(store.openManagerReport(id, username));
                    load();
                } catch (const std::exception& error) {
                    QMessageBox::critical(page, QStringLiteral("Manager Reports"), QString::fromUtf8(error.what()));
                }
            });
        QObject::connect(remove, &QPushButton::clicked, page, [&store, username, page, reports, text, load] {
            const int row = reports->currentRow();
            if (row < 0 || !reports->item(row, 0)) return;
            if (QMessageBox::question(page, QStringLiteral("Delete report"),
                QStringLiteral("Permanently delete this Manager report?")) != QMessageBox::Yes) return;
            try {
                store.deleteManagerReport(reports->item(row, 0)->text().toLongLong(), username);
                text->clear();
                load();
            } catch (const std::exception& error) {
                QMessageBox::critical(page, QStringLiteral("Manager Reports"), QString::fromUtf8(error.what()));
            }
        });
        layout->addWidget(new QLabel(QStringLiteral("Double-click a report to open it; opening marks it as read."), page));
        load();
    } else {
        auto* form = new QFormLayout();
        auto* month = new QDateEdit(QDate::currentDate(), page);
        month->setDisplayFormat(QStringLiteral("yyyy-MM"));
        month->setCalendarPopup(true);
        auto* title = new QLineEdit(QStringLiteral("Monthly report"), page);
        auto* notes = new QPlainTextEdit(page);
        notes->setPlaceholderText(QStringLiteral("What happened this month? Note problems, needs, and requests."));
        auto* preview = new QPlainTextEdit(page);
        preview->setReadOnly(true);
        form->addRow(QStringLiteral("Report month"), month);
        form->addRow(QStringLiteral("Title"), title);
        form->addRow(QStringLiteral("Manager's report"), notes);
        form->addRow(QStringLiteral("Auto-filled summary"), preview);
        layout->addLayout(form);
        auto* actions = new QHBoxLayout();
        auto* refresh = new QPushButton(QStringLiteral("Refresh Figures"), page);
        auto* submit = new QPushButton(QStringLiteral("Submit to Admin"), page);
        actions->addWidget(refresh);
        actions->addWidget(submit);
        actions->addStretch();
        layout->addLayout(actions);
        const auto updatePreview = [&store, page, month, preview] {
            try {
                preview->setPlainText(store.managerReportSummary(month->date().toString(QStringLiteral("yyyy-MM"))));
            } catch (const std::exception& error) {
                QMessageBox::critical(page, QStringLiteral("Manager Report"), QString::fromUtf8(error.what()));
            }
        };
        QObject::connect(refresh, &QPushButton::clicked, page, updatePreview);
        QObject::connect(month, &QDateEdit::dateChanged, page, updatePreview);
        QObject::connect(submit, &QPushButton::clicked, page,
            [&store, username, page, month, title, notes, updatePreview] {
                try {
                    store.submitManagerReport(username, month->date().toString(QStringLiteral("yyyy-MM")),
                                              title->text(), notes->toPlainText());
                    notes->clear();
                    updatePreview();
                    QMessageBox::information(page, QStringLiteral("Manager Report"),
                        QStringLiteral("Your monthly report was submitted to the Admin."));
                } catch (const std::exception& error) {
                    QMessageBox::critical(page, QStringLiteral("Manager Report"), QString::fromUtf8(error.what()));
                }
            });
        updatePreview();
    }
    return page;
}

QWidget* createReportsPage(Store& store, const Product& product, const QString& username,
                           bool admin, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* filters = new QFormLayout();
    auto* dates = new QHBoxLayout();
    auto* from = new QDateEdit(QDate::currentDate().addDays(-30), page);
    auto* to = new QDateEdit(QDate::currentDate(), page);
    from->setCalendarPopup(true);
    to->setCalendarPopup(true);
    dates->addWidget(from);
    dates->addWidget(new QLabel(QStringLiteral("through"), page));
    dates->addWidget(to);
    auto* kind = new QComboBox(page);
    kind->addItems({QStringLiteral("All"), QStringLiteral("Purchase"), QStringLiteral("Sale"),
        QStringLiteral("Milling"), QStringLiteral("Expense"), QStringLiteral("Money"),
        QStringLiteral("Payroll"), QStringLiteral("Tax Payment"), QStringLiteral("Credit Payment"),
        QStringLiteral("Bank Deposit"), QStringLiteral("Company Milling"),
        QStringLiteral("Customer Balances"), QStringLiteral("Supplier Balances"),
        QStringLiteral("Open Credits"), QStringLiteral("Stock Balance"),
        QStringLiteral("Profit Summary"), QStringLiteral("Activity Log"),
        QStringLiteral("Daily Cash & Bank"), QStringLiteral("Monthly Cash Flow")});
    auto* search = new QLineEdit(page);
    search->setPlaceholderText(QStringLiteral("Search party, reference, product, or grade"));
    auto* person = new QLineEdit(page);
    person->setPlaceholderText(QStringLiteral("Filter supplier, customer, operator, or teller"));
    filters->addRow(QStringLiteral("Date range"), dates);
    filters->addRow(QStringLiteral("Record type"), kind);
    filters->addRow(QStringLiteral("Search"), search);
    filters->addRow(QStringLiteral("Person"), person);
    layout->addLayout(filters);

    auto* records = new QTableWidget(page);
    configureBusinessTable(records);
    records->setColumnCount(10);
    records->setHorizontalHeaderLabels({QStringLiteral("Type"), QStringLiteral("Date"), QStringLiteral("Reference"),
        QStringLiteral("Supplier / Customer / Operator"), QStringLiteral("Product"), QStringLiteral("Grade"),
        QStringLiteral("Kg"), QStringLiteral("Price / kg (UGX)"), QStringLiteral("Total (UGX)"),
        QStringLiteral("Paid / received (UGX)")});
    records->setSelectionBehavior(QAbstractItemView::SelectRows);
    records->setSelectionMode(QAbstractItemView::SingleSelection);
    records->setEditTriggers(QAbstractItemView::NoEditTriggers);
    records->horizontalHeader()->setSectionsMovable(true);
    const QString layoutKey = QStringLiteral("REPORT_LAYOUT_%1_%2")
        .arg(product.grain ? QStringLiteral("F") : QStringLiteral("COFFEE"), username);
    const QString savedLayout = store.setting(layoutKey);
    if (!savedLayout.isEmpty() && !records->horizontalHeader()->restoreState(QByteArray::fromBase64(savedLayout.toLatin1())))
        QMessageBox::warning(page, product.name(), QStringLiteral("The saved report layout was invalid; default columns are shown."));
    const auto saveLayout = [&store, layoutKey, records] {
        store.setSetting(layoutKey, QString::fromLatin1(records->horizontalHeader()->saveState().toBase64()));
    };
    QObject::connect(records->horizontalHeader(), &QHeaderView::sectionMoved, page,
        [saveLayout](int, int, int) { saveLayout(); });
    layout->addWidget(records);

    auto* actions = new QHBoxLayout();
    auto* refresh = new QPushButton(QStringLiteral("Refresh"), page);
    auto* exportCsv = new QPushButton(QStringLiteral("Export CSV"), page);
    auto* exportPdf = new QPushButton(QStringLiteral("Save PDF"), page);
    auto* print = new QPushButton(QStringLiteral("Print"), page);
    auto* edit = new QPushButton(QStringLiteral("Edit Selected Record"), page);
    auto* columns = new QPushButton(QStringLiteral("Report Columns"), page);
    edit->setEnabled(false);
    actions->addWidget(refresh);
    actions->addWidget(exportCsv);
    actions->addWidget(exportPdf);
    actions->addWidget(print);
    actions->addWidget(columns);
    actions->addWidget(edit);
    actions->addStretch();
    auto* note = new QLabel(admin
        ? (product.grain
            ? QStringLiteral("Admin edits to purchase, sale, and customer-milling records update their ledgers. Expenses, payroll, tax, and cash rows are view-only.")
            : QStringLiteral("Admin edits to purchase, sale, and customer-milling records update their ledgers. Expenses, payroll, tax, and cash rows are view-only."))
        : QStringLiteral("Reports are read-only for this account."), page);
    note->setWordWrap(true);
    actions->addWidget(note);
    layout->addLayout(actions);
    QObject::connect(columns, &QPushButton::clicked, page, [page, records, saveLayout] {
        QDialog dialog(page);
        dialog.setWindowTitle(QStringLiteral("Customize report columns"));
        auto* dialogLayout = new QVBoxLayout(&dialog);
        QList<QCheckBox*> checks;
        for (int column = 0; column < records->columnCount(); ++column) {
            auto* check = new QCheckBox(records->horizontalHeaderItem(column)->text(), &dialog);
            check->setChecked(!records->isColumnHidden(column));
            checks.push_back(check);
            dialogLayout->addWidget(check);
        }
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
        dialogLayout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            int visibleCount = 0;
            for (QCheckBox* check : checks) if (check->isChecked()) ++visibleCount;
            if (visibleCount == 0) {
                QMessageBox::warning(&dialog, QStringLiteral("Report Columns"),
                                     QStringLiteral("At least one column must remain visible."));
                return;
            }
            for (int column = 0; column < checks.size(); ++column)
                records->setColumnHidden(column, !checks[column]->isChecked());
            saveLayout();
            dialog.accept();
        });
        dialog.exec();
    });

    auto rows = std::make_shared<QList<ReportEntry>>();
    const auto refreshRows = [&store, page, from, to, kind, search, person, records, rows, edit, admin, product] {
        try {
            QDate startDate = from->date();
            QDate endDate = to->date();
            if (startDate > endDate) std::swap(startDate, endDate);
            *rows = store.reportRows(startDate, endDate, kind->currentText(), search->text(), person->text());
            records->setRowCount(rows->size());
            for (int i = 0; i < rows->size(); ++i) {
                const ReportEntry& entry = rows->at(i);
                const QStringList values = {
                    entry.type, entry.date, entry.reference, entry.party, entry.product, entry.grade,
                    QString::number(entry.kg, 'f', 2),
                    entry.type == QStringLiteral("Milling") ? QString() : QString::number(entry.price, 'f', 0),
                    QString::number(entry.total, 'f', 0), QString::number(entry.paid, 'f', 0)};
                for (int column = 0; column < values.size(); ++column)
                    records->setItem(i, column, new QTableWidgetItem(values[column]));
            }
            formatTableColumn(records, 6, 2);
            formatTableColumn(records, 7);
            formatTableColumn(records, 8);
            formatTableColumn(records, 9);
            records->resizeColumnsToContents();
            const int selected = records->currentRow();
            const QString selectedType = selected >= 0 && selected < rows->size()
                ? rows->at(selected).type : QString();
            edit->setEnabled(admin && (selectedType == QStringLiteral("Purchase") ||
                selectedType == QStringLiteral("Sale") || selectedType == QStringLiteral("Milling")));
        } catch (const std::exception& error) {
            QMessageBox::critical(page, product.name(), QString::fromUtf8(error.what()));
        }
    };
    QObject::connect(refresh, &QPushButton::clicked, page, refreshRows);
    QObject::connect(from, &QDateEdit::dateChanged, page, refreshRows);
    QObject::connect(to, &QDateEdit::dateChanged, page, refreshRows);
    QObject::connect(kind, &QComboBox::currentTextChanged, page, refreshRows);
    QObject::connect(search, &QLineEdit::textChanged, page, refreshRows);
    QObject::connect(person, &QLineEdit::textChanged, page, refreshRows);
    QObject::connect(exportCsv, &QPushButton::clicked, page, [records, page, product] {
        const QString path = QFileDialog::getSaveFileName(page, QStringLiteral("Export report"),
            QStringLiteral("%1-report.csv").arg(product.grain ? QStringLiteral("mill-manager-f")
                                                              : QStringLiteral("mill-manager")),
            QStringLiteral("CSV files (*.csv)"));
        if (path.isEmpty()) return;
        const auto csvField = [](QString value) {
            value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            return QLatin1Char('"') + value + QLatin1Char('"');
        };
        QByteArray csv;
        QStringList headers;
        QList<int> columns;
        QHeaderView* header = records->horizontalHeader();
        for (int visual = 0; visual < header->count(); ++visual) {
            const int column = header->logicalIndex(visual);
            if (!records->isColumnHidden(column)) columns.push_back(column);
        }
        for (int column : columns)
            headers.push_back(csvField(records->horizontalHeaderItem(column)->text()));
        csv += headers.join(QLatin1Char(',')).toUtf8() + '\n';
        for (int row = 0; row < records->rowCount(); ++row) {
            QStringList fields;
            for (int column : columns)
                fields.push_back(csvField(records->item(row, column) ? records->item(row, column)->text() : QString()));
            csv += fields.join(QLatin1Char(',')).toUtf8() + '\n';
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(csv) != csv.size()) {
            QMessageBox::critical(page, product.name(),
                QStringLiteral("Could not write the report file: %1").arg(file.errorString()));
            return;
        }
        QMessageBox::information(page, product.name(), QStringLiteral("Report exported to %1").arg(path));
    });
    const auto reportHtml = [records, from, to, kind, search, person, product, &store] {
        QString html = QStringLiteral("<html><body><h2>%1</h2><p>%2 - %3 | %4 | %5</p>"
            "<table border='1' cellspacing='0' cellpadding='4'><tr>")
            .arg(product.name().toHtmlEscaped(), from->date().toString(Qt::ISODate).toHtmlEscaped(),
                 to->date().toString(Qt::ISODate).toHtmlEscaped(), kind->currentText().toHtmlEscaped(),
                 (search->text() + QStringLiteral(" ") + person->text()).toHtmlEscaped());
        html.replace(QStringLiteral("<body>"),
            QStringLiteral("<body>%1").arg(businessBrandHtml(
                store.setting(QStringLiteral("business_name"), product.name()),
                store.setting(QStringLiteral("BUSINESS_LOGO_PATH")))));
        QList<int> columns;
        QHeaderView* header = records->horizontalHeader();
        for (int visual = 0; visual < header->count(); ++visual) {
            const int column = header->logicalIndex(visual);
            if (!records->isColumnHidden(column)) columns.push_back(column);
        }
        for (int column : columns)
            html += QStringLiteral("<th>%1</th>").arg(records->horizontalHeaderItem(column)->text().toHtmlEscaped());
        html += QStringLiteral("</tr>");
        for (int row = 0; row < records->rowCount(); ++row) {
            html += QStringLiteral("<tr>");
            for (int column : columns) {
                const QTableWidgetItem* item = records->item(row, column);
                html += QStringLiteral("<td>%1</td>").arg(item ? item->text().toHtmlEscaped() : QString());
            }
            html += QStringLiteral("</tr>");
        }
        html += QStringLiteral("</table></body></html>");
        return html;
    };
    QObject::connect(exportPdf, &QPushButton::clicked, page, [page, product, reportHtml] {
        const QString path = QFileDialog::getSaveFileName(page, QStringLiteral("Save report as PDF"),
            QStringLiteral("%1-report.pdf").arg(product.grain ? QStringLiteral("mill-manager-f")
                                                              : QStringLiteral("mill-manager")),
            QStringLiteral("PDF files (*.pdf)"));
        if (path.isEmpty()) return;
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        QTextDocument document;
        document.setHtml(reportHtml());
        document.print(&printer);
        QMessageBox::information(page, product.name(), QStringLiteral("Report saved to %1").arg(path));
    });
    QObject::connect(print, &QPushButton::clicked, page, [page, reportHtml, &store] {
        QPrinter printer(QPrinter::HighResolution);
        const QString normalPrinter = store.setting(QStringLiteral("NORMAL_PRINTER_NAME")).trimmed();
        if (!normalPrinter.isEmpty())
            printer.setPrinterName(normalPrinter);
        QPrintDialog dialog(&printer, page);
        if (dialog.exec() != QDialog::Accepted) return;
        QTextDocument document;
        document.setHtml(reportHtml());
        document.print(&printer);
    });
    QObject::connect(records, &QTableWidget::itemSelectionChanged, page, [=] {
        const int selected = records->currentRow();
        const QString selectedType = selected >= 0 && selected < rows->size()
            ? rows->at(selected).type : QString();
        edit->setEnabled(admin && (selectedType == QStringLiteral("Purchase") ||
            selectedType == QStringLiteral("Sale") || selectedType == QStringLiteral("Milling")));
    });
    QObject::connect(edit, &QPushButton::clicked, page, [&store, username, rows, records, page, admin, product, refreshRows] {
        const int selected = records->currentRow();
        if (!admin || selected < 0 || selected >= rows->size()) return;
        const ReportEntry entry = rows->at(selected);
        QDialog dialog(page);
        dialog.setWindowTitle(QStringLiteral("Edit %1 record %2").arg(entry.type, entry.reference));
        auto* form = new QFormLayout(&dialog);
        auto* date = new QDateEdit(QDate::fromString(entry.date, Qt::ISODate), &dialog);
        date->setCalendarPopup(true);
        auto* party = new QLineEdit(entry.party, &dialog);
        auto* item = new QComboBox(&dialog);
        item->addItems(entry.type == QStringLiteral("Purchase") ? store.availablePurchaseProducts()
            : entry.type == QStringLiteral("Milling") ? store.availableMillingProducts()
            : store.availableSaleProducts());
        if (item->findText(entry.product) < 0) item->addItem(entry.product);
        item->setCurrentText(entry.product);
        auto* grade = new QLineEdit(entry.grade, &dialog);
        auto* kg = new QLineEdit(QString::number(entry.kg, 'f', 2), &dialog);
        auto* outputKg = new QLineEdit(QString::number(entry.outputKg, 'f', 2), &dialog);
        auto* price = new QLineEdit(QString::number(entry.price, 'f', 0), &dialog);
        auto* paid = new QLineEdit(QString::number(entry.paid, 'f', 0), &dialog);
        auto* account = new QComboBox(&dialog);
        account->addItems({QStringLiteral("Cash"), QStringLiteral("Bank")});
        account->setCurrentText(entry.account);
        form->addRow(QStringLiteral("Date"), date);
        form->addRow(entry.type == QStringLiteral("Purchase") ? QStringLiteral("Supplier") : QStringLiteral("Customer"), party);
        form->addRow(QStringLiteral("Product"), item);
        form->addRow(QStringLiteral("Grade"), grade);
        form->addRow(QStringLiteral("Weight (kg)"), kg);
        if (entry.type == QStringLiteral("Milling"))
            form->addRow(QStringLiteral("Output weight (kg)"), outputKg);
        form->addRow(QStringLiteral("Price per kg (UGX)"), price);
        form->addRow(entry.type == QStringLiteral("Purchase") ? QStringLiteral("Amount paid (UGX)") : QStringLiteral("Amount received (UGX)"), paid);
        form->addRow(QStringLiteral("Account"), account);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            bool kgOk = false, outputOk = true, priceOk = false, paidOk = false;
            const double kgValue = kg->text().toDouble(&kgOk);
            const double outputValue = entry.type == QStringLiteral("Milling")
                ? outputKg->text().toDouble(&outputOk) : entry.outputKg;
            const double priceValue = price->text().toDouble(&priceOk);
            const double paidValue = paid->text().toDouble(&paidOk);
            if (!kgOk || !outputOk || !priceOk || !paidOk) {
                QMessageBox::warning(&dialog, product.name(), QStringLiteral("Enter valid numbers for weight, price, and payment."));
                return;
            }
            try {
                store.editReportRecord(username, entry, date->date(), party->text(), item->currentText(),
                                       grade->text(), kgValue, outputValue, priceValue, paidValue, account->currentText());
                dialog.accept();
            } catch (const std::exception& error) {
                QMessageBox::critical(&dialog, product.name(), QString::fromUtf8(error.what()));
            }
        });
        if (dialog.exec() == QDialog::Accepted) refreshRows();
    });
    auto* timer = new QTimer(page);
    QObject::connect(timer, &QTimer::timeout, page, refreshRows);
    timer->start(5000);
    refreshRows();
    return page;
}

QWidget* createCompanyMillingSection(Store& store, const QString& username, bool canWrite,
                                     QWidget* parent, bool kibookoToKasseOnly) {
    auto* section = new QGroupBox(kibookoToKasseOnly
        ? QStringLiteral("Company stock milling: Kibooko to Kasse")
        : QStringLiteral("Company stock milling"), parent);
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(10, 10, 10, 8);
    auto* description = new QLabel(
        QStringLiteral("Mill the company's own Kibooko stock into Kasse. The input is removed from Kibooko "
                       "stock, the output is added to Kasse stock, and the saved run appears in the records and reports."),
        section);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* form = new QFormLayout();
    auto* inputType = new QComboBox(section);
    if (kibookoToKasseOnly) {
        inputType->addItem(QStringLiteral("Kibooko"));
        inputType->setEnabled(false);
    } else {
        inputType->addItems({QStringLiteral("Kakuta"), QStringLiteral("Kibooko")});
    }
    auto* inputGrade = new QLineEdit(QStringLiteral("Grade A"), section);
    auto* inputKg = new QLineEdit(section);
    auto* outputType = new QLabel(section);
    auto* outputGrade = new QLineEdit(QStringLiteral("Grade A"), section);
    auto* outputKg = new QLineEdit(section);
    auto* notes = new QLineEdit(section);
    auto* save = new QPushButton(QStringLiteral("Record company stock milling"), section);
    save->setEnabled(canWrite);
    form->addRow(QStringLiteral("Input coffee"), inputType);
    form->addRow(QStringLiteral("Input grade"), inputGrade);
    form->addRow(QStringLiteral("Input weight (kg)"), inputKg);
    form->addRow(QStringLiteral("Output coffee"), outputType);
    form->addRow(QStringLiteral("Output grade"), outputGrade);
    form->addRow(QStringLiteral("Output weight (kg)"), outputKg);
    form->addRow(QStringLiteral("Notes"), notes);
    form->addRow(save);
    layout->addLayout(form);
    auto* recent = new QTableWidget(0, 8, section);
    configureBusinessTable(recent);
    recent->setHorizontalHeaderLabels({QStringLiteral("Date"), QStringLiteral("Input"), QStringLiteral("Grade"),
        QStringLiteral("Input kg"), QStringLiteral("Output"), QStringLiteral("Grade"),
        QStringLiteral("Output kg"), QStringLiteral("Loss kg")});
    recent->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recent->setMaximumHeight(190);
    layout->addWidget(recent);
    const auto updateOutput = [inputType, outputType] {
        outputType->setText(inputType->currentText() == QStringLiteral("Kakuta")
            ? QStringLiteral("Kibooko") : QStringLiteral("Kasse"));
    };
    const auto refresh = [&store, page = section, recent, kibookoToKasseOnly] {
        try {
            QSqlQuery query(store.db());
            const QString filter = kibookoToKasseOnly
                ? QStringLiteral("WHERE input_type='Kibooko' AND output_type='Kasse' ")
                : QString();
            query.prepare(QStringLiteral("SELECT date,input_type,input_grade,kg_in,output_type,output_grade,kg_out,loss_kg "
                "FROM company_milling %1ORDER BY id DESC LIMIT 40").arg(filter));
            if (!query.exec()) throw std::runtime_error(query.lastError().text().toStdString());
            QList<QStringList> rows;
            while (query.next()) {
                rows.push_back({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                    QString::number(query.value(3).toDouble(), 'f', 2), query.value(4).toString(),
                    query.value(5).toString(), QString::number(query.value(6).toDouble(), 'f', 2),
                    QString::number(query.value(7).toDouble(), 'f', 2)});
            }
            recent->setRowCount(rows.size());
            for (int row = 0; row < rows.size(); ++row)
                for (int column = 0; column < rows[row].size(); ++column)
                    recent->setItem(row, column, new QTableWidgetItem(rows[row][column]));
            formatTableColumn(recent, 3, 2);
            formatTableColumn(recent, 6, 2);
            formatTableColumn(recent, 7, 2);
            recent->resizeColumnsToContents();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Company milling"), QString::fromUtf8(error.what()));
        }
    };
    QObject::connect(inputType, &QComboBox::currentTextChanged, section, updateOutput);
    QObject::connect(save, &QPushButton::clicked, section, [&store, username, section, inputType, inputGrade,
            inputKg, outputGrade, outputKg, notes, refresh] {
        bool inputOk = false, outputOk = false;
        const double input = inputKg->text().toDouble(&inputOk);
        const double output = outputKg->text().toDouble(&outputOk);
        if (!inputOk || !outputOk) {
            QMessageBox::warning(section, QStringLiteral("Company milling"), QStringLiteral("Enter valid input and output weights."));
            return;
        }
        try {
            store.saveCompanyMilling(username, inputType->currentText(), inputGrade->text(), input,
                                     output, outputGrade->text(), notes->text());
            inputKg->clear();
            outputKg->clear();
            notes->clear();
            refresh();
        } catch (const std::exception& error) {
            QMessageBox::critical(section, QStringLiteral("Company milling"), QString::fromUtf8(error.what()));
        }
    });
    updateOutput();
    refresh();
    return section;
}

QWidget* createCoffeeMillingPage(Store& store, const QString& username, bool canWrite, QWidget* parent) {
    const bool grain = store.grainPackage();
    const QString millingName = grain ? QStringLiteral("grain") : QStringLiteral("coffee");
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout();
    auto* customer = new QLineEdit(page);
    auto* coffee = new QComboBox(page);
    coffee->addItems(store.availableMillingProducts());
    auto* inputKg = new QLineEdit(page);
    auto* outputKg = new QLineEdit(page);
    auto* charge = new QLineEdit(page);
    auto* received = new QLineEdit(QStringLiteral("0"), page);
    auto* account = new QComboBox(page);
    account->addItems({QStringLiteral("Cash"), QStringLiteral("Bank")});
    auto* total = new QLabel(page);
    auto* save = new QPushButton(QStringLiteral("Save Milling Job"), page);
    save->setEnabled(canWrite);
    form->addRow(QStringLiteral("Customer"), customer);
    form->addRow(grain ? QStringLiteral("Grain type") : QStringLiteral("Coffee type"), coffee);
    form->addRow(QStringLiteral("Input weight (kg)"), inputKg);
    form->addRow(QStringLiteral("Output weight (kg)"), outputKg);
    form->addRow(QStringLiteral("Charge per kg (UGX)"), charge);
    form->addRow(QStringLiteral("Amount received (UGX)"), received);
    form->addRow(QStringLiteral("Received into"), account);
    form->addRow(QStringLiteral("Total charge"), total);
    form->addRow(save);
    layout->addLayout(form);
    layout->addWidget(new QLabel(QStringLiteral(
        "Customer-owned %1 milling is recorded separately from the mill's own stock. Use Reports to filter or edit saved jobs.")
        .arg(millingName),
        page));
    if (!grain)
        layout->addWidget(createCompanyMillingSection(store, username, canWrite, page, true));
    layout->addStretch();
    const auto updateTotal = [inputKg, charge, total] {
        bool kgOk = false, priceOk = false;
        const double kgValue = inputKg->text().toDouble(&kgOk);
        const double priceValue = charge->text().toDouble(&priceOk);
        total->setText(kgOk && priceOk
            ? QStringLiteral("UGX %1").arg(kgValue * priceValue, 0, 'f', 0)
            : QStringLiteral("Enter input weight and charge."));
    };
    QObject::connect(inputKg, &QLineEdit::textChanged, page, updateTotal);
    QObject::connect(charge, &QLineEdit::textChanged, page, updateTotal);
    QObject::connect(save, &QPushButton::clicked, page, [&store, username, page, customer, coffee,
            inputKg, outputKg, charge, received, account, updateTotal] {
        bool inputOk = false, outputOk = false, chargeOk = false, receivedOk = false;
        const double input = inputKg->text().toDouble(&inputOk);
        const double output = outputKg->text().toDouble(&outputOk);
        const double chargeValue = charge->text().toDouble(&chargeOk);
        const double receivedValue = received->text().toDouble(&receivedOk);
        if (!inputOk || !outputOk || !chargeOk || !receivedOk) {
            QMessageBox::warning(page, QStringLiteral("Mill Manager"), QStringLiteral("Enter valid weights and amounts."));
            return;
        }
        if (receivedValue > input * chargeValue &&
            QMessageBox::question(page, QStringLiteral("Overpayment"),
                QStringLiteral("The amount received is greater than the total charge. Continue?")) != QMessageBox::Yes)
            return;
        try {
            store.saveCoffeeMilling(username, customer->text(), coffee->currentText(), input, output,
                                    chargeValue, receivedValue, account->currentText());
            QMessageBox::information(page, QStringLiteral("Mill Manager"), QStringLiteral("Milling job saved."));
            customer->clear();
            inputKg->clear();
            outputKg->clear();
            charge->clear();
            received->setText(QStringLiteral("0"));
            updateTotal();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Mill Manager"), QString::fromUtf8(error.what()));
        }
    });
    updateTotal();
    return page;
}

QWidget* createCompanyMillingPage(Store& store, const QString& username, bool canWrite, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(createCompanyMillingSection(store, username, canWrite, page, false));
    layout->addStretch();
    return page;
}

QWidget* createPeoplePage(Store& store, const QString& username, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* type = new QComboBox(page);
    type->addItems({QStringLiteral("Customer"), QStringLiteral("Supplier"), QStringLiteral("Worker")});
    layout->addWidget(type);
    auto* table = new QTableWidget(page);
    configureBusinessTable(table);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({QStringLiteral("Code"), QStringLiteral("Name"),
        QStringLiteral("Phone"), QStringLiteral("TIN / ID"), QStringLiteral("Pay rate"), QStringLiteral("Active")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table);
    auto* actions = new QHBoxLayout();
    auto* add = new QPushButton(QStringLiteral("Add"), page);
    auto* edit = new QPushButton(QStringLiteral("Edit Selected"), page);
    auto* refresh = new QPushButton(QStringLiteral("Refresh"), page);
    auto* link = new QPushButton(QStringLiteral("Link Same Person"), page);
    auto* unlink = new QPushButton(QStringLiteral("Unlink Selected"), page);
    auto* autoLink = new QPushButton(QStringLiteral("Match by Phone / ID"), page);
    actions->addWidget(add);
    actions->addWidget(edit);
    actions->addWidget(refresh);
    actions->addWidget(link);
    actions->addWidget(unlink);
    actions->addWidget(autoLink);
    actions->addStretch();
    layout->addLayout(actions);

    const auto load = [&store, page, type, table] {
        try {
            const auto rows = store.people(type->currentText());
            table->setRowCount(rows.size());
            for (int row = 0; row < rows.size(); ++row) {
                const QStringList& person = rows[row];
                const QStringList values = {person[1], person[2], person[3], person[4],
                    person[9], person[10] == QStringLiteral("1") ? QStringLiteral("Yes") : QStringLiteral("No")};
                for (int column = 0; column < values.size(); ++column) {
                    auto* item = new QTableWidgetItem(values[column]);
                    if (column == 0) item->setData(Qt::UserRole, person[0].toLongLong());
                    table->setItem(row, column, item);
                }
            }
            formatTableColumn(table, 4);
            table->resizeColumnsToContents();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Registers"), QString::fromUtf8(error.what()));
        }
    };
    const auto editPerson = [&store, username, page, type, table, load](bool creating) {
        const int row = table->currentRow();
        if (!creating && (row < 0 || !table->item(row, 0))) return;
        const qint64 id = creating ? 0 : table->item(row, 0)->data(Qt::UserRole).toLongLong();
        QStringList values(11, QString());
        if (!creating) {
            const auto rows = store.people(type->currentText());
            const auto found = std::find_if(rows.begin(), rows.end(), [id](const QStringList& item) {
                return item[0].toLongLong() == id;
            });
            if (found == rows.end()) {
                QMessageBox::warning(page, QStringLiteral("Registers"), QStringLiteral("The selected record no longer exists."));
                load();
                return;
            }
            values = *found;
        }
        QDialog dialog(page);
        dialog.setWindowTitle(creating ? QStringLiteral("Add %1").arg(type->currentText())
                                       : QStringLiteral("Edit %1").arg(type->currentText()));
        auto* form = new QFormLayout(&dialog);
        auto* name = new QLineEdit(values[2], &dialog);
        auto* phone = new QLineEdit(values[3], &dialog);
        auto* identity = new QLineEdit(values[4], &dialog);
        auto* address = new QLineEdit(values[5], &dialog);
        auto* notes = new QLineEdit(values[6], &dialog);
        auto* job = new QLineEdit(values[7], &dialog);
        auto* payType = new QComboBox(&dialog);
        payType->addItems({QStringLiteral("Monthly Salary"), QStringLiteral("Daily Wage"), QStringLiteral("Weekly Wage")});
        if (!values[8].isEmpty() && payType->findText(values[8]) < 0) payType->addItem(values[8]);
        payType->setCurrentText(values[8].isEmpty() ? QStringLiteral("Monthly Salary") : values[8]);
        auto* rate = new QLineEdit(values[9].isEmpty() ? QStringLiteral("0") : values[9], &dialog);
        auto* active = new QCheckBox(&dialog);
        active->setChecked(values[10] != QStringLiteral("0"));
        form->addRow(QStringLiteral("Name"), name);
        form->addRow(QStringLiteral("Phone"), phone);
        form->addRow(type->currentText() == QStringLiteral("Customer") ? QStringLiteral("TIN")
                                                                      : QStringLiteral("ID number"), identity);
        form->addRow(QStringLiteral("Address"), address);
        form->addRow(QStringLiteral("Notes"), notes);
        if (type->currentText() == QStringLiteral("Worker")) {
            form->addRow(QStringLiteral("Job"), job);
            form->addRow(QStringLiteral("Pay type"), payType);
            form->addRow(QStringLiteral("Rate / salary (UGX)"), rate);
            form->addRow(QStringLiteral("Active"), active);
        }
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            bool rateOk = true;
            const double rateValue = type->currentText() == QStringLiteral("Worker")
                ? rate->text().toDouble(&rateOk) : 0;
            if (!rateOk) {
                QMessageBox::warning(&dialog, QStringLiteral("Registers"), QStringLiteral("Enter a valid pay rate."));
                return;
            }
            try {
                store.savePerson(username, type->currentText(), id, name->text(), phone->text(),
                    identity->text(), address->text(), notes->text(), job->text(),
                    payType->currentText(), rateValue, active->isChecked());
                dialog.accept();
            } catch (const std::exception& error) {
                QMessageBox::critical(&dialog, QStringLiteral("Registers"), QString::fromUtf8(error.what()));
            }
        });
        if (dialog.exec() == QDialog::Accepted) load();
    };
    QObject::connect(type, &QComboBox::currentTextChanged, page, load);
    QObject::connect(add, &QPushButton::clicked, page, [editPerson] { editPerson(true); });
    QObject::connect(edit, &QPushButton::clicked, page, [editPerson] { editPerson(false); });
    QObject::connect(refresh, &QPushButton::clicked, page, load);
    QObject::connect(link, &QPushButton::clicked, page, [&store, username, page, type, table, load] {
        const int row = table->currentRow();
        if (row < 0 || !table->item(row, 0)) return;
        const qint64 id = table->item(row, 0)->data(Qt::UserRole).toLongLong();
        const QString currentType = type->currentText();
        if (currentType == QStringLiteral("Worker")) {
            QMessageBox::information(page, QStringLiteral("Link Same Person"),
                QStringLiteral("Only customer and supplier records can be linked."));
            return;
        }
        const QString otherType = currentType == QStringLiteral("Customer")
            ? QStringLiteral("Supplier") : QStringLiteral("Customer");
        try {
            const auto candidates = store.people(otherType);
            QStringList labels;
            QList<qint64> ids;
            for (const QStringList& candidate : candidates) {
                labels.push_back(QStringLiteral("%1 - %2 (%3)").arg(candidate[1], candidate[2], candidate[3]));
                ids.push_back(candidate[0].toLongLong());
            }
            if (labels.isEmpty()) {
                QMessageBox::information(page, QStringLiteral("Link Same Person"),
                    QStringLiteral("There are no %1 records to link.").arg(otherType.toLower()));
                return;
            }
            bool ok = false;
            const QString selected = QInputDialog::getItem(page, QStringLiteral("Confirm same person"),
                QStringLiteral("Select the %1 record belonging to the same person:").arg(otherType.toLower()),
                labels, 0, false, &ok);
            if (!ok) return;
            const qint64 otherId = ids[labels.indexOf(selected)];
            const qint64 supplierId = currentType == QStringLiteral("Supplier") ? id : otherId;
            const qint64 customerId = currentType == QStringLiteral("Customer") ? id : otherId;
            store.linkPersonRecords(username, supplierId, customerId);
            QMessageBox::information(page, QStringLiteral("Registers"),
                QStringLiteral("Customer and supplier records are linked as the same person."));
            load();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Registers"), QString::fromUtf8(error.what()));
        }
    });
    QObject::connect(unlink, &QPushButton::clicked, page, [&store, username, page, type, table, load] {
        const int row = table->currentRow();
        if (row < 0 || !table->item(row, 0)) return;
        if (type->currentText() == QStringLiteral("Worker")) {
            QMessageBox::information(page, QStringLiteral("Unlink"), QStringLiteral("Worker records cannot be linked."));
            return;
        }
        if (QMessageBox::question(page, QStringLiteral("Unlink records"),
            QStringLiteral("Remove same-person links for this record and prevent automatic relinking?")) != QMessageBox::Yes)
            return;
        try {
            store.unlinkPersonRecords(username, type->currentText(),
                table->item(row, 0)->data(Qt::UserRole).toLongLong());
            load();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Registers"), QString::fromUtf8(error.what()));
        }
    });
    QObject::connect(autoLink, &QPushButton::clicked, page, [&store, username, page, load] {
        try {
            const int count = store.autoLinkPeople(username);
            load();
            QMessageBox::information(page, QStringLiteral("Same people"),
                QStringLiteral("Created %1 link(s) for matching phone numbers or ID numbers.").arg(count));
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Registers"), QString::fromUtf8(error.what()));
        }
    });
    QObject::connect(table, &QTableWidget::itemSelectionChanged, page, [table, edit] {
        edit->setEnabled(table->currentRow() >= 0);
    });
    edit->setEnabled(false);
    load();
    return page;
}

QWidget* createCreditAndDepositPage(Store& store, const QString& username, bool canWrite, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);

    auto* creditBox = new QGroupBox(QStringLiteral("Settle customer or supplier credit"), page);
    auto* creditForm = new QFormLayout(creditBox);
    auto* person = new QComboBox(creditBox);
    auto* balance = new QLabel(creditBox);
    auto* amount = new QLineEdit(creditBox);
    auto* account = new QComboBox(creditBox);
    account->addItems({QStringLiteral("Cash"), QStringLiteral("Bank")});
    auto* date = new QDateEdit(QDate::currentDate(), creditBox);
    date->setCalendarPopup(true);
    auto* reference = new QLineEdit(creditBox);
    auto* notes = new QLineEdit(creditBox);
    auto* saveCredit = new QPushButton(QStringLiteral("Record Credit Payment"), creditBox);
    saveCredit->setEnabled(canWrite);
    creditForm->addRow(QStringLiteral("Customer / supplier"), person);
    creditForm->addRow(QStringLiteral("Current position"), balance);
    creditForm->addRow(QStringLiteral("Amount (UGX)"), amount);
    creditForm->addRow(QStringLiteral("Account"), account);
    creditForm->addRow(QStringLiteral("Date"), date);
    creditForm->addRow(QStringLiteral("Reference"), reference);
    creditForm->addRow(QStringLiteral("Notes"), notes);
    creditForm->addRow(saveCredit);
    layout->addWidget(creditBox);

    auto* offsetBox = new QGroupBox(QStringLiteral("Offset linked customer credit against supplier payable"), page);
    auto* offsetForm = new QFormLayout(offsetBox);
    auto* offsetPair = new QComboBox(offsetBox);
    auto* offsetAvailable = new QLabel(offsetBox);
    auto* offsetAmount = new QLineEdit(offsetBox);
    auto* offsetDate = new QDateEdit(QDate::currentDate(), offsetBox);
    offsetDate->setCalendarPopup(true);
    auto* offsetReference = new QLineEdit(offsetBox);
    auto* offsetNotes = new QLineEdit(offsetBox);
    auto* saveOffset = new QPushButton(QStringLiteral("Record Credit Offset"), offsetBox);
    saveOffset->setEnabled(canWrite);
    offsetForm->addRow(QStringLiteral("Linked same-person records"), offsetPair);
    offsetForm->addRow(QStringLiteral("Available to offset"), offsetAvailable);
    offsetForm->addRow(QStringLiteral("Amount (UGX)"), offsetAmount);
    offsetForm->addRow(QStringLiteral("Date"), offsetDate);
    offsetForm->addRow(QStringLiteral("Reference"), offsetReference);
    offsetForm->addRow(QStringLiteral("Notes"), offsetNotes);
    offsetForm->addRow(saveOffset);
    layout->addWidget(offsetBox);

    auto* depositBox = new QGroupBox(QStringLiteral("Record bank deposit"), page);
    auto* depositForm = new QFormLayout(depositBox);
    auto* receipt = new QLineEdit(depositBox);
    auto* depositDate = new QDateEdit(QDate::currentDate(), depositBox);
    depositDate->setCalendarPopup(true);
    auto* teller = new QLineEdit(depositBox);
    auto* receiptCode = new QLineEdit(depositBox);
    auto* depositAmount = new QLineEdit(depositBox);
    auto* bank = new QLineEdit(depositBox);
    auto* depositNotes = new QLineEdit(depositBox);
    auto* saveDeposit = new QPushButton(QStringLiteral("Save Bank Deposit"), depositBox);
    saveDeposit->setEnabled(canWrite);
    depositForm->addRow(QStringLiteral("Receipt number"), receipt);
    depositForm->addRow(QStringLiteral("Deposit date"), depositDate);
    depositForm->addRow(QStringLiteral("Teller"), teller);
    depositForm->addRow(QStringLiteral("Receipt code"), receiptCode);
    depositForm->addRow(QStringLiteral("Amount (UGX)"), depositAmount);
    depositForm->addRow(QStringLiteral("Bank"), bank);
    depositForm->addRow(QStringLiteral("Notes"), depositNotes);
    depositForm->addRow(saveDeposit);
    layout->addWidget(depositBox);

    auto* balances = new QTableWidget(0, 4, page);
    configureBusinessTable(balances);
    balances->setHorizontalHeaderLabels({QStringLiteral("Party"), QStringLiteral("ID"),
        QStringLiteral("Name"), QStringLiteral("Position (UGX)")});
    balances->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(balances);

    const auto position = [&store](const QString& type, qint64 id) {
        return store.creditPosition(type, id);
    };
    const auto load = [&store, page, person, balance, balances, position, offsetPair, offsetAvailable] {
        try {
            const QString selected = person->currentData().toString();
            person->clear();
            balances->setRowCount(0);
            const QString selectedPair = offsetPair->currentData().toString();
            offsetPair->clear();
            const auto appendPeople = [&](const QString& type, const QString& table, const QString& codeColumn) {
                QSqlQuery query(store.db());
                query.prepare(QStringLiteral("SELECT id,name,%1 FROM %2 ORDER BY name").arg(codeColumn, table));
                if (!query.exec()) throw std::runtime_error(query.lastError().text().toStdString());
                while (query.next()) {
                    const qint64 id = query.value(0).toLongLong();
                    const QString name = query.value(1).toString();
                    const QString code = query.value(2).toString();
                    const double current = position(type, id);
                    const QString key = type + QLatin1Char('|') + QString::number(id);
                    person->addItem(QStringLiteral("%1 - %2 (%3)").arg(code, name, type), key);
                    if (std::abs(current) <= 0.5)
                        continue;
                    const int row = balances->rowCount();
                    balances->insertRow(row);
                    balances->setItem(row, 0, new QTableWidgetItem(type));
                    balances->setItem(row, 1, new QTableWidgetItem(code));
                    balances->setItem(row, 2, new QTableWidgetItem(name));
                    balances->setItem(row, 3, new QTableWidgetItem(groupedMoney(current)));
                }
            };
            for (const QStringList& pair : store.linkedPersonPairs()) {
                const QString key = pair[0] + QLatin1Char('|') + pair[3];
                offsetPair->addItem(QStringLiteral("%1 - %2 / %3 - %4 (customer +%5; supplier %6)")
                    .arg(pair[1], pair[2], pair[4], pair[5],
                         groupedMoney(pair[7].toDouble()), groupedMoney(pair[8].toDouble())), key);
            }
            appendPeople(QStringLiteral("Customer"), QStringLiteral("customers"), QStringLiteral("buyer_code"));
            appendPeople(QStringLiteral("Supplier"), QStringLiteral("suppliers"), QStringLiteral("supplier_code"));
            if (!selected.isEmpty()) {
                const int index = person->findData(selected);
                if (index >= 0) person->setCurrentIndex(index);
            }
            if (!selectedPair.isEmpty()) {
                const int index = offsetPair->findData(selectedPair);
                if (index >= 0) offsetPair->setCurrentIndex(index);
            }
            balances->resizeColumnsToContents();
            formatTableColumn(balances, 3);
            formatTableColumn(balances, 3);
            const QStringList selectedParts = person->currentData().toString().split(QLatin1Char('|'));
            balance->setText(selectedParts.size() == 2
                ? QStringLiteral("UGX %1").arg(groupedMoney(position(selectedParts[0], selectedParts[1].toLongLong())))
                : QStringLiteral("No registered customers or suppliers."));
            const QStringList pairIds = offsetPair->currentData().toString().split(QLatin1Char('|'));
            const double available = pairIds.size() == 2
                ? store.availableCreditOffset(pairIds[0].toLongLong(), pairIds[1].toLongLong()) : 0;
            offsetAvailable->setText(QStringLiteral("UGX %1").arg(groupedMoney(available)));
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Credits"), QString::fromUtf8(error.what()));
        }
    };
    QObject::connect(person, &QComboBox::currentTextChanged, page, [person, balance, position] {
        const QStringList parts = person->currentData().toString().split(QLatin1Char('|'));
        if (parts.size() == 2) {
            try {
                balance->setText(QStringLiteral("UGX %1")
                    .arg(groupedMoney(position(parts[0], parts[1].toLongLong()))));
            } catch (const std::exception& error) {
                balance->setText(QString::fromUtf8(error.what()));
            }
        }
    });
    QObject::connect(offsetPair, &QComboBox::currentTextChanged, page, [&store, offsetPair, offsetAvailable] {
        const QStringList parts = offsetPair->currentData().toString().split(QLatin1Char('|'));
        try {
            const double available = parts.size() == 2
                ? store.availableCreditOffset(parts[0].toLongLong(), parts[1].toLongLong()) : 0;
            offsetAvailable->setText(QStringLiteral("UGX %1").arg(groupedMoney(available)));
        } catch (const std::exception& error) {
            offsetAvailable->setText(QString::fromUtf8(error.what()));
        }
    });
    QObject::connect(saveCredit, &QPushButton::clicked, page, [&store, username, page, person, amount,
            account, date, reference, notes, load] {
        const QStringList parts = person->currentData().toString().split(QLatin1Char('|'));
        bool amountOk = false;
        const double value = amount->text().toDouble(&amountOk);
        if (parts.size() != 2 || !amountOk) {
            QMessageBox::warning(page, QStringLiteral("Credit payment"),
                QStringLiteral("Select a customer or supplier and enter a valid amount."));
            return;
        }
        try {
            store.saveCreditPayment(username, parts[0], parts[1].toLongLong(), value,
                                    account->currentText(), date->date(), reference->text(), notes->text());
            amount->clear();
            reference->clear();
            notes->clear();
            load();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Credit payment"), QString::fromUtf8(error.what()));
        }
    });
    QObject::connect(saveOffset, &QPushButton::clicked, page,
        [&store, username, page, offsetPair, offsetAmount, offsetDate, offsetReference, offsetNotes, load] {
            const QStringList parts = offsetPair->currentData().toString().split(QLatin1Char('|'));
            bool amountOk = false;
            const double value = offsetAmount->text().toDouble(&amountOk);
            if (parts.size() != 2 || !amountOk) {
                QMessageBox::warning(page, QStringLiteral("Credit Offset"),
                    QStringLiteral("Link the same customer and supplier first, then enter an amount."));
                return;
            }
            try {
                store.saveCreditOffset(username, parts[0].toLongLong(), parts[1].toLongLong(),
                    value, offsetDate->date(), offsetReference->text(), offsetNotes->text());
                offsetAmount->clear();
                offsetReference->clear();
                offsetNotes->clear();
                load();
                QMessageBox::information(page, QStringLiteral("Credit Offset"),
                    QStringLiteral("The receivable and payable were offset without moving cash."));
            } catch (const std::exception& error) {
                QMessageBox::critical(page, QStringLiteral("Credit Offset"), QString::fromUtf8(error.what()));
            }
        });
    QObject::connect(saveDeposit, &QPushButton::clicked, page, [&store, username, page, receipt, depositDate,
            teller, receiptCode, depositAmount, bank, depositNotes, load] {
        bool amountOk = false;
        const double value = depositAmount->text().toDouble(&amountOk);
        if (!amountOk) {
            QMessageBox::warning(page, QStringLiteral("Bank deposit"), QStringLiteral("Enter a valid amount."));
            return;
        }
        try {
            store.saveBankDeposit(username, receipt->text(), depositDate->date(), teller->text(),
                receiptCode->text(), value, bank->text(), depositNotes->text());
            receipt->clear();
            teller->clear();
            receiptCode->clear();
            depositAmount->clear();
            depositNotes->clear();
            load();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Bank deposit"), QString::fromUtf8(error.what()));
        }
    });
    load();
    layout->addStretch();
    return page;
}

QWidget* createMoneyPage(Store& store, const QString& username, bool canWrite, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout();
    auto* account = new QComboBox(page);
    account->addItems({QStringLiteral("Cash"), QStringLiteral("Bank")});
    auto* kind = new QComboBox(page);
    kind->addItems({QStringLiteral("OUT"), QStringLiteral("IN")});
    auto* category = new QComboBox(page);
    category->setEditable(true);
    category->addItems({QStringLiteral("Fuel"), QStringLiteral("Repairs"), QStringLiteral("Electricity"),
        QStringLiteral("Rent"), QStringLiteral("Licence"), QStringLiteral("Transport"),
        QStringLiteral("Wages (casual)"), QStringLiteral("Packaging"), QStringLiteral("Other income"),
        QStringLiteral("Other expense")});
    auto* amount = new QLineEdit(page);
    auto* date = new QDateEdit(QDate::currentDate(), page);
    date->setCalendarPopup(true);
    auto* reference = new QLineEdit(page);
    auto* description = new QLineEdit(page);
    auto* save = new QPushButton(QStringLiteral("Save Transaction"), page);
    save->setEnabled(canWrite);
    form->addRow(QStringLiteral("Account"), account);
    form->addRow(QStringLiteral("Direction"), kind);
    form->addRow(QStringLiteral("Category"), category);
    form->addRow(QStringLiteral("Amount (UGX)"), amount);
    form->addRow(QStringLiteral("Date"), date);
    form->addRow(QStringLiteral("Reference"), reference);
    form->addRow(QStringLiteral("Description"), description);
    form->addRow(save);
    layout->addLayout(form);
    auto* totals = new QLabel(page);
    layout->addWidget(totals);
    auto* recent = new QTableWidget(0, 7, page);
    configureBusinessTable(recent);
    recent->setHorizontalHeaderLabels({QStringLiteral("Date"), QStringLiteral("Account"), QStringLiteral("Type"),
        QStringLiteral("Category"), QStringLiteral("Amount (UGX)"), QStringLiteral("Reference"),
        QStringLiteral("Description")});
    recent->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recent->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(recent);
    const auto refresh = [&store, page, totals, recent] {
        try {
            const QJsonObject totalsData = store.dashboard();
            totals->setText(QStringLiteral("Cash balance: UGX %1    Bank balance: UGX %2")
                .arg(groupedMoney(totalsData.value(QStringLiteral("cash")).toDouble()),
                     groupedMoney(totalsData.value(QStringLiteral("bank")).toDouble())));
            QSqlQuery query(store.db());
            query.prepare(QStringLiteral("SELECT date,account,kind,category,amount,reference,description "
                                         "FROM cash_transactions ORDER BY id DESC LIMIT 40"));
            if (!query.exec()) throw std::runtime_error(query.lastError().text().toStdString());
            QList<QStringList> rows;
            while (query.next()) {
                rows.push_back({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                    query.value(3).toString(), groupedMoney(query.value(4).toDouble()),
                    query.value(5).toString(), query.value(6).toString()});
            }
            recent->setRowCount(rows.size());
            for (int i = 0; i < rows.size(); ++i)
                for (int j = 0; j < rows[i].size(); ++j)
                    recent->setItem(i, j, new QTableWidgetItem(rows[i][j]));
            formatTableColumn(recent, 4);
            recent->resizeColumnsToContents();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Money"), QString::fromUtf8(error.what()));
        }
    };
    QObject::connect(save, &QPushButton::clicked, page, [&store, username, page, account, kind, category,
            amount, date, reference, description, refresh] {
        bool ok = false;
        const double value = amount->text().toDouble(&ok);
        if (!ok) {
            QMessageBox::warning(page, QStringLiteral("Money"), QStringLiteral("Enter a valid amount."));
            return;
        }
        try {
            store.saveMoney(username, account->currentText(), kind->currentText(), category->currentText(),
                            value, date->date(), reference->text(), description->text());
            amount->clear();
            reference->clear();
            description->clear();
            refresh();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Money"), QString::fromUtf8(error.what()));
        }
    });
    auto* timer = new QTimer(page);
    QObject::connect(timer, &QTimer::timeout, page, refresh);
    timer->start(5000);
    refresh();
    return page;
}

QWidget* createWorkersPage(Store& store, const QString& username, bool canWrite, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* createCard = new QGroupBox(QStringLiteral("Add Worker"), page);
    auto* createForm = new QFormLayout(createCard);
    auto* workerName = new QLineEdit(createCard);
    auto* job = new QLineEdit(createCard);
    auto* payType = new QComboBox(createCard);
    payType->addItems({QStringLiteral("Monthly Salary"), QStringLiteral("Daily Wage"), QStringLiteral("Weekly Wage")});
    auto* rate = new QLineEdit(createCard);
    auto* addWorker = new QPushButton(QStringLiteral("Create Worker"), createCard);
    addWorker->setEnabled(canWrite);
    createForm->addRow(QStringLiteral("Name"), workerName);
    createForm->addRow(QStringLiteral("Job"), job);
    createForm->addRow(QStringLiteral("Pay type"), payType);
    createForm->addRow(QStringLiteral("Rate / salary (UGX)"), rate);
    createForm->addRow(addWorker);
    layout->addWidget(createCard);

    auto* payrollCard = new QGroupBox(QStringLiteral("Payroll"), page);
    auto* form = new QFormLayout(payrollCard);
    auto* worker = new QComboBox(payrollCard);
    auto* period = new QLineEdit(QDate::currentDate().toString(QStringLiteral("yyyy-MM")), payrollCard);
    auto* payTypeForRun = new QComboBox(payrollCard);
    payTypeForRun->addItems({QStringLiteral("Monthly Salary"), QStringLiteral("Daily Wage"), QStringLiteral("Weekly Wage")});
    auto* days = new QLineEdit(QStringLiteral("0"), payrollCard);
    auto* salary = new QLineEdit(payrollCard);
    auto* overtime = new QLineEdit(QStringLiteral("0"), payrollCard);
    auto* overtimeRate = new QLineEdit(QStringLiteral("0"), payrollCard);
    auto* deductions = new QLineEdit(QStringLiteral("0"), payrollCard);
    auto* paid = new QLineEdit(QStringLiteral("0"), payrollCard);
    auto* account = new QComboBox(payrollCard);
    account->addItems({QStringLiteral("Cash"), QStringLiteral("Bank")});
    auto* date = new QDateEdit(QDate::currentDate(), payrollCard);
    date->setCalendarPopup(true);
    auto* notes = new QLineEdit(payrollCard);
    auto* calculation = new QLabel(payrollCard);
    auto* save = new QPushButton(QStringLiteral("Save Payroll"), payrollCard);
    save->setEnabled(canWrite);
    form->addRow(QStringLiteral("Worker"), worker);
    form->addRow(QStringLiteral("Period (YYYY-MM)"), period);
    form->addRow(QStringLiteral("Pay type"), payTypeForRun);
    form->addRow(QStringLiteral("Days / weeks worked"), days);
    form->addRow(QStringLiteral("Rate / salary (UGX)"), salary);
    form->addRow(QStringLiteral("Overtime hours"), overtime);
    form->addRow(QStringLiteral("Overtime rate (UGX/hour)"), overtimeRate);
    form->addRow(QStringLiteral("Other deductions (UGX)"), deductions);
    form->addRow(QStringLiteral("Paid now (UGX)"), paid);
    form->addRow(QStringLiteral("Paid from"), account);
    form->addRow(QStringLiteral("Payment date"), date);
    form->addRow(QStringLiteral("Notes"), notes);
    form->addRow(calculation);
    form->addRow(save);
    layout->addWidget(payrollCard);
    layout->addStretch();

    const auto loadWorkers = [&store, page, worker] {
        try {
            const QString selected = worker->currentText();
            worker->clear();
            worker->addItems(store.activeWorkers());
            worker->setCurrentText(selected);
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Workers"), QString::fromUtf8(error.what()));
        }
    };
    const auto recalculate = [payTypeForRun, days, salary, overtime, overtimeRate, deductions, paid, calculation] {
        const double base = payTypeForRun->currentText() == QStringLiteral("Monthly Salary")
            ? salary->text().toDouble() : days->text().toDouble() * salary->text().toDouble();
        const double gross = base + overtime->text().toDouble() * overtimeRate->text().toDouble();
        const double paye = calculatePaye(gross);
        const double net = std::max(0.0, gross - paye - deductions->text().toDouble());
        calculation->setText(QStringLiteral("Gross: UGX %1    PAYE: UGX %2    Net: UGX %3")
            .arg(gross, 0, 'f', 0).arg(paye, 0, 'f', 0).arg(net, 0, 'f', 0));
        if (paid->text().trimmed().isEmpty() || paid->text() == QStringLiteral("0"))
            paid->setText(QString::number(net, 'f', 0));
    };
    for (QLineEdit* field : {days, salary, overtime, overtimeRate, deductions})
        QObject::connect(field, &QLineEdit::textChanged, page, recalculate);
    QObject::connect(payTypeForRun, &QComboBox::currentTextChanged, page, recalculate);
    QObject::connect(addWorker, &QPushButton::clicked, page, [&store, username, page, workerName, job, payType, rate, loadWorkers] {
        bool ok = false;
        const double value = rate->text().toDouble(&ok);
        if (!ok) {
            QMessageBox::warning(page, QStringLiteral("Workers"), QStringLiteral("Enter a valid pay rate."));
            return;
        }
        try {
            store.createWorker(username, workerName->text(), job->text(), payType->currentText(), value);
            workerName->clear();
            job->clear();
            rate->clear();
            loadWorkers();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Workers"), QString::fromUtf8(error.what()));
        }
    });
    QObject::connect(save, &QPushButton::clicked, page, [&store, username, page, worker, period, days, overtime,
            salary, overtimeRate, deductions, paid, date, account, notes, payTypeForRun, recalculate] {
        const double units = days->text().toDouble();
        const double extraHours = overtime->text().toDouble();
        const double rateValue = salary->text().toDouble();
        const double gross = (payTypeForRun->currentText() == QStringLiteral("Monthly Salary")
            ? rateValue : units * rateValue) + extraHours * overtimeRate->text().toDouble();
        const double paye = calculatePaye(gross);
        const double deductionsValue = deductions->text().toDouble();
        const double net = std::max(0.0, gross - paye - deductionsValue);
        bool paidOk = false;
        const double paidValue = paid->text().toDouble(&paidOk);
        if (!paidOk) {
            QMessageBox::warning(page, QStringLiteral("Payroll"), QStringLiteral("Enter a valid amount paid."));
            return;
        }
        try {
            store.savePayroll(username, worker->currentText(), period->text().trimmed(), units, extraHours,
                gross, paye, deductionsValue, net, paidValue, date->date(), account->currentText(), notes->text());
            QMessageBox::information(page, QStringLiteral("Payroll"), QStringLiteral("Payroll entry saved."));
            recalculate();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("Payroll"), QString::fromUtf8(error.what()));
        }
    });
    loadWorkers();
    recalculate();
    return page;
}

QWidget* createTaxPage(Store& store, const QString& username, bool canWrite, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout();
    auto* taxType = new QComboBox(page);
    taxType->setEditable(true);
    taxType->addItems({QStringLiteral("PAYE"), QStringLiteral("VAT"), QStringLiteral("Withholding tax"),
                       QStringLiteral("Income tax"), QStringLiteral("Other tax")});
    auto* period = new QLineEdit(QDate::currentDate().toString(QStringLiteral("yyyy-MM")), page);
    auto* amount = new QLineEdit(page);
    auto* date = new QDateEdit(QDate::currentDate(), page);
    date->setCalendarPopup(true);
    auto* account = new QComboBox(page);
    account->addItems({QStringLiteral("Cash"), QStringLiteral("Bank")});
    auto* reference = new QLineEdit(page);
    auto* notes = new QLineEdit(page);
    auto* save = new QPushButton(QStringLiteral("Record Tax Payment"), page);
    save->setEnabled(canWrite);
    form->addRow(QStringLiteral("Tax type"), taxType);
    form->addRow(QStringLiteral("Period"), period);
    form->addRow(QStringLiteral("Amount (UGX)"), amount);
    form->addRow(QStringLiteral("Payment date"), date);
    form->addRow(QStringLiteral("Paid from"), account);
    form->addRow(QStringLiteral("Reference"), reference);
    form->addRow(QStringLiteral("Notes"), notes);
    form->addRow(save);
    layout->addLayout(form);
    auto* recent = new QTableWidget(0, 6, page);
    configureBusinessTable(recent);
    recent->setHorizontalHeaderLabels({QStringLiteral("Date"), QStringLiteral("Tax"), QStringLiteral("Period"),
        QStringLiteral("Amount (UGX)"), QStringLiteral("Reference"), QStringLiteral("Notes")});
    recent->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(recent);
    const auto refresh = [&store, page, recent] {
        try {
            QSqlQuery query(store.db());
            query.prepare(QStringLiteral("SELECT date,tax_type,period,amount,reference,notes FROM tax_payments ORDER BY id DESC LIMIT 60"));
            if (!query.exec()) throw std::runtime_error(query.lastError().text().toStdString());
            QList<QStringList> rows;
            while (query.next())
                rows.push_back({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                    groupedMoney(query.value(3).toDouble()), query.value(4).toString(), query.value(5).toString()});
            recent->setRowCount(rows.size());
            for (int i = 0; i < rows.size(); ++i)
                for (int j = 0; j < rows[i].size(); ++j)
                    recent->setItem(i, j, new QTableWidgetItem(rows[i][j]));
            formatTableColumn(recent, 3);
            recent->resizeColumnsToContents();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("URA Tax"), QString::fromUtf8(error.what()));
        }
    };
    QObject::connect(save, &QPushButton::clicked, page, [&store, username, page, taxType, period, amount,
            date, reference, notes, account, refresh] {
        bool ok = false;
        const double value = amount->text().toDouble(&ok);
        if (!ok) {
            QMessageBox::warning(page, QStringLiteral("URA Tax"), QStringLiteral("Enter a valid amount."));
            return;
        }
        try {
            store.saveTaxPayment(username, taxType->currentText(), period->text(), value, date->date(),
                                 reference->text(), notes->text(), account->currentText());
            amount->clear();
            reference->clear();
            notes->clear();
            refresh();
        } catch (const std::exception& error) {
            QMessageBox::critical(page, QStringLiteral("URA Tax"), QString::fromUtf8(error.what()));
        }
    });
    refresh();
    return page;
}

void changePin(Store& store, const QString& username, QWidget* parent) {
    bool ok = false;
    const QString oldPin = QInputDialog::getText(parent, QStringLiteral("Change PIN"),
        QStringLiteral("Current PIN:"), QLineEdit::Password, QString(), &ok);
    if (!ok) return;
    const QString newPin = QInputDialog::getText(parent, QStringLiteral("Change PIN"),
        QStringLiteral("New PIN (at least four characters):"), QLineEdit::Password, QString(), &ok);
    if (!ok) return;
    try {
        store.changePin(username, oldPin, newPin);
        QMessageBox::information(parent, QStringLiteral("Change PIN"), QStringLiteral("PIN updated."));
    } catch (const std::exception& error) {
        QMessageBox::critical(parent, QStringLiteral("Could not change PIN"), QString::fromUtf8(error.what()));
    }
}

QWidget* createDesktopChatPage(Store& store, PhoneServer& server, const Product& product,
                               const QString& username, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* title = new QLabel(QStringLiteral("Secure linked-device messages"), page);
    title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 600;"));
    auto* description = new QLabel(
        QStringLiteral("Open a private chat workspace in your browser. It links as a separate desktop "
                       "chat device, so the 2 Admin / 1 Manager phone limits are unchanged. "
                       "Choose any linked device to send messages and receive replies; message contents "
                       "remain end-to-end encrypted between the selected devices."), page);
    description->setWordWrap(true);
    auto* openChat = new QPushButton(QStringLiteral("Open secure chat"), page);
    auto* status = new QLabel(QStringLiteral("The desktop app must remain open while chatting."), page);
    status->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(openChat, 0, Qt::AlignLeft);
    layout->addWidget(status);
    layout->addStretch();
    QObject::connect(openChat, &QPushButton::clicked, page, [&store, &server, product, username, page, status] {
        if (!server.isListening()) {
            const int defaultPort = product.grain ? 8766 : static_cast<int>(kPhonePort);
            bool ok = false;
            const int port = store.setting(QStringLiteral("PHONE_PORT"), QString::number(defaultPort)).toInt(&ok);
            if (!ok || port < 1 || port > 65535) {
                QMessageBox::critical(page, product.name(), QStringLiteral("The configured phone server port is invalid."));
                return;
            }
            if (!server.start(static_cast<quint16>(port))) {
                QMessageBox::critical(page, product.name(),
                    QStringLiteral("Could not start linked-device access on port %1: %2")
                        .arg(port).arg(server.errorString()));
                return;
            }
        }
        try {
            const QString code = store.createPhoneLinkCode(username, store.roleFor(username), true);
            const QUrl chatUrl(QStringLiteral("http://localhost:%1/#desktop-pair=%2")
                                   .arg(server.serverPort()).arg(code));
            if (!QDesktopServices::openUrl(chatUrl)) {
                status->setText(QStringLiteral("Could not open the default browser. Check that a browser is installed, then retry."));
                return;
            }
            status->setText(QStringLiteral("Secure chat opened in your browser. Leave Mill Manager running to keep the chat connected."));
        } catch (const std::exception& error) {
            QMessageBox::critical(page, product.name(), QString::fromUtf8(error.what()));
        }
    });
    return page;
}

int runApp(const Product& product) {
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                            QLatin1Char('/') + product.dataDirectory();
    if (!QDir().mkpath(dataDir)) throw std::runtime_error("Could not create the business data folder.");
    Store store(product, dataDir + QLatin1Char('/') + product.databaseFile());
    QString username;
    if (!login(store, username)) return 0;

    PhoneServer server(store, product);
    AppTheme theme;
    theme.dark = store.setting(QStringLiteral("THEME_DARK"), QStringLiteral("0")) == QStringLiteral("1");
    const QColor savedPrimary(store.setting(QStringLiteral("THEME_PRIMARY"), QStringLiteral("#286b43")));
    const QColor savedBackground(store.setting(QStringLiteral("THEME_BACKGROUND"), QStringLiteral("#f3f5f7")));
    if (savedPrimary.isValid()) theme.primary = savedPrimary;
    if (savedBackground.isValid()) theme.background = savedBackground;
    const QString savedImage = store.setting(QStringLiteral("THEME_BACKGROUND_IMAGE"));
    if (!savedImage.isEmpty() && QFileInfo(savedImage).isFile())
        theme.backgroundImage = savedImage;
    const auto applyTheme = [&theme] {
        QPalette palette = qApp->palette();
        const QColor surface = theme.dark ? QColor(QStringLiteral("#2b3239")) : QColor(QStringLiteral("#ffffff"));
        const QColor text = theme.dark ? QColor(QStringLiteral("#e6e9ec")) : QColor(QStringLiteral("#1f2933"));
        palette.setColor(QPalette::Window, theme.background);
        palette.setColor(QPalette::WindowText, text);
        palette.setColor(QPalette::Base, surface);
        palette.setColor(QPalette::AlternateBase,
                         theme.dark ? QColor(QStringLiteral("#333b43")) : QColor(QStringLiteral("#f5f8f6")));
        palette.setColor(QPalette::Text, text);
        palette.setColor(QPalette::Button, theme.primary);
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::Highlight, theme.primary);
        palette.setColor(QPalette::HighlightedText, Qt::white);
        qApp->setPalette(palette);
        qApp->setStyleSheet(appStyleSheet(theme));
    };
    applyTheme();
    QMainWindow window;
    window.setWindowTitle(product.name());
    window.setWindowIcon(QIcon(QStringLiteral(":/assets/mill-manager.png")));
    window.resize(1280, 820);
    window.setMinimumSize(960, 640);
    auto* root = new QWidget(&window);
    root->setObjectName(QStringLiteral("appRoot"));
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    auto* header = new QWidget(root);
    header->setObjectName(QStringLiteral("appHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 12, 20, 12);
    auto* logo = new QLabel(header);
    QString logoPath = store.setting(QStringLiteral("BUSINESS_LOGO_PATH"));
    QPixmap logoPixmap(logoPath);
    if (logoPixmap.isNull())
        logoPixmap.load(QStringLiteral(":/assets/mill-manager.png"));
    if (!logoPixmap.isNull()) {
        logo->setPixmap(logoPixmap.scaled(54, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        logo->setFixedSize(58, 58);
    }
    headerLayout->addWidget(logo);
    auto* heading = new QVBoxLayout();
    const QString businessName = store.setting(QStringLiteral("business_name"),
        product.grain ? QStringLiteral("Mill Manager F") : QStringLiteral("Mateete Coffee Mill"));
    auto* businessTitle = new QLabel(businessName, header);
    businessTitle->setObjectName(QStringLiteral("headerTitle"));
    auto* appSubtitle = new QLabel(QStringLiteral("%1  |  %2")
        .arg(product.name(), store.setting(QStringLiteral("location"), QStringLiteral("Uganda"))), header);
    appSubtitle->setObjectName(QStringLiteral("headerSubtitle"));
    heading->addWidget(businessTitle);
    heading->addWidget(appSubtitle);
    headerLayout->addLayout(heading);
    headerLayout->addStretch();
    auto* identity = new QVBoxLayout();
    auto* clockLabel = new QLabel(header);
    clockLabel->setObjectName(QStringLiteral("headerClock"));
    clockLabel->setAlignment(Qt::AlignRight);
    auto* signedIn = new QLabel(QStringLiteral("Signed in: %1  (%2)").arg(username, store.roleFor(username)), header);
    signedIn->setObjectName(QStringLiteral("headerIdentity"));
    signedIn->setAlignment(Qt::AlignRight);
    auto* windowControls = new QHBoxLayout();
    windowControls->setSpacing(6);
    auto* minimize = new QPushButton(QStringLiteral("Minimize"), header);
    minimize->setObjectName(QStringLiteral("windowControl"));
    auto* maximize = new QPushButton(QStringLiteral("Maximize"), header);
    maximize->setObjectName(QStringLiteral("windowControl"));
    windowControls->addStretch();
    windowControls->addWidget(minimize);
    windowControls->addWidget(maximize);
    identity->addWidget(clockLabel);
    identity->addWidget(signedIn);
    identity->addLayout(windowControls);
    headerLayout->addLayout(identity);
    rootLayout->addWidget(header);

    auto* body = new QWidget(root);
    body->setObjectName(QStringLiteral("mainBody"));
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    auto* sidebar = new QWidget(body);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(220);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 12, 0, 10);
    sidebarLayout->setSpacing(2);
    auto* menuCaption = new QLabel(QStringLiteral("MENU"), sidebar);
    menuCaption->setObjectName(QStringLiteral("menuCaption"));
    menuCaption->setContentsMargins(17, 4, 0, 5);
    sidebarLayout->addWidget(menuCaption);
    auto* tabs = new QStackedWidget(body);
    QList<QPair<QPushButton*, QWidget*>> navigation;
    const auto selectPage = [tabs, &navigation](QWidget* page) {
        tabs->setCurrentWidget(page);
        for (const auto& item : navigation) {
            const bool active = item.second == page;
            item.first->setProperty("active", active);
            item.first->style()->unpolish(item.first);
            item.first->style()->polish(item.first);
        }
    };
    const auto addPage = [tabs, sidebarLayout, &navigation, selectPage]
        (QWidget* page, const QString& title) {
        auto* scrollPage = new QScrollArea(tabs);
        scrollPage->setObjectName(QStringLiteral("pageScrollArea"));
        scrollPage->setWidgetResizable(true);
        scrollPage->setFrameShape(QFrame::NoFrame);
        scrollPage->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollPage->setWidget(page);
        tabs->addWidget(scrollPage);
        auto* button = new QPushButton(title, sidebarLayout->parentWidget());
        button->setObjectName(QStringLiteral("navButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setProperty("active", false);
        sidebarLayout->addWidget(button);
        navigation.append(qMakePair(button, static_cast<QWidget*>(scrollPage)));
        QObject::connect(button, &QPushButton::clicked, tabs, [selectPage, scrollPage] {
            selectPage(scrollPage);
        });
        if (navigation.size() == 1) {
            button->setProperty("active", true);
            tabs->setCurrentWidget(scrollPage);
        }
    };
    bodyLayout->addWidget(sidebar);
    bodyLayout->addWidget(tabs, 1);
    rootLayout->addWidget(body, 1);
    auto* footer = new QStatusBar(root);
    footer->showMessage(QStringLiteral("Data folder: %1").arg(QDir::toNativeSeparators(dataDir)));
    rootLayout->addWidget(footer);
    window.setCentralWidget(root);
    QObject::connect(minimize, &QPushButton::clicked, &window, &QWidget::showMinimized);
    QObject::connect(maximize, &QPushButton::clicked, &window, [&window, maximize] {
        const bool goingToMaximize = !window.isMaximized();
        if (goingToMaximize) window.showMaximized();
        else window.showNormal();
        maximize->setText(goingToMaximize ? QStringLiteral("Restore") : QStringLiteral("Maximize"));
    });
    QTimer clockTimer;
    QObject::connect(&clockTimer, &QTimer::timeout, &window, [clockLabel] {
        clockLabel->setText(QDateTime::currentDateTime().toString(QStringLiteral("ddd, dd MMM yyyy  hh:mm:ss")));
    });
    clockTimer.start(1000);
    clockLabel->setText(QDateTime::currentDateTime().toString(QStringLiteral("ddd, dd MMM yyyy  hh:mm:ss")));

    auto* dashboard = new QWidget(tabs);
    auto* dashboardLayout = new QVBoxLayout(dashboard);
    dashboardLayout->setContentsMargins(16, 14, 16, 12);
    dashboardLayout->setSpacing(8);
    auto* dashboardTitle = new QLabel(QStringLiteral("Business Dashboard"), dashboard);
    dashboardTitle->setObjectName(QStringLiteral("pageTitle"));
    dashboardTitle->setObjectName(QStringLiteral("dashboardTitle"));
    dashboardLayout->addWidget(dashboardTitle);
    auto* dashboardWelcome = new QLabel(QStringLiteral("Live figures from all recorded transactions"), dashboard);
    dashboardWelcome->setObjectName(QStringLiteral("dashboardWelcome"));
    dashboardLayout->addWidget(dashboardWelcome);
    auto* dashboardNote = new QLabel(dashboard);
    dashboardNote->setWordWrap(true);
    dashboardNote->setObjectName(QStringLiteral("dashboardNote"));
    auto* metricsGrid = new QGridLayout;
    metricsGrid->setContentsMargins(0, 0, 0, 0);
    metricsGrid->setHorizontalSpacing(6);
    metricsGrid->setVerticalSpacing(6);
    auto* metricsWidget = new QWidget(dashboard);
    metricsWidget->setLayout(metricsGrid);
    dashboardLayout->addWidget(metricsWidget);
    QHash<QString, QLabel*> metricValues;
    QVector<QPair<QString, QString>> metricDefinitions{
        {QStringLiteral("Cash at hand"), QStringLiteral("cash")},
        {QStringLiteral("Bank balance"), QStringLiteral("bank")}
    };
    const QVector<QPair<QString, QString>> stockMetrics = product.grain
        ? QVector<QPair<QString, QString>>{
              {QStringLiteral("Maize Stock"), QStringLiteral("Maize")},
              {QStringLiteral("Cassava Stock"), QStringLiteral("Cassava")},
              {QStringLiteral("Maize Flour Stock"), QStringLiteral("Maize Flour")},
              {QStringLiteral("Cassava Flour Stock"), QStringLiteral("Cassava Flour")},
              {QStringLiteral("Broken Feed Stock"), QStringLiteral("Broken Feed")},
              {QStringLiteral("Kyakyu Stock"), QStringLiteral("Kyakyu")}}
        : QVector<QPair<QString, QString>>{
              {QStringLiteral("Kakuta Stock"), QStringLiteral("Kakuta")},
              {QStringLiteral("Kibooko Stock"), QStringLiteral("Kibooko")},
              {QStringLiteral("Kasse Stock"), QStringLiteral("Kasse")}};
    for (const auto& stockMetric : stockMetrics)
        metricDefinitions.push_back({stockMetric.first, QStringLiteral("stock:") + stockMetric.second});
    const QString productLabel = product.grain ? QStringLiteral("Grain") : QStringLiteral("Coffee");
    metricDefinitions += {
        {productLabel + QStringLiteral(" bought"), QStringLiteral("boughtKg")},
        {productLabel + QStringLiteral(" sold"), QStringLiteral("soldKg")},
        {QStringLiteral("Sales revenue"), QStringLiteral("salesRevenue")},
        {QStringLiteral("Sales received"), QStringLiteral("salesReceived")},
        {QStringLiteral("TUBANJA"), QStringLiteral("receivable")},
        {QStringLiteral("Milling revenue"), QStringLiteral("millingRevenue")},
        {QStringLiteral("Expenses"), QStringLiteral("expenses")},
        {QStringLiteral("Payroll paid"), QStringLiteral("workersPaid")},
        {QStringLiteral("ABATUBANJA"), QStringLiteral("payable")}
    };
    for (qsizetype i = 0; i < metricDefinitions.size(); ++i) {
        const auto& definition = metricDefinitions.at(i);
        auto* card = new QFrame(metricsWidget);
        card->setObjectName(QStringLiteral("dashboardCard"));
        card->setFrameShape(QFrame::StyledPanel);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 8);
        cardLayout->setSpacing(0);
        auto* accent = new QFrame(card);
        accent->setObjectName(QStringLiteral("dashboardAccent"));
        accent->setFixedHeight(3);
        auto* caption = new QLabel(definition.first, card);
        caption->setObjectName(QStringLiteral("dashboardCaption"));
        caption->setAlignment(Qt::AlignCenter);
        auto* value = new QLabel(QStringLiteral("—"), card);
        value->setObjectName(QStringLiteral("dashboardMetricValue"));
        value->setMinimumHeight(34);
        value->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(accent);
        cardLayout->addWidget(caption);
        cardLayout->addWidget(value);
        metricsGrid->addWidget(card, static_cast<int>(i / 4), static_cast<int>(i % 4));
        metricValues.insert(definition.second, value);
    }
    for (int column = 0; column < 4; ++column)
        metricsGrid->setColumnStretch(column, 1);
    dashboardLayout->addWidget(dashboardNote);
    auto* quickActions = new QGroupBox(QStringLiteral("Quick actions"), dashboard);
    auto* quickActionsLayout = new QHBoxLayout(quickActions);
    quickActionsLayout->setContentsMargins(0, 12, 0, 0);
    quickActionsLayout->setSpacing(6);
    const auto addQuickAction = [quickActions, quickActionsLayout, &navigation, selectPage]
        (const QString& label, const QString& pageName) {
            const auto page = std::find_if(navigation.cbegin(), navigation.cend(),
                [&pageName](const auto& item) { return item.first->text() == pageName; });
            if (page == navigation.cend()) return;
            auto* action = new QPushButton(label, quickActions);
            action->setObjectName(QStringLiteral("quickAction"));
            action->setCursor(Qt::PointingHandCursor);
            QObject::connect(action, &QPushButton::clicked, quickActions, [selectPage, pageWidget = page->second] {
                selectPage(pageWidget);
            });
            quickActionsLayout->addWidget(action);
        };
    dashboardLayout->addWidget(quickActions);
    auto* developerCredit = new QLabel(
        QStringLiteral("Designed by Mutebi Auhad Jaber  |  JM Group  |  mutebiauhad4@gmail.com"), dashboard);
    developerCredit->setObjectName(QStringLiteral("developerCredit"));
    developerCredit->setAlignment(Qt::AlignCenter);
    dashboardLayout->addWidget(developerCredit);
    const auto renderDashboard = [&store, metricValues, dashboardNote] {
        const QJsonObject data = store.dashboard();
        const QJsonObject stock = data.value(QStringLiteral("stockByProduct")).toObject();
        for (auto it = metricValues.cbegin(); it != metricValues.cend(); ++it) {
            const QString key = it.key();
            double value = 0;
            bool isWeight = false;
            if (key.startsWith(QStringLiteral("stock:"))) {
                value = stock.value(key.mid(6)).toDouble();
                isWeight = true;
            } else {
                value = data.value(key).toDouble();
                isWeight = key == QStringLiteral("boughtKg") || key == QStringLiteral("soldKg");
            }
            const QString formatted = isWeight
                ? dashboardWeightText(value) : dashboardMoneyText(value);
            it.value()->setText(formatted);
            it.value()->setProperty("negative", !isWeight && value < 0);
            it.value()->style()->unpolish(it.value());
            it.value()->style()->polish(it.value());
        }
        const double cash = data.value(QStringLiteral("cash")).toDouble();
        QString note = QStringLiteral("Cash and Bank = opening balance + recorded money movements.");
        if (cash < 0) {
            note += QStringLiteral(" CASH IS NEGATIVE: set the real opening cash in Settings > Opening Balances, "
                                   "or check for a payment made from Bank instead of Cash.");
        }
        dashboardNote->setText(note);
        dashboardNote->setProperty("negative", cash < 0);
        dashboardNote->style()->unpolish(dashboardNote);
        dashboardNote->style()->polish(dashboardNote);
    };
    try {
        renderDashboard();
    } catch (const std::exception& error) {
        QMessageBox::critical(&window, product.name(), QString::fromUtf8(error.what()));
    }
    if (store.roleFor(username) == QStringLiteral("Admin") || store.roleFor(username) == QStringLiteral("Manager"))
        addPage(dashboard, QStringLiteral("Dashboard"));
    const QString role = store.roleFor(username);
    const bool canWrite = role == QStringLiteral("Admin") || role == QStringLiteral("Manager") ||
                          role == QStringLiteral("Staff");
    const bool admin = store.roleFor(username) == QStringLiteral("Admin");
    if (role == QStringLiteral("Admin") || role == QStringLiteral("Manager"))
        addPage(createProgressPage(store, tabs), QStringLiteral("Progress"));
    if (role == QStringLiteral("Admin") || role == QStringLiteral("Manager"))
        addPage(createManagerReportsPage(store, username, admin, tabs), QStringLiteral("Manager Reports"));
    if (role == QStringLiteral("Admin") || role == QStringLiteral("Manager"))
        addPage(createDesktopChatPage(store, server, product, username, tabs), QStringLiteral("Messages"));
    addPage(createReportsPage(store, product, username, admin, tabs), QStringLiteral("Reports"));
    if (role != QStringLiteral("Guest")) {
        if (role == QStringLiteral("Admin") || role == QStringLiteral("Manager"))
            addPage(createPeoplePage(store, username, tabs), QStringLiteral("Registers"));
        addPage(new EntryForm(store, product, true, username, canWrite, tabs), QStringLiteral("Purchase"));
        addPage(new EntryForm(store, product, false, username, canWrite, tabs), QStringLiteral("Sale"));
        addPage(createMoneyPage(store, username, canWrite, tabs), QStringLiteral("Money"));
        addPage(createCreditAndDepositPage(store, username, canWrite, tabs), QStringLiteral("Credits & Deposits"));
        addPage(createWorkersPage(store, username, canWrite, tabs), QStringLiteral("Workers"));
        if (role == QStringLiteral("Admin") || role == QStringLiteral("Manager"))
            addPage(createTaxPage(store, username, canWrite, tabs), QStringLiteral("URA Tax"));
        addPage(createCoffeeMillingPage(store, username, canWrite, tabs), QStringLiteral("Milling"));
        if (!product.grain) {
            addPage(createCompanyMillingPage(store, username, canWrite, tabs), QStringLiteral("Company Milling"));
        }
        addPage(createDocumentStudioPage(dataDir, store.setting(QStringLiteral("NORMAL_PRINTER_NAME")), tabs,
                    [&store, &product] {
                        return store.setting(QStringLiteral("business_name"), product.name());
                    },
                    [&store] { return store.setting(QStringLiteral("BUSINESS_LOGO_PATH")); },
                    [&store] { return store.setting(QStringLiteral("NORMAL_PRINTER_NAME")); }),
                QStringLiteral("Documents"));
        addPage(createKiyambiPage(store.db(), product.grain, tabs), QStringLiteral("KIYAMBI"));
    }
    if (admin) {
        auto* settings = new QWidget(tabs);
        auto* settingsOuter = new QVBoxLayout(settings);
        auto* settingsScroll = new QScrollArea(settings);
        settingsScroll->setWidgetResizable(true);
        auto* settingsContent = new QWidget(settingsScroll);
        auto* settingsLayout = new QVBoxLayout(settingsContent);
        settingsScroll->setWidget(settingsContent);
        settingsOuter->addWidget(settingsScroll);
        auto* appearance = new QGroupBox(QStringLiteral("Appearance"), settingsContent);
        auto* appearanceForm = new QFormLayout(appearance);
        auto* darkMode = new QCheckBox(QStringLiteral("Use dark mode"), appearance);
        darkMode->setChecked(theme.dark);
        auto* primaryColor = new QPushButton(QStringLiteral("Choose accent color"), appearance);
        const auto pendingPrimary = std::make_shared<QColor>(theme.primary);
        const auto pendingBackground = std::make_shared<QColor>(theme.background);
        const auto showColor = [](QPushButton* button, const QColor& color) {
            button->setStyleSheet(QStringLiteral(
                "QPushButton { background:%1; color:%2; border:1px solid %3; "
                "border-radius:4px; padding:3px 9px; min-height:0; max-height:32px; }")
                .arg(color.name(),
                     color.lightness() < 130 ? QStringLiteral("#ffffff") : QStringLiteral("#1f2933"),
                     color.darker(120).name()));
            button->setMaximumWidth(260);
            button->setMaximumHeight(32);
        };
        showColor(primaryColor, theme.primary);
        auto* backgroundColor = new QPushButton(QStringLiteral("Choose page background"), appearance);
        showColor(backgroundColor, theme.background);
        auto* backgroundImage = new QLabel(appearance);
        const QString initialImage = store.setting(QStringLiteral("THEME_BACKGROUND_IMAGE"));
        backgroundImage->setText(initialImage.isEmpty()
            ? QStringLiteral("No background image selected") : QDir::toNativeSeparators(initialImage));
        backgroundImage->setTextInteractionFlags(Qt::TextSelectableByMouse);
        backgroundImage->setWordWrap(true);
        auto* chooseImage = new QPushButton(QStringLiteral("Choose background image…"), appearance);
        auto* clearImage = new QPushButton(QStringLiteral("Clear image"), appearance);
        auto* imageActions = new QHBoxLayout;
        imageActions->addWidget(chooseImage);
        imageActions->addWidget(clearImage);
        auto* saveAppearance = new QPushButton(QStringLiteral("Save appearance"), appearance);
        for (QPushButton* button : {backgroundColor, chooseImage, clearImage, saveAppearance}) {
            button->setMaximumWidth(260);
            button->setMaximumHeight(32);
        }
        appearanceForm->addRow(darkMode);
        appearanceForm->addRow(QStringLiteral("Accent / button color"), primaryColor);
        appearanceForm->addRow(QStringLiteral("Page background color"), backgroundColor);
        appearanceForm->addRow(QStringLiteral("Background image"), backgroundImage);
        appearanceForm->addRow(imageActions);
        appearanceForm->addRow(saveAppearance);
        settingsLayout->addWidget(appearance);
        QObject::connect(primaryColor, &QPushButton::clicked, &window,
            [primaryColor, showColor, pendingPrimary] {
            const QColor selected = QColorDialog::getColor(*pendingPrimary, primaryColor,
                QStringLiteral("Choose accent color"));
            if (selected.isValid()) {
                *pendingPrimary = selected;
                showColor(primaryColor, selected);
            }
        });
        QObject::connect(backgroundColor, &QPushButton::clicked, &window,
            [backgroundColor, showColor, pendingBackground] {
                const QColor selected = QColorDialog::getColor(*pendingBackground, backgroundColor,
                    QStringLiteral("Choose page background color"));
                if (selected.isValid()) {
                    *pendingBackground = selected;
                    showColor(backgroundColor, selected);
                }
            });
        QObject::connect(chooseImage, &QPushButton::clicked, &window, [&window, backgroundImage] {
            const QString path = QFileDialog::getOpenFileName(&window,
                QStringLiteral("Choose background image"), QDir::homePath(),
                QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
            if (!path.isEmpty())
                backgroundImage->setText(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
        });
        QObject::connect(clearImage, &QPushButton::clicked, &window, [backgroundImage] {
            backgroundImage->setText(QStringLiteral("No background image selected"));
        });
        QObject::connect(saveAppearance, &QPushButton::clicked, &window,
            [&store, &theme, darkMode, primaryColor, backgroundColor, backgroundImage,
             pendingPrimary, pendingBackground, showColor, applyTheme, &window, product] {
                const QColor primary = *pendingPrimary;
                const QColor background = *pendingBackground;
                if (!primary.isValid() || !background.isValid()) {
                    QMessageBox::warning(&window, product.name(),
                        QStringLiteral("Choose valid colors before saving."));
                    return;
                }
                const QString imageText = backgroundImage->text().trimmed();
                QString imagePath;
                if (imageText != QStringLiteral("No background image selected")) {
                    const QFileInfo imageInfo(imageText);
                    if (!imageInfo.isFile() || !imageInfo.isReadable()) {
                        QMessageBox::warning(&window, product.name(),
                            QStringLiteral("The selected background image cannot be read."));
                        return;
                    }
                    imagePath = imageInfo.absoluteFilePath();
                }
                try {
                    store.setSetting(QStringLiteral("THEME_DARK"), darkMode->isChecked()
                        ? QStringLiteral("1") : QStringLiteral("0"));
                    store.setSetting(QStringLiteral("THEME_PRIMARY"), primary.name());
                    store.setSetting(QStringLiteral("THEME_BACKGROUND"), background.name());
                    store.setSetting(QStringLiteral("THEME_BACKGROUND_IMAGE"), imagePath);
                    theme.dark = darkMode->isChecked();
                    theme.primary = primary;
                    theme.background = background;
                    theme.backgroundImage = imagePath;
                    applyTheme();
                    showColor(primaryColor, primary);
                    showColor(backgroundColor, background);
                    QMessageBox::information(&window, product.name(),
                        QStringLiteral("Appearance saved. Your theme is active and will be restored next time."));
                } catch (const std::exception& error) {
                    QMessageBox::critical(&window, product.name(), QString::fromUtf8(error.what()));
                }
            });
        auto* businessSettings = new QGroupBox(QStringLiteral("Business details and opening balances"), settingsContent);
        auto* businessForm = new QFormLayout(businessSettings);
        auto* businessName = new QLineEdit(store.setting(QStringLiteral("business_name"), QStringLiteral("Mateete Coffee Mill")), businessSettings);
        auto* location = new QLineEdit(store.setting(QStringLiteral("location")), businessSettings);
        auto* businessPhone = new QLineEdit(store.setting(QStringLiteral("business_phone")), businessSettings);
        auto* businessEmail = new QLineEdit(store.setting(QStringLiteral("business_email")), businessSettings);
        auto* openingCash = new QLineEdit(store.setting(QStringLiteral("OPENING_CASH"), QStringLiteral("0")), businessSettings);
        auto* openingBank = new QLineEdit(store.setting(QStringLiteral("OPENING_BANK"), QStringLiteral("0")), businessSettings);
        auto* saveBusiness = new QPushButton(QStringLiteral("Save Business Settings"), businessSettings);
        saveBusiness->setMaximumWidth(260);
        saveBusiness->setMaximumHeight(32);
        businessForm->addRow(QStringLiteral("Business name"), businessName);
        businessForm->addRow(QStringLiteral("Location"), location);
        businessForm->addRow(QStringLiteral("Business phone"), businessPhone);
        businessForm->addRow(QStringLiteral("Business email"), businessEmail);
        businessForm->addRow(QStringLiteral("Opening cash (UGX)"), openingCash);
        businessForm->addRow(QStringLiteral("Opening bank (UGX)"), openingBank);
        businessForm->addRow(saveBusiness);
        settingsLayout->addWidget(businessSettings);
        auto* branding = new QGroupBox(QStringLiteral("Business logo"), settingsContent);
        auto* brandingLayout = new QFormLayout(branding);
        auto* logoPreview = new QLabel(branding);
        logoPreview->setFixedSize(150, 72);
        logoPreview->setAlignment(Qt::AlignCenter);
        logoPreview->setFrameShape(QFrame::StyledPanel);
        const auto refreshLogoPreview = [logoPreview](const QString& path) {
            QPixmap preview(path);
            logoPreview->setPixmap(preview.isNull()
                ? QPixmap() : preview.scaled(140, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            logoPreview->setText(preview.isNull() ? QStringLiteral("No logo selected") : QString());
        };
        refreshLogoPreview(logoPath);
        auto* logoPathLabel = new QLabel(logoPath.isEmpty()
            ? QStringLiteral("Using the built-in Mill Manager logo")
            : QDir::toNativeSeparators(logoPath), branding);
        logoPathLabel->setWordWrap(true);
        auto* chooseLogo = new QPushButton(QStringLiteral("Choose business logo"), branding);
        auto* clearLogo = new QPushButton(QStringLiteral("Use built-in logo"), branding);
        auto* saveLogo = new QPushButton(QStringLiteral("Save business logo"), branding);
        auto* logoActions = new QHBoxLayout;
        logoActions->addWidget(chooseLogo);
        logoActions->addWidget(clearLogo);
        brandingLayout->addRow(QStringLiteral("Preview"), logoPreview);
        brandingLayout->addRow(QStringLiteral("Selected file"), logoPathLabel);
        brandingLayout->addRow(logoActions);
        brandingLayout->addRow(saveLogo);
        settingsLayout->addWidget(branding);
        QObject::connect(chooseLogo, &QPushButton::clicked, &window,
            [&window, logoPathLabel, refreshLogoPreview] {
                const QString path = QFileDialog::getOpenFileName(&window,
                    QStringLiteral("Choose business logo"), QDir::homePath(),
                    QStringLiteral("Image files (*.png *.jpg *.jpeg *.bmp *.webp)"));
                if (path.isEmpty())
                    return;
                if (QPixmap(path).isNull()) {
                    QMessageBox::warning(&window, QStringLiteral("Business logo"),
                        QStringLiteral("The selected file is not a readable image."));
                    return;
                }
                const QString absolute = QFileInfo(path).absoluteFilePath();
                logoPathLabel->setText(QDir::toNativeSeparators(absolute));
                refreshLogoPreview(absolute);
            });
        QObject::connect(clearLogo, &QPushButton::clicked, &window, [logoPathLabel, refreshLogoPreview] {
            logoPathLabel->setText(QStringLiteral("Using the built-in Mill Manager logo"));
            refreshLogoPreview(QStringLiteral(":/assets/mill-manager.png"));
        });
        QObject::connect(saveLogo, &QPushButton::clicked, &window,
            [&store, logoPathLabel, logo, &logoPath, &window, product, refreshLogoPreview] {
                const QString label = logoPathLabel->text();
                QString path;
                if (label != QStringLiteral("Using the built-in Mill Manager logo")) {
                    path = QDir::fromNativeSeparators(label);
                    if (!QFileInfo(path).isFile() || QPixmap(path).isNull()) {
                        QMessageBox::warning(&window, product.name(),
                            QStringLiteral("Choose a readable image file before saving the logo."));
                        return;
                    }
                    path = QFileInfo(path).absoluteFilePath();
                }
                try {
                    store.setSetting(QStringLiteral("BUSINESS_LOGO_PATH"), path);
                    logoPath = path;
                    QPixmap pixmap(path);
                    if (pixmap.isNull())
                        pixmap.load(QStringLiteral(":/assets/mill-manager.png"));
                    logo->setPixmap(pixmap.scaled(54, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    refreshLogoPreview(path.isEmpty() ? QStringLiteral(":/assets/mill-manager.png") : path);
                    QMessageBox::information(&window, product.name(),
                        QStringLiteral("Business logo saved. It will appear on receipts, reports, and printed/exported documents."));
                } catch (const std::exception& error) {
                    QMessageBox::critical(&window, product.name(), QString::fromUtf8(error.what()));
                }
            });
        auto* printerSettings = new QGroupBox(QStringLiteral("Receipt and document printers"), settingsContent);
        auto* printerForm = new QFormLayout(printerSettings);
        auto* posPrinter = new QComboBox(printerSettings);
        auto* normalPrinter = new QComboBox(printerSettings);
        posPrinter->setEditable(true);
        normalPrinter->setEditable(true);
        for (const QPrinterInfo& printerInfo : QPrinterInfo::availablePrinters()) {
            posPrinter->addItem(printerInfo.printerName());
            normalPrinter->addItem(printerInfo.printerName());
        }
        QString savedPosPrinter = store.setting(QStringLiteral("POS_PRINTER_NAME")).trimmed();
        if (savedPosPrinter.isEmpty())
            savedPosPrinter = detectedPosPrinterName();
        posPrinter->setCurrentText(savedPosPrinter);
        normalPrinter->setCurrentText(store.setting(QStringLiteral("NORMAL_PRINTER_NAME")).trimmed());
        posPrinter->setPlaceholderText(QStringLiteral("Select a detected POS / thermal printer"));
        normalPrinter->setPlaceholderText(QStringLiteral("Select normal printer or use system default"));
        auto* paperWidth = new QComboBox(printerSettings);
        paperWidth->addItems({QStringLiteral("Auto-detect POS roll width"),
                              QStringLiteral("58 mm"), QStringLiteral("80 mm")});
        const QString savedWidth = store.setting(QStringLiteral("POS_PAPER_WIDTH_MM"));
        if (savedWidth == QStringLiteral("58"))
            paperWidth->setCurrentIndex(1);
        else if (savedWidth == QStringLiteral("80"))
            paperWidth->setCurrentIndex(2);
        auto* printerStatus = new QLabel(printerSettings);
        const auto updatePrinterSuggestion = [posPrinter, paperWidth, printerStatus](bool resetWidth) {
            const QString name = posPrinter->currentText().trimmed();
            const int width = detectedReceiptWidthMm(name);
            if (resetWidth)
                paperWidth->setCurrentIndex(0);
            printerStatus->setText(name.isEmpty()
                ? QStringLiteral("No POS printer selected. Connect one or select its installed printer entry.")
                : QStringLiteral("Receipt roll auto-detected as approximately %1 mm. "
                                 "The app prints receipts directly to the selected thermal printer.")
                    .arg(width));
        };
        updatePrinterSuggestion(false);
        auto* detectPrinters = new QPushButton(QStringLiteral("Detect connected printers"), printerSettings);
        detectPrinters->setMaximumWidth(260);
        detectPrinters->setMaximumHeight(32);
        auto* savePrinters = new QPushButton(QStringLiteral("Save printer routing"), printerSettings);
        savePrinters->setMaximumWidth(260);
        savePrinters->setMaximumHeight(32);
        printerForm->addRow(QStringLiteral("POS receipt printer"), posPrinter);
        printerForm->addRow(QStringLiteral("POS paper width"), paperWidth);
        printerForm->addRow(printerStatus);
        printerForm->addRow(detectPrinters);
        printerForm->addRow(QStringLiteral("Normal document printer"), normalPrinter);
        printerForm->addRow(savePrinters);
        settingsLayout->addWidget(printerSettings);
        QObject::connect(posPrinter, &QComboBox::currentTextChanged, &window,
            [updatePrinterSuggestion] { updatePrinterSuggestion(true); });
        QObject::connect(detectPrinters, &QPushButton::clicked, &window,
            [posPrinter, normalPrinter, updatePrinterSuggestion] {
                const QString previousPos = posPrinter->currentText();
                const QString previousNormal = normalPrinter->currentText();
                posPrinter->clear();
                normalPrinter->clear();
                for (const QPrinterInfo& printerInfo : QPrinterInfo::availablePrinters()) {
                    posPrinter->addItem(printerInfo.printerName());
                    normalPrinter->addItem(printerInfo.printerName());
                }
                const QString detected = detectedPosPrinterName();
                posPrinter->setCurrentText(previousPos.trimmed().isEmpty() ? detected : previousPos);
                normalPrinter->setCurrentText(previousNormal);
                updatePrinterSuggestion(true);
            });
        QObject::connect(savePrinters, &QPushButton::clicked, &window, [&] {
            const QString pos = posPrinter->currentText().trimmed();
            const QString normal = normalPrinter->currentText().trimmed();
            if (!pos.isEmpty() && pos == normal) {
                QMessageBox::warning(&window, product.name(),
                    QStringLiteral("Choose different POS and normal printer names."));
                return;
            }
            store.setSetting(QStringLiteral("POS_PRINTER_NAME"), pos);
            store.setSetting(QStringLiteral("NORMAL_PRINTER_NAME"), normal);
            const int width = paperWidth->currentIndex() == 1 ? 58 :
                paperWidth->currentIndex() == 2 ? 80 : detectedReceiptWidthMm(pos);
            store.setSetting(QStringLiteral("POS_PAPER_WIDTH_MM"), QString::number(width));
            QMessageBox::information(&window, product.name(),
                QStringLiteral("Printer routing saved. POS receipts will print directly using the detected %1 mm roll; reports/documents use the normal printer.")
                    .arg(width));
        });
        QObject::connect(saveBusiness, &QPushButton::clicked, &window, [&] {
            bool cashOk = false, bankOk = false;
            const double cash = openingCash->text().toDouble(&cashOk);
            const double bank = openingBank->text().toDouble(&bankOk);
            if (!cashOk || !bankOk || !std::isfinite(cash) || !std::isfinite(bank) || cash < 0 || bank < 0) {
                QMessageBox::warning(&window, product.name(), QStringLiteral("Opening balances must be valid non-negative amounts."));
                return;
            }
            store.setSetting(QStringLiteral("business_name"), businessName->text().trimmed());
            store.setSetting(QStringLiteral("location"), location->text().trimmed());
            store.setSetting(QStringLiteral("business_phone"), businessPhone->text().trimmed());
            store.setSetting(QStringLiteral("business_email"), businessEmail->text().trimmed());
            store.setSetting(QStringLiteral("OPENING_CASH"), QString::number(cash, 'f', 2));
            store.setSetting(QStringLiteral("OPENING_BANK"), QString::number(bank, 'f', 2));
            QMessageBox::information(&window, product.name(), QStringLiteral("Business settings saved."));
        });
        settingsLayout->addWidget(new QLabel(
            QStringLiteral("For phone linking and encrypted chat, use the HTTPS URL from Tailscale Serve. "
                           "Generate a separate one-use code for each phone; entering the code automatically applies its Admin or Manager role."),
            settingsContent));
        auto* phoneForm = new QFormLayout();
        const quint16 defaultPhonePort = product.grain ? static_cast<quint16>(8766) : kPhonePort;
        auto* port = new QLineEdit(store.setting(QStringLiteral("PHONE_PORT"), QString::number(defaultPhonePort)), settingsContent);
        auto* autoStart = new QCheckBox(QStringLiteral("Start phone access automatically when an Admin signs in"), settingsContent);
        autoStart->setChecked(store.setting(QStringLiteral("PHONE_AUTO_START"), QStringLiteral("1")) == QStringLiteral("1"));
        auto* state = new QLabel(settingsContent);
        auto* addresses = new QLabel(settingsContent);
        addresses->setTextInteractionFlags(Qt::TextSelectableByMouse);
        phoneForm->addRow(QStringLiteral("Port"), port);
        phoneForm->addRow(autoStart);
        settingsLayout->addLayout(phoneForm);
        settingsLayout->addWidget(state);
        settingsLayout->addWidget(addresses);

        auto* actions = new QHBoxLayout();
        auto* start = new QPushButton(QStringLiteral("Start"), settingsContent);
        auto* newCode = new QPushButton(QStringLiteral("New Link Code"), settingsContent);
        auto* showAddress = new QPushButton(QStringLiteral("Show phone address"), settingsContent);
        auto* saveSettings = new QPushButton(QStringLiteral("Save Port & Auto-start"), settingsContent);
        auto* stop = new QPushButton(QStringLiteral("Stop"), settingsContent);
        auto* managerAccount = new QPushButton(QStringLiteral("Create Manager Account"), settingsContent);
        for (QPushButton* button : {start, newCode, showAddress, saveSettings, stop, managerAccount})
            actions->addWidget(button);
        settingsLayout->addLayout(actions);

        auto* linked = new QTableWidget(0, 7, settingsContent);
        configureBusinessTable(linked);
        linked->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Phone"),
            QStringLiteral("Admin / Manager"), QStringLiteral("Role"), QStringLiteral("Linked"),
            QStringLiteral("Last seen"), QStringLiteral("Status")});
        linked->setSelectionBehavior(QAbstractItemView::SelectRows);
        linked->setSelectionMode(QAbstractItemView::SingleSelection);
        linked->setEditTriggers(QAbstractItemView::NoEditTriggers);
        auto* linkedActions = new QHBoxLayout();
        auto* remove = new QPushButton(QStringLiteral("Remove Selected Phone"), settingsContent);
        auto* refresh = new QPushButton(QStringLiteral("Refresh"), settingsContent);
        linkedActions->addWidget(remove);
        linkedActions->addWidget(refresh);
        linkedActions->addStretch();
        settingsLayout->addWidget(new QLabel(QStringLiteral("Linked phones"), settings));
        settingsLayout->addWidget(linked);
        settingsLayout->addLayout(linkedActions);
        addPage(settings, QStringLiteral("Settings"));

        auto refreshPhones = [&] {
            const auto rows = store.linkedPhones();
            linked->setRowCount(rows.size());
            for (int row = 0; row < rows.size(); ++row) {
                for (int column = 0; column < rows[row].size(); ++column) {
                    auto* item = new QTableWidgetItem(rows[row][column]);
                    if (column == 0) item->setData(Qt::UserRole, rows[row][0].toLongLong());
                    linked->setItem(row, column, item);
                }
            }
            linked->resizeColumnsToContents();
        };
        auto updatePhoneStatus = [&] {
            if (server.isListening()) {
                state->setText(QStringLiteral("Status: ON"));
                addresses->setText(server.addressText(server.serverPort()));
            } else {
                state->setText(QStringLiteral("Status: OFF"));
                addresses->clear();
            }
        };
        auto startServer = [&] {
            bool ok = false;
            const int value = port->text().toInt(&ok);
            if (!ok || value < 1 || value > 65535) {
                QMessageBox::warning(&window, product.name(), QStringLiteral("Port must be between 1 and 65535."));
                return;
            }
            if (!server.start(static_cast<quint16>(value))) {
                QMessageBox::critical(&window, product.name(),
                    QStringLiteral("Could not start phone access on port %1: %2").arg(value).arg(server.errorString()));
                return;
            }
            updatePhoneStatus();
        };
        QObject::connect(start, &QPushButton::clicked, &window, startServer);
        QObject::connect(managerAccount, &QPushButton::clicked, &window, [&] {
            createManager(store, &window);
        });
        QObject::connect(stop, &QPushButton::clicked, &window, [&] {
            server.close();
            updatePhoneStatus();
        });
        QObject::connect(showAddress, &QPushButton::clicked, &window, [&] {
            if (!server.isListening()) {
                QMessageBox::information(&window, QStringLiteral("Phone address"),
                    QStringLiteral("Start phone access first. For encrypted phone linking, run this on the desktop:\n"
                                   "tailscale serve %1\n\nThen run `tailscale serve status` and open its HTTPS URL on the phone. "
                                   "The direct IP addresses below use HTTP and cannot be used for encrypted phone linking.\n\n"
                                   "Direct HTTP addresses:\n%2")
                        .arg(server.serverPort()).arg(server.addressText(server.serverPort())));
                return;
            }
            QMessageBox::information(&window, QStringLiteral("Phone address"),
                QStringLiteral("For end-to-end encrypted phone linking, run this on the desktop:\n"
                               "tailscale serve %1\n\nThen run `tailscale serve status` and open its HTTPS URL on the phone.\n\n"
                               "Direct HTTP addresses (not for new encrypted phone links):\n%2")
                    .arg(server.serverPort()).arg(server.addressText(server.serverPort())));
        });
        QObject::connect(saveSettings, &QPushButton::clicked, &window, [&] {
            bool ok = false;
            const int value = port->text().toInt(&ok);
            if (!ok || value < 1 || value > 65535) {
                QMessageBox::warning(&window, product.name(), QStringLiteral("Port must be between 1 and 65535."));
                return;
            }
            const bool restart = server.isListening();
            store.setSetting(QStringLiteral("PHONE_PORT"), QString::number(value));
            store.setSetting(QStringLiteral("PHONE_AUTO_START"), autoStart->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
            if (restart) startServer();
            else updatePhoneStatus();
        });
        QObject::connect(newCode, &QPushButton::clicked, &window, [&] {
            QDialog dialog(&window);
            dialog.setWindowTitle(QStringLiteral("Create phone link code"));
            auto* form = new QFormLayout(&dialog);
            auto* role = new QComboBox(&dialog);
            role->addItems({QStringLiteral("Admin"), QStringLiteral("Manager")});
            auto* account = new QComboBox(&dialog);
            account->addItem(username);
            account->addItems(store.managerUsers());
            form->addRow(QStringLiteral("Phone role"), role);
            form->addRow(QStringLiteral("Account"), account);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            form->addRow(buttons);
            QObject::connect(role, &QComboBox::currentTextChanged, &dialog, [&] {
                account->clear();
                if (role->currentText() == QStringLiteral("Admin")) account->addItem(username);
                else account->addItems(store.managerUsers());
            });
            QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted) return;
            if (role->currentText() == QStringLiteral("Manager") && account->count() == 0) {
                QMessageBox::warning(&window, product.name(), QStringLiteral("Create a Manager account before generating its phone link."));
                return;
            }
            if (!server.isListening()) {
                startServer();
                if (!server.isListening())
                    return;
            }
            try {
                const QString code = store.createPhoneLinkCode(account->currentText(), role->currentText());
                QMessageBox::information(&window, QStringLiteral("Phone link code"),
                    QStringLiteral("Run `tailscale serve %1` on this desktop and use the HTTPS URL from "
                                   "`tailscale serve status` on the phone.\n\nLink code: %2\n"
                                   "Phone role: %3 (assigned automatically when this code is entered)\n"
                                   "Account: %4\n\nThe code expires in 10 minutes and can be used once.")
                        .arg(server.serverPort()).arg(code)
                        .arg(role->currentText()).arg(account->currentText()));
            } catch (const std::exception& error) {
                QMessageBox::critical(&window, product.name(), QString::fromUtf8(error.what()));
            }
        });
        QObject::connect(remove, &QPushButton::clicked, &window, [&] {
            const int row = linked->currentRow();
            if (row < 0 || !linked->item(row, 0)) return;
            const qint64 id = linked->item(row, 0)->data(Qt::UserRole).toLongLong();
            if (QMessageBox::question(&window, product.name(), QStringLiteral("Remove this linked phone?")) != QMessageBox::Yes)
                return;
            store.removePhone(id);
            refreshPhones();
        });
        QObject::connect(refresh, &QPushButton::clicked, &window, [&] {
            try {
                refreshPhones();
            } catch (const std::exception& error) {
                QMessageBox::critical(&window, product.name(), QString::fromUtf8(error.what()));
            }
        });
        refreshPhones();
        updatePhoneStatus();
        if (autoStart->isChecked()) startServer();
    }
    auto* pinButton = new QPushButton(QStringLiteral("Change PIN"), dashboard);
    QObject::connect(pinButton, &QPushButton::clicked, &window, [&] { changePin(store, username, &window); });
    auto* sidebarSpacer = new QWidget(sidebar);
    sidebarSpacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    sidebarLayout->addWidget(sidebarSpacer);
    if (store.roleFor(username) != QStringLiteral("Guest")) {
        pinButton->setParent(sidebar);
        pinButton->setObjectName(QStringLiteral("sideAction"));
        sidebarLayout->addWidget(pinButton);
    }
    auto* switchAccountButton = new QPushButton(QStringLiteral("Switch account"), sidebar);
    switchAccountButton->setObjectName(QStringLiteral("sideAction"));
    QObject::connect(switchAccountButton, &QPushButton::clicked, &window, [&window, product] {
        if (QMessageBox::question(&window, product.name(),
                QStringLiteral("Sign out and sign in as another account?\n\n"
                               "Any unsaved form entries will be lost. Saved business records will remain."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QApplication::exit(kSwitchAccountExitCode);
    });
    sidebarLayout->addWidget(switchAccountButton);
    auto* exitButton = new QPushButton(QStringLiteral("Exit"), sidebar);
    exitButton->setObjectName(QStringLiteral("sideAction"));
    QObject::connect(exitButton, &QPushButton::clicked, &window, &QWidget::close);
    sidebarLayout->addWidget(exitButton);

    if (product.grain) {
        auto* milling = new QWidget(tabs);
        auto* form = new QFormLayout(milling);
        auto* inputProduct = new QComboBox(milling);
        inputProduct->addItems({QStringLiteral("Maize"), QStringLiteral("Cassava")});
        auto* inputKg = new QLineEdit(milling);
        auto* flour = new QLineEdit(QStringLiteral("0"), milling);
        auto* feed = new QLineEdit(QStringLiteral("0"), milling);
        auto* kyakyu = new QLineEdit(QStringLiteral("0"), milling);
        form->addRow(QStringLiteral("Input grain"), inputProduct);
        form->addRow(QStringLiteral("Input weight (kg)"), inputKg);
        form->addRow(QStringLiteral("Flour output (kg)"), flour);
        form->addRow(QStringLiteral("Chicken feed / broken grain (kg)"), feed);
        form->addRow(QStringLiteral("Kyakyu husks (kg)"), kyakyu);
        auto* save = new QPushButton(QStringLiteral("Save milling run"), milling);
        save->setEnabled(canWrite);
        form->addRow(save);
        QObject::connect(save, &QPushButton::clicked, &window, [&] {
            bool a = false, b = false, c = false, d = false;
            const double input = inputKg->text().toDouble(&a);
            const double flourValue = flour->text().toDouble(&b);
            const double feedValue = feed->text().toDouble(&c);
            const double kyakyuValue = kyakyu->text().toDouble(&d);
            if (!a || !b || !c || !d) {
                QMessageBox::warning(&window, product.name(), QStringLiteral("Enter valid weights."));
                return;
            }
            try {
                store.saveMilling(username, inputProduct->currentText(), input, flourValue, feedValue, kyakyuValue);
                QMessageBox::information(&window, product.name(), QStringLiteral("Milling run recorded."));
            } catch (const std::exception& error) {
                QMessageBox::critical(&window, product.name(), QString::fromUtf8(error.what()));
            }
        });
        addPage(milling, QStringLiteral("Company Milling"));
    }
    addQuickAction(QStringLiteral("Record Purchase"), QStringLiteral("Purchase"));
    addQuickAction(QStringLiteral("Record Milling"), QStringLiteral("Milling"));
    addQuickAction(QStringLiteral("Record Sale"), QStringLiteral("Sale"));
    addQuickAction(QStringLiteral("Money In / Out"), QStringLiteral("Money"));
    addQuickAction(QStringLiteral("View Reports"), QStringLiteral("Reports"));
    QTimer dashboardRefresh;
    QObject::connect(&dashboardRefresh, &QTimer::timeout, &window, [&, renderDashboard] {
        try {
            renderDashboard();
        } catch (const std::exception& error) {
            dashboardRefresh.stop();
            QMessageBox::critical(&window, product.name(), QString::fromUtf8(error.what()));
        }
    });
    dashboardRefresh.start(3000);
    window.show();
    return QApplication::exec();
}

QByteArray phoneHttpRequest(quint16 port, const QByteArray& request) {
    QTcpSocket socket;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QByteArray response;
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&socket, &request] {
        socket.write(request);
    });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&socket, &response] {
        response += socket.readAll();
    });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(3000);
    socket.connectToHost(QHostAddress::LocalHost, port);
    loop.exec();
    response += socket.readAll();
    return response;
}

QByteArray phoneHttpBody(const QByteArray& response) {
    const qsizetype separator = response.indexOf("\r\n\r\n");
    return separator < 0 ? QByteArray() : response.mid(separator + 4);
}

QString testPhonePublicKey() {
    return QString::fromLatin1(QByteArray(91, 'x').toBase64());
}

QJsonObject testPhoneEnvelope(char seed) {
    return QJsonObject{{QStringLiteral("iv"), QString::fromLatin1(QByteArray(12, seed).toBase64())},
                       {QStringLiteral("ciphertext"),
                        QString::fromLatin1(QByteArray(32, static_cast<char>(seed + 1)).toBase64())}};
}

void runSelfTests() {
    QTemporaryDir temp;
    if (!temp.isValid()) throw std::runtime_error("Could not create a temporary test directory.");
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    for (int session = 0; session < 2; ++session) {
        QTimer::singleShot(0, [] { QApplication::exit(kSwitchAccountExitCode); });
        require(QApplication::exec() == kSwitchAccountExitCode,
                "The application event loop could not restart for account switching.");
    }
    QStyle* compactBase = QStyleFactory::create(QStringLiteral("Fusion"));
    require(compactBase, "The Fusion style was unavailable for compact-button verification.");
    CompactButtonStyle compactStyle(compactBase);
    QStyleOptionButton buttonOption;
    const QSize compactButtonSize = compactStyle.sizeFromContents(
        QStyle::CT_PushButton, &buttonOption, QSize(240, 72), nullptr);
    require(compactButtonSize.height() <= 32,
            "The application style did not constrain oversized buttons.");
    AppTheme darkTheme;
    darkTheme.dark = true;
    darkTheme.primary = QColor(QStringLiteral("#553399"));
    darkTheme.background = QColor(QStringLiteral("#112233"));
    darkTheme.backgroundImage = temp.filePath(QStringLiteral("wall paper.png"));
    const QString darkStyle = appStyleSheet(darkTheme);
    require(darkStyle.contains(QStringLiteral("#553399")) &&
            darkStyle.contains(QStringLiteral("#112233")) &&
            darkStyle.contains(QStringLiteral("#2b3239")) &&
            darkStyle.contains(QStringLiteral("padding:3px 9px; min-height:0px; max-height:32px")) &&
            darkStyle.contains(QStringLiteral("QPushButton:pressed")) &&
            darkStyle.contains(QStringLiteral("background-image:url(")),
            "Custom colors, dark mode, compact button styling, or background image were not applied to the application theme.");
    const QString logoPath = temp.filePath(QStringLiteral("business logo.png"));
    QPixmap testLogo(32, 32);
    testLogo.fill(QColor(QStringLiteral("#286b43")));
    require(testLogo.save(logoPath), "The business-logo print test image could not be created.");
    const QString brandHtml = businessBrandHtml(QStringLiteral("Test Mill & Co"), logoPath);
    require(brandHtml.contains(QStringLiteral("Test Mill &amp; Co")) &&
            brandHtml.contains(QStringLiteral("business%20logo.png")),
            "The business logo/name was not safely included in branded document output.");
    const QString receiptPdfPath = temp.filePath(QStringLiteral("pos-receipt-test.pdf"));
    QPrinter receiptPdf(QPrinter::HighResolution);
    receiptPdf.setOutputFormat(QPrinter::PdfFormat);
    receiptPdf.setOutputFileName(receiptPdfPath);
    configureReceiptPage(receiptPdf, 58);
    const QSizeF receiptSizeMm = receiptPdf.pageLayout().pageSize().size(QPageSize::Millimeter);
    require(std::abs(receiptSizeMm.width() - 58) < 1,
            "The receipt printer did not receive the selected 58 mm roll page width.");
    QTextDocument receiptDocument;
    receiptDocument.setHtml(QStringLiteral("<html><body>%1<h2>Test receipt</h2>"
        "<p>UGX 12,000</p></body></html>").arg(brandHtml));
    receiptDocument.print(&receiptPdf);
    require(QFileInfo(receiptPdfPath).isFile() && QFileInfo(receiptPdfPath).size() > 0,
            "Thermal-width receipt layout failed to render to a printable PDF.");
    const QString pdfPath = temp.filePath(QStringLiteral("report-test.pdf"));
    QPrinter pdfPrinter(QPrinter::HighResolution);
    pdfPrinter.setOutputFormat(QPrinter::PdfFormat);
    pdfPrinter.setOutputFileName(pdfPath);
    QTextDocument pdfDocument;
    pdfDocument.setHtml(QStringLiteral("<h1>Mill Manager report test</h1><p>PDF export</p>"));
    pdfDocument.print(&pdfPrinter);
    require(QFileInfo(pdfPath).isFile() && QFileInfo(pdfPath).size() > 0,
            "PDF report generation did not create a file.");
    std::unique_ptr<QWidget> documentPage(createDocumentStudioPage(temp.path(), QString(), nullptr));
    auto* documentEditor = documentPage ? documentPage->findChild<QTextEdit*>() : nullptr;
    require(documentPage && documentEditor,
            "The document studio did not initialize its rich-text editor.");
    require(QDir(temp.path()).exists(QStringLiteral("Documents")),
            "The document studio did not create its documents folder.");
    bool hasSaveAction = false;
    for (QPushButton* button : documentPage->findChildren<QPushButton*>())
        hasSaveAction = hasSaveAction || button->text() == QStringLiteral("Save Document");
    require(hasSaveAction, "The document studio did not provide its save action.");
    documentEditor->setPlainText(QStringLiteral("Document formatting test"));
    QTextCursor documentCursor(documentEditor->document());
    documentCursor.select(QTextCursor::WordUnderCursor);
    documentEditor->setTextCursor(documentCursor);
    QPushButton* boldButton = nullptr;
    for (QPushButton* button : documentPage->findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Bold")) {
            boldButton = button;
            break;
        }
    }
    require(boldButton, "The document studio did not provide text formatting controls.");
    boldButton->click();
    require(documentEditor->textCursor().charFormat().fontWeight() == QFont::Bold,
            "The document studio Bold command did not format selected text.");
    {
        const Product coffee{false};
        Store assistantStore(coffee, temp.filePath(QStringLiteral("assistant.db")));
        std::unique_ptr<QWidget> assistantPage(createKiyambiPage(assistantStore.db(), false, nullptr));
        auto* assistantInput = assistantPage
            ? assistantPage->findChild<QLineEdit*>(QStringLiteral("kiyambiQuestion")) : nullptr;
        auto* assistantOutput = assistantPage
            ? assistantPage->findChild<QPlainTextEdit*>(QStringLiteral("kiyambiAnswer")) : nullptr;
        auto* assistantAsk = assistantPage
            ? assistantPage->findChild<QPushButton*>(QStringLiteral("kiyambiAsk")) : nullptr;
        require(assistantPage && assistantInput && assistantOutput && assistantAsk,
                "The KIYAMBI page did not initialize its input and answer controls.");
        assistantStore.savePurchase(QStringLiteral("owner"), QStringLiteral("Farmer F"),
            QStringLiteral("Kasse"), QStringLiteral("Grade A"), 100, 10, 0, QStringLiteral("Cash"));
        assistantStore.savePurchase(QStringLiteral("owner"), QStringLiteral("Farmer F"),
            QStringLiteral("Kibooko"), QStringLiteral("Grade A"), 50, 10, 0, QStringLiteral("Cash"));
        assistantStore.saveSale(QStringLiteral("owner"), QStringLiteral("Customer A"),
            QStringLiteral("Kasse"), QStringLiteral("Grade A"), 40, 20, 0, QStringLiteral("Cash"));
        const qint64 supplierId = assistantStore.personId(QStringLiteral("Supplier"), QStringLiteral("Farmer F"));
        assistantStore.saveCreditPayment(QStringLiteral("owner"), QStringLiteral("Supplier"),
            supplierId, 50, QStringLiteral("Cash"), QDate::currentDate(),
            QStringLiteral("PAY-1"), QStringLiteral("Supplier payment"));
        require(std::abs(kiyambi_detail::creditTotal(assistantStore.db(), false, false) - 1450) < 0.01,
                "KIYAMBI supplier payables were not aggregated correctly across purchases and payments.");
        require(kiyambi_detail::answer(assistantStore.db(), false, kiyambi_detail::Intent::History)
                    .contains(QStringLiteral("Farmer F")),
                "KIYAMBI purchase history did not include the supplier.");
        assistantInput->setText(QStringLiteral("show latest purchases"));
        assistantAsk->click();
        require(assistantOutput->toPlainText().contains(QStringLiteral("Farmer F")),
                "The KIYAMBI widget did not answer a recent-purchase question.");
        assistantInput->setText(QStringLiteral("what is the cash and bank balance"));
        assistantAsk->click();
        require(assistantOutput->toPlainText().contains(QStringLiteral("Cash balance:")) &&
                assistantOutput->toPlainText().contains(QStringLiteral("Bank balance:")),
                "The KIYAMBI widget did not answer a financial-balance question.");
        assistantInput->setText(QStringLiteral("Please give me business ideas to grow from our data"));
        assistantAsk->click();
        require(assistantOutput->toPlainText().contains(QStringLiteral("BUSINESS IDEAS")) &&
                assistantOutput->toPlainText().contains(QStringLiteral("Kasse")) &&
                assistantOutput->toPlainText().contains(QStringLiteral("800")),
                "KIYAMBI did not generate business ideas grounded in recent sales.");
        assistantInput->setText(QStringLiteral("why?"));
        assistantAsk->click();
        require(assistantOutput->toPlainText().contains(QStringLiteral("Following up on:")),
                "KIYAMBI did not maintain conversational context for follow-up questions.");
        require(kiyambi_detail::classify(QStringLiteral("Hello there!")) == kiyambi_detail::Intent::Greeting &&
                kiyambi_detail::classify(QStringLiteral("What should we do to grow our business?")) ==
                    kiyambi_detail::Intent::Ideas &&
                kiyambi_detail::classify(QStringLiteral("Can you help me?")) == kiyambi_detail::Intent::Help,
                "KIYAMBI did not classify conversational greetings, natural questions, and help requests.");
        const QString recommendations = kiyambi_detail::foundationInsights(assistantStore.db(), false);
        for (const QString& principle : {QStringLiteral("COST REDUCTION"), QStringLiteral("QUALITY CONTROL"),
                 QStringLiteral("STRATEGIC PLANNING"), QStringLiteral("COMPETITIVE ADVANTAGE"),
                 QStringLiteral("RISK MANAGEMENT"), QStringLiteral("TIME SAVINGS")}) {
            require(recommendations.contains(principle),
                    "KIYAMBI did not provide guidance for every requested business fundamental.");
        }
        const qint64 surveyId = kiyambi_detail::createSurvey(assistantStore.db(),
            QStringLiteral("Customer service"), QStringLiteral("Customers"),
            {QStringLiteral("How satisfied are you?"), QStringLiteral("What should we improve?")});
        require(kiyambi_detail::surveys(assistantStore.db()).size() == 1,
                "KIYAMBI did not persist the new survey.");
        kiyambi_detail::submitSurveyResponse(assistantStore.db(), surveyId, QString(),
            {QStringLiteral("Very satisfied"), QStringLiteral("Faster service")});
        const QString surveyReport = kiyambi_detail::surveyResults(assistantStore.db(), surveyId);
        require(surveyReport.contains(QStringLiteral("1 response")) &&
                surveyReport.contains(QStringLiteral("Very satisfied")) &&
                surveyReport.contains(QStringLiteral("Faster service")),
                "KIYAMBI survey responses were not saved and summarized.");
        require(kiyambi_detail::classify(QStringLiteral("make a survey for customers")) ==
                    kiyambi_detail::Intent::Surveys &&
                kiyambi_detail::classify(QStringLiteral("show risk and cost recommendations")) ==
                    kiyambi_detail::Intent::Insights,
                "KIYAMBI did not understand survey or business-insight requests.");
    }

    {
        const Product coffee{false};
        Store store(coffee, temp.filePath(QStringLiteral("coffee.db")));
        store.createManager(QStringLiteral("manager"), QStringLiteral("1234"));
        const QString desktopToken = store.pairPhone(
            store.createPhoneLinkCode(QStringLiteral("owner"), QStringLiteral("Admin"), true),
            QStringLiteral("Desktop chat"), testPhonePublicKey());
        const QString adminToken = store.pairPhone(
            store.createPhoneLinkCode(QStringLiteral("owner"), QStringLiteral("Admin")),
            QStringLiteral("Admin phone"), testPhonePublicKey());
        const QString managerToken = store.pairPhone(
            store.createPhoneLinkCode(QStringLiteral("manager"), QStringLiteral("Manager")),
            QStringLiteral("Manager phone"), testPhonePublicKey());
        const QString secondAdminToken = store.pairPhone(
            store.createPhoneLinkCode(QStringLiteral("owner"), QStringLiteral("Admin")),
            QStringLiteral("Second Admin phone"), testPhonePublicKey());
        PhoneSession session;
        require(store.phoneSession(adminToken, session) && session.role == QStringLiteral("Admin"),
                "Admin phone did not link.");
        require(store.phoneSession(managerToken, session) && session.role == QStringLiteral("Manager"),
                "Manager phone did not link alongside the Admin phone.");
        PhoneSession adminSession;
        PhoneSession managerSession;
        PhoneSession desktopSession;
        require(store.phoneSession(adminToken, adminSession) &&
                store.phoneSession(managerToken, managerSession) &&
                store.phoneSession(desktopToken, desktopSession) && desktopSession.desktopChat &&
                !adminSession.desktopChat && !managerSession.desktopChat,
                "Linked devices could not authenticate for messaging.");
        require(store.phoneContacts(adminSession).size() == 3 &&
                store.phoneContacts(managerSession).size() == 3 &&
                store.phoneContacts(desktopSession).size() == 3,
                "Linked phones did not discover every other active linked device for chat.");
        store.sendPhoneMessage(desktopSession, managerSession.id,
                               testPhoneEnvelope('g'), testPhoneEnvelope('h'));
        QJsonArray desktopMessages = store.phoneMessages(managerSession, 0, desktopSession.id);
        require(desktopMessages.size() == 1 &&
                desktopMessages.first().toObject().value(QStringLiteral("envelope")).isObject() &&
                store.phoneMessages(desktopSession, 0, managerSession.id).size() == 1,
                "The desktop chat participant could not send and receive encrypted messages.");
        store.sendPhoneMessage(managerSession, desktopSession.id,
                               testPhoneEnvelope('i'), testPhoneEnvelope('j'));
        require(store.phoneMessages(desktopSession, 1, managerSession.id).size() == 1 &&
                store.phoneMessages(managerSession, 1, desktopSession.id).size() == 1,
                "A linked phone could not reply to the desktop chat participant.");
        store.sendPhoneMessage(adminSession, managerSession.id,
                               testPhoneEnvelope('a'), testPhoneEnvelope('b'));
        QJsonArray managerMessages = store.phoneMessages(managerSession, 0, adminSession.id);
        require(managerMessages.size() == 1 &&
                managerMessages.first().toObject().value(QStringLiteral("envelope")).isObject() &&
                !managerMessages.first().toObject().contains(QStringLiteral("message")) &&
                store.phoneMessages(adminSession, 0, managerSession.id).size() == 1,
                "Encrypted Admin-to-Manager chat was not delivered as ciphertext to both devices.");
        const qint64 adminMessageId =
            managerMessages.first().toObject().value(QStringLiteral("id")).toInteger();
        store.sendPhoneMessage(managerSession, adminSession.id,
                               testPhoneEnvelope('c'), testPhoneEnvelope('d'));
        managerMessages = store.phoneMessages(managerSession, adminMessageId, adminSession.id);
        require(managerMessages.size() == 1 &&
                managerMessages.first().toObject().value(QStringLiteral("senderDeviceId")).toInteger() ==
                    managerSession.id &&
                store.phoneMessages(adminSession, adminMessageId, managerSession.id).size() == 1,
                "Encrypted Manager-to-Admin chat or incremental polling did not work.");
        const QJsonObject reportData = store.phoneReports(QDate(2000, 1, 1), QDate::currentDate(),
                                                          QStringLiteral("Profit Summary"), {});
        require(reportData.value(QStringLiteral("records")).toArray().size() == 7,
                "The linked-phone read-only report endpoint did not provide report data.");
        PhoneServer phoneServer(store, coffee);
        require(phoneServer.start(0), "The phone link server could not start on an available test port.");
        const QByteArray managerCookie = "mill_phone=" + managerToken.toLatin1();
        const QByteArray sessionResponse = phoneHttpRequest(phoneServer.serverPort(),
            "GET /api/session HTTP/1.1\r\nHost: 127.0.0.1\r\nCookie: " +
            managerCookie + "\r\nConnection: close\r\n\r\n");
        require(sessionResponse.startsWith("HTTP/1.1 200 OK") &&
                QJsonDocument::fromJson(phoneHttpBody(sessionResponse)).object()
                    .value(QStringLiteral("username")).toString() == QStringLiteral("manager") &&
                QJsonDocument::fromJson(phoneHttpBody(sessionResponse)).object()
                    .value(QStringLiteral("role")).toString() == QStringLiteral("Manager") &&
                !QJsonDocument::fromJson(phoneHttpBody(sessionResponse)).object()
                    .value(QStringLiteral("desktopChat")).toBool(),
                "The paired-phone HTTP session link did not authenticate its cookie.");
        const QByteArray desktopSessionResponse = phoneHttpRequest(phoneServer.serverPort(),
            "GET /api/session HTTP/1.1\r\nHost: localhost\r\nCookie: mill_phone=" +
            desktopToken.toLatin1() + "\r\nConnection: close\r\n\r\n");
        require(desktopSessionResponse.startsWith("HTTP/1.1 200 OK") &&
                QJsonDocument::fromJson(phoneHttpBody(desktopSessionResponse)).object()
                    .value(QStringLiteral("desktopChat")).toBool(),
                "The desktop chat HTTP session was not distinguished from a phone session.");
        const QByteArray phonePageResponse = phoneHttpRequest(phoneServer.serverPort(),
            "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
        const QByteArray phonePage = phoneHttpBody(phonePageResponse);
        require(phonePageResponse.startsWith("HTTP/1.1 200 OK") &&
                phonePage.contains("automatically links this phone as Admin or Manager") &&
                phonePage.contains("managerActions") && phonePage.contains("crypto.subtle") &&
                phonePage.contains("#desktop-pair=") && phonePage.contains("Secure desktop chat"),
                "The phone page did not explain automatic role linking, read-only controls, and encrypted chat.");
        require(phonePage.contains("id='chatList'") &&
                phonePage.indexOf("id='chat'") < phonePage.indexOf("id=\"recordsPanel\"") &&
                phonePage.contains("Select a linked device to open that private conversation."),
                "The phone page did not show the linked-device conversation list above business records.");
        const QByteArray contactsResponse = phoneHttpRequest(phoneServer.serverPort(),
            "GET /api/contacts HTTP/1.1\r\nHost: 127.0.0.1\r\nCookie: " +
            managerCookie + "\r\nConnection: close\r\n\r\n");
        require(contactsResponse.startsWith("HTTP/1.1 200 OK") &&
                QJsonDocument::fromJson(phoneHttpBody(contactsResponse)).array().size() == 3,
                "The paired-phone contacts endpoint did not expose all linked devices.");
        const QByteArray messageBody = QJsonDocument(QJsonObject{
            {QStringLiteral("recipientDeviceId"), static_cast<double>(adminSession.id)},
            {QStringLiteral("envelopeToRecipient"), testPhoneEnvelope('e')},
            {QStringLiteral("envelopeToSender"), testPhoneEnvelope('f')}
        }).toJson(QJsonDocument::Compact);
        const QByteArray messageRequest = "POST /api/messages HTTP/1.1\r\nHost: 127.0.0.1\r\nCookie: " +
            managerCookie + "\r\nContent-Type: application/json\r\nContent-Length: " +
            QByteArray::number(messageBody.size()) + "\r\nConnection: close\r\n\r\n" + messageBody;
        const QByteArray sentResponse = phoneHttpRequest(phoneServer.serverPort(), messageRequest);
        require(sentResponse.startsWith("HTTP/1.1 201 Created") &&
                store.phoneMessages(adminSession, 0, managerSession.id).size() == 3,
                "The paired-phone messaging HTTP endpoint did not store encrypted chat content.");
        const QByteArray plaintextBody = QJsonDocument(QJsonObject{
            {QStringLiteral("recipient"), QStringLiteral("owner")},
            {QStringLiteral("message"), QStringLiteral("This must never be stored as plaintext.")}
        }).toJson(QJsonDocument::Compact);
        const QByteArray plaintextRequest = "POST /api/messages HTTP/1.1\r\nHost: 127.0.0.1\r\nCookie: " +
            managerCookie + "\r\nContent-Type: application/json\r\nContent-Length: " +
            QByteArray::number(plaintextBody.size()) + "\r\nConnection: close\r\n\r\n" + plaintextBody;
        require(phoneHttpRequest(phoneServer.serverPort(), plaintextRequest).startsWith("HTTP/1.1 400 Bad Request") &&
                store.phoneMessages(adminSession, 0, managerSession.id).size() == 3,
                "The chat endpoint accepted a plaintext message or stored it.");
        const QByteArray readonlyBody = "{}";
        const QByteArray readonlyRequest = "POST /api/sales HTTP/1.1\r\nHost: 127.0.0.1\r\nCookie: " +
            QByteArray("mill_phone=" + adminToken.toLatin1()) +
            "\r\nContent-Type: application/json\r\nContent-Length: " +
            QByteArray::number(readonlyBody.size()) + "\r\nConnection: close\r\n\r\n" + readonlyBody;
        require(phoneHttpRequest(phoneServer.serverPort(), readonlyRequest).startsWith("HTTP/1.1 403 Forbidden"),
                "An Admin phone was able to submit a record instead of being read-only.");
        const QByteArray managerWriteRequest = "POST /api/sales HTTP/1.1\r\nHost: 127.0.0.1\r\nCookie: " +
            managerCookie + "\r\nContent-Type: application/json\r\nContent-Length: " +
            QByteArray::number(readonlyBody.size()) + "\r\nConnection: close\r\n\r\n" + readonlyBody;
        require(phoneHttpRequest(phoneServer.serverPort(), managerWriteRequest).startsWith("HTTP/1.1 400 Bad Request"),
                "The Manager phone could not reach its record-entry route.");
        const QByteArray reportsResponse = phoneHttpRequest(phoneServer.serverPort(),
            "GET /api/reports?from=2000-01-01&to=" +
            QDate::currentDate().toString(Qt::ISODate).toLatin1() +
            "&type=All&search= HTTP/1.1\r\nHost: 127.0.0.1\r\nCookie: " +
            QByteArray("mill_phone=" + adminToken.toLatin1()) +
            "\r\nConnection: close\r\n\r\n");
        require(reportsResponse.startsWith("HTTP/1.1 200 OK") &&
                QJsonDocument::fromJson(phoneHttpBody(reportsResponse)).object()
                    .value(QStringLiteral("records")).isArray(),
                "A linked Admin phone could not load read-only business records.");
        phoneServer.close();
        PhoneSession secondAdminSession;
        require(store.phoneSession(secondAdminToken, secondAdminSession),
                "The second Admin device was not available for the device-limit test.");
        bool adminLimitEnforced = false;
        try {
            store.pairPhone(store.createPhoneLinkCode(QStringLiteral("owner"), QStringLiteral("Admin")),
                            QStringLiteral("Third Admin phone"), testPhonePublicKey());
        } catch (const InputError&) {
            adminLimitEnforced = true;
        }
        bool managerLimitEnforced = false;
        try {
            store.pairPhone(store.createPhoneLinkCode(QStringLiteral("manager"), QStringLiteral("Manager")),
                            QStringLiteral("Second Manager phone"), testPhonePublicKey());
        } catch (const InputError&) {
            managerLimitEnforced = true;
        }
        require(adminLimitEnforced && managerLimitEnforced,
                "Phone pairing did not enforce the two Admin and one Manager device limits.");

        store.savePurchase(QStringLiteral("manager"), QStringLiteral("Supplier A"), QStringLiteral("Kasse"),
            QStringLiteral("Grade A"), 100, 10, 50, QStringLiteral("Cash"));
        store.saveSale(QStringLiteral("manager"), QStringLiteral("Customer A"), QStringLiteral("Kasse"),
            QStringLiteral("Grade A"), 10, 20, 100, QStringLiteral("Bank"));
        store.saveCoffeeMilling(QStringLiteral("manager"), QStringLiteral("Customer A"),
            QStringLiteral("Kibooko"), 20, 18, 2, 10, QStringLiteral("Cash"));
        const QJsonObject coffeeDashboard = store.dashboard();
        require(std::abs(coffeeDashboard.value(QStringLiteral("boughtKg")).toDouble() - 100) < 0.001 &&
                std::abs(coffeeDashboard.value(QStringLiteral("soldKg")).toDouble() - 10) < 0.001 &&
                std::abs(coffeeDashboard.value(QStringLiteral("stockByProduct")).toObject()
                             .value(QStringLiteral("Kasse")).toDouble() - 90) < 0.001 &&
                coffeeDashboard.contains(QStringLiteral("salesRevenue")) &&
                coffeeDashboard.contains(QStringLiteral("receivable")) &&
                coffeeDashboard.contains(QStringLiteral("payable")) &&
                coffeeDashboard.contains(QStringLiteral("workersPaid")),
                "Coffee dashboard metrics did not reflect the recorded business activity.");

        const QDate today = QDate::currentDate();
        auto rows = store.reportRows(today, today, QStringLiteral("All"), {});
        require(rows.size() >= 3, "Coffee purchase, sale, and milling records were not reported.");
        const auto purchase = std::find_if(rows.begin(), rows.end(),
            [](const ReportEntry& row) { return row.type == QStringLiteral("Purchase"); });
        require(purchase != rows.end(), "Purchase row was not found for editing.");
        const ReportEntry purchaseCopy = *purchase;
        const QDate editedDate = today.addDays(-1);
        store.editReportRecord(QStringLiteral("owner"), purchaseCopy, editedDate, QStringLiteral("Supplier A"),
            QStringLiteral("Kasse"), QStringLiteral("Grade A"), 90, 0, 10, 60, QStringLiteral("Bank"));
        rows = store.reportRows(editedDate, editedDate, QStringLiteral("Purchase"), purchaseCopy.reference);
        require(rows.size() == 1 && std::abs(rows.front().kg - 90) < 0.001 &&
                std::abs(rows.front().paid - 60) < 0.001 &&
                rows.front().account == QStringLiteral("Bank"),
                "Editing a purchase did not update the filtered report.");
        require(store.reportRows(editedDate, editedDate, QStringLiteral("Purchase"), {},
                                 QStringLiteral("Supplier A")).size() == 1,
                "The report person filter did not return the matching supplier.");

        QSqlQuery customer(store.db());
        require(customer.exec(QStringLiteral("SELECT id FROM customers WHERE name='Customer A'")) && customer.next(),
                "Customer created by a sale was not available for credit settlement.");
        const qint64 customerId = customer.value(0).toLongLong();
        require(store.personId(QStringLiteral("Customer"), QStringLiteral("Customer A")) == customerId,
                "Phone credit lookup did not resolve the registered customer.");
        store.saveCreditPayment(QStringLiteral("owner"), QStringLiteral("Customer"),
            customerId, 25, QStringLiteral("Cash"), today,
            QStringLiteral("CR-1"), QStringLiteral("Partial payment"));
        QSqlQuery supplier(store.db());
        require(supplier.exec(QStringLiteral("SELECT id FROM suppliers WHERE name='Supplier A'")) && supplier.next(),
                "Supplier created by a purchase was not available for credit settlement.");
        store.saveCreditPayment(QStringLiteral("owner"), QStringLiteral("Supplier"),
            supplier.value(0).toLongLong(), 25, QStringLiteral("Bank"), today,
            QStringLiteral("CR-2"), QStringLiteral("Supplier payment"));
        require(store.reportRows(today, today, QStringLiteral("Credit Payment"), {}).size() == 2,
                "Credit payment was not included in reports.");
        QSqlQuery supplierIdQuery(store.db());
        require(supplierIdQuery.exec(QStringLiteral("SELECT id FROM suppliers WHERE name='Supplier A'")) &&
                supplierIdQuery.next(), "The supplier record could not be linked.");
        const qint64 supplierId = supplierIdQuery.value(0).toLongLong();
        store.linkPersonRecords(QStringLiteral("owner"), supplierId, customerId);
        require(store.linkedPersonPairs().size() == 1,
                "The same customer and supplier were not linked.");
        const double customerBeforeOffset = store.creditPosition(QStringLiteral("Customer"), customerId);
        const double supplierBeforeOffset = store.creditPosition(QStringLiteral("Supplier"), supplierId);
        const double cashBeforeOffset = store.dashboard().value(QStringLiteral("cash")).toDouble();
        require(store.availableCreditOffset(supplierId, customerId) > 0,
                "The linked party's credit balances were not eligible for offset.");
        store.saveCreditOffset(QStringLiteral("owner"), supplierId, customerId, 25, today,
            QStringLiteral("OFF-1"), QStringLiteral("Same-person credit offset"));
        require(std::abs(store.creditPosition(QStringLiteral("Customer"), customerId) -
                         (customerBeforeOffset - 25)) < 0.01 &&
                std::abs(store.creditPosition(QStringLiteral("Supplier"), supplierId) -
                         (supplierBeforeOffset + 25)) < 0.01 &&
                std::abs(store.dashboard().value(QStringLiteral("cash")).toDouble() - cashBeforeOffset) < 0.01,
                "A credit offset did not reduce both balances without changing cash.");
        store.unlinkPersonRecords(QStringLiteral("owner"), QStringLiteral("Customer"), customerId);
        require(store.linkedPersonPairs().isEmpty(),
                "Unlinking a customer did not remove the same-person mapping.");
        store.saveBankDeposit(QStringLiteral("owner"), QStringLiteral("DEP-1"), today,
            QStringLiteral("Teller A"), QStringLiteral("RCP-1"), 1000,
            QStringLiteral("Test Bank"), QStringLiteral("Cash deposit"));
        require(store.reportRows(today, today, QStringLiteral("Bank Deposit"), {}).size() == 1,
                "Bank deposit was not included in reports.");
        store.savePurchase(QStringLiteral("manager"), QStringLiteral("Supplier A"), QStringLiteral("Kibooko"),
            QStringLiteral("Grade A"), 50, 10, 0, QStringLiteral("Cash"));
        store.savePerson(QStringLiteral("owner"), QStringLiteral("Customer"), 0,
            QStringLiteral("Customer B"), QStringLiteral("0771234567"), QStringLiteral("TIN-1"),
            QStringLiteral("Town"), QStringLiteral("Register test"), {}, QStringLiteral("Monthly Salary"), 0, true);
        store.savePerson(QStringLiteral("owner"), QStringLiteral("Supplier"), 0,
            QStringLiteral("Supplier B"), QStringLiteral("256771234567"), QStringLiteral("ID-1"),
            QStringLiteral("Town"), QStringLiteral("Register test"), {}, QStringLiteral("Monthly Salary"), 0, true);
        require(store.autoLinkPeople(QStringLiteral("owner")) == 1,
                "Matching customer and supplier phone numbers were not auto-linked.");
        require(store.people(QStringLiteral("Customer")).size() == 2,
                "The customer register did not retain both registered customers.");
        const auto stockForGrade = [&store](const QString& coffeeType) {
            QSqlQuery stock(store.db());
            stock.prepare(QStringLiteral("SELECT COALESCE(SUM(kg),0) FROM stock_movements "
                                         "WHERE coffee_type=? AND grade='Grade A'"));
            stock.addBindValue(coffeeType);
            if (!stock.exec() || !stock.next())
                throw std::runtime_error("Company stock totals could not be queried.");
            return stock.value(0).toDouble();
        };
        const double kibookoBeforeMilling = stockForGrade(QStringLiteral("Kibooko"));
        const double kasseBeforeMilling = stockForGrade(QStringLiteral("Kasse"));
        store.saveCompanyMilling(QStringLiteral("owner"), QStringLiteral("Kibooko"), QStringLiteral("Grade A"),
            20, 18, QStringLiteral("Grade A"), QStringLiteral("Internal processing"));
        require(store.reportRows(today, today, QStringLiteral("Company Milling"), {}).size() == 1,
                "Company-owned milling was not included in reports.");
        require(std::abs(stockForGrade(QStringLiteral("Kibooko")) -
                         (kibookoBeforeMilling - 20)) < 0.001 &&
                std::abs(stockForGrade(QStringLiteral("Kasse")) -
                         (kasseBeforeMilling + 18)) < 0.001,
                "Company Kibooko-to-Kasse milling did not decrease input stock and add output stock.");
        std::unique_ptr<QWidget> coffeeMillingPage(
            createCoffeeMillingPage(store, QStringLiteral("owner"), true, nullptr));
        const auto companySections = coffeeMillingPage->findChildren<QGroupBox*>();
        require(std::any_of(companySections.cbegin(), companySections.cend(), [](const QGroupBox* section) {
                    return section->title().contains(QStringLiteral("Kibooko to Kasse"));
                }),
                "The Kibooko-to-Kasse company stock form was not placed below the customer milling workflow.");
        require(store.reportRows(today, today, QStringLiteral("Customer Balances"), {}).size() == 1 &&
                store.reportRows(today, today, QStringLiteral("Open Credits"), {}).size() >= 2,
                "Customer and combined credit balance reports were not generated.");
        require(store.reportRows(today, today, QStringLiteral("Stock Balance"), {}).size() >= 2,
                "Stock balance report was not generated.");
        require(store.reportRows(today, today, QStringLiteral("Profit Summary"), {}).size() == 7,
                "Profit summary report was not generated.");
        require(!store.reportRows(today, today, QStringLiteral("Activity Log"), {}).isEmpty(),
                "Activity log report was not generated.");
        require(!store.reportRows(today, today, QStringLiteral("Daily Cash & Bank"), {}).isEmpty() &&
                !store.reportRows(today, today, QStringLiteral("Monthly Cash Flow"), {}).isEmpty(),
                "Cash and bank flow reports were not generated.");
        const auto progress = store.progressMonths(6);
        require(progress.size() == 6 && progress.back().month == today.toString(QStringLiteral("MMM yy")),
                "The business progress series did not include the requested months.");

        store.saveMoney(QStringLiteral("manager"), QStringLiteral("Cash"), QStringLiteral("OUT"),
            QStringLiteral("Fuel"), 5, today, QStringLiteral("FUEL-1"), QStringLiteral("Test fuel"));
        store.createWorker(QStringLiteral("owner"), QStringLiteral("Worker A"), QStringLiteral("Operator"),
            QStringLiteral("Monthly Salary"), 300000);
        store.savePayroll(QStringLiteral("owner"), store.activeWorkers().front(),
            today.toString(QStringLiteral("yyyy-MM")), 0, 0, 300000, calculatePaye(300000),
            0, 270000, 270000, today, QStringLiteral("Cash"), {});
        store.saveTaxPayment(QStringLiteral("owner"), QStringLiteral("PAYE"),
            today.toString(QStringLiteral("yyyy-MM")), 1000, today, QStringLiteral("TAX-1"), {},
            QStringLiteral("Bank"));
        require(store.reportRows(today, today, QStringLiteral("Money"), {}).size() >= 4,
                "Money activity was not available in reports.");
        require(store.reportRows(today, today, QStringLiteral("Payroll"), {}).size() == 1,
                "Payroll was not available in reports.");
        require(store.reportRows(today, today, QStringLiteral("Tax Payment"), {}).size() == 1,
                "Tax payment was not available in reports.");
        const qint64 reportId = store.submitManagerReport(QStringLiteral("manager"),
            today.toString(QStringLiteral("yyyy-MM")), QStringLiteral("Monthly report"),
            QStringLiteral("Test report submitted by the Manager."));
        const auto managerReports = store.managerReports();
        require(managerReports.size() == 1 && managerReports.front().id == reportId &&
                managerReports.front().readAt.isEmpty(),
                "The Manager's monthly report was not saved as unread.");
        bool managerReadDenied = false;
        try {
            store.openManagerReport(reportId, QStringLiteral("manager"));
        } catch (const InputError&) {
            managerReadDenied = true;
        }
        require(managerReadDenied, "A Manager was able to read Admin-only monthly reports.");
        const QString reportText = store.openManagerReport(reportId, QStringLiteral("owner"));
        require(reportText.contains(QStringLiteral("Test report submitted by the Manager.")) &&
                store.managerReports().front().readAt.size() > 0,
                "The Admin could not read and mark the Manager's monthly report.");
        std::unique_ptr<QWidget> managerReportsPage(
            createManagerReportsPage(store, QStringLiteral("manager"), false, nullptr));
        std::unique_ptr<QWidget> adminReportsPage(
            createManagerReportsPage(store, QStringLiteral("owner"), true, nullptr));
        std::unique_ptr<QWidget> filteredReportsPage(
            createReportsPage(store, coffee, QStringLiteral("owner"), true, nullptr));
        require(managerReportsPage && adminReportsPage && filteredReportsPage,
                "The report and Manager review screens did not initialize.");
        QTableWidget layoutSource(0, 3);
        layoutSource.horizontalHeader()->setSectionsMovable(true);
        layoutSource.horizontalHeader()->moveSection(0, 2);
        layoutSource.setColumnHidden(1, true);
        store.setSetting(QStringLiteral("REPORT_LAYOUT_TEST"),
                         QString::fromLatin1(layoutSource.horizontalHeader()->saveState().toBase64()));
        QTableWidget layoutRestored(0, 3);
        require(layoutRestored.horizontalHeader()->restoreState(QByteArray::fromBase64(
                    store.setting(QStringLiteral("REPORT_LAYOUT_TEST")).toLatin1())) &&
                layoutRestored.isColumnHidden(1) &&
                layoutRestored.horizontalHeader()->visualIndex(0) == 2,
                "A customized report column order or visibility setting was not saved.");
    }

    {
        const Product grain{true};
        Store store(grain, temp.filePath(QStringLiteral("grain.db")));
        store.createManager(QStringLiteral("manager"), QStringLiteral("1234"));
        store.savePurchase(QStringLiteral("manager"), QStringLiteral("Supplier G"), QStringLiteral("Maize"),
            QStringLiteral("Grade A"), 100, 5, 25, QStringLiteral("Cash"));
        store.saveMilling(QStringLiteral("manager"), QStringLiteral("Maize"), 20, 15, 3, 1);
        store.saveCoffeeMilling(QStringLiteral("manager"), QStringLiteral("Customer Mill G"),
            QStringLiteral("Maize"), 20, 18, 2, 0, QStringLiteral("Cash"));
        store.saveSale(QStringLiteral("manager"), QStringLiteral("Customer G"), QStringLiteral("Maize Flour"),
            QStringLiteral("Grade A"), 5, 8, 20, QStringLiteral("Bank"));
        const QJsonObject grainDashboard = store.dashboard();
        require(std::abs(grainDashboard.value(QStringLiteral("boughtKg")).toDouble() - 100) < 0.001 &&
                std::abs(grainDashboard.value(QStringLiteral("soldKg")).toDouble() - 5) < 0.001 &&
                grainDashboard.value(QStringLiteral("stockByProduct")).toObject()
                    .contains(QStringLiteral("Maize Flour")) &&
                std::abs(grainDashboard.value(QStringLiteral("millingRevenue")).toDouble() - 40) < 0.001 &&
                grainDashboard.contains(QStringLiteral("salesRevenue")) &&
                grainDashboard.contains(QStringLiteral("receivable")) &&
                grainDashboard.contains(QStringLiteral("payable")),
                "Grain dashboard metrics did not reflect the recorded business activity.");
        require(std::abs(kiyambi_detail::creditTotal(store.db(), true, true) - 60) < 0.01,
                "Grain customer milling charges were not included in customer receivables.");
        std::unique_ptr<QWidget> customerMillingPage(
            createCoffeeMillingPage(store, QStringLiteral("manager"), true, nullptr));
        require(customerMillingPage != nullptr, "The grain customer milling page did not initialize.");
        const QDate today = QDate::currentDate();
        const auto rows = store.reportRows(today, today, QStringLiteral("All"), {});
        require(std::any_of(rows.begin(), rows.end(),
                    [](const ReportEntry& row) { return row.type == QStringLiteral("Milling"); }),
                "Grain milling did not appear in the report.");
        require(std::any_of(rows.begin(), rows.end(),
                    [](const ReportEntry& row) { return row.type == QStringLiteral("Company Milling"); }),
                "Grain company milling did not appear in the report.");
        const auto purchase = std::find_if(rows.begin(), rows.end(),
            [](const ReportEntry& row) { return row.type == QStringLiteral("Purchase"); });
        require(purchase != rows.end(), "Grain purchase row was not found.");
        const ReportEntry purchaseCopy = *purchase;
        store.editReportRecord(QStringLiteral("owner"), purchaseCopy, today, QStringLiteral("Supplier G"),
            QStringLiteral("Maize"), QStringLiteral("Grade A"), 95, 0, 5, 30, QStringLiteral("Cash"));
        const auto filtered = store.reportRows(today, today, QStringLiteral("Purchase"), purchaseCopy.reference);
        require(filtered.size() == 1 && std::abs(filtered.front().kg - 95) < 0.001,
                "Editing a grain purchase did not update the report.");
        require(!store.reportRows(today, today, QStringLiteral("Supplier Balances"), {}).isEmpty() &&
                !store.reportRows(today, today, QStringLiteral("Stock Balance"), {}).isEmpty(),
                "Grain supplier and stock reports were not generated.");
        require(store.managerReportSummary(today.toString(QStringLiteral("yyyy-MM"))).contains(
                    QStringLiteral("Purchases:")),
                "The grain package Manager report summary was not generated.");
    }

    const QString legacyPath = temp.filePath(QStringLiteral("legacy.db"));
    {
        const QString connectionName = QStringLiteral("mill_manager_legacy_seed");
        QSqlDatabase seed = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        seed.setDatabaseName(legacyPath);
        if (!seed.open()) throw std::runtime_error(seed.lastError().text().toStdString());
        execSql(seed, QStringLiteral("CREATE TABLE audit_log(id INTEGER PRIMARY KEY,date TEXT,username TEXT,action TEXT,details TEXT)"));
        execSql(seed, QStringLiteral("INSERT INTO audit_log(date,username,action,details) "
                                     "VALUES(date('now','localtime'),'legacy','Existing entry','Keep this row')"));
        seed.close();
        seed = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
    {
        const Product coffee{false};
        Store store(coffee, legacyPath);
        QSqlQuery query(store.db());
        require(query.exec(QStringLiteral("SELECT details FROM audit_log WHERE username='legacy'")) &&
                query.next() && query.value(0).toString() == QStringLiteral("Keep this row"),
                "Additive database initialization did not preserve the legacy audit record.");
        require(store.reportRows(QDate::currentDate(), QDate::currentDate(),
                                 QStringLiteral("Activity Log"), {}).size() == 1,
                "The audit-log schema migration did not make legacy activity reportable.");
    }
}

void checkDatabase(const Product& product, const QString& path) {
    Store store(product, path);
    QSqlQuery integrity(store.db());
    if (!integrity.exec(QStringLiteral("PRAGMA integrity_check")) || !integrity.next() ||
        integrity.value(0).toString() != QStringLiteral("ok"))
        throw std::runtime_error("SQLite integrity_check failed.");

    const QJsonObject dashboard = store.dashboard();
    const QList<ReportEntry> rows = store.reportRows(
        QDate(1900, 1, 1), QDate(2100, 12, 31), QStringLiteral("All"), {});
    const QDate from(1900, 1, 1);
    const QDate to(2100, 12, 31);
    store.reportRows(from, to, QStringLiteral("Stock Balance"), {});
    store.reportRows(from, to, QStringLiteral("Customer Balances"), {});
    store.reportRows(from, to, QStringLiteral("Supplier Balances"), {});
    store.reportRows(from, to, QStringLiteral("Open Credits"), {});
    const int customerCount = store.people(QStringLiteral("Customer")).size();
    const int supplierCount = store.people(QStringLiteral("Supplier")).size();
    const QList<ProgressMonth> progress = store.progressMonths(6);
    if (progress.size() != 6)
        throw std::runtime_error("Business progress query returned an incomplete six-month series.");
    store.managerReportSummary(QDate::currentDate().toString(QStringLiteral("yyyy-MM")));
    for (const kiyambi_detail::Intent intent : {
             kiyambi_detail::Intent::Financials, kiyambi_detail::Intent::Inventory,
             kiyambi_detail::Intent::Receivables, kiyambi_detail::Intent::Payables,
             kiyambi_detail::Intent::History}) {
        if (kiyambi_detail::answer(store.db(), product.grain, intent).isEmpty())
            throw std::runtime_error("KIYAMBI returned an empty database diagnostic response.");
    }
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    QTextStream(stdout) << product.name() << ": database check passed; "
        << rows.size() << " report rows; " << customerCount << " customers; "
        << supplierCount << " suppliers; " << progress.size() << " progress months; bought kg "
        << locale.toString(dashboard.value(QStringLiteral("boughtKg")).toDouble(), 'f', 2)
        << "; sales UGX " << locale.toString(dashboard.value(QStringLiteral("salesRevenue")).toDouble(), 'f', 0)
        << "; milling UGX " << locale.toString(dashboard.value(QStringLiteral("millingRevenue")).toDouble(), 'f', 0)
        << "; receivables UGX " << locale.toString(dashboard.value(QStringLiteral("receivable")).toDouble(), 'f', 0)
        << "; payables UGX " << locale.toString(dashboard.value(QStringLiteral("payable")).toDouble(), 'f', 0) << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QStyle* fusionStyle = QStyleFactory::create(QStringLiteral("Fusion"));
    if (!fusionStyle) {
        QTextStream(stderr) << "Could not initialize the Fusion desktop style.\n";
        return 1;
    }
    app.setStyle(new CompactButtonStyle(fusionStyle));
    app.setStyleSheet(QStringLiteral(
        "QPushButton { padding:3px 9px; min-height:0; max-height:32px; "
        "border:0; border-radius:4px; }"));
    const Product product{MILL_MANAGER_GRAIN != 0};
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--self-test")) {
        try {
            runSelfTests();
            QTextStream(stdout) << "Self-test passed.\n";
            return 0;
        } catch (const std::exception& error) {
            QTextStream(stderr) << "Self-test failed: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--check-database")) {
        try {
            checkDatabase(product, QString::fromLocal8Bit(argv[2]));
            return 0;
        } catch (const std::exception& error) {
            QTextStream(stderr) << "Database check failed: " << error.what() << '\n';
            return 1;
        }
    }
    while (true) {
        try {
            const int result = runApp(product);
            if (result != kSwitchAccountExitCode)
                return result;
        } catch (const std::exception& error) {
            QMessageBox::critical(nullptr, product.name(), QString::fromUtf8(error.what()));
            return 1;
        }
    }
}
