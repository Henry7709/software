#pragma once

#include <QDate>
#include <QCoreApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPair>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextCursor>
#include <QTextStream>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <QStringConverter>

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace kiyambi_detail {

enum class Intent {
    Unknown, Greeting, Help, Financials, Inventory, Receivables,
    Payables, History, Insights, Ideas, Surveys
};

inline QStringList tokens(const QString& text) {
    static const QSet<QString> ignored{
        QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("and"), QStringLiteral("are"),
        QStringLiteral("can"), QStringLiteral("could"), QStringLiteral("do"), QStringLiteral("does"),
        QStringLiteral("for"), QStringLiteral("from"), QStringLiteral("give"), QStringLiteral("hello"),
        QStringLiteral("how"), QStringLiteral("i"), QStringLiteral("in"), QStringLiteral("is"),
        QStringLiteral("kindly"), QStringLiteral("me"), QStringLiteral("my"), QStringLiteral("of"),
        QStringLiteral("please"), QStringLiteral("show"), QStringLiteral("tell"), QStringLiteral("the"),
        QStringLiteral("to"), QStringLiteral("want"), QStringLiteral("what"), QStringLiteral("you")
    };
    QString normalized = text.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    QStringList result;
    QSet<QString> seen;
    for (const QString& word : normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!ignored.contains(word) && !word.at(0).isDigit() && !seen.contains(word)) {
            seen.insert(word);
            result.push_back(word);
        }
    }
    return result;
}

inline Intent intentForCsvLabel(const QString& label) {
    const QString value = label.trimmed().toLower();
    if (value == QStringLiteral("cash_balance") || value == QStringLiteral("bank_balance") ||
        value == QStringLiteral("all_balances"))
        return Intent::Financials;
    if (value == QStringLiteral("coffee_stock") || value == QStringLiteral("maize_stock"))
        return Intent::Inventory;
    if (value == QStringLiteral("customer_owes")) return Intent::Receivables;
    if (value == QStringLiteral("supplier_owes")) return Intent::Payables;
    if (value == QStringLiteral("purchase_history")) return Intent::History;
    return Intent::Unknown;
}

inline QStringList csvFields(const QString& line) {
    QStringList fields;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                field += ch;
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == QLatin1Char(',') && !quoted) {
            fields.push_back(field);
            field.clear();
        } else {
            field += ch;
        }
    }
    fields.push_back(field);
    return fields;
}

struct Example {
    QStringList words;
    Intent intent = Intent::Unknown;
};

inline const QList<Example>& phraseExamples() {
    static const QList<Example> examples = [] {
        QList<Example> loaded;
        const QStringList locations{
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("uganda_mill_ai_10000.csv")),
            QDir(QCoreApplication::applicationDirPath()).filePath(
                QStringLiteral("../MILL_MANAGER/uganda_mill_ai_10000.csv")),
            QDir::current().filePath(QStringLiteral("uganda_mill_ai_10000.csv")),
            QDir::current().filePath(QStringLiteral("MILL_MANAGER/uganda_mill_ai_10000.csv")),
            QDir::current().filePath(QStringLiteral("../MILL_MANAGER/uganda_mill_ai_10000.csv"))
        };
        for (const QString& path : locations) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            QTextStream stream(&file);
            stream.setEncoding(QStringConverter::Utf8);
            if (!stream.atEnd())
                stream.readLine();
            while (!stream.atEnd()) {
                const QStringList fields = csvFields(stream.readLine());
                if (fields.size() < 4)
                    continue;
                const Intent intent = intentForCsvLabel(fields.at(1));
                if (intent == Intent::Unknown)
                    continue;
                const QStringList phraseWords = tokens(fields.at(2) + QLatin1Char(' ') + fields.at(3));
                QStringList words;
                QSet<QString> seen;
                for (const QString& word : phraseWords) {
                    if (!seen.contains(word)) {
                        seen.insert(word);
                        words.push_back(word);
                    }
                }
                if (!words.isEmpty())
                    loaded.push_back({words, intent});
            }
            if (!loaded.isEmpty())
                break;
        }
        return loaded;
    }();
    return examples;
}

inline Intent classify(const QString& question) {
    const QStringList words = tokens(question);
    if (words.isEmpty())
        return Intent::Unknown;
    const QString normalized = question.toLower().trimmed();
    const QString greetingText = QString(normalized).remove(QRegularExpression(QStringLiteral("[.!?,]+$"))).simplified();
    static const QSet<QString> greetings{
        QStringLiteral("hi"), QStringLiteral("hello"), QStringLiteral("hey"),
        QStringLiteral("good morning"), QStringLiteral("good afternoon"),
        QStringLiteral("good evening"), QStringLiteral("thanks"), QStringLiteral("thank you")
    };
    if (greetings.contains(greetingText) ||
        QRegularExpression(QStringLiteral("^(hi|hello|hey)(\\s|$)")).match(greetingText).hasMatch())
        return Intent::Greeting;
    if (normalized.contains(QStringLiteral("help")) ||
        normalized == QStringLiteral("what can you do"))
        return Intent::Help;
    for (const QString& word : words) {
        if (word == QStringLiteral("survey") || word == QStringLiteral("surveys") ||
            word == QStringLiteral("feedback") || word == QStringLiteral("questionnaire"))
            return Intent::Surveys;
    }

    const QHash<Intent, QSet<QString>> synonyms{
        {Intent::Financials, {QStringLiteral("cash"), QStringLiteral("bank"), QStringLiteral("balance"),
            QStringLiteral("balances"), QStringLiteral("money"), QStringLiteral("funds"),
            QStringLiteral("account"), QStringLiteral("accounts"), QStringLiteral("ugx"),
            QStringLiteral("shilling"), QStringLiteral("shillings"), QStringLiteral("ssente"),
            QStringLiteral("ensimbi"), QStringLiteral("drawer"), QStringLiteral("wallet"),
            QStringLiteral("deposit"), QStringLiteral("financial"), QStringLiteral("financials"),
            QStringLiteral("capital"), QStringLiteral("income"), QStringLiteral("revenue")}},
        {Intent::Inventory, {QStringLiteral("stock"), QStringLiteral("stocks"), QStringLiteral("inventory"),
            QStringLiteral("kg"), QStringLiteral("kgs"), QStringLiteral("kilograms"), QStringLiteral("coffee"),
            QStringLiteral("maize"), QStringLiteral("cassava"), QStringLiteral("kasse"),
            QStringLiteral("kakuta"), QStringLiteral("kibooko"), QStringLiteral("flour"),
            QStringLiteral("kyakyu"), QStringLiteral("goods"), QStringLiteral("warehouse"),
            QStringLiteral("store"), QStringLiteral("remaining"), QStringLiteral("available")}},
        {Intent::Receivables, {QStringLiteral("customer"), QStringLiteral("customers"), QStringLiteral("buyer"),
            QStringLiteral("buyers"), QStringLiteral("client"), QStringLiteral("clients"), QStringLiteral("owe"),
            QStringLiteral("owes"), QStringLiteral("owed"), QStringLiteral("debt"), QStringLiteral("debts"),
            QStringLiteral("receivable"), QStringLiteral("receivables"), QStringLiteral("unpaid"),
            QStringLiteral("outstanding"), QStringLiteral("arrears"), QStringLiteral("invoice"),
            QStringLiteral("invoices"), QStringLiteral("tubanja"), QStringLiteral("uncollected")}},
        {Intent::Payables, {QStringLiteral("supplier"), QStringLiteral("suppliers"), QStringLiteral("farmer"),
            QStringLiteral("farmers"), QStringLiteral("payable"), QStringLiteral("payables"),
            QStringLiteral("liability"), QStringLiteral("liabilities"), QStringLiteral("purchase"),
            QStringLiteral("purchases"), QStringLiteral("owe"), QStringLiteral("owes"), QStringLiteral("owed"),
            QStringLiteral("debt"), QStringLiteral("debts"), QStringLiteral("unpaid"),
            QStringLiteral("outstanding"), QStringLiteral("abatubanja"), QStringLiteral("settle")}},
        {Intent::History, {QStringLiteral("bought"), QStringLiteral("purchased"), QStringLiteral("purchase"),
            QStringLiteral("purchases"), QStringLiteral("recent"), QStringLiteral("history"),
            QStringLiteral("latest"), QStringLiteral("previous"), QStringLiteral("batch"),
            QStringLiteral("batches"), QStringLiteral("procurement"), QStringLiteral("acquired"),
            QStringLiteral("records"), QStringLiteral("timeline")}},
        {Intent::Insights, {QStringLiteral("recommendation"), QStringLiteral("recommendations"),
            QStringLiteral("improve"), QStringLiteral("improvement"), QStringLiteral("risk"),
            QStringLiteral("quality"), QStringLiteral("strategy"), QStringLiteral("strategic"),
            QStringLiteral("competitive"), QStringLiteral("advantage"), QStringLiteral("reduce"),
            QStringLiteral("cost"), QStringLiteral("costs"), QStringLiteral("save"), QStringLiteral("savings")}},
        {Intent::Ideas, {QStringLiteral("idea"), QStringLiteral("ideas"), QStringLiteral("opportunity"),
            QStringLiteral("opportunities"), QStringLiteral("grow"), QStringLiteral("growth"),
            QStringLiteral("expand"), QStringLiteral("expansion"), QStringLiteral("business"),
            QStringLiteral("generative"), QStringLiteral("innovate"), QStringLiteral("innovation"),
            QStringLiteral("increase"), QStringLiteral("profit"), QStringLiteral("profitable"),
            QStringLiteral("earn"), QStringLiteral("earning"), QStringLiteral("market"),
            QStringLiteral("marketing"), QStringLiteral("customer"), QStringLiteral("customers")}},
        {Intent::Surveys, {QStringLiteral("survey"), QStringLiteral("surveys"),
            QStringLiteral("feedback"), QStringLiteral("questionnaire")}}
    };
    const QHash<Intent, QSet<QString>> cues{
        {Intent::Financials, {QStringLiteral("cash"), QStringLiteral("bank"), QStringLiteral("money"),
            QStringLiteral("funds"), QStringLiteral("ugx"), QStringLiteral("shillings")}},
        {Intent::Inventory, {QStringLiteral("stock"), QStringLiteral("inventory"), QStringLiteral("coffee"),
            QStringLiteral("maize"), QStringLiteral("cassava")}},
        {Intent::Receivables, {QStringLiteral("customer"), QStringLiteral("customers"), QStringLiteral("buyer"),
            QStringLiteral("receivable")}},
        {Intent::Payables, {QStringLiteral("supplier"), QStringLiteral("suppliers"), QStringLiteral("farmer"),
            QStringLiteral("payable")}},
        {Intent::History, {QStringLiteral("bought"), QStringLiteral("purchased"), QStringLiteral("purchase"),
            QStringLiteral("purchases"), QStringLiteral("batch")}},
        {Intent::Insights, {QStringLiteral("recommendation"), QStringLiteral("risk"),
            QStringLiteral("quality"), QStringLiteral("improve"), QStringLiteral("cost")}},
        {Intent::Ideas, {QStringLiteral("idea"), QStringLiteral("ideas"), QStringLiteral("grow"),
            QStringLiteral("growth"), QStringLiteral("profit"), QStringLiteral("business")}},
        {Intent::Surveys, {QStringLiteral("survey"), QStringLiteral("surveys"), QStringLiteral("feedback")}}
    };
    const QList<Intent> priority{Intent::Financials, Intent::Inventory, Intent::Receivables,
                                 Intent::Payables, Intent::History, Intent::Insights, Intent::Ideas,
                                 Intent::Surveys};
    Intent best = Intent::Unknown;
    int bestScore = 0;
    int bestCue = 0;
    int bestPriority = priority.size();
    for (int i = 0; i < priority.size(); ++i) {
        const Intent intent = priority.at(i);
        int score = 0;
        int cue = 0;
        for (const QString& word : words) {
            if (synonyms.value(intent).contains(word)) ++score;
            if (cues.value(intent).contains(word)) ++cue;
        }
        if (score == 0)
            continue;
        if (score > bestScore || (score == bestScore && cue > bestCue) ||
            (score == bestScore && cue == bestCue && i < bestPriority)) {
            best = intent;
            bestScore = score;
            bestCue = cue;
            bestPriority = i;
        }
    }

    int exampleScore = 0;
    Intent exampleIntent = Intent::Unknown;
    QSet<QString> queryWords;
    for (const QString& word : words)
        queryWords.insert(word);
    for (const Example& example : phraseExamples()) {
        int overlap = 0;
        for (const QString& word : example.words)
            if (queryWords.contains(word))
                ++overlap;
        if (overlap > exampleScore && overlap >= 2) {
            exampleScore = overlap;
            exampleIntent = example.intent;
        }
    }
    if ((best == Intent::Insights || best == Intent::Ideas || best == Intent::Surveys) && bestCue > 0)
        return best;
    return exampleScore >= bestScore && exampleIntent != Intent::Unknown ? exampleIntent : best;
}

inline QString number(double value, int precision = 2) {
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(value, 'f', precision);
}

inline void execute(QSqlQuery& query, const QString& operation) {
    if (!query.exec())
        throw std::runtime_error(QStringLiteral("%1: %2").arg(operation, query.lastError().text()).toStdString());
}

inline void prepare(QSqlQuery& query, const QString& sql, const QString& operation) {
    if (!query.prepare(sql))
        throw std::runtime_error(QStringLiteral("%1 (%2): %3")
            .arg(operation, sql, query.lastError().text()).toStdString());
}

inline double scalar(QSqlDatabase db, const QString& sql, const QVariantList& bindings = {}) {
    QSqlQuery query(db);
    prepare(query, sql, QStringLiteral("Preparing business data query"));
    for (const QVariant& value : bindings)
        query.addBindValue(value);
    execute(query, QStringLiteral("Reading business data"));
    return query.next() ? query.value(0).toDouble() : 0.0;
}

inline double accountBalance(QSqlDatabase db, const QString& account) {
    QSqlQuery opening(db);
    prepare(opening, QStringLiteral(
        "SELECT month,cash,bank FROM monthly_opening_balances "
        "WHERE month<=? AND confirmed=1 ORDER BY month DESC LIMIT 1"),
        QStringLiteral("Preparing opening balance query"));
    opening.addBindValue(QDate::currentDate().toString(QStringLiteral("yyyy-MM")));
    execute(opening, QStringLiteral("Reading opening balances"));

    QString startDate;
    double amount = 0.0;
    if (opening.next()) {
        startDate = opening.value(0).toString() + QStringLiteral("-01");
        amount = opening.value(account == QStringLiteral("Cash") ? 1 : 2).toDouble();
    } else {
        QSqlQuery setting(db);
        prepare(setting, QStringLiteral("SELECT value FROM app_settings WHERE key=?"),
                QStringLiteral("Preparing opening account setting query"));
        setting.addBindValue(account == QStringLiteral("Cash")
            ? QStringLiteral("OPENING_CASH") : QStringLiteral("OPENING_BANK"));
        execute(setting, QStringLiteral("Reading opening account setting"));
        if (setting.next()) {
            bool valid = false;
            amount = setting.value(0).toString().toDouble(&valid);
            if (!valid)
                throw std::runtime_error("The opening account balance is not a valid number.");
        }
    }
    QString sql = QStringLiteral(
        "SELECT COALESCE(SUM(CASE WHEN kind='IN' THEN amount ELSE -amount END),0) "
        "FROM cash_transactions WHERE account=?");
    if (!startDate.isEmpty())
        sql += QStringLiteral(" AND date>=?");
    QVariantList bindings{account};
    if (!startDate.isEmpty())
        bindings.push_back(startDate);
    return amount + scalar(db, sql, bindings);
}

inline double creditTotal(QSqlDatabase db, bool grain, bool receivable) {
    const QString purchase = grain ? QStringLiteral("mill_purchases") : QStringLiteral("coffee_batches");
    const QString sale = grain ? QStringLiteral("mill_sales") : QStringLiteral("sales");
    const QString customerMillJoin = QStringLiteral(
        " LEFT JOIN (SELECT customer_id,SUM(total-paid) balance FROM milling GROUP BY customer_id) m "
        "ON m.customer_id=c.id");
    const QString customerMillBalance = QStringLiteral("COALESCE(m.balance,0)+");

    const QString customerPosition = QStringLiteral(
        "SELECT %1COALESCE(s.balance,0)-COALESCE(cp.amount,0) position "
        "FROM customers c "
        "LEFT JOIN (SELECT customer_id,SUM(total-received) balance FROM %2 GROUP BY customer_id) s "
        "ON s.customer_id=c.id%3 "
        "LEFT JOIN (SELECT party_id,SUM(amount) amount FROM credit_payments "
        "WHERE party_type='Customer' GROUP BY party_id) cp ON cp.party_id=c.id")
        .arg(customerMillBalance, sale, customerMillJoin);
    const QString supplierPosition = QStringLiteral(
        "SELECT COALESCE(p.balance,0)+COALESCE(cp.amount,0) position "
        "FROM suppliers s "
        "LEFT JOIN (SELECT supplier_id,SUM(paid-total) balance FROM %1 GROUP BY supplier_id) p "
        "ON p.supplier_id=s.id "
        "LEFT JOIN (SELECT party_id,SUM(amount) amount FROM credit_payments "
        "WHERE party_type='Supplier' GROUP BY party_id) cp ON cp.party_id=s.id").arg(purchase);

    const QString customerSum = QStringLiteral(
        "SELECT COALESCE(SUM(CASE WHEN position %1 0.5 THEN %2 ELSE 0 END),0) FROM (%3)")
        .arg(receivable ? QStringLiteral(">") : QStringLiteral("<"),
             receivable ? QStringLiteral("position") : QStringLiteral("-position"), customerPosition);
    const QString supplierSum = QStringLiteral(
        "SELECT COALESCE(SUM(CASE WHEN position %1 0.5 THEN %2 ELSE 0 END),0) FROM (%3)")
        .arg(receivable ? QStringLiteral(">") : QStringLiteral("<"),
             receivable ? QStringLiteral("position") : QStringLiteral("-position"), supplierPosition);
    return scalar(db, customerSum) + scalar(db, supplierSum);
}

inline void initializeSurveyTables(QSqlDatabase db) {
    const auto createTable = [db](const QString& sql) {
        QSqlQuery query(db);
        if (!query.exec(sql))
            throw std::runtime_error(QStringLiteral("Creating KIYAMBI survey storage: %1")
                .arg(query.lastError().text()).toStdString());
    };
    createTable(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS kiyambi_surveys("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT NOT NULL,audience TEXT NOT NULL,"
        "created_at TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'Open')"));
    createTable(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS kiyambi_survey_questions("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,survey_id INTEGER NOT NULL,"
        "prompt TEXT NOT NULL,sort_order INTEGER NOT NULL)"));
    createTable(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS kiyambi_survey_responses("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,survey_id INTEGER NOT NULL,"
        "respondent TEXT NOT NULL DEFAULT '',submitted_at TEXT NOT NULL)"));
    createTable(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS kiyambi_survey_answers("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,response_id INTEGER NOT NULL,"
        "question_id INTEGER NOT NULL,answer TEXT NOT NULL)"));
}

struct Survey {
    qint64 id = 0;
    QString title;
    QString audience;
    int questionCount = 0;
    int responseCount = 0;
};

struct SurveyQuestion {
    qint64 id = 0;
    QString prompt;
};

inline QList<Survey> surveys(QSqlDatabase db) {
    QSqlQuery query(db);
    prepare(query, QStringLiteral(
        "SELECT s.id,s.title,s.audience,"
        "(SELECT COUNT(*) FROM kiyambi_survey_questions q WHERE q.survey_id=s.id),"
        "(SELECT COUNT(*) FROM kiyambi_survey_responses r WHERE r.survey_id=s.id) "
        "FROM kiyambi_surveys s ORDER BY s.created_at DESC,s.id DESC"),
        QStringLiteral("Preparing survey list"));
    execute(query, QStringLiteral("Reading surveys"));
    QList<Survey> result;
    while (query.next()) {
        result.push_back({query.value(0).toLongLong(), query.value(1).toString(),
                          query.value(2).toString(), query.value(3).toInt(),
                          query.value(4).toInt()});
    }
    return result;
}

inline QList<SurveyQuestion> surveyQuestions(QSqlDatabase db, qint64 surveyId) {
    QSqlQuery query(db);
    prepare(query, QStringLiteral(
        "SELECT id,prompt FROM kiyambi_survey_questions WHERE survey_id=? ORDER BY sort_order,id"),
        QStringLiteral("Preparing survey questions"));
    query.addBindValue(surveyId);
    execute(query, QStringLiteral("Reading survey questions"));
    QList<SurveyQuestion> result;
    while (query.next())
        result.push_back({query.value(0).toLongLong(), query.value(1).toString()});
    return result;
}

inline qint64 createSurvey(QSqlDatabase db, const QString& title, const QString& audience,
                           const QStringList& prompts) {
    const QString cleanTitle = title.trimmed();
    const QStringList validAudiences{
        QStringLiteral("Customers"), QStringLiteral("Farmers and suppliers"),
        QStringLiteral("Staff"), QStringLiteral("General")
    };
    QStringList cleanPrompts;
    for (const QString& prompt : prompts) {
        const QString clean = prompt.trimmed();
        if (!clean.isEmpty())
            cleanPrompts.push_back(clean);
    }
    if (cleanTitle.isEmpty())
        throw std::runtime_error("Enter a title for the survey.");
    if (!validAudiences.contains(audience))
        throw std::runtime_error("Select a valid survey audience.");
    if (cleanPrompts.isEmpty() || cleanPrompts.size() > 30)
        throw std::runtime_error("A survey must contain between 1 and 30 questions.");
    if (!db.transaction())
        throw std::runtime_error(QStringLiteral("Starting survey transaction: %1")
            .arg(db.lastError().text()).toStdString());
    try {
        QSqlQuery survey(db);
        prepare(survey, QStringLiteral(
            "INSERT INTO kiyambi_surveys(title,audience,created_at,status) VALUES(?,?,?,'Open')"),
            QStringLiteral("Preparing survey creation"));
        survey.addBindValue(cleanTitle);
        survey.addBindValue(audience);
        survey.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        execute(survey, QStringLiteral("Creating survey"));
        const qint64 surveyId = survey.lastInsertId().toLongLong();
        QSqlQuery question(db);
        prepare(question, QStringLiteral(
            "INSERT INTO kiyambi_survey_questions(survey_id,prompt,sort_order) VALUES(?,?,?)"),
            QStringLiteral("Preparing survey question"));
        for (qsizetype i = 0; i < cleanPrompts.size(); ++i) {
            question.bindValue(0, surveyId);
            question.bindValue(1, cleanPrompts.at(i));
            question.bindValue(2, i);
            execute(question, QStringLiteral("Saving survey question"));
        }
        if (!db.commit())
            throw std::runtime_error(QStringLiteral("Saving survey: %1")
                .arg(db.lastError().text()).toStdString());
        return surveyId;
    } catch (...) {
        db.rollback();
        throw;
    }
}

inline void submitSurveyResponse(QSqlDatabase db, qint64 surveyId, const QString& respondent,
                                 const QStringList& answers) {
    const QList<SurveyQuestion> questions = surveyQuestions(db, surveyId);
    if (questions.isEmpty() || questions.size() != answers.size())
        throw std::runtime_error("The survey questions changed. Reopen the survey and try again.");
    for (const QString& answer : answers) {
        if (answer.trimmed().isEmpty())
            throw std::runtime_error("Please answer every question before submitting.");
    }
    if (!db.transaction())
        throw std::runtime_error(QStringLiteral("Starting response transaction: %1")
            .arg(db.lastError().text()).toStdString());
    try {
        QSqlQuery response(db);
        prepare(response, QStringLiteral(
            "INSERT INTO kiyambi_survey_responses(survey_id,respondent,submitted_at) VALUES(?,?,?)"),
            QStringLiteral("Preparing survey response"));
        response.addBindValue(surveyId);
        response.addBindValue(respondent.trimmed().isEmpty()
            ? QStringLiteral("") : respondent.trimmed());
        response.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        execute(response, QStringLiteral("Saving survey response"));
        const qint64 responseId = response.lastInsertId().toLongLong();
        QSqlQuery answerQuery(db);
        prepare(answerQuery, QStringLiteral(
            "INSERT INTO kiyambi_survey_answers(response_id,question_id,answer) VALUES(?,?,?)"),
            QStringLiteral("Preparing survey answer"));
        for (qsizetype i = 0; i < questions.size(); ++i) {
            answerQuery.bindValue(0, responseId);
            answerQuery.bindValue(1, questions.at(i).id);
            answerQuery.bindValue(2, answers.at(i).trimmed());
            execute(answerQuery, QStringLiteral("Saving survey answer"));
        }
        if (!db.commit())
            throw std::runtime_error(QStringLiteral("Saving survey response: %1")
                .arg(db.lastError().text()).toStdString());
    } catch (...) {
        db.rollback();
        throw;
    }
}

inline QString surveyResults(QSqlDatabase db, qint64 surveyId) {
    QSqlQuery survey(db);
    prepare(survey, QStringLiteral(
        "SELECT title,audience FROM kiyambi_surveys WHERE id=?"),
        QStringLiteral("Preparing survey results"));
    survey.addBindValue(surveyId);
    execute(survey, QStringLiteral("Reading survey results"));
    if (!survey.next())
        return QStringLiteral("This survey no longer exists.");
    QSqlQuery responseCount(db);
    prepare(responseCount, QStringLiteral(
        "SELECT COUNT(*) FROM kiyambi_survey_responses WHERE survey_id=?"),
        QStringLiteral("Preparing survey response count"));
    responseCount.addBindValue(surveyId);
    execute(responseCount, QStringLiteral("Reading survey response count"));
    responseCount.next();
    QStringList lines{
        QStringLiteral("%1 — audience: %2 — %3 response(s)")
            .arg(survey.value(0).toString(), survey.value(1).toString(),
                 responseCount.value(0).toString())
    };
    QSqlQuery questions(db);
    prepare(questions, QStringLiteral(
        "SELECT id,prompt FROM kiyambi_survey_questions WHERE survey_id=? ORDER BY sort_order,id"),
        QStringLiteral("Preparing survey result questions"));
    questions.addBindValue(surveyId);
    execute(questions, QStringLiteral("Reading survey result questions"));
    while (questions.next()) {
        const qint64 questionId = questions.value(0).toLongLong();
        lines.push_back(QStringLiteral("\nQ: %1").arg(questions.value(1).toString()));
        QSqlQuery answers(db);
        prepare(answers, QStringLiteral(
            "SELECT a.answer,r.respondent,r.submitted_at FROM kiyambi_survey_answers a "
            "JOIN kiyambi_survey_responses r ON r.id=a.response_id "
            "WHERE r.survey_id=? AND a.question_id=? ORDER BY r.id"),
            QStringLiteral("Preparing survey answer results"));
        answers.addBindValue(surveyId);
        answers.addBindValue(questionId);
        execute(answers, QStringLiteral("Reading survey answers"));
        int answerNumber = 0;
        while (answers.next()) {
            ++answerNumber;
            const QString who = answers.value(1).toString().trimmed();
            lines.push_back(QStringLiteral("%1. %2%3")
                .arg(QString::number(answerNumber), answers.value(0).toString(),
                     who.isEmpty() ? QString() : QStringLiteral(" — ") + who));
        }
        if (answerNumber == 0)
            lines.push_back(QStringLiteral("No responses yet."));
    }
    return lines.join(QLatin1Char('\n'));
}

inline QString foundationInsights(QSqlDatabase db, bool grain) {
    const QString purchaseTable = grain ? QStringLiteral("mill_purchases") : QStringLiteral("coffee_batches");
    const QString saleTable = grain ? QStringLiteral("mill_sales") : QStringLiteral("sales");
    const QString cashBalance = number(accountBalance(db, QStringLiteral("Cash")));
    const QString bankBalance = number(accountBalance(db, QStringLiteral("Bank")));
    const double receivables = creditTotal(db, grain, true);
    const double payables = creditTotal(db, grain, false);
    const QString last30Days = QDate::currentDate().addDays(-30).toString(Qt::ISODate);
    const QString topExpense = [&] {
        QSqlQuery query(db);
        prepare(query, QStringLiteral(
            "SELECT COALESCE(NULLIF(trim(category),''),'Uncategorized'),SUM(amount) "
            "FROM expenses WHERE date>=? GROUP BY category ORDER BY SUM(amount) DESC LIMIT 1"),
            QStringLiteral("Preparing expense insight"));
        query.addBindValue(last30Days);
        execute(query, QStringLiteral("Reading expense insight"));
        return query.next()
            ? QStringLiteral("%1 (UGX %2 in the last 30 days)")
                .arg(query.value(0).toString(), number(query.value(1).toDouble()))
            : QStringLiteral("No expenses are recorded in the last 30 days.");
    }();
    const double incompletePurchases = scalar(db, QStringLiteral(
        "SELECT COUNT(*) FROM %1 WHERE COALESCE(kg,0)<=0 OR trim(COALESCE(grade,''))=''")
        .arg(purchaseTable));
    const QString topCustomer = [&] {
        QSqlQuery query(db);
        prepare(query, QStringLiteral(
            "SELECT COALESCE(NULLIF(trim(c.name),''),'Unregistered customer'),SUM(s.total) "
            "FROM %1 s LEFT JOIN customers c ON c.id=s.customer_id "
            "WHERE s.date>=? GROUP BY s.customer_id ORDER BY SUM(s.total) DESC LIMIT 1")
            .arg(saleTable), QStringLiteral("Preparing customer insight"));
        query.addBindValue(last30Days);
        execute(query, QStringLiteral("Reading customer insight"));
        return query.next()
            ? QStringLiteral("%1 (UGX %2 in the last 30 days)")
                .arg(query.value(0).toString(), number(query.value(1).toDouble()))
            : QStringLiteral("No sales are recorded in the last 30 days.");
    }();
    QStringList lines{
        QStringLiteral("COST REDUCTION — Largest recent expense: %1 Review its receipts and compare supplier prices before cutting essential quality.")
            .arg(topExpense),
        QStringLiteral("QUALITY CONTROL — %1 purchase record(s) have missing grade or non-positive weight. Correct these records and check weights/grades at receiving.")
            .arg(number(incompletePurchases, 0)),
        QStringLiteral("STRATEGIC PLANNING — Cash UGX %1; bank UGX %2; receivables UGX %3; payables UGX %4. Use these verified balances when planning purchases and payments.")
            .arg(cashBalance, bankBalance, number(receivables), number(payables)),
        QStringLiteral("COMPETITIVE ADVANTAGE — Leading recent customer: %1. Use survey feedback to improve service and retain customers.")
            .arg(topCustomer),
        QStringLiteral("RISK MANAGEMENT — %1 Review overdue credits, reconcile balances, and maintain enough cash for near-term supplier obligations.")
            .arg(payables > std::max(0.0, accountBalance(db, QStringLiteral("Cash")) +
                                             accountBalance(db, QStringLiteral("Bank")))
                ? QStringLiteral("Payables exceed available cash and bank.")
                : QStringLiteral("Compare payables with available cash and bank before committing funds.")),
        QStringLiteral("TIME SAVINGS — Create reusable customer, farmer/supplier, and staff surveys below. KIYAMBI reads this local database and does not send business data online.")
    };
    return lines.join(QStringLiteral("\n\n"));
}

inline QString generatedBusinessIdeas(QSqlDatabase db, bool grain) {
    const QString salesTable = grain ? QStringLiteral("mill_sales") : QStringLiteral("sales");
    const QString productColumn = grain ? QStringLiteral("product") : QStringLiteral("coffee_type");
    const QString start = QDate::currentDate().addDays(-90).toString(Qt::ISODate);
    const double receivables = creditTotal(db, grain, true);
    const double payables = creditTotal(db, grain, false);
    const double cashAndBank = accountBalance(db, QStringLiteral("Cash")) +
                                accountBalance(db, QStringLiteral("Bank"));
    QStringList ideas;

    QSqlQuery product(db);
    prepare(product, QStringLiteral(
        "SELECT %1,COALESCE(SUM(kg),0),COALESCE(SUM(total),0) FROM %2 "
        "WHERE date>=? GROUP BY %1 ORDER BY SUM(total) DESC LIMIT 1")
        .arg(productColumn, salesTable), QStringLiteral("Preparing top-selling product idea"));
    product.addBindValue(start);
    execute(product, QStringLiteral("Reading top-selling product"));
    if (product.next()) {
        ideas.push_back(QStringLiteral(
            "1. BUILD A REPEAT-ORDER OFFER — %1 led recorded sales over the past 90 days "
            "(%2 kg; UGX %3 in sales). Ask recent buyers whether they want scheduled weekly "
            "or monthly orders, then trial the offer with a small group.")
            .arg(product.value(0).toString(), number(product.value(1).toDouble(), 0),
                 number(product.value(2).toDouble(), 0)));
    } else {
        ideas.push_back(QStringLiteral(
            "1. TEST DEMAND BEFORE EXPANDING — No sales are recorded in the last 90 days. "
            "Ask existing customers which product, pack size, and collection day they prefer; "
            "trial one small batch before committing more stock."));
    }

    QSqlQuery expense(db);
    prepare(expense, QStringLiteral(
        "SELECT COALESCE(NULLIF(trim(category),''),'Uncategorized'),COALESCE(SUM(amount),0) "
        "FROM expenses WHERE date>=? GROUP BY category ORDER BY SUM(amount) DESC LIMIT 1"),
        QStringLiteral("Preparing expense-based idea"));
    expense.addBindValue(start);
    execute(expense, QStringLiteral("Reading expense categories"));
    if (expense.next()) {
        ideas.push_back(QStringLiteral(
            "2. TEST A PURCHASING SAVING — %1 was the largest recorded expense category "
            "in the last 90 days (UGX %2). Compare two supplier quotes or negotiate a "
            "volume rate, then compare the actual cost next month without lowering quality.")
            .arg(expense.value(0).toString(), number(expense.value(1).toDouble(), 0)));
    } else {
        ideas.push_back(QStringLiteral(
            "2. START A COST-SAVING TRIAL — Expense categories are not recorded for the "
            "last 90 days. Record fuel, packaging, transport, and maintenance separately; "
            "then test one supplier quote against the current cost."));
    }

    if (receivables > 0) {
        ideas.push_back(QStringLiteral(
            "3. TURN CREDIT INTO A CUSTOMER RETENTION PLAN — The mill has UGX %1 in "
            "recorded receivables. Offer customers a clear statement and agreed payment "
            "dates; test a modest prompt-payment reward only after checking its cost.")
            .arg(number(receivables, 0)));
    } else {
        ideas.push_back(QStringLiteral(
            "3. LEARN WHAT CUSTOMERS WOULD BUY NEXT — No positive customer receivable "
            "balance is recorded. Use a short customer survey to test demand for delivery, "
            "different pack sizes, or pre-orders before investing."));
    }

    if (payables > cashAndBank) {
        ideas.push_back(QStringLiteral(
            "4. PROTECT WORKING CAPITAL — Recorded supplier payables (UGX %1) exceed "
            "cash and bank (UGX %2). Prioritize a cash-flow review and negotiate delivery "
            "or payment timing before launching a cash-intensive expansion.")
            .arg(number(payables, 0), number(cashAndBank, 0)));
    } else {
        ideas.push_back(QStringLiteral(
            "4. RUN A SMALL, MEASURABLE PILOT — Recorded cash and bank total UGX %1; "
            "supplier payables are UGX %2. Set a spending cap and measure actual sales "
            "and costs before scaling a new offer.")
            .arg(number(cashAndBank, 0), number(payables, 0)));
    }
    ideas.push_back(QStringLiteral(
        "\nThese are generated experiments from recorded activity, not guaranteed "
        "forecasts. Check stock, margins, records, and customer demand before spending."));
    return QStringLiteral("BUSINESS IDEAS BASED ON THE LAST 90 DAYS OF RECORDED DATA\n\n%1")
        .arg(ideas.join(QStringLiteral("\n\n")));
}

inline bool isConversationalFollowUp(const QString& question) {
    const QString normalized = QString(question.toLower()).remove(
        QRegularExpression(QStringLiteral("[.!?,]+$"))).simplified();
    static const QSet<QString> followUps{
        QStringLiteral("why"), QStringLiteral("why is that"), QStringLiteral("explain"),
        QStringLiteral("explain more"), QStringLiteral("more"), QStringLiteral("tell me more"),
        QStringLiteral("go on"), QStringLiteral("continue"), QStringLiteral("what next"),
        QStringLiteral("next"), QStringLiteral("how so"), QStringLiteral("details")
    };
    return followUps.contains(normalized);
}

inline QString answer(QSqlDatabase db, bool grain, Intent intent) {
    const QString product = grain ? QStringLiteral("grain") : QStringLiteral("coffee");
    switch (intent) {
    case Intent::Financials:
        return QStringLiteral("Cash balance: UGX %1\nBank balance: UGX %2")
            .arg(number(accountBalance(db, QStringLiteral("Cash"))),
                 number(accountBalance(db, QStringLiteral("Bank"))));
    case Intent::Inventory: {
        const QString table = grain ? QStringLiteral("grain_stock_movements")
                                    : QStringLiteral("stock_movements");
        const QString column = grain ? QStringLiteral("product") : QStringLiteral("coffee_type");
        QSqlQuery query(db);
        prepare(query, QStringLiteral("SELECT %1,COALESCE(SUM(kg),0) FROM %2 GROUP BY %1 ORDER BY %1")
            .arg(column, table), QStringLiteral("Preparing stock query"));
        execute(query, QStringLiteral("Reading %1 stock").arg(product));
        QStringList lines;
        while (query.next())
            lines.push_back(QStringLiteral("%1: %2 kg").arg(query.value(0).toString(), number(query.value(1).toDouble())));
        return QStringLiteral("Current %1 stock:\n%2").arg(product,
            lines.isEmpty() ? QStringLiteral("No stock movements are recorded.") : lines.join(QLatin1Char('\n')));
    }
    case Intent::Receivables:
        return QStringLiteral("Total amount owed to the mill: UGX %1")
            .arg(number(creditTotal(db, grain, true)));
    case Intent::Payables:
        return QStringLiteral("Total amount the mill owes: UGX %1")
            .arg(number(creditTotal(db, grain, false)));
    case Intent::History: {
        const QString table = grain ? QStringLiteral("mill_purchases") : QStringLiteral("coffee_batches");
        const QString type = grain ? QStringLiteral("product") : QStringLiteral("coffee_type");
        const QString supplierJoin = QStringLiteral(" LEFT JOIN suppliers s ON s.id=p.supplier_id");
        QSqlQuery query(db);
        prepare(query, QStringLiteral(
            "SELECT p.date,p.kg,p.%1,p.grade,s.name FROM %2 p%3 "
            "ORDER BY p.date DESC,p.id DESC LIMIT ?").arg(type, table, supplierJoin),
            QStringLiteral("Preparing recent purchases query"));
        query.addBindValue(QVariant(5));
        execute(query, QStringLiteral("Reading recent %1 purchases").arg(product));
        QStringList lines;
        while (query.next()) {
            QString line = QStringLiteral("%1 — %2 kg %3")
                .arg(query.value(0).toString().isEmpty() ? QStringLiteral("Unknown date") : query.value(0).toString(),
                     number(query.value(1).toDouble()), query.value(2).toString());
            if (!query.value(3).toString().isEmpty())
                line += QStringLiteral(" (%1)").arg(query.value(3).toString());
            if (!query.value(4).toString().isEmpty())
                line += QStringLiteral(" — %1").arg(query.value(4).toString());
            lines.push_back(line);
        }
        return QStringLiteral("Recent %1 purchases:\n%2").arg(product,
            lines.isEmpty() ? QStringLiteral("No recent purchases were found.") : lines.join(QLatin1Char('\n')));
    }
    case Intent::Insights:
        return foundationInsights(db, grain);
    case Intent::Ideas:
        return generatedBusinessIdeas(db, grain);
    case Intent::Surveys:
        return QStringLiteral("Use Create survey to build a local feedback form, Take survey to collect a response, or Survey results to review feedback. Survey data stays in this business database.");
    case Intent::Greeting:
        return QStringLiteral("Hello! I can look up your mill's cash, stock, customer balances, supplier payables, and recent purchases; generate business ideas from recorded data; review risks; or help with surveys. What would you like to explore?");
    case Intent::Help:
        return QStringLiteral("Try asking naturally:\n• How much cash and bank balance do we have?\n• Which product sold most recently?\n• Give me business ideas based on our data.\n• What costs or risks should I review?\n• Help me create a customer survey.\nYou can also ask “why?”, “more”, or “what next?” to continue the conversation. KIYAMBI reads this local business database.");
    default:
        return QStringLiteral("I didn't quite understand. You can ask about cash, stock, customers, suppliers, recent activity, risks, costs, or business ideas; or type “help” for examples.");
    }
}

} // namespace kiyambi_detail

inline qint64 chooseSurvey(QSqlDatabase db, QWidget* parent, const QString& title) {
    const QList<kiyambi_detail::Survey> available = kiyambi_detail::surveys(db);
    if (available.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Create a survey first."));
        return 0;
    }
    QStringList labels;
    for (const auto& survey : available)
        labels.push_back(QStringLiteral("%1 — %2 (%3 response(s))")
            .arg(survey.title, survey.audience, QString::number(survey.responseCount)) +
            QStringLiteral(" [#%1]").arg(survey.id));
    bool accepted = false;
    const QString selected = QInputDialog::getItem(parent, title, QStringLiteral("Survey:"),
        labels, 0, false, &accepted);
    if (!accepted)
        return 0;
    const qsizetype index = labels.indexOf(selected);
    return index >= 0 ? available.at(index).id : 0;
}

inline QWidget* createKiyambiPage(QSqlDatabase db, bool grain, QWidget* parent = nullptr) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* heading = new QLabel(QStringLiteral("KIYAMBI — Your Mill Business Chatbot"), page);
    QFont headingFont = heading->font();
    headingFont.setPointSize(16);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    auto* privacy = new QLabel(
        QStringLiteral("Ask in natural language, follow up conversationally, or request business ideas generated from this %1 mill's recorded data. Chat stays on this device; ideas are evidence-based suggestions, not guarantees.")
            .arg(grain ? QStringLiteral("grain") : QStringLiteral("coffee")), page);
    privacy->setWordWrap(true);
    auto* foundationBox = new QGroupBox(QStringLiteral("Live review of the six fundamentals"), page);
    auto* foundationLayout = new QVBoxLayout(foundationBox);
    auto* foundationText = new QPlainTextEdit(foundationBox);
    foundationText->setReadOnly(true);
    foundationText->setMaximumHeight(210);
    foundationLayout->addWidget(foundationText);
    auto* surveyButtons = new QHBoxLayout;
    auto* createSurvey = new QPushButton(QStringLiteral("Create survey"), page);
    auto* takeSurvey = new QPushButton(QStringLiteral("Take survey"), page);
    auto* surveyResultsButton = new QPushButton(QStringLiteral("Survey results"), page);
    createSurvey->setObjectName(QStringLiteral("kiyambiCreateSurvey"));
    takeSurvey->setObjectName(QStringLiteral("kiyambiTakeSurvey"));
    surveyResultsButton->setObjectName(QStringLiteral("kiyambiSurveyResults"));
    surveyButtons->addWidget(createSurvey);
    surveyButtons->addWidget(takeSurvey);
    surveyButtons->addWidget(surveyResultsButton);
    auto* promptRow = new QHBoxLayout;
    const auto makePrompt = [page, promptRow](const QString& label, const QString& prompt) {
        auto* button = new QPushButton(label, page);
        button->setObjectName(QStringLiteral("kiyambiPrompt"));
        button->setProperty("chatPrompt", prompt);
        promptRow->addWidget(button);
        return button;
    };
    makePrompt(QStringLiteral("Business ideas"), QStringLiteral("Give me business ideas based on our data"));
    makePrompt(QStringLiteral("Money & risk"), QStringLiteral("Review our money and business risks"));
    makePrompt(QStringLiteral("Stock"), QStringLiteral("What stock opportunities do we have?"));
    makePrompt(QStringLiteral("Help"), QStringLiteral("Help"));
    auto* question = new QLineEdit(page);
    question->setPlaceholderText(QStringLiteral("Ask KIYAMBI… e.g. “What should I improve?”"));
    question->setObjectName(QStringLiteral("kiyambiQuestion"));
    auto* ask = new QPushButton(QStringLiteral("Ask"), page);
    ask->setObjectName(QStringLiteral("kiyambiAsk"));
    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(question, 1);
    inputRow->addWidget(ask);
    auto* result = new QPlainTextEdit(page);
    result->setReadOnly(true);
    result->setObjectName(QStringLiteral("kiyambiAnswer"));
    result->setPlaceholderText(QStringLiteral("Your conversation with KIYAMBI will appear here."));
    result->setMinimumHeight(240);
    result->setPlainText(QStringLiteral(
        "KIYAMBI\nHello! Ask me about this mill, request business ideas from your records, "
        "or choose a suggested question below. You can follow up with “why?”, “more”, or “what next?”\n"));

    layout->addWidget(heading);
    layout->addWidget(privacy);
    layout->addWidget(foundationBox);
    layout->addLayout(surveyButtons);
    layout->addWidget(new QLabel(QStringLiteral("SUGGESTED QUESTIONS"), page));
    layout->addLayout(promptRow);
    layout->addLayout(inputRow);
    layout->addWidget(result, 1);

    bool surveysReady = true;
    try {
        kiyambi_detail::initializeSurveyTables(db);
        foundationText->setPlainText(kiyambi_detail::foundationInsights(db, grain));
    } catch (const std::exception& error) {
        surveysReady = false;
        foundationText->setPlainText(QStringLiteral("Unable to load business insights:\n%1")
            .arg(QString::fromUtf8(error.what())));
    }
    createSurvey->setEnabled(surveysReady);
    takeSurvey->setEnabled(surveysReady);
    surveyResultsButton->setEnabled(surveysReady);
    QObject::connect(createSurvey, &QPushButton::clicked, page, [db, page, result] {
        QDialog dialog(page);
        dialog.setWindowTitle(QStringLiteral("Create a KIYAMBI survey"));
        auto* dialogLayout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout;
        auto* title = new QLineEdit(&dialog);
        title->setMaxLength(120);
        auto* audience = new QComboBox(&dialog);
        audience->addItems({QStringLiteral("Customers"), QStringLiteral("Farmers and suppliers"),
                            QStringLiteral("Staff"), QStringLiteral("General")});
        auto* prompts = new QPlainTextEdit(&dialog);
        prompts->setPlaceholderText(QStringLiteral(
            "Enter one question per line, for example:\nHow satisfied are you with our service?\nWhat should we improve?"));
        prompts->setMinimumHeight(130);
        form->addRow(QStringLiteral("Survey title"), title);
        form->addRow(QStringLiteral("Audience"), audience);
        form->addRow(QStringLiteral("Questions"), prompts);
        dialogLayout->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        dialogLayout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QStringList questions = prompts->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        try {
            const qint64 surveyId = kiyambi_detail::createSurvey(
                db, title->text(), audience->currentText(), questions);
            result->setPlainText(QStringLiteral("Survey created and saved locally (ID %1). "
                "Use Take survey to record responses.").arg(surveyId));
        } catch (const std::exception& error) {
            QMessageBox::warning(page, QStringLiteral("Could not create survey"),
                                 QString::fromUtf8(error.what()));
        }
    });
    QObject::connect(takeSurvey, &QPushButton::clicked, page, [db, page, result] {
        const qint64 surveyId = chooseSurvey(db, page, QStringLiteral("Take a survey"));
        if (surveyId <= 0)
            return;
        const QList<kiyambi_detail::SurveyQuestion> questions =
            kiyambi_detail::surveyQuestions(db, surveyId);
        QDialog dialog(page);
        dialog.setWindowTitle(QStringLiteral("Record survey response"));
        auto* dialogLayout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout;
        auto* respondent = new QLineEdit(&dialog);
        respondent->setMaxLength(120);
        respondent->setPlaceholderText(QStringLiteral("Optional — leave blank for an anonymous response"));
        form->addRow(QStringLiteral("Respondent (optional)"), respondent);
        QList<QLineEdit*> answers;
        for (const auto& surveyQuestion : questions) {
            auto* field = new QLineEdit(&dialog);
            field->setMaxLength(2000);
            answers.push_back(field);
            form->addRow(surveyQuestion.prompt, field);
        }
        auto* scroll = new QScrollArea(&dialog);
        auto* formWidget = new QWidget(scroll);
        formWidget->setLayout(form);
        scroll->setWidget(formWidget);
        scroll->setWidgetResizable(true);
        dialogLayout->addWidget(scroll);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        dialogLayout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        QStringList answerValues;
        for (const QLineEdit* field : answers)
            answerValues.push_back(field->text());
        try {
            kiyambi_detail::submitSurveyResponse(db, surveyId, respondent->text(), answerValues);
            result->setPlainText(QStringLiteral("Survey response saved locally. Thank you."));
        } catch (const std::exception& error) {
            QMessageBox::warning(page, QStringLiteral("Could not save response"),
                                 QString::fromUtf8(error.what()));
        }
    });
    QObject::connect(surveyResultsButton, &QPushButton::clicked, page, [db, page, result] {
        const qint64 surveyId = chooseSurvey(db, page, QStringLiteral("Survey results"));
        if (surveyId <= 0)
            return;
        try {
            result->setPlainText(kiyambi_detail::surveyResults(db, surveyId));
        } catch (const std::exception& error) {
            QMessageBox::warning(page, QStringLiteral("Could not read survey results"),
                                 QString::fromUtf8(error.what()));
        }
    });

    const auto conversation = std::make_shared<QPair<kiyambi_detail::Intent, QString>>(
        kiyambi_detail::Intent::Unknown, QString());
    const auto sendMessage = [db, grain, question, result, foundationText,
                              conversation] {
        const QString entered = question->text().trimmed();
        if (entered.isEmpty()) {
            return;
        }
        result->appendPlainText(QStringLiteral("You\n%1\n").arg(entered));
        try {
            const bool followUp = kiyambi_detail::isConversationalFollowUp(entered) &&
                                  conversation->first != kiyambi_detail::Intent::Unknown;
            const kiyambi_detail::Intent intent = followUp
                ? conversation->first : kiyambi_detail::classify(entered);
            QString reply;
            if (followUp) {
                reply = QStringLiteral("Following up on: %1\n\nHere is the latest supporting evidence:\n%2\n\n"
                                       "What would you like me to compare or explain next?")
                    .arg(conversation->second.left(160),
                         kiyambi_detail::answer(db, grain, conversation->first));
            } else {
                reply = kiyambi_detail::answer(db, grain, intent);
            }
            result->appendPlainText(QStringLiteral("KIYAMBI\n%1\n").arg(reply));
            result->moveCursor(QTextCursor::End);
            if (intent == kiyambi_detail::Intent::Insights || intent == kiyambi_detail::Intent::Ideas)
                foundationText->setPlainText(kiyambi_detail::foundationInsights(db, grain));
            conversation->first = intent;
            conversation->second = reply;
        } catch (const std::exception& error) {
            const QString failure = QStringLiteral("I couldn't read that from the business database:\n%1")
                .arg(QString::fromUtf8(error.what()));
            result->appendPlainText(QStringLiteral("KIYAMBI\n%1\n").arg(failure));
            result->moveCursor(QTextCursor::End);
            conversation->first = kiyambi_detail::Intent::Unknown;
            conversation->second.clear();
        }
        question->clear();
    };
    QObject::connect(ask, &QPushButton::clicked, page, sendMessage);
    QObject::connect(question, &QLineEdit::returnPressed, page, sendMessage);
    for (QPushButton* button : page->findChildren<QPushButton*>(QStringLiteral("kiyambiPrompt"))) {
        QObject::connect(button, &QPushButton::clicked, page, [button, question, ask] {
            question->setText(button->property("chatPrompt").toString());
            ask->click();
        });
    }
    return page;
}
