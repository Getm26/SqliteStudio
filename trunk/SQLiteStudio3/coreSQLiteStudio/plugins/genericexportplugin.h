#ifndef GENERICEXPORTPLUGIN_H
#define GENERICEXPORTPLUGIN_H

#include "exportplugin.h"
#include "genericplugin.h"

class API_EXPORT GenericExportPlugin : virtual public GenericPlugin, public ExportPlugin
{
    public:
        bool initBeforeExport(Db* db, QIODevice* output, const ExportManager::StandardExportConfig& config);
        ExportManager::ExportModes getSupportedModes() const;
        ExportManager::ExportProviderFlags getProviderFlags() const;
        QString getExportConfigFormName() const;
        CfgMain* getConfig();
        QString getConfigFormName(ExportManager::ExportMode exportMode) const;
        QString getMimeType() const;
        bool isBinaryData() const;
        void setExportMode(ExportManager::ExportMode exportMode);
        bool afterExportQueryResults();
        bool afterExportTable();
        bool beforeExportTables();
        bool afterExportTables();
        bool beforeExportIndexes();
        bool afterExportIndexes();
        bool beforeExportTriggers();
        bool afterExportTriggers();
        bool beforeExportViews();
        bool afterExportViews();
        bool afterExportDatabase();
        bool afterExport();

        /**
         * @brief Does the initial entry in the export.
         * @return true for success, or false in case of a fatal error.
         *
         * Use it to write the header, or something like that. Default implementation does nothing.
         * This is called from initBeforeExport(), after exportMode and other settings are already prepared.
         */
        virtual bool beforeExport();

    protected:
        virtual bool initBeforeExport();
        void write(const QString& str);
        void writeln(const QString& str);
        bool isTableExport() const;

        Db* db = nullptr;
        QIODevice* output = nullptr;
        const ExportManager::StandardExportConfig* config = nullptr;
        QTextCodec* codec = nullptr;
        ExportManager::ExportMode exportMode = ExportManager::UNDEFINED;
};

#endif // GENERICEXPORTPLUGIN_H
