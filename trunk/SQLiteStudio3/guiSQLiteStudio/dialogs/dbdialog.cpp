#include "dbdialog.h"
#include "ui_dbdialog.h"
#include "services/pluginmanager.h"
#include "plugins/dbplugin.h"
#include "uiutils.h"
#include "common/utils.h"
#include "uiconfig.h"
#include "services/dbmanager.h"
#include "common/global.h"
#include "iconmanager.h"
#include "common/unused.h"
#include <QDateTimeEdit>
#include <QSpinBox>
#include <QDebug>
#include <QPushButton>
#include <QFileDialog>
#include <QComboBox>
#include <QTimer>

DbDialog::DbDialog(Mode mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DbDialog),
    mode(mode)
{
    init();
}

DbDialog::~DbDialog()
{
    delete ui;
}

void DbDialog::setDb(Db* db)
{
    this->db = db;
}

void DbDialog::setPermanent(bool perm)
{
    ui->permamentCheckBox->setChecked(perm);
}

QString DbDialog::getPath()
{
    return ui->fileEdit->text();
}

void DbDialog::setPath(const QString& path)
{
    ui->fileEdit->setText(path);
}

QString DbDialog::getName()
{
    return ui->nameEdit->text();
}

bool DbDialog::isPermanent()
{
    return ui->permamentCheckBox->isChecked();
}

void DbDialog::changeEvent(QEvent *e)
{
    QDialog::changeEvent(e);
    switch (e->type()) {
        case QEvent::LanguageChange:
            ui->retranslateUi(this);
            break;
        default:
            break;
    }
}

void DbDialog::showEvent(QShowEvent *e)
{
    if (db)
    {
        disableTypeAutodetection = true;
        int idx = ui->typeCombo->findText(db->getTypeLabel());
        ui->typeCombo->setCurrentIndex(idx);

        ui->generateCheckBox->setChecked(false);
        ui->fileEdit->setText(db->getPath());
        ui->nameEdit->setText(db->getName());
        disableTypeAutodetection = false;
    }
    else if (ui->typeCombo->count() > 0)
    {
        int idx = ui->typeCombo->findText("SQLite 3", Qt::MatchFixedString); // we should have SQLite 3 plugin
        if (idx > -1)
            ui->typeCombo->setCurrentIndex(idx);
        else
            ui->typeCombo->setCurrentIndex(0);
    }

    existingDatabaseNames = DBLIST->getDbNames();
    if (mode == EDIT)
        existingDatabaseNames.removeOne(db->getName());

    updateOptions();
    updateState();

    QDialog::showEvent(e);
}

void DbDialog::init()
{
    ui->setupUi(this);

    ui->browseCreateButton->setIcon(ICONS.PLUS);

    for (DbPlugin* dbPlugin : PLUGINS->getLoadedPlugins<DbPlugin>())
        dbPlugins[dbPlugin->getLabel()] = dbPlugin;

    QStringList typeLabels;
    typeLabels += dbPlugins.keys();
    typeLabels.sort(Qt::CaseInsensitive);
    ui->typeCombo->addItems(typeLabels);

    ui->browseCreateButton->setVisible(true);
    ui->testConnIcon->setVisible(false);

    connect(ui->fileEdit, SIGNAL(textChanged(QString)), this, SLOT(fileChanged(QString)));
    connect(ui->nameEdit, SIGNAL(textChanged(QString)), this, SLOT(nameModified(QString)));
    connect(ui->generateCheckBox, SIGNAL(toggled(bool)), this, SLOT(generateNameSwitched(bool)));
    connect(ui->browseCreateButton, SIGNAL(clicked()), this, SLOT(browseClicked()));
    connect(ui->browseOpenButton, SIGNAL(clicked()), this, SLOT(browseClicked()));
    connect(ui->testConnButton, SIGNAL(clicked()), this, SLOT(testConnectionClicked()));
    connect(ui->typeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(dbTypeChanged(int)));

    generateNameSwitched(true);

    layout()->setSizeConstraint(QLayout::SetFixedSize);
}

void DbDialog::updateOptions()
{
    setUpdatesEnabled(false);

    // Remove olds
    foreach (QWidget* w, optionWidgets)
    {
        ui->optionsGrid->removeWidget(w);
        delete w;
    }

    customBrowseHandler = nullptr;
    ui->pathGroup->setTitle(tr("File"));
    ui->browseOpenButton->setToolTip(tr("Browse for existing database file on local computer"));
    ui->browseCreateButton->setVisible(true);

    optionWidgets.clear();
    optionKeyToWidget.clear();
    optionKeyToType.clear();
    helperToKey.clear();

    lastWidgetInTabOrder = ui->permamentCheckBox;

    // Retrieve new list
    if (ui->typeCombo->currentIndex() > -1)
    {
        DbPlugin* plugin = dbPlugins[ui->typeCombo->currentText()];
        QList<DbPluginOption> optList = plugin->getOptionsList();
        if (optList.size() > 0)
        {
            // Add new options
            int row = ADDITIONAL_ROWS_BEGIN_INDEX;
            for (const DbPluginOption& opt : optList)
            {
                addOption(opt, row);
                row++;
            }
        }
    }


    setUpdatesEnabled(true);
}

void DbDialog::addOption(const DbPluginOption& option, int& row)
{
    if (option.type == DbPluginOption::CUSTOM_PATH_BROWSE)
    {
        // This option does not add any editor, but has it's own label for path edit.
        row--;
        ui->pathGroup->setTitle(option.label);
        ui->browseCreateButton->setVisible(false);
        if (!option.toolTip.isEmpty())
            ui->browseOpenButton->setToolTip(option.toolTip);

        customBrowseHandler = option.customBrowseHandler;
        return;
    }

    QLabel* label = new QLabel(option.label, this);
    label->setAlignment(Qt::AlignVCenter|Qt::AlignRight);
    QWidget* editor = nullptr;
    QWidget* editorHelper = nullptr; // TODO, based on plugins for Url handlers

    editor = getEditor(option, editorHelper);
    Q_ASSERT(editor != nullptr);

    if (!option.toolTip.isNull())
        editor->setToolTip(option.toolTip);

    optionWidgets << label << editor;

    optionKeyToWidget[option.key] = editor;
    optionKeyToType[option.key] = option.type;
    ui->optionsGrid->addWidget(label, row, 0);
    ui->optionsGrid->addWidget(editor, row, 1);

    setTabOrder(lastWidgetInTabOrder, editor);
    lastWidgetInTabOrder = editor;

    if (editorHelper)
    {
        ui->optionsGrid->addWidget(editorHelper, row, 2);
        optionWidgets << editorHelper;
        helperToKey[editorHelper] = option.key;

        setTabOrder(lastWidgetInTabOrder, editorHelper);
        lastWidgetInTabOrder = editorHelper;
    }

    if (db && db->getConnectionOptions().contains(option.key))
        setValueFor(option.type, editor, db->getConnectionOptions()[option.key]);
}

QWidget *DbDialog::getEditor(const DbPluginOption& opt, QWidget*& editorHelper)
{
    QWidget* editor = nullptr;
    QLineEdit* le = nullptr;
    editorHelper = nullptr;
    switch (opt.type)
    {
        case DbPluginOption::STRING:
        {
            editor = new QLineEdit(this);
            le = dynamic_cast<QLineEdit*>(editor);
            connect(le, SIGNAL(textChanged(QString)), this, SLOT(propertyChanged()));
            break;
        }
        case DbPluginOption::PASSWORD:
        {
            editor = new QLineEdit(this);
            le = dynamic_cast<QLineEdit*>(editor);
            le->setEchoMode(QLineEdit::Password);
            connect(le, SIGNAL(textChanged(QString)), this, SLOT(propertyChanged()));
            break;
        }
        case DbPluginOption::CHOICE:
        {
            QComboBox* cb = new QComboBox(this);
            editor = cb;
            cb->setEditable(!opt.choiceReadOnly);
            cb->addItems(opt.choiceValues);
            cb->setCurrentText(opt.defaultValue.toString());
            connect(cb, SIGNAL(currentIndexChanged(QString)), this, SLOT(propertyChanged()));
            break;
        }
        case DbPluginOption::INT:
        {
            QSpinBox* sb = new QSpinBox(this);
            editor = sb;
            if (!opt.minValue.isNull())
                sb->setMinimum(opt.minValue.toInt());

            if (!opt.maxValue.isNull())
                sb->setMaximum(opt.maxValue.toInt());

            if (!opt.defaultValue.isNull())
                sb->setValue(opt.defaultValue.toInt());

            connect(sb, SIGNAL(valueChanged(int)), this, SLOT(propertyChanged()));
            break;
        }
        case DbPluginOption::FILE:
        {
            editor = new QLineEdit(this);
            le = dynamic_cast<QLineEdit*>(editor);
            editorHelper = new QPushButton(tr("Browse"), this);
            connect(le, SIGNAL(textChanged(QString)), this, SLOT(propertyChanged()));
            connect(editorHelper, SIGNAL(pressed()), this, SLOT(browseForFile()));
            break;
        }
        case DbPluginOption::BOOL:
        {
            QCheckBox* cb = new QCheckBox(this);
            editor = cb;
            if (!opt.defaultValue.isNull())
                cb->setChecked(opt.defaultValue.toBool());

            connect(cb, SIGNAL(stateChanged(int)), this, SLOT(propertyChanged()));
            break;
        }
        case DbPluginOption::DOUBLE:
        {
            QDoubleSpinBox* sb = new QDoubleSpinBox(this);
            editor = sb;
            if (!opt.minValue.isNull())
                sb->setMinimum(opt.minValue.toDouble());

            if (!opt.maxValue.isNull())
                sb->setMaximum(opt.maxValue.toDouble());

            if (!opt.defaultValue.isNull())
                sb->setValue(opt.defaultValue.toDouble());

            connect(sb, SIGNAL(valueChanged(double)), this, SLOT(propertyChanged()));
            break;
        }
        case DbPluginOption::CUSTOM_PATH_BROWSE:
            return nullptr; // should not happen ever, asserted one stack level before
        default:
            // TODO plugin based handling of custom editors
            qWarning() << "Unhandled DbDialog option for creating editor.";
            break;
    }

    if (le)
    {
        le->setPlaceholderText(opt.placeholderText);
        le->setText(opt.defaultValue.toString());
    }

    return editor;
}

QVariant DbDialog::getValueFrom(DbPluginOption::Type type, QWidget *editor)
{
    QVariant value;
    switch (type)
    {
        case DbPluginOption::STRING:
        case DbPluginOption::PASSWORD:
        case DbPluginOption::FILE:
            value = dynamic_cast<QLineEdit*>(editor)->text();
            break;
        case DbPluginOption::INT:
            value = dynamic_cast<QSpinBox*>(editor)->value();
            break;
        case DbPluginOption::BOOL:
            value = dynamic_cast<QCheckBox*>(editor)->isChecked();
            break;
        case DbPluginOption::DOUBLE:
            value = dynamic_cast<QDoubleSpinBox*>(editor)->value();
            break;
        case DbPluginOption::CHOICE:
            value = dynamic_cast<QComboBox*>(editor)->currentText();
            break;
        case DbPluginOption::CUSTOM_PATH_BROWSE:
            break; // should not happen ever
        default:
            // TODO plugin based handling of custom editors
            qWarning() << "Unhandled DbDialog option for value.";
            break;
    }
    return value;
}

void DbDialog::setValueFor(DbPluginOption::Type type, QWidget *editor, const QVariant &value)
{
    switch (type)
    {
        case DbPluginOption::STRING:
        case DbPluginOption::FILE:
        case DbPluginOption::PASSWORD:
            dynamic_cast<QLineEdit*>(editor)->setText(value.toString());
            break;
        case DbPluginOption::INT:
            dynamic_cast<QSpinBox*>(editor)->setValue(value.toInt());
            break;
        case DbPluginOption::BOOL:
            dynamic_cast<QCheckBox*>(editor)->setChecked(value.toBool());
            break;
        case DbPluginOption::DOUBLE:
            dynamic_cast<QDoubleSpinBox*>(editor)->setValue(value.toDouble());
            break;
        case DbPluginOption::CHOICE:
            dynamic_cast<QComboBox*>(editor)->setCurrentText(value.toString());
            break;
        case DbPluginOption::CUSTOM_PATH_BROWSE:
            break; // should not happen ever
        default:
            qWarning() << "Unhandled DbDialog option to set value.";
            // TODO plugin based handling of custom editors
            break;
    }
}

void DbDialog::updateType()
{
    if (disableTypeAutodetection)
        return;

    QFileInfo file(ui->fileEdit->text());
    if (!file.exists() || file.isDir())
        return;

    QString currentPluginLabel = ui->typeCombo->currentText();
    QList<DbPlugin*> validPlugins;
    QHash<QString,QVariant> options;
    QString path = ui->fileEdit->text();
    Db* probeDb = nullptr;
    for (DbPlugin* plugin : dbPlugins)
    {
        probeDb = plugin->getInstance("", path, options);
        if (probeDb)
        {
            delete probeDb;
            probeDb = nullptr;

            if (plugin->getLabel() == currentPluginLabel)
                return; // current plugin is among valid plugins, no need to change anything

            validPlugins << plugin;
        }
    }

    if (validPlugins.size() > 0)
        ui->typeCombo->setCurrentText(validPlugins.first()->getLabel());
}

QHash<QString, QVariant> DbDialog::collectOptions()
{
    QHash<QString, QVariant> options;
    if (ui->typeCombo->currentIndex() < 0)
        return options;

    for (const QString key : optionKeyToWidget.keys())
        options[key] = getValueFrom(optionKeyToType[key], optionKeyToWidget[key]);

    DbPlugin* plugin = nullptr;
    if (dbPlugins.count() > 0)
    {
        plugin = dbPlugins[ui->typeCombo->currentText()];
        options[DB_PLUGIN] = plugin->getName();
    }

    return options;
}

bool DbDialog::testDatabase()
{
    if (ui->typeCombo->currentIndex() < 0)
        return false;

    QString path = ui->fileEdit->text();
    if (path.isEmpty())
        return false;

    QUrl url(path);
    if (url.scheme().isEmpty())
        url.setScheme("file");

    QHash<QString, QVariant> options = collectOptions();
    DbPlugin* plugin = dbPlugins[ui->typeCombo->currentText()];
    Db* testDb = plugin->getInstance("", path, options);

    bool res = false;
    if (testDb)
    {
        if (testDb->openForProbing())
        {
            res = !testDb->getEncoding().isEmpty();
            testDb->closeQuiet();
        }
        delete testDb;
    }

    return res;
}

bool DbDialog::validate()
{
    // Name
    if (!ui->generateCheckBox->isChecked())
    {
        if (ui->nameEdit->text().isEmpty())
        {
            setValidState(ui->nameEdit, false, tr("Enter an unique database name."));
            return false;
        }
    }

    Db* registeredDb = DBLIST->getByName(ui->nameEdit->text());
    if (registeredDb && (mode == Mode::ADD || registeredDb != db))
    {
        qDebug() << ui->generateCheckBox->isChecked();
        setValidState(ui->nameEdit, false, tr("This name is already in use. Please enter unique name."));
        return false;
    }
    setValidState(ui->nameEdit, true);

    // File
    if (ui->fileEdit->text().isEmpty())
    {
        setValidState(ui->fileEdit, false, tr("Enter a database file path."));
        return false;
    }

    registeredDb = DBLIST->getByPath(ui->fileEdit->text());
    if (registeredDb && (mode == Mode::ADD || registeredDb != db))
    {
        setValidState(ui->fileEdit, false, tr("This database is already on the list under name: %1").arg(registeredDb->getName()));
        return false;
    }
    setValidState(ui->fileEdit, true);

    // Type
    if (ui->typeCombo->count() == 0)
    {
        // No need to set validation message here. SQLite3 plugin is built in,
        // so if this happens, something is really, really wrong.
        qCritical() << "No db plugins loaded in db dialog!";
        return false;
    }

    if (ui->typeCombo->currentIndex() < 0)
    {
        setValidState(ui->typeCombo, false, tr("Select a database type."));
        return false;
    }
    setValidState(ui->typeCombo, true);

    return true;
}

void DbDialog::updateState()
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(validate());
}

void DbDialog::propertyChanged()
{
    ui->testConnIcon->setVisible(false);
}

void DbDialog::typeChanged(int index)
{
    UNUSED(index);
    updateOptions();
    updateState();
}

void DbDialog::valueForNameGenerationChanged()
{
    updateState();
    if (!ui->generateCheckBox->isChecked())
        return;

    if (dbPlugins.count() > 0)
    {
        DbPlugin* plugin = dbPlugins[ui->typeCombo->currentText()];
        QString generatedName = plugin->generateDbName(ui->fileEdit->text());
        generatedName = generateUniqueName(generatedName, existingDatabaseNames);
        ui->nameEdit->setText(generatedName);
    }
}

void DbDialog::browseForFile()
{
    QString dir = getFileDialogInitPath();
    QString path = QFileDialog::getOpenFileName(0, QString(), dir);
    if (path.isNull())
        return;

    QString key = helperToKey[dynamic_cast<QWidget*>(sender())];
    setValueFor(optionKeyToType[key], optionKeyToWidget[key], path);

    setFileDialogInitPathByFile(path);
}

void DbDialog::generateNameSwitched(bool checked)
{
    if (checked)
    {
        ui->nameEdit->setPlaceholderText(tr("Auto-generated"));
        valueForNameGenerationChanged();
    }
    else
    {
        ui->nameEdit->setPlaceholderText(tr("Type the name"));
    }

    ui->nameEdit->setReadOnly(checked);
}

void DbDialog::fileChanged(const QString &arg1)
{
    UNUSED(arg1);
    valueForNameGenerationChanged();
    updateType();
    propertyChanged();
}

void DbDialog::browseClicked()
{
    if (customBrowseHandler)
    {
        QString newUrl = customBrowseHandler(ui->fileEdit->text());
        if (!newUrl.isNull())
        {
            ui->fileEdit->setText(newUrl);
            updateState();
        }
        return;
    }

    bool createMode = (sender() == ui->browseCreateButton);

    QFileInfo fileInfo(ui->fileEdit->text());
    QString dir;
    if (ui->fileEdit->text().isEmpty())
        dir = getFileDialogInitPath();
    else if (fileInfo.exists() && fileInfo.isFile())
        dir = fileInfo.absolutePath();
    else if (fileInfo.dir().exists())
        dir = fileInfo.dir().absolutePath();
    else
        dir = getFileDialogInitPath();

    QString path = getDbPath(createMode, dir);
    if (path.isNull())
        return;

    setFileDialogInitPathByFile(path);

    ui->fileEdit->setText(path);
    updateState();
}

void DbDialog::testConnectionClicked()
{
    ui->testConnIcon->setPixmap(testDatabase() ? ICONS.TEST_CONN_OK : ICONS.TEST_CONN_ERROR);
    ui->testConnIcon->setVisible(true);
}

void DbDialog::dbTypeChanged(int index)
{
    typeChanged(index);
    propertyChanged();
}

void DbDialog::nameModified(const QString &arg1)
{
    UNUSED(arg1);
    updateState();
}

void DbDialog::accept()
{
    QString name = getName();
    QString path = getPath();
    QHash<QString, QVariant> options = collectOptions();
    bool perm = isPermanent();
    bool result;
    if (mode == ADD)
        result = DBLIST->addDb(name, path, options, perm);
    else
        result = DBLIST->updateDb(db, name, path, options, perm);

    if (result)
        QDialog::accept();
}