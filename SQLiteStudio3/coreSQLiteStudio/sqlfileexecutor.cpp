#include "sqlfileexecutor.h"
#include "db/db.h"
#include "db/sqlquery.h"
#include "services/notifymanager.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QElapsedTimer>
#include <QFile>

SqlFileExecutor::SqlFileExecutor(QObject *parent)
    : QObject{parent}
{
}

void SqlFileExecutor::execSqlFromFile(Db* db, const QString& filePath, bool ignoreErrors, QString codec)
{
    if (!db || !db->isOpen())
    {
        emit execEnded();
        return;
    }

    if (executionInProgress)
    {
        emit execEnded();
        return;
    }

    // Exec file
    executionInProgress = 1;
    this->ignoreErrors = ignoreErrors;
    this->codec = codec;
    this->filePath = filePath;
    this->db = db;
    emit updateProgress(0);
    if (!db->begin())
    {
        notifyError(tr("Could not execute SQL, because application has failed to start transaction: %1").arg(db->getErrorText()));
        emit execEnded();
        return;
    }

    QtConcurrent::run(this, &SqlFileExecutor::execInThread);
}

bool SqlFileExecutor::isExecuting() const
{
    return executionInProgress;
}

void SqlFileExecutor::stopExecution()
{
    if (!executionInProgress)
    {
        emit execEnded();
        return;
    }

    executionInProgress = 0;

    if (db) // should always be there, but just in case
    {
        db->interrupt();
        db->rollback();
        db = nullptr;
        notifyWarn(tr("Execution from file cancelled. Any queries executed so far have been rolled back."));
    }
    emit execEnded();
}

bool SqlFileExecutor::execQueryFromFile(Db* db, const QString& sql)
{
    return !db->exec(sql)->isError();
}

void SqlFileExecutor::execInThread()
{
    // Open file
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        notifyError(tr("Could not open file '%1' for reading: %2").arg(filePath, file.errorString()));
        executionInProgress = 0;
        emit execEnded();
        return;
    }

    QTextStream stream(&file);
    stream.setCodec(codec.toLatin1().constData());

    qint64 fileSize = file.size();
    int attemptedExecutions = 0;
    int executed = 0;
    bool ok = true;

    QElapsedTimer timer;
    timer.start();
    QList<QPair<QString, QString>> errors = executeFromStream(stream, executed, attemptedExecutions, ok, fileSize);
    int millis = timer.elapsed();
    if (executionInProgress.loadAcquire())
    {
        handleExecutionResults(db, executed, attemptedExecutions, ok, ignoreErrors, millis);
        if (!errors.isEmpty())
            emit execErrors(errors, !ok && !ignoreErrors);
    }

    file.close();
    emit execEnded();
    executionInProgress = 0;
}

void SqlFileExecutor::handleExecutionResults(Db* db, int executed, int attemptedExecutions, bool ok, bool ignoreErrors, int millis)
{
    bool doCommit = ok ? true : ignoreErrors;
    if (doCommit)
    {
        if (!db->commit())
        {
            notifyError(tr("Could not execute SQL, because application has failed to commit the transaction: %1").arg(db->getErrorText()));
            db->rollback();
        }
        else if (!ok) // committed with errors
        {
            notifyInfo(tr("Finished executing %1 queries in %2 seconds. %3 were not executed due to errors.")
                           .arg(QString::number(executed), QString::number(millis / 1000.0), QString::number(attemptedExecutions - executed)));
            emit schemaNeedsRefreshing(db);
        }
        else
        {
            notifyInfo(tr("Finished executing %1 queries in %2 seconds.").arg(QString::number(executed), QString::number(millis / 1000.0)));
            emit schemaNeedsRefreshing(db);
        }
    }
    else
    {
        db->rollback();
        notifyError(tr("Could not execute SQL due to error."));
    }
}

QList<QPair<QString, QString>> SqlFileExecutor::executeFromStream(QTextStream& stream, int& executed, int& attemptedExecutions, bool& ok, qint64 fileSize)
{
    QList<QPair<QString, QString>> errors;
    qint64 pos = 0;
    QChar c;
    QString sql;
    sql.reserve(10000);
    SqlQueryPtr results;
    while (!stream.atEnd() && executionInProgress.loadAcquire())
    {
        while (!db->isComplete(sql) && !stream.atEnd())
        {
            stream >> c;
            sql.append(c);
            while (c != ';' && !stream.atEnd())
            {
                stream >> c;
                sql.append(c);
            }
        }

        if (shouldSkipQuery(sql))
        {
            sql.clear();;
            continue;
        }

        results = db->exec(sql);
        attemptedExecutions++;
        if (results->isError())
        {
            ok = false;
            errors << QPair<QString, QString>(sql, results->getErrorText());

            if (!ignoreErrors)
                break;
        }
        else
            executed++;

        sql.clear();
        if (attemptedExecutions % 100 == 0)
        {
            pos = stream.device()->pos();
            emit updateProgress(static_cast<int>(100 * pos / fileSize));
        }
    }
    return errors;
}

bool SqlFileExecutor::shouldSkipQuery(const QString& sql)
{
    if (sql.trimmed().isEmpty())
        return true;

    QString upper = sql.toUpper().trimmed().split("\n").last().trimmed();
    return (upper.startsWith("BEGIN") ||
            upper.startsWith("COMMIT") ||
            upper.startsWith("ROLLBACK") ||
            upper.startsWith("END"));
}
