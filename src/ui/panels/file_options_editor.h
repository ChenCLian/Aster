#ifndef ASTER_FILE_OPTIONS_EDITOR_H
#define ASTER_FILE_OPTIONS_EDITOR_H
#include "io/files/series_file_reader.h"
#include <QWidget>
class QComboBox;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QThread;
class QTableWidget;
namespace aster::ui
{
class FileOptionsEditor : public QWidget
{
    Q_OBJECT
  public:
    explicit FileOptionsEditor(QWidget *parent = nullptr);
    ~FileOptionsEditor() override;
    void setFile(QString path, QJsonObject options);
    QJsonObject options() const;
    bool ready() const;
  signals:
    void applyRequested(QJsonObject options, bool toSelection);
    void readinessChanged(bool ready);
    void previewInputChanged(QString path, QJsonObject options, bool ready);

  private:
    void requestProbe();
    void startProbe();
    void refreshPreview();
    void notifyPreviewInput();
    QString m_path;
    QJsonObject m_loadedOptions;
    bool m_initialized = false;
    SeriesFileOptions m_options;
    FileProbe m_probe;
    QTableWidget *m_preview;
    QLabel *m_previewHint;
    QComboBox *m_delimiter, *m_header, *m_x, *m_y;
    QLineEdit *m_xUnit, *m_yUnit;
    QCheckBox *m_skip;
    QLabel *m_info;
    QPushButton *m_apply, *m_applySelection;
    QThread *m_worker = nullptr;
    quint64 m_generation = 0;
    bool m_pending = false;
};
} // namespace aster::ui
#endif
